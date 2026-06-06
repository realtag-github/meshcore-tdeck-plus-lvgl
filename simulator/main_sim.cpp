#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "app/app_types.h"
#include "app/mock_data.h"
#include "app/navigation.h"

namespace {

using meshcore::Action;
using meshcore::AppSnapshot;
using meshcore::ScreenId;

struct DownloadArtifact {
    std::string filename;
    std::uintmax_t size = 0;
};

std::string html_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            default:
                out += ch;
                break;
        }
    }
    return out;
}

bool is_data_url_safe(unsigned char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
           ch == '-' || ch == '_' || ch == '.' || ch == '~';
}

std::string data_url(const std::string& mime, const std::string& value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out = "url(\"data:" + mime + ",";
    for (unsigned char ch : value) {
        if (is_data_url_safe(ch)) {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(hex[(ch >> 4) & 0x0f]);
            out.push_back(hex[ch & 0x0f]);
        }
    }
    out += "\")";
    return out;
}

std::string svg_icon_style(const std::string& name, const std::string& svg) {
    return ".icon-" + name + "{background:" + data_url("image/svg+xml", svg) +
           " center/32px 32px no-repeat}";
}

std::string clock_text(unsigned epoch_seconds) {
    const unsigned minutes = (epoch_seconds / 60U) % (24U * 60U);
    const unsigned hour_24 = minutes / 60U;
    const unsigned minute = minutes % 60U;
    unsigned hour_12 = hour_24 % 12U;
    if (hour_12 == 0) {
        hour_12 = 12;
    }
    char buffer[8] = {};
    std::snprintf(buffer, sizeof(buffer), "%u:%02u%s", hour_12, minute, hour_24 < 12U ? "AM" : "PM");
    return buffer;
}

std::string bytes_text(unsigned value) {
    if (value >= 1024U * 1024U) {
        return std::to_string(value / (1024U * 1024U)) + " MB";
    }
    if (value >= 1024U) {
        return std::to_string(value / 1024U) + " KB";
    }
    return std::to_string(value) + " B";
}

std::string fmt_float(float value, int precision = 1) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string bandwidth_text(unsigned bandwidth_hz) {
    if (bandwidth_hz % 1000U == 0) {
        return std::to_string(bandwidth_hz / 1000U) + " kHz";
    }
    return fmt_float(static_cast<float>(bandwidth_hz) / 1000.0f, 1) + " kHz";
}

std::string file_size_text(std::uintmax_t value) {
    if (value >= 1024ULL * 1024ULL) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(1)
            << static_cast<double>(value) / static_cast<double>(1024ULL * 1024ULL) << " MB";
        return out.str();
    }
    if (value >= 1024ULL) {
        return std::to_string(value / 1024ULL) + " KB";
    }
    return std::to_string(value) + " B";
}

std::string json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += ch;
                break;
        }
    }
    return out;
}

std::vector<DownloadArtifact> copy_downloads(const std::filesystem::path& build_dir) {
    std::vector<DownloadArtifact> artifacts;
    const auto dist_dir = std::filesystem::current_path().parent_path() / "dist";
    if (!std::filesystem::exists(dist_dir)) {
        return artifacts;
    }

    const auto downloads_dir = build_dir / "downloads";
    std::filesystem::create_directories(downloads_dir);
    for (const auto& entry : std::filesystem::directory_iterator(dist_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto filename = entry.path().filename().string();
        const bool firmware_artifact = entry.path().extension() == ".bin" &&
                                       (filename.rfind("MeshCore-TDeckPlus-Launcher-", 0) == 0 ||
                                        filename.rfind("tdeck-plus-", 0) == 0);
        const bool manifest = filename == "manifest.txt";
        if (!firmware_artifact && !manifest) {
            continue;
        }
        const auto destination = downloads_dir / filename;
        std::error_code remove_error;
        std::filesystem::remove(destination, remove_error);
        std::error_code link_error;
        std::filesystem::create_symlink(std::filesystem::absolute(entry.path()), destination, link_error);
        if (link_error) {
            std::filesystem::copy_file(entry.path(), destination,
                                       std::filesystem::copy_options::overwrite_existing);
        }
        artifacts.push_back({filename, entry.file_size()});
    }
    std::sort(artifacts.begin(), artifacts.end(), [](const auto& left, const auto& right) {
        return left.filename < right.filename;
    });
    return artifacts;
}

const DownloadArtifact* find_artifact(const std::vector<DownloadArtifact>& artifacts, const std::string& filename) {
    const auto found = std::find_if(artifacts.begin(), artifacts.end(), [&](const auto& artifact) {
        return artifact.filename == filename;
    });
    return found == artifacts.end() ? nullptr : &(*found);
}

bool has_artifact(const std::vector<DownloadArtifact>& artifacts, const std::string& filename) {
    return find_artifact(artifacts, filename) != nullptr;
}

std::string webflash_version() {
    char buffer[32] = {};
    const std::time_t now = std::time(nullptr);
    std::tm utc = {};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::strftime(buffer, sizeof(buffer), "%Y%m%d%H%M%S", &utc);
    return buffer;
}

void write_webflash_manifest(const std::filesystem::path& build_dir,
                             const std::vector<DownloadArtifact>& artifacts,
                             const std::string& version) {
    const bool has_split_image =
        has_artifact(artifacts, "tdeck-plus-915-bootloader.bin") &&
        has_artifact(artifacts, "tdeck-plus-915-partitions.bin") &&
        has_artifact(artifacts, "tdeck-plus-915-boot_app0.bin") &&
        has_artifact(artifacts, "tdeck-plus-915-firmware.bin");
    const bool has_merged_image = has_artifact(artifacts, "tdeck-plus-915-merged.bin");

    std::ofstream manifest(build_dir / "webflash-manifest.json");
    manifest << "{\n";
    manifest << "  \"name\": \"MeshCore T-Deck Plus 915 MHz\",\n";
    manifest << "  \"version\": \"" << json_escape(version) << "\",\n";
    manifest << "  \"builds\": [";
    if (has_split_image) {
        manifest << "\n";
        manifest << "    {\n";
        manifest << "      \"chipFamily\": \"ESP32-S3\",\n";
        manifest << "      \"parts\": [\n";
        manifest << "        { \"path\": \"downloads/tdeck-plus-915-bootloader.bin\", \"offset\": 0 },\n";
        manifest << "        { \"path\": \"downloads/tdeck-plus-915-partitions.bin\", \"offset\": 32768 },\n";
        manifest << "        { \"path\": \"downloads/tdeck-plus-915-boot_app0.bin\", \"offset\": 57344 },\n";
        manifest << "        { \"path\": \"downloads/tdeck-plus-915-firmware.bin\", \"offset\": 65536 }\n";
        manifest << "      ]\n";
        manifest << "    }\n";
    } else if (has_merged_image) {
        manifest << "\n";
        manifest << "    {\n";
        manifest << "      \"chipFamily\": \"ESP32-S3\",\n";
        manifest << "      \"parts\": [\n";
        manifest << "        { \"path\": \"downloads/tdeck-plus-915-merged.bin\", \"offset\": 0 }\n";
        manifest << "      ]\n";
        manifest << "    }\n";
    }
    manifest << "  ]\n";
    manifest << "}\n";
}

void write_webflash_page(const std::filesystem::path& build_dir,
                         const std::vector<DownloadArtifact>& artifacts,
                         const std::string& version) {
    const bool has_split_image =
        has_artifact(artifacts, "tdeck-plus-915-bootloader.bin") &&
        has_artifact(artifacts, "tdeck-plus-915-partitions.bin") &&
        has_artifact(artifacts, "tdeck-plus-915-boot_app0.bin") &&
        has_artifact(artifacts, "tdeck-plus-915-firmware.bin");
    const bool has_merged_image = has_artifact(artifacts, "tdeck-plus-915-merged.bin");
    const bool flash_ready = has_split_image || has_merged_image;

    std::ofstream out(build_dir / "flasher.html");
    out << "<!doctype html>\n<html><head><meta charset=\"utf-8\">";
    out << "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    out << "<title>MeshCore T-Deck Plus Web Flasher</title>\n";
    out << "<script type=\"module\" src=\"https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module\"></script>\n";
    out << "<style>";
    out << "html,body{min-height:100%}";
    out << "body{margin:0;background:#008080;color:#000;font:11px 'MS Sans Serif',Tahoma,Arial,sans-serif;padding:18px 10px 42px;box-sizing:border-box}";
    out << "body:after{content:'MeshCore T-Deck Plus';position:fixed;left:0;right:0;bottom:0;height:28px;line-height:26px;background:#c0c0c0;border-top:1px solid #fff;box-shadow:0 -1px 0 #808080;color:#000;padding-left:8px;box-sizing:border-box;pointer-events:none}";
    out << ".window{width:min(760px,calc(100vw - 20px));margin:0 auto;background:#c0c0c0;border-top:2px solid #fff;border-left:2px solid #fff;border-right:2px solid #404040;border-bottom:2px solid #404040;box-shadow:1px 1px 0 #000}";
    out << ".title{height:22px;line-height:22px;background:#000080;color:#fff;font-weight:bold;padding:0 3px 0 6px;display:flex;align-items:center;justify-content:space-between;box-sizing:border-box}";
    out << ".win-controls{display:flex;gap:2px}.ctrl{width:16px;height:14px;line-height:12px;text-align:center;background:#c0c0c0;color:#000;border-top:1px solid #fff;border-left:1px solid #fff;border-right:1px solid #404040;border-bottom:1px solid #404040;font-size:10px;font-weight:bold}";
    out << ".menubar{height:20px;line-height:20px;padding:0 8px;background:#c0c0c0;border-bottom:1px solid #808080;box-shadow:0 1px 0 #fff inset}.menubar span{margin-right:18px}";
    out << ".body{padding:8px}.panel{background:#c0c0c0;border-top:1px solid #808080;border-left:1px solid #808080;border-right:1px solid #fff;border-bottom:1px solid #fff;padding:8px;margin:0 0 8px;box-sizing:border-box}.panel-title{display:inline-block;background:#c0c0c0;margin:-16px 0 6px 6px;padding:0 4px;font-weight:bold}";
    out << "h1{font-size:13px;line-height:16px;margin:0 0 6px;font-weight:bold}.muted{color:#404040}.warn{background:#d8d8d8}.error{background:#d8d8d8;color:#800000;font-weight:bold}";
    out << ".actions{display:flex;align-items:center;gap:6px;flex-wrap:wrap;margin:8px 0 0}.files{display:grid;grid-template-columns:minmax(0,1fr) 70px;gap:2px 8px;max-width:650px;align-items:center}";
    out << "a{color:#000080}.actions a,.back,.win-button{display:inline-block;min-height:22px;line-height:20px;padding:0 8px;background:#c0c0c0;color:#000;border-top:1px solid #fff;border-left:1px solid #fff;border-right:1px solid #404040;border-bottom:1px solid #404040;text-decoration:none;font:11px 'MS Sans Serif',Tahoma,Arial,sans-serif;box-sizing:border-box}.actions a:active,.back:active,.win-button:active{border-top-color:#404040;border-left-color:#404040;border-right-color:#fff;border-bottom-color:#fff}.install-widget{display:inline-block;vertical-align:top}.unsupported{display:inline-block;color:#800000;font-weight:bold;padding:4px}";
    out << ".files a{overflow:hidden;white-space:nowrap;text-overflow:clip}.files span{text-align:right;color:#404040}code{display:block;margin-top:6px;background:#fff;border-top:1px solid #808080;border-left:1px solid #808080;border-right:1px solid #fff;border-bottom:1px solid #fff;padding:5px;font:11px Consolas,'Courier New',monospace;color:#000;overflow:auto}.back{margin-top:8px}.footer{height:24px;line-height:22px;background:#c0c0c0;border-top:1px solid #808080;box-shadow:0 1px 0 #fff inset;padding:0 6px;font-size:11px}";
    out << "</style></head><body>\n";
    out << "<div class=\"window\"><div class=\"title\"><span>MeshCore T-Deck Plus Web Flasher</span><span class=\"win-controls\"><span class=\"ctrl\">_</span><span class=\"ctrl\">x</span></span></div>";
    out << "<div class=\"menubar\"><span>File</span><span>Tools</span><span>Help</span></div><div class=\"body\">";
    out << "<div class=\"panel\"><div class=\"panel-title\">Firmware Setup</div><h1>Install latest 915 MHz firmware</h1>";
    out << "<div class=\"muted\">Build manifest " << html_escape(version) << ". Connect the T-Deck Plus over USB, then click Install.</div>";
    out << "<div class=\"actions\">";
    if (flash_ready) {
        out << "<span class=\"install-widget\"><esp-web-install-button manifest=\"webflash-manifest.json?v=" << html_escape(version)
            << "\"><button class=\"win-button\" slot=\"activate\" type=\"button\">Install</button><span class=\"unsupported\" slot=\"unsupported\">Web Serial unavailable</span></esp-web-install-button></span>";
    } else {
        out << "<b>Firmware package missing.</b>";
    }
    if (has_merged_image) {
        out << "<a href=\"downloads/tdeck-plus-915-merged.bin\" download>Download full image</a>";
    }
    if (has_artifact(artifacts, "MeshCore-TDeckPlus-Launcher-915.bin")) {
        out << "<a href=\"downloads/MeshCore-TDeckPlus-Launcher-915.bin\" download>Download Launcher image</a>";
    }
    out << "</div></div>";
    out << "<div id=\"secure-warning\" class=\"panel warn\" hidden><div class=\"panel-title\">Connection</div>";
    out << "Web Serial only works from HTTPS or localhost. Use https://dev-host.local:8092/flasher.html, or use the container SSH tunnel and open https://localhost:8092/flasher.html in Chrome or Edge.";
    out << "<code>ssh -p 2231 -L 8092:127.0.0.1:8080 <ssh-user>@dev-host.local</code>";
    out << "<p>If Chrome still marks the page as unsafe, install the local certificate as trusted: <a href=\"dev-host-local-cert.pem\" download>dev-host-local-cert.pem</a>.</p>";
    out << "</div>";
    out << "<div id=\"serial-warning\" class=\"panel error\" hidden><div class=\"panel-title\">Browser</div>";
    out << "This browser does not expose Web Serial. Use Chrome or Edge on desktop.";
    out << "</div>";
    out << "<div class=\"panel warn\"><div class=\"panel-title\">Warning</div>Do not choose a full erase when you are only updating firmware; erasing wipes MeshCore identity and saved settings.</div>";
    out << "<div class=\"panel\"><div class=\"panel-title\">Files used by the installer</div><div class=\"files\">";
    if (flash_ready) {
        const std::vector<std::string> preferred = {
            "tdeck-plus-915-bootloader.bin",
            "tdeck-plus-915-partitions.bin",
            "tdeck-plus-915-boot_app0.bin",
            "tdeck-plus-915-firmware.bin",
            "tdeck-plus-915-merged.bin",
            "MeshCore-TDeckPlus-Launcher-915.bin",
        };
        for (const auto& filename : preferred) {
            const auto* artifact = find_artifact(artifacts, filename);
            if (artifact == nullptr) {
                continue;
            }
            out << "<a href=\"downloads/" << html_escape(artifact->filename) << "\" download>"
                << html_escape(artifact->filename) << "</a><span>" << file_size_text(artifact->size) << "</span>";
        }
    } else {
        out << "<span>Run tools/package_firmware.sh first.</span><span></span>";
    }
    out << "</div><a class=\"back\" href=\"index.html\">Back to mock screens</a></div>";
    out << "</div><div class=\"footer\">Status: ESP32-S3 Web Serial flasher using local firmware artifacts.</div></div>";
    out << "<script>";
    out << "if(!window.isSecureContext){document.getElementById('secure-warning').hidden=false;}";
    out << "else if(!('serial' in navigator)){document.getElementById('serial-warning').hidden=false;}";
    out << "</script></body></html>\n";
}

void write_tdeck_vnc_page(const std::filesystem::path& build_dir) {
    std::ofstream out(build_dir / "tdeck-vnc.html");
    out << R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>T-Deck Live Simulator</title>
<style>
html,body,#screen{width:100%;height:100%;margin:0;padding:0;overflow:hidden;background:#000;touch-action:none}
#screen{position:relative}
#status{position:absolute;left:4px;top:4px;z-index:2;min-width:74px;height:16px;line-height:14px;padding:0 4px;box-sizing:border-box;background:#c0c0c0;color:#000;border-top:1px solid #fff;border-left:1px solid #fff;border-right:1px solid #404040;border-bottom:1px solid #404040;font:11px 'MS Sans Serif',Tahoma,Arial,sans-serif;pointer-events:none}
#status.connected{display:none}
canvas{image-rendering:pixelated;image-rendering:crisp-edges}
</style>
<script type="module">
import RFB from './core/rfb.js';
const query = new URLSearchParams(location.search);
const hash = new URLSearchParams(location.hash.replace(/^#/, ''));
const param = (name, fallback = '') => query.get(name) ?? hash.get(name) ?? fallback;
const host = param('host', location.hostname);
const port = param('port', location.port);
const path = param('path', 'websockify').replace(/^\//, '');
const password = param('password', '');
const statusBox = document.getElementById('status');
function setStatus(text, connected = false) {
  statusBox.textContent = text;
  statusBox.classList.toggle('connected', connected);
}
const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
const url = `${scheme}://${host}${port ? `:${port}` : ''}/${path}`;
const options = password ? { credentials: { password } } : {};
setStatus('Connecting');
const rfb = new RFB(document.getElementById('screen'), url, options);
rfb.viewOnly = param('view_only', '0') === '1';
rfb.scaleViewport = true;
rfb.resizeSession = false;
rfb.focusOnClick = true;
rfb.addEventListener('connect', () => setStatus('Connected', true));
rfb.addEventListener('disconnect', (event) => setStatus(event.detail.clean ? 'Disconnected' : 'Connection lost'));
rfb.addEventListener('credentialsrequired', () => {
  const entered = window.prompt('Password Required:');
  rfb.sendCredentials({ password: entered || '' });
});
</script>
</head><body><div id="screen"><div id="status">Loading</div></div></body></html>
)HTML";
}

void write_hardware_tdeck_page(const std::filesystem::path& build_dir) {
    std::ofstream out(build_dir / "hardware-tdeck.html");
    out << R"HARDWARE(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Real T-Deck Hardware</title>
<style>
html,body,#view{width:100%;height:100%;margin:0;padding:0;overflow:hidden;background:#000;touch-action:none;color:#000;font:10px 'MS Sans Serif',Tahoma,Arial,sans-serif}
#view{position:relative;width:320px;height:240px;user-select:none}
#cam{position:absolute;inset:0;width:320px;height:240px;object-fit:cover;background:#000;image-rendering:auto;transform-origin:center center}
body.contain #cam{object-fit:contain}
body.rot180 #cam{transform:rotate(180deg)}
#status{position:absolute;left:2px;top:2px;right:2px;height:16px;line-height:14px;padding:0 4px;background:#c0c0c0;border-top:1px solid #fff;border-left:1px solid #fff;border-right:1px solid #404040;border-bottom:1px solid #404040;box-sizing:border-box;white-space:nowrap;overflow:hidden;text-overflow:clip;pointer-events:none}
#toolbar{position:absolute;left:2px;right:2px;bottom:2px;height:18px;display:flex;gap:2px;align-items:center;pointer-events:auto}
#toolbar button{height:18px;line-height:16px;min-width:36px;padding:0 4px;background:#c0c0c0;color:#000;border-top:1px solid #fff;border-left:1px solid #fff;border-right:1px solid #404040;border-bottom:1px solid #404040;border-radius:0;font:10px 'MS Sans Serif',Tahoma,Arial,sans-serif;font-weight:bold}
#toolbar button:active{border-top-color:#404040;border-left-color:#404040;border-right-color:#fff;border-bottom-color:#fff}
#error{position:absolute;left:18px;right:18px;top:78px;min-height:46px;padding:8px;background:#c0c0c0;border-top:2px solid #fff;border-left:2px solid #fff;border-right:2px solid #404040;border-bottom:2px solid #404040;box-sizing:border-box;display:none;line-height:13px}
body.camera-error #error{display:block}
</style></head><body>
<div id="view"><img id="cam" alt="real T-Deck camera feed"><div id="status">Connecting to hardware</div><div id="error">Camera feed offline.<br>Check /tmp/meshcore-tdeck-camera.log on dev-host.</div><div id="toolbar"><button type="button" data-cmd="home">Home</button><button type="button" data-cmd="chat">Chat</button><button type="button" data-cmd="status">Status</button><button type="button" data-cmd="rotate">Rotate</button><button type="button" data-cmd="fit">Fit</button></div></div>
<script>
const W = 320;
const H = 240;
const bridgeBase = `${location.protocol}//${location.hostname}:8093`;
const cam = document.getElementById('cam');
const statusBox = document.getElementById('status');
let snapshot = { screen: 'Home', messages: [], channels: [] };
let rotate180 = localStorage.getItem('meshcore-camera-rotate180') === '1';
let contain = localStorage.getItem('meshcore-camera-contain') === '1';
document.body.classList.toggle('rot180', rotate180);
document.body.classList.toggle('contain', contain);
function setStatus(text) { statusBox.textContent = text; }
function refreshCamera() { cam.src = `camera.jpg?t=${Date.now()}`; }
cam.addEventListener('load', () => document.body.classList.remove('camera-error'));
cam.addEventListener('error', () => document.body.classList.add('camera-error'));
async function api(path, body) {
  const res = await fetch(bridgeBase + path, {
    method: body ? 'POST' : 'GET', mode: 'cors', headers: {'Content-Type': 'application/json'}, body: body ? JSON.stringify(body) : undefined
  });
  return res.json();
}
function renderSnapshot(data) {
  if (!data || data.ok === false) {
    setStatus(data?.busy ? 'Hardware busy' : 'Hardware bridge offline');
    return;
  }
  snapshot = data;
  const status = data.status?.values || {};
  const health = data.health?.values || {};
  const chan = data.state?.channel || (data.channels || []).find((c) => c.active)?.name || 'test';
  const msgCount = data.messages?.length ?? health.messages ?? 0;
  setStatus(`${data.screen || 'Home'}  ${chan}  msg:${msgCount}  ${status.ble || 'ble'}  ${status.battery || ''}`);
}
async function loadSnapshot() {
  try { renderSnapshot(await api('/api/snapshot')); }
  catch (error) { setStatus('Open https://' + location.hostname + ':8093/api/status'); }
}
async function send(command, expect = 'hil:') {
  setStatus(`> ${command}`);
  try {
    const data = await api('/api/command', { command, expect, timeout_s: 4 });
    if (data.ok === false) setStatus(data.error || 'command failed');
    setTimeout(loadSnapshot, 250);
  } catch (error) {
    setStatus('Hardware command failed');
  }
}
function screenPoint(event) {
  const rect = cam.getBoundingClientRect();
  let x = (event.clientX - rect.left) * W / rect.width;
  let y = (event.clientY - rect.top) * H / rect.height;
  if (rotate180) { x = W - x; y = H - y; }
  return { x, y };
}
const desktopIcons = [
  ['Inbox',8,8], ['Contacts',70,8], ['ChannelEditor',132,8], ['Radio',194,8], ['Map',256,8],
  ['Nodes',8,80], ['RadioAdvanced',70,80], ['Identity',132,80], ['Ble',194,80], ['Settings',256,80],
  ['Servers',8,152], ['Tools',70,152], ['Diagnostics',132,152]
];
function tapHardware(x, y) {
  const screen = snapshot.screen || 'Home';
  if (screen === 'Home') {
    if (y >= 212 && x <= 58) { send('ui show Inbox', 'hil: ui scheduled'); return; }
    for (const [target, ix, iy] of desktopIcons) {
      if (x >= ix && x <= ix + 58 && y >= iy && y <= iy + 58) { send(`ui show ${target}`, 'hil: ui scheduled'); return; }
    }
    return;
  }
  if (x >= 292 && y <= 24) { send('ui home', 'hil: ui scheduled'); return; }
  if (y >= 24 && y <= 54) {
    const index = Math.max(0, Math.min(3, Math.floor((x - 4) / 58)));
    send(`ui action ${index}`, 'hil: ui action=');
    return;
  }
  if (y >= 212 && x <= 58) { send('ui show Inbox', 'hil: ui scheduled'); }
}
document.getElementById('view').addEventListener('click', (event) => {
  if (event.target.closest('#toolbar')) return;
  const pt = screenPoint(event);
  tapHardware(pt.x, pt.y);
});
document.getElementById('view').addEventListener('wheel', (event) => {
  event.preventDefault();
  send(`ui scroll ${event.deltaY > 0 ? 1 : -1}`, 'hil: ui scroll=');
}, {passive:false});
document.getElementById('toolbar').addEventListener('click', (event) => {
  const button = event.target.closest('button[data-cmd]');
  if (!button) return;
  const cmd = button.dataset.cmd;
  if (cmd === 'home') send('ui home', 'hil: ui scheduled');
  if (cmd === 'chat') send('ui show Inbox', 'hil: ui scheduled');
  if (cmd === 'status') loadSnapshot();
  if (cmd === 'rotate') { rotate180 = !rotate180; localStorage.setItem('meshcore-camera-rotate180', rotate180 ? '1' : '0'); document.body.classList.toggle('rot180', rotate180); }
  if (cmd === 'fit') { contain = !contain; localStorage.setItem('meshcore-camera-contain', contain ? '1' : '0'); document.body.classList.toggle('contain', contain); }
});
refreshCamera();
loadSnapshot();
setInterval(refreshCamera, 550);
setInterval(loadSnapshot, 1500);
</script></body></html>
)HARDWARE";
}

void write_webflash_assets(const std::filesystem::path& build_dir,
                           const std::vector<DownloadArtifact>& artifacts) {
    const auto version = webflash_version();
    write_webflash_manifest(build_dir, artifacts, version);
    write_webflash_page(build_dir, artifacts, version);
    write_tdeck_vnc_page(build_dir);
    write_hardware_tdeck_page(build_dir);
}

void render_downloads(std::ostream& out, const std::vector<DownloadArtifact>& artifacts) {
    const bool flash_ready =
        (has_artifact(artifacts, "tdeck-plus-915-bootloader.bin") &&
         has_artifact(artifacts, "tdeck-plus-915-partitions.bin") &&
         has_artifact(artifacts, "tdeck-plus-915-boot_app0.bin") &&
         has_artifact(artifacts, "tdeck-plus-915-firmware.bin")) ||
        has_artifact(artifacts, "tdeck-plus-915-merged.bin");
    const auto version = webflash_version();

    out << "<aside class=\"install-window movable-window minimized\" data-window=\"installer\" data-title=\"Installer\"><div class=\"install-title window-titlebar\"><span>Installer</span><span class=\"window-controls\"><button type=\"button\" aria-label=\"Minimize\" data-window-minimize>_</button><button type=\"button\" aria-label=\"Close\" data-window-close>x</button></span></div>";
    out << "<div class=\"install-body\">";
    if (flash_ready) {
        out << "<esp-web-install-button manifest=\"webflash-manifest.json?v=" << html_escape(version)
            << "\"><button class=\"win-button\" slot=\"activate\" type=\"button\">Install to TDeck</button><span class=\"unsupported\" slot=\"unsupported\">Web Serial unavailable</span></esp-web-install-button>";
    } else {
        out << "<span class=\"unsupported\">Package missing</span>";
    }
    out << "</div></aside>\n";
}

std::string row(const std::string& left, const std::string& right, const std::string& detail = "") {
    std::ostringstream out;
    out << "<div class=\"row\"><div><b>" << html_escape(left) << "</b>";
    if (!detail.empty()) {
        out << "<small>" << html_escape(detail) << "</small>";
    }
    out << "</div><span>" << html_escape(right) << "</span></div>";
    return out.str();
}

std::string screen_key(ScreenId screen) {
    switch (screen) {
        case ScreenId::Boot:
            return "Boot";
        case ScreenId::Home:
            return "Home";
        case ScreenId::Inbox:
            return "Inbox";
        case ScreenId::MessageView:
            return "MessageView";
        case ScreenId::Compose:
            return "Compose";
        case ScreenId::Nodes:
            return "Nodes";
        case ScreenId::Contacts:
            return "Contacts";
        case ScreenId::Channels:
            return "Channels";
        case ScreenId::ChannelEditor:
            return "ChannelEditor";
        case ScreenId::Map:
            return "Map";
        case ScreenId::Settings:
            return "Settings";
        case ScreenId::Radio:
            return "Radio";
        case ScreenId::RadioAdvanced:
            return "RadioAdvanced";
        case ScreenId::RadioTuning:
            return "RadioTuning";
        case ScreenId::Identity:
            return "Identity";
        case ScreenId::Ble:
            return "Ble";
        case ScreenId::Servers:
            return "Servers";
        case ScreenId::Tools:
            return "Tools";
        case ScreenId::Diagnostics:
            return "Diagnostics";
    }
    return "Home";
}

std::string desktop_icon(const std::string& label, const std::string& glyph, ScreenId target) {
    std::ostringstream out;
    out << "<div class=\"deskicon\" data-target=\"" << screen_key(target) << "\" tabindex=\"0\"><div class=\"icon icon-"
        << html_escape(glyph) << "\"><i></i></div><span>" << html_escape(label) << "</span></div>";
    return out.str();
}

bool is_chat_screen(ScreenId screen) {
    return screen == ScreenId::Inbox || screen == ScreenId::MessageView || screen == ScreenId::Compose
        || screen == ScreenId::Channels;
}

std::string window_title(ScreenId screen) {
    return is_chat_screen(screen) ? "Chat" : meshcore::screen_title(screen);
}

std::string chat_time(unsigned index) {
    static constexpr unsigned base_hour = 9;
    static constexpr unsigned base_minute = 42;
    const unsigned total_minutes = base_hour * 60 + base_minute + index * 2;
    std::ostringstream out;
    out << std::setw(2) << std::setfill('0') << ((total_minutes / 60) % 24)
        << ":" << std::setw(2) << std::setfill('0') << (total_minutes % 60);
    return out.str();
}

std::string chat_target_name(const AppSnapshot& data, bool channel_mode) {
    if (channel_mode) {
        if (!data.channels.empty()) {
            const auto index = std::min<std::size_t>(
                data.state.selected_channel < 0 ? 0 : static_cast<std::size_t>(data.state.selected_channel),
                data.channels.size() - 1);
            return "#" + data.channels[index].name;
        }
        return "#test";
    }
    if (!data.nodes.empty()) {
        const auto index = std::min<std::size_t>(
            data.state.selected_node < 0 ? 0 : static_cast<std::size_t>(data.state.selected_node),
            data.nodes.size() - 1);
        return data.nodes[index].name;
    }
    return data.state.compose_recipient;
}

bool chat_is_channel_message(const meshcore::MeshMessage& message) {
    return message.subject.rfind("Channel ", 0) == 0;
}

std::string chat_message_channel(const meshcore::MeshMessage& message) {
    return chat_is_channel_message(message) ? message.subject.substr(8) : "";
}

bool text_matches_peer(const std::string& value, const meshcore::NodeInfo& node) {
    return value == node.short_id || value == node.name ||
           (!node.short_id.empty() && value.find(node.short_id) != std::string::npos) ||
           (!node.name.empty() && value.find(node.name) != std::string::npos);
}

bool chat_message_matches(const AppSnapshot& data, const meshcore::MeshMessage& message, bool channel_mode) {
    if (channel_mode) {
        if (!chat_is_channel_message(message)) {
            return false;
        }
        const std::string channel = chat_message_channel(message);
        if (!data.channels.empty()) {
            const auto index = std::min<std::size_t>(
                data.state.selected_channel < 0 ? 0 : static_cast<std::size_t>(data.state.selected_channel),
                data.channels.size() - 1);
            return channel == data.channels[index].name;
        }
        return channel == data.state.channel;
    }
    if (chat_is_channel_message(message)) {
        return false;
    }
    if (data.nodes.empty()) {
        return true;
    }
    const auto index = std::min<std::size_t>(
        data.state.selected_node < 0 ? 0 : static_cast<std::size_t>(data.state.selected_node),
        data.nodes.size() - 1);
    const auto& selected = data.nodes[index];
    return message.outgoing ? text_matches_peer(message.subject, selected)
                            : text_matches_peer(message.sender, selected);
}

std::size_t bounded_index(int value, std::size_t total) {
    if (total == 0 || value < 0) {
        return 0;
    }
    return std::min<std::size_t>(static_cast<std::size_t>(value), total - 1);
}

std::size_t first_visible(std::size_t total, std::size_t visible, std::size_t selected, std::size_t before) {
    if (total <= visible || visible == 0) {
        return 0;
    }
    return std::min<std::size_t>(selected > before ? selected - before : 0, total - visible);
}

std::string passive_scrollbar(std::size_t total,
                              std::size_t visible,
                              std::size_t first,
                              const std::string& class_name) {
    if (total <= visible || visible == 0) {
        return "";
    }
    const auto max_first = std::max<std::size_t>(1, total - visible);
    const auto thumb = std::min<std::size_t>(100, std::max<std::size_t>(12, (visible * 100) / total));
    const auto top = ((100 - thumb) * std::min(first, total - visible)) / max_first;
    std::ostringstream out;
    out << "<div class=\"scrollbar " << html_escape(class_name) << "\"><i style=\"height:"
        << thumb << "%;top:" << top << "%\"></i></div>";
    return out.str();
}

std::string bool_text(bool value) {
    return value ? "on" : "off";
}

std::string key_prefix(const std::array<unsigned char, meshcore::NodeInfo::public_key_size>& key) {
    bool any = false;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < 4 && i < key.size(); ++i) {
        any = any || key[i] != 0;
        out << std::setw(2) << static_cast<unsigned>(key[i]);
    }
    return any ? out.str() : "none";
}

std::string path_text(const meshcore::NodeInfo& node) {
    if (node.out_path_len == meshcore::NodeInfo::out_path_unknown) {
        return "unknown";
    }
    if (node.out_path_len == 0) {
        return "direct";
    }
    return std::to_string(node.out_path_len) + " hop";
}

std::string secret_text(const meshcore::ChannelInfo& channel) {
    bool any = false;
    for (const auto byte : channel.secret) {
        any = any || byte != 0;
    }
    return any ? "custom" : "public";
}

std::string advert_policy_text(const AppSnapshot& data) {
    switch (data.state.advert_location_policy % 3U) {
        case 1:
            return "manual";
        case 2:
            return "gps";
        default:
            return "none";
    }
}

std::string chat_app(const AppSnapshot& data, bool channel_mode) {
    std::ostringstream out;
    const auto target = chat_target_name(data, channel_mode);
    out << "<div class=\"chatapp\">";
    out << "<div class=\"chatstatus\"><span class=\"" << (channel_mode ? "channel" : "dm") << "\">"
        << (channel_mode ? "Channel selected" : "DM selected") << "</span><span>MeshCore "
        << (data.state.connected ? "online" : "offline") << "</span></div>";
    out << "<div class=\"chatbody\"><aside class=\"roster\"><b>Contacts</b>";
    const auto selected_node = bounded_index(data.state.selected_node, data.nodes.size());
    const auto visible_contacts = std::min<std::size_t>(3, data.nodes.size());
    const auto first_contact = data.nodes.size() <= visible_contacts
        ? 0
        : std::min<std::size_t>(selected_node, data.nodes.size() - visible_contacts);
    for (std::size_t offset = 0; offset < visible_contacts; ++offset) {
        const auto i = first_contact + offset;
        out << "<div class=\"buddy " << (!channel_mode && i == selected_node ? "on" : "") << "\"><i></i><span>"
            << html_escape(data.nodes[i].name) << "</span><small>DM</small></div>";
    }
    out << passive_scrollbar(data.nodes.size(), visible_contacts, first_contact, "contact-scroll");
    out << "<b>Channels</b>";
    const auto selected_channel = bounded_index(data.state.selected_channel, data.channels.size());
    const auto visible_channels = std::min<std::size_t>(3, data.channels.size());
    const auto first_channel = data.channels.size() <= visible_channels
        ? 0
        : std::min<std::size_t>(selected_channel, data.channels.size() - visible_channels);
    for (std::size_t offset = 0; offset < visible_channels; ++offset) {
        const auto i = first_channel + offset;
        out << "<div class=\"buddy " << (channel_mode && i == selected_channel ? "on" : "") << "\"><i></i><span>#"
            << html_escape(data.channels[i].name) << "</span><small>" << data.channels[i].users << "</small></div>";
    }
    out << passive_scrollbar(data.channels.size(), visible_channels, first_channel, "channel-scroll");
    out << "</aside><section class=\"transcript\">";
    out << "<div class=\"threadtitle\">" << html_escape(target) << "</div>";
    std::vector<std::size_t> visible_messages;
    bool selected_in_conversation = false;
    for (std::size_t i = 0; i < data.messages.size(); ++i) {
        if (!chat_message_matches(data, data.messages[i], channel_mode)) {
            continue;
        }
        if (static_cast<int>(i) == data.state.selected_message) {
            selected_in_conversation = true;
        }
        visible_messages.push_back(i);
    }
    std::size_t selected_visible = 0;
    if (selected_in_conversation) {
        for (std::size_t i = 0; i < visible_messages.size(); ++i) {
            if (static_cast<int>(visible_messages[i]) == data.state.selected_message) {
                selected_visible = i;
                break;
            }
        }
    }
    constexpr std::size_t transcript_rows = 7;
    const auto first_message = first_visible(visible_messages.size(), transcript_rows, selected_visible, 3);
    std::size_t displayed = 0;
    for (std::size_t i = first_message; i < visible_messages.size() && displayed < transcript_rows; ++i) {
        const auto message_index = visible_messages[i];
        const auto& message = data.messages[message_index];
        const std::string sender = message.outgoing ? "me" : message.sender;
        const bool selected = selected_in_conversation
            ? static_cast<int>(message_index) == data.state.selected_message
            : displayed == 0;
        out << "<div class=\"chatline " << (message.outgoing ? "out" : "in")
            << (selected ? " selected" : "") << "\"><time>"
            << chat_time(static_cast<unsigned>(displayed)) << "</time><b>" << html_escape(sender)
            << "</b><span class=\"" << (selected ? "marquee" : "") << "\">"
            << html_escape(message.body) << "</span></div>";
        ++displayed;
    }
    out << passive_scrollbar(visible_messages.size(), transcript_rows, first_message, "thread-scroll");
    if (displayed == 0) {
        out << "<div class=\"chatline sys\"><time>--:--</time><b>sys</b><span>No messages in this conversation.</span></div>";
    }
    out << "</section></div>";
    if (!visible_messages.empty()) {
        const auto selected_index = selected_in_conversation
            ? visible_messages[selected_visible]
            : visible_messages.front();
        const auto& message = data.messages[selected_index];
        out << "<div class=\"msgstatus\">" << (message.outgoing ? "out " : "in ")
            << (message.acked ? "ack " : message.delivered ? "sent " : "")
            << (message.persisted ? "saved " : "") << html_escape(message.status) << "</div>";
    }
    out << "<div class=\"sendrow\"><span>To: " << html_escape(target) << "</span><div class=\"sendbox\">"
        << html_escape(data.state.compose_text) << "</div><button>Send</button></div>";
    out << "</div>";
    return out.str();
}

std::string content_for(ScreenId screen, const AppSnapshot& data) {
    std::ostringstream out;
    switch (screen) {
        case ScreenId::Boot:
            out << "<div class=\"boot\">MeshCore<br><small>T-Deck Plus</small></div>";
            break;
        case ScreenId::Home:
            out << "<div class=\"desktop\">";
            out << desktop_icon("Chat", "mail", ScreenId::Inbox);
            out << desktop_icon("Contacts", "list", ScreenId::Contacts);
            out << desktop_icon("Channels", "folder", ScreenId::ChannelEditor);
            out << desktop_icon("Radio", "radio", ScreenId::Radio);
            out << desktop_icon("Map", "nav", ScreenId::Map);
            out << desktop_icon("Nodes", "list", ScreenId::Nodes);
            out << desktop_icon("Radio+", "radio", ScreenId::RadioAdvanced);
            out << desktop_icon("Identity", "edit", ScreenId::Identity);
            out << desktop_icon("BLE", "radio", ScreenId::Ble);
            out << desktop_icon("Settings", "gear", ScreenId::Settings);
            out << desktop_icon("Servers", "drive", ScreenId::Servers);
            out << desktop_icon("Tools", "gear", ScreenId::Tools);
            out << desktop_icon("Diag", "warn", ScreenId::Diagnostics);
            out << "</div><div class=\"taskbar\"><b data-taskbar-chat=\"1\" tabindex=\"0\">Chat</b>"
                << "<span class=\"clock\" data-battery=\""
                << data.state.battery_percent << "\">"
                << html_escape(std::to_string(data.state.battery_percent)) << "% <b>"
                << html_escape(clock_text(data.state.current_epoch_seconds)) << "</b></span></div>";
            break;
        case ScreenId::Inbox:
        case ScreenId::MessageView:
        case ScreenId::Compose:
            out << chat_app(data, false);
            break;
        case ScreenId::Nodes:
        {
            constexpr std::size_t visible_rows = 5;
            const auto selected = bounded_index(data.state.selected_node, data.nodes.size());
            const auto first = first_visible(data.nodes.size(), visible_rows, selected, 2);
            out << "<div class=\"listview\">";
            for (std::size_t i = first; i < data.nodes.size() && i < first + visible_rows; ++i) {
                const auto& node = data.nodes[i];
                std::ostringstream radio;
                radio << node.rssi << "dBm " << std::fixed << std::setprecision(1) << node.snr;
                out << row(node.name, radio.str(), node.short_id + " seen " + std::to_string(node.last_seen_seconds) + "m");
            }
            out << passive_scrollbar(data.nodes.size(), visible_rows, first, "screen-scroll") << "</div>";
            break;
        }
        case ScreenId::Contacts:
            if (data.nodes.empty()) {
                out << row("No contacts", "", "use BLE companion or advert scan");
            } else {
                const auto& node = data.nodes[bounded_index(data.state.selected_node, data.nodes.size())];
                out << row(node.name, std::to_string(node.rssi) + " dBm", node.short_id);
                out << row("Public key", key_prefix(node.public_key), "first 4 bytes");
                out << row("Path", path_text(node), "route to contact");
                out << row("Flags", std::to_string(node.contact_flags), "type " + std::to_string(node.contact_type));
                out << row("Last mod", std::to_string(node.lastmod), "contact record");
                out << passive_scrollbar(data.nodes.size(), 1, bounded_index(data.state.selected_node, data.nodes.size()), "screen-scroll");
            }
            break;
        case ScreenId::Channels:
            out << chat_app(data, true);
            break;
        case ScreenId::ChannelEditor:
            if (data.channels.empty()) {
                out << row("No channels", "", "BLE companion can provision channels");
            } else {
                const auto& channel = data.channels[bounded_index(data.state.selected_channel, data.channels.size())];
                out << row("#" + channel.name, std::to_string(channel.users) + " users", channel.active ? "active" : "inactive");
                out << row("Secret", secret_text(channel), "channel encryption");
                out << row("Slot", std::to_string(data.state.selected_channel), "selected channel");
                out << row("Default", data.state.channel, "current selected channel");
                out << row("Compose", data.state.compose_recipient == "broadcast" ? "broadcast" : "DM", "target");
                out << passive_scrollbar(data.channels.size(), 1, bounded_index(data.state.selected_channel, data.channels.size()), "screen-scroll");
            }
            break;
        case ScreenId::Map:
            out << "<div class=\"map\"><div class=\"pin\"></div><span>Base</span><span>Alpha-7</span><span>Relay</span></div>";
            if (!data.nodes.empty() && data.nodes.front().has_position) {
                out << "<p class=\"coords\">z" << data.state.map_zoom << " " << std::fixed << std::setprecision(4)
                    << data.nodes.front().latitude << ", " << data.nodes.front().longitude << "</p>";
            } else {
                out << "<p class=\"coords\">z" << data.state.map_zoom << " " << std::fixed << std::setprecision(4) << data.state.latitude
                    << ", " << data.state.longitude << "</p>";
            }
            out << "<p class=\"coords\">selected node position</p>";
            break;
        case ScreenId::Settings:
            out << row("Channel", data.state.channel);
            out << row("Node", data.state.local_node_id, data.state.device_name);
            out << row("GPS", data.state.gps_enabled ? "on" : "off", data.state.gps_state);
            out << row("Bluetooth", data.state.ble_connected ? "link" : "adv", data.state.ble_state);
            out << row("Storage", data.state.storage_writable ? "write" : "read", data.state.storage_state);
            break;
        case ScreenId::Radio:
            out << row("Region", data.state.region, std::to_string(data.state.radio_frequency_khz) + " kHz");
            out << row("State", data.state.connected ? "online" : "offline", data.state.radio_state);
            out << row("RX/TX",
                       std::to_string(data.state.radio_rx_decoded_count) + "/" +
                           std::to_string(data.state.radio_rx_raw_count) + " ok/raw",
                       std::to_string(data.state.packet_tx_count) + " tx");
            out << row("Last RF",
                       "type " + std::to_string(data.state.radio_last_packet_type) +
                           " len " + std::to_string(data.state.radio_last_packet_len),
                       data.state.radio_last_decode);
            out << row("Signal",
                       std::to_string(data.state.last_rssi) + " dBm",
                       "SNR " + fmt_float(static_cast<float>(data.state.last_snr_quarters) / 4.0f));
            break;
        case ScreenId::RadioAdvanced:
            out << row("Frequency", std::to_string(data.state.radio_frequency_khz) + " kHz", "numeric LoRa");
            out << row("Bandwidth", bandwidth_text(data.state.radio_bandwidth_hz), "modem setting");
            out << row("SF / CR", std::to_string(data.state.radio_spreading_factor) + " / " + std::to_string(data.state.radio_coding_rate), "spreading + coding");
            out << row("Repeat", bool_text(data.state.client_repeat), "client repeat");
            out << row("Tuning", std::to_string(data.state.rx_delay_base_ms) + "/" + std::to_string(data.state.airtime_factor_ms), "rx/airtime");
            break;
        case ScreenId::RadioTuning:
            out << row("Path hash",
                       std::to_string(data.state.path_hash_mode + 1),
                       "wire path bytes");
            out << row("Client repeat", bool_text(data.state.client_repeat), "MeshCore relay behavior");
            out << row("RX delay",
                       std::to_string(data.state.rx_delay_base_ms) + " ms",
                       "airtime " + std::to_string(data.state.airtime_factor_ms));
            out << row("CAD",
                       data.state.radio_cad_status,
                       std::to_string(data.state.radio_cad_detected_count) + "/" +
                           std::to_string(data.state.radio_cad_error_count));
            out << row("Hardware",
                       data.state.radio_dio2_as_rf_switch ? "DIO2 RF" : "RF fixed",
                       "tcxo " + std::to_string(data.state.radio_tcxo_mv) + " mV");
            break;
        case ScreenId::Identity:
            out << row("Node", data.state.local_node_id, data.state.device_name);
            out << row("Public key", key_prefix(data.state.public_key), "first 4 bytes");
            out << row("Advert", advert_policy_text(data), "location policy");
            out << row("Location", std::to_string(data.state.latitude).substr(0, 7), std::to_string(data.state.longitude).substr(0, 8));
            out << row("Security", data.state.private_key_export_enabled ? "export" : "locked", data.state.device_pin_set ? "PIN set" : "no PIN");
            break;
        case ScreenId::Ble:
            out << row("Companion", data.state.ble_connected ? "connected" : bool_text(data.state.ble_enabled), data.state.ble_state);
            out << row("Frames", std::to_string(data.state.ble_rx_frames) + " / " + std::to_string(data.state.ble_tx_frames), "rx / tx");
            out << row("Last cmd", data.state.ble_last_command, "BLE protocol");
            out << row("Last error", data.state.ble_last_error, "BLE protocol");
            out << row("Messages", std::to_string(data.state.unread_count), "waiting sync");
            break;
        case ScreenId::Servers:
            out << row("Room server", data.state.room_logged_in ? "login" : "off", "stored posts");
            out << row("Repeater", data.state.repeater_admin ? "admin" : "locked", "remote admin");
            out << row("Clock sync", "ready", "RF command");
            out << row("Register", data.state.registered ? "yes" : "no", "advanced");
            break;
        case ScreenId::Tools:
            out << row(data.state.tool_title, data.state.tool_status, data.state.tool_detail);
            out << row("Path", data.state.tool_path.empty() ? "none" : data.state.tool_path, "latest response");
            out << row("Telemetry", std::to_string(data.state.packet_rx_count) + "/" + std::to_string(data.state.packet_tx_count) + " q" + std::to_string(data.state.queue_len), "rx/tx queue");
            out << row("Custom vars", std::to_string(data.state.custom_var_index) + "/" + std::to_string(data.state.custom_var_value), "index/value");
            out << row("Flood scope", std::to_string(data.state.default_flood_scope) + "/" + std::to_string(data.state.flood_scope_key), "default/key");
            break;
        case ScreenId::Diagnostics:
        {
            constexpr std::size_t visible_rows = 5;
            const std::size_t total_rows = visible_rows + data.logs.size();
            const std::size_t first = total_rows <= visible_rows
                ? 0
                : std::min<std::size_t>(bounded_index(data.state.diagnostics_scroll, total_rows),
                                        total_rows - visible_rows);
            out << "<div class=\"listview\">";
            for (std::size_t line = 0; line < visible_rows && first + line < total_rows; ++line) {
                const std::size_t item = first + line;
                if (item == 0) {
                    out << row("Radio", data.state.connected ? "ok" : "off", data.state.radio_state);
                } else if (item == 1) {
                    out << row("Battery", std::to_string(data.state.battery_percent) + "%",
                               std::to_string(data.state.battery_mv) + " mV");
                } else if (item == 2) {
                    out << row("Memory", bytes_text(data.state.heap_free_bytes),
                               data.state.psram_total_bytes > 0 ? bytes_text(data.state.psram_free_bytes) : "no psram");
                } else if (item == 3) {
                    out << row("Storage", std::to_string(data.state.persisted_message_count) + " msg "
                               + std::to_string(data.state.persisted_node_count) + " nodes",
                               data.state.storage_writable ? "ok" : "off");
                } else if (item == 4) {
                    out << row("Bluetooth", data.state.ble_connected ? "connected" : "ready", data.state.ble_state);
                } else {
                    out << row("Log", "", data.logs[item - 5]);
                }
            }
            out << passive_scrollbar(total_rows, visible_rows, first, "screen-scroll") << "</div>";
            break;
        }
    }
    return out.str();
}

[[maybe_unused]] void render_screen(std::ostream& out, ScreenId screen, const AppSnapshot& data) {
    out << "  <section class=\"screen" << (screen == ScreenId::Home ? " active" : "")
        << "\" id=\"" << meshcore::screen_title(screen) << "\" data-screen=\"" << screen_key(screen) << "\">\n";
    if (screen == ScreenId::Home) {
        out << "    <main class=\"home-main\">" << content_for(screen, data) << "</main>\n";
        out << "  </section>\n";
        return;
    }
    out << "    <div class=\"top\"><b>" << window_title(screen) << "</b><button class=\"close\">X</button></div>\n";
    out << "    <div class=\"toolbar\">";
    if (is_chat_screen(screen)) {
        out << "<button data-action-index=\"0\">DM</button><button data-action-index=\"1\">Chan</button>"
            << "<button data-action-index=\"2\">Send</button><button data-action-index=\"3\">Clear</button>";
    } else {
        int index = 0;
        for (const auto& action : meshcore::screen_actions(screen)) {
            out << "<button data-action-index=\"" << index << "\">" << html_escape(action.label) << "</button>";
            ++index;
        }
    }
    out << "</div>\n";
    out << "    <main" << (is_chat_screen(screen) ? " class=\"chat-main\"" : "") << ">"
        << content_for(screen, data) << "</main>\n";
    out << "    <nav><button data-taskbar-chat=\"1\">Chat</button>"
        << "<span class=\"tray clock\" data-battery=\""
        << data.state.battery_percent << "\">"
        << html_escape(std::to_string(data.state.battery_percent)) << "% <b>"
        << html_escape(clock_text(data.state.current_epoch_seconds)) << "</b></span></nav>\n";
    out << "  </section>\n";
}

void render_interaction_script(std::ostream& out) {
    out << "<script>\n";
    out << "(() => {\n";
    out << "  const actions = {\n";
    for (const auto screen : meshcore::all_screens()) {
        out << "    " << screen_key(screen) << ": [";
        const auto screen_actions = meshcore::screen_actions(screen);
        for (std::size_t i = 0; i < screen_actions.size(); ++i) {
            if (i > 0) {
                out << ",";
            }
            out << "{target:'" << screen_key(screen_actions[i].target) << "',label:'"
                << html_escape(screen_actions[i].label) << "'}";
        }
        out << "],\n";
    }
    out << "  };\n";
    out << "  let current = 'Home';\n";
    out << "  const screens = Array.from(document.querySelectorAll('.screen'));\n";
    out << "  const desktopWindows = Array.from(document.querySelectorAll('.movable-window'));\n";
    out << "  const taskButtons = document.querySelector('.task-buttons');\n";
    out << "  const pageClock = document.querySelector('.page-clock');\n";
    out << "  const windowStateKey = 'meshcore-tdeck-web-window-state-v1';\n";
    out << "  let topZ = 20;\n";
    out << R"JS(  const classicWebToolsCss = `
    :host {
      font-family: 'MS Sans Serif', Tahoma, Arial, sans-serif !important;
      letter-spacing: 0 !important;
      color: #000 !important;
      --md-sys-color-primary: #000080 !important;
      --md-sys-color-on-primary: #fff !important;
      --md-sys-color-surface: #c0c0c0 !important;
      --md-sys-color-on-surface: #000 !important;
      --md-sys-color-outline: #404040 !important;
      --md-dialog-container-shape: 0 !important;
      --md-filled-button-container-shape: 0 !important;
      --md-text-button-container-shape: 0 !important;
      animation: none !important;
      transform: none !important;
      rotate: 0deg !important;
    }
    *, *::before, *::after {
      border-radius: 0 !important;
      letter-spacing: 0 !important;
      box-shadow: none !important;
      font-family: 'MS Sans Serif', Tahoma, Arial, sans-serif !important;
    }
    :host(ewt-install-dialog), :host(ew-dialog) {
      background: transparent !important;
      color: #000 !important;
    }
    @keyframes classic-progress {
      from { transform: translateX(-64px); }
      to { transform: translateX(220px); }
    }
    @keyframes classic-dialog-open {
      from { opacity: 0; transform: scale(.96); }
      to { opacity: 1; transform: scale(1); }
    }
    :host(ewt-install-dialog) ew-dialog {
      --md-dialog-container-color: #c0c0c0 !important;
      --md-dialog-headline-color: #fff !important;
      --md-dialog-supporting-text-color: #000 !important;
      --md-dialog-container-shape: 0 !important;
      color: #000 !important;
    }
    :host(ew-dialog) dialog {
      background: transparent !important;
      border: 0 !important;
      padding: 0 !important;
    }
    :host(ew-dialog) .container {
      background: #c0c0c0 !important;
      color: #000 !important;
      border-top: 2px solid #fff !important;
      border-left: 2px solid #fff !important;
      border-right: 2px solid #404040 !important;
      border-bottom: 2px solid #404040 !important;
      box-sizing: border-box !important;
      animation: classic-dialog-open 120ms steps(4, end) !important;
    }
    :host(ew-dialog) .headline {
      display: block !important;
      min-height: 22px !important;
      line-height: 22px !important;
      margin: 0 !important;
      padding: 0 44px 0 6px !important;
      background: #000080 !important;
      color: #fff !important;
      font-size: 12px !important;
      font-weight: bold !important;
      box-sizing: border-box !important;
      cursor: move !important;
      position: relative !important;
      user-select: none !important;
      overflow: hidden !important;
    }
    :host(ew-dialog) .headline slot,
    :host(ew-dialog) slot[name="headline"],
    :host(ew-dialog) ::slotted([slot="headline"]) {
      color: #fff !important;
      background: transparent !important;
      font-size: 12px !important;
      font-weight: bold !important;
      line-height: 22px !important;
      margin: 0 !important;
      padding: 0 !important;
      display: inline !important;
      min-height: 0 !important;
    }
    :host(ew-dialog) .container.meshcore-webtools-movable {
      position: fixed !important;
      margin: 0 !important;
      transform: none !important;
      max-width: min(92vw, 560px) !important;
      max-height: calc(100vh - 42px) !important;
    }
    .meshcore-webtools-controls {
      position: absolute !important;
      right: 3px !important;
      top: 4px !important;
      display: flex !important;
      gap: 2px !important;
      z-index: 2 !important;
    }
    .meshcore-webtools-control {
      width: 16px !important;
      height: 14px !important;
      min-height: 14px !important;
      line-height: 12px !important;
      padding: 0 !important;
      text-align: center !important;
      background: #c0c0c0 !important;
      color: #000 !important;
      border-top: 1px solid #fff !important;
      border-left: 1px solid #fff !important;
      border-right: 1px solid #404040 !important;
      border-bottom: 1px solid #404040 !important;
      font: bold 10px 'MS Sans Serif', Tahoma, Arial, sans-serif !important;
      box-sizing: border-box !important;
      cursor: default !important;
    }
    .meshcore-webtools-control:active {
      border-top-color: #404040 !important;
      border-left-color: #404040 !important;
      border-right-color: #fff !important;
      border-bottom-color: #fff !important;
    }
    :host(ew-dialog) .scroller,
    :host(ew-dialog) .content {
      background: #c0c0c0 !important;
      color: #000 !important;
    }
    :host(ew-dialog) .content {
      padding: 8px !important;
      box-sizing: border-box !important;
    }
    :host(ew-dialog) .actions {
      background: #c0c0c0 !important;
      color: #000 !important;
      border-top: 1px solid #808080 !important;
      box-shadow: inset 0 1px #fff !important;
      padding: 6px !important;
    }
    .scrim, [class*="scrim"], [class*="backdrop"] {
      background: rgba(0, 0, 0, .22) !important;
    }
    ew-list-item [slot="headline"], ew-list-item div[slot="headline"], :host(ew-list-item) ::slotted([slot="headline"]) {
      display: block !important;
      visibility: visible !important;
      opacity: 1 !important;
      min-height: 14px !important;
      height: auto !important;
      margin: 0 !important;
      padding: 0 !important;
      background: transparent !important;
      color: #000 !important;
      font-size: 11px !important;
      line-height: 14px !important;
      font-weight: normal !important;
      overflow: visible !important;
      transform: none !important;
    }
    [slot="content"], ::slotted([slot="content"]), .content, [class*="content"] {
      background: #c0c0c0 !important;
      color: #000 !important;
      font-size: 11px !important;
      line-height: 15px !important;
    }
    ew-button, ew-text-button, md-filled-button, md-text-button, md-outlined-button {
      min-height: 22px !important;
      background: #c0c0c0 !important;
      color: #000 !important;
      border-top: 1px solid #fff !important;
      border-left: 1px solid #fff !important;
      border-right: 1px solid #404040 !important;
      border-bottom: 1px solid #404040 !important;
      padding: 0 8px !important;
      font: 11px 'MS Sans Serif', Tahoma, Arial, sans-serif !important;
      text-transform: none !important;
    }
    ew-button:active, ew-text-button:active, md-filled-button:active, md-text-button:active, md-outlined-button:active {
      border-top-color: #404040 !important;
      border-left-color: #404040 !important;
      border-right-color: #fff !important;
      border-bottom-color: #fff !important;
    }
    ew-button, ew-text-button, ew-list-item {
      --md-sys-color-primary: #c0c0c0 !important;
      --md-sys-color-on-primary: #000 !important;
      --md-sys-color-on-surface: #000 !important;
      --md-filled-button-container-color: #c0c0c0 !important;
      --md-filled-button-label-text-color: #000 !important;
      --md-text-button-label-text-color: #000 !important;
      --md-list-item-container-shape: 0 !important;
    }
    ew-list-item, :host(ew-list-item) .list-item {
      background: #c0c0c0 !important;
      color: #000 !important;
      border-top: 1px solid #fff !important;
      border-left: 1px solid #fff !important;
      border-right: 1px solid #808080 !important;
      border-bottom: 1px solid #808080 !important;
      margin: 2px 0 !important;
    }
    ew-list-item {
      display: block !important;
      min-height: 38px !important;
      opacity: 1 !important;
      visibility: visible !important;
      --md-list-item-label-text-color: #000 !important;
      --md-list-item-headline-color: #000 !important;
      --md-list-item-leading-icon-color: #000 !important;
      --md-list-item-trailing-icon-color: #000 !important;
    }
    :host(ew-list-item) .list-item, :host(ew-list-item) button {
      display: flex !important;
      width: 100% !important;
      min-height: 38px !important;
      height: auto !important;
      align-items: center !important;
      overflow: visible !important;
      color: #000 !important;
      opacity: 1 !important;
      visibility: visible !important;
    }
    :host(ew-list-item) slot, :host(ew-list-item) ::slotted(*) {
      color: #000 !important;
      opacity: 1 !important;
      visibility: visible !important;
    }
    ewt-console, textarea, pre, code, input, .console, .terminal,
    [class*="console"], [class*="terminal"], [class*="log"] {
      background: #fff !important;
      color: #000 !important;
      border-top: 1px solid #808080 !important;
      border-left: 1px solid #808080 !important;
      border-right: 1px solid #fff !important;
      border-bottom: 1px solid #fff !important;
      font-family: 'Courier New', monospace !important;
      font-size: 11px !important;
      line-height: 14px !important;
    }
    ewt-page-progress {
      display: flex !important;
      flex-direction: column !important;
      align-items: center !important;
      justify-content: center !important;
      gap: 8px !important;
      min-width: 260px !important;
      min-height: 82px !important;
      color: #000 !important;
      font-size: 11px !important;
      text-align: center !important;
    }
    ew-circular-progress, ew-linear-progress, md-circular-progress, md-linear-progress,
    progress, [role="progressbar"], .progress, [class*="progress"],
    .meshcore-classic-progress {
      width: 220px !important;
      height: 16px !important;
      min-height: 16px !important;
      display: block !important;
      position: relative !important;
      margin: 8px auto !important;
      background: #fff !important;
      border-top: 1px solid #808080 !important;
      border-left: 1px solid #808080 !important;
      border-right: 1px solid #fff !important;
      border-bottom: 1px solid #fff !important;
      overflow: hidden !important;
      color: #000 !important;
      appearance: none !important;
      -webkit-appearance: none !important;
      box-shadow: inset 1px 1px #808080, inset -1px -1px #fff !important;
    }
    ew-circular-progress::before, ew-linear-progress::before,
    md-circular-progress::before, md-linear-progress::before,
    progress::before, [role="progressbar"]::before, .progress::before,
    [class*="progress"]::before, .meshcore-classic-progress::before {
      content: "" !important;
      position: absolute !important;
      inset: 2px !important;
      background: #fff !important;
    }
    ew-circular-progress::after, ew-linear-progress::after,
    md-circular-progress::after, md-linear-progress::after,
    progress::after, [role="progressbar"]::after, .progress::after,
    [class*="progress"]::after, .meshcore-classic-progress::after {
      content: "" !important;
      position: absolute !important;
      left: 2px !important;
      top: 2px !important;
      bottom: 2px !important;
      width: var(--meshcore-progress-width, 64px) !important;
      background: repeating-linear-gradient(90deg, #000080 0 9px, transparent 9px 12px) !important;
      animation: var(--meshcore-progress-animation, classic-progress 950ms steps(24, end) infinite) !important;
    }
    progress::-webkit-progress-bar {
      background: #fff !important;
      border: 0 !important;
    }
    progress::-webkit-progress-value {
      background: repeating-linear-gradient(90deg, #000080 0 9px, transparent 9px 12px) !important;
    }
    progress::-moz-progress-bar {
      background: repeating-linear-gradient(90deg, #000080 0 9px, transparent 9px 12px) !important;
    }
    ew-circular-progress *, ew-linear-progress *,
    md-circular-progress *, md-linear-progress *,
    [role="progressbar"] *, .progress *, [class*="progress"] * {
      animation: none !important;
      transform: none !important;
      rotate: 0deg !important;
    }
    ew-circular-progress svg, ew-linear-progress svg,
    md-circular-progress svg, md-linear-progress svg,
    [role="progressbar"] svg, .progress svg, [class*="progress"] svg,
    .track, .bar, .indicator, .active-indicator, .inactive-track {
      display: none !important;
    }
    svg { fill: currentColor; }
    .danger, [class*="danger"] {
      color: #800000 !important;
      --md-sys-color-primary: #800000 !important;
      --md-sys-color-on-surface: #800000 !important;
    }
  `;
  const classicStyledRoots = new WeakSet();
  function injectClassicWebToolsStyle(root) {
    if (!root || classicStyledRoots.has(root)) return;
    classicStyledRoots.add(root);
    const style = document.createElement('style');
    style.textContent = classicWebToolsCss;
    root.appendChild(style);
  }
  function forceVisibleWebToolsText(node) {
    const roots = [];
    if (node.shadowRoot) roots.push(node.shadowRoot);
    if (node.querySelectorAll) {
      node.querySelectorAll('*').forEach((item) => {
        if (item.shadowRoot) roots.push(item.shadowRoot);
      });
    }
    roots.forEach((root) => {
      root.querySelectorAll('ew-list-item').forEach((item) => {
        item.style.setProperty('display', 'block', 'important');
        item.style.setProperty('min-height', '38px', 'important');
        item.style.setProperty('visibility', 'visible', 'important');
        item.style.setProperty('opacity', '1', 'important');
        item.style.setProperty('color', '#000', 'important');
        item.style.setProperty('--md-list-item-label-text-color', '#000', 'important');
        item.style.setProperty('--md-list-item-headline-color', '#000', 'important');
        item.querySelectorAll('[slot="headline"]').forEach((label) => {
          label.style.setProperty('display', 'block', 'important');
          label.style.setProperty('visibility', 'visible', 'important');
          label.style.setProperty('opacity', '1', 'important');
          label.style.setProperty('color', '#000', 'important');
          label.style.setProperty('background', 'transparent', 'important');
          label.style.setProperty('font-size', '11px', 'important');
          label.style.setProperty('line-height', '14px', 'important');
          label.style.setProperty('height', 'auto', 'important');
          label.style.setProperty('min-height', '14px', 'important');
          label.style.setProperty('overflow', 'visible', 'important');
          label.style.setProperty('transform', 'none', 'important');
        });
      });
    });
  }
  function forceClassicWebToolsProgress(node) {
    const roots = [];
    if (node.shadowRoot) roots.push(node.shadowRoot);
    if (node.querySelectorAll) {
      node.querySelectorAll('*').forEach((item) => {
        if (item.shadowRoot) roots.push(item.shadowRoot);
      });
    }
    const selector = 'ew-circular-progress, ew-linear-progress, md-circular-progress, md-linear-progress, progress, [role="progressbar"], .progress, [class*="progress"]';
    roots.forEach((root) => {
      root.querySelectorAll(selector).forEach((bar) => {
        if (bar.tagName && bar.tagName.toLowerCase() === 'ewt-page-progress') return;
        bar.classList?.add('meshcore-classic-progress');
        const rawNow = bar.getAttribute('aria-valuenow') ?? bar.getAttribute('value') ?? bar.value;
        const rawMax = bar.getAttribute('aria-valuemax') ?? bar.getAttribute('max') ?? bar.max ?? 100;
        const now = Number(rawNow);
        const max = Number(rawMax);
        if (Number.isFinite(now) && Number.isFinite(max) && max > 0) {
          const percent = Math.max(0, Math.min(100, (now / max) * 100));
          bar.style.setProperty('--meshcore-progress-width', percent + '%');
          bar.style.setProperty('--meshcore-progress-animation', 'none');
        } else {
          bar.style.removeProperty('--meshcore-progress-width');
          bar.style.removeProperty('--meshcore-progress-animation');
        }
      });
    });
  }
  const webToolsDialogPositionKey = 'meshcore-tdeck-webtools-dialog-position-v1';
  function webToolsDialogTitle(installer) {
    return installer?.shadowRoot?.querySelector('[slot="headline"]')?.textContent?.trim() ||
      'MeshCore T-Deck Plus 915 MHz';
  }
  function readWebToolsDialogPosition() {
    try { return JSON.parse(localStorage.getItem(webToolsDialogPositionKey) || '{}') || {}; }
    catch (error) { return {}; }
  }
  function saveWebToolsDialogPosition(container) {
    if (!container) return;
    const left = Number.parseInt(container.style.left || '', 10);
    const top = Number.parseInt(container.style.top || '', 10);
    if (!Number.isFinite(left) || !Number.isFinite(top)) return;
    try { localStorage.setItem(webToolsDialogPositionKey, JSON.stringify({ left, top })); }
    catch (error) {}
  }
  function clampWebToolsDialog(container, left, top) {
    const taskbar = document.querySelector('.desktop-taskbar');
    const taskbarHeight = taskbar ? taskbar.offsetHeight : 28;
    const width = container.offsetWidth || 360;
    const height = container.offsetHeight || 220;
    const maxLeft = Math.max(0, window.innerWidth - width - 2);
    const maxTop = Math.max(0, window.innerHeight - taskbarHeight - height - 2);
    return {
      left: Math.min(Math.max(0, left), maxLeft),
      top: Math.min(Math.max(0, top), maxTop)
    };
  }
  function webToolsTaskButton(installer) {
    if (!taskButtons) return null;
    let button = taskButtons.querySelector('[data-task-window="webtools-installer"]');
    if (!button) {
      button = document.createElement('button');
      button.type = 'button';
      button.className = 'task-button minimized';
      button.dataset.taskWindow = 'webtools-installer';
      button.textContent = webToolsDialogTitle(installer);
      button.addEventListener('click', () => restoreWebToolsDialog(installer));
      taskButtons.appendChild(button);
    }
    return button;
  }
  function removeWebToolsTaskButton() {
    taskButtons?.querySelector('[data-task-window="webtools-installer"]')?.remove();
  }
  function minimizeWebToolsDialog(installer) {
    if (!installer) return;
    installer.classList.add('webtools-minimized');
    document.body.style.overflow = '';
    const button = webToolsTaskButton(installer);
    if (button) button.classList.add('minimized');
  }
  function restoreWebToolsDialog(installer) {
    if (!installer) return;
    installer.classList.remove('webtools-minimized');
    document.body.style.overflow = 'hidden';
    removeWebToolsTaskButton();
    scanClassicWebTools(installer);
    setTimeout(() => scanClassicWebTools(installer), 60);
  }
  function closeWebToolsDialog(installer) {
    if (!installer) return;
    removeWebToolsTaskButton();
    document.body.style.overflow = '';
    const ewDialog = installer.shadowRoot?.querySelector('ew-dialog');
    if (ewDialog && typeof ewDialog.close === 'function') {
      ewDialog.close();
    } else {
      installer.remove();
    }
  }
  function enhanceWebToolsDialog(installer) {
    if (!installer || installer.classList.contains('webtools-minimized')) return;
    const ewDialog = installer.shadowRoot?.querySelector('ew-dialog');
    const root = ewDialog?.shadowRoot;
    if (!root) return;
    const container = root.querySelector('.container');
    const headline = root.querySelector('.headline');
    if (!container || !headline) return;
    const rect = container.getBoundingClientRect();
    container.classList.add('meshcore-webtools-movable');
    if (!container.dataset.meshcorePositioned) {
      const saved = readWebToolsDialogPosition();
      const left = Number.isFinite(Number(saved.left)) ? Number(saved.left) : rect.left;
      const top = Number.isFinite(Number(saved.top)) ? Number(saved.top) : rect.top;
      const point = clampWebToolsDialog(container, left, top);
      container.style.left = point.left + 'px';
      container.style.top = point.top + 'px';
      container.dataset.meshcorePositioned = '1';
    }
    if (!headline.querySelector('.meshcore-webtools-controls')) {
      const controls = document.createElement('span');
      controls.className = 'meshcore-webtools-controls';
      controls.innerHTML = '<button type="button" class="meshcore-webtools-control" aria-label="Minimize">_</button><button type="button" class="meshcore-webtools-control" aria-label="Close">x</button>';
      const buttons = controls.querySelectorAll('button');
      buttons[0].addEventListener('click', (event) => {
        event.preventDefault();
        event.stopPropagation();
        minimizeWebToolsDialog(installer);
      });
      buttons[1].addEventListener('click', (event) => {
        event.preventDefault();
        event.stopPropagation();
        closeWebToolsDialog(installer);
      });
      headline.appendChild(controls);
    }
    if (!headline.dataset.meshcoreDragBound) {
      headline.dataset.meshcoreDragBound = '1';
      headline.addEventListener('pointerdown', (event) => {
        if (event.target.closest('.meshcore-webtools-controls')) return;
        event.preventDefault();
        const start = container.getBoundingClientRect();
        const offsetX = event.clientX - start.left;
        const offsetY = event.clientY - start.top;
        const move = (moveEvent) => {
          const point = clampWebToolsDialog(container, moveEvent.clientX - offsetX, moveEvent.clientY - offsetY);
          container.style.left = point.left + 'px';
          container.style.top = point.top + 'px';
        };
        const up = () => {
          document.removeEventListener('pointermove', move);
          document.removeEventListener('pointerup', up);
          saveWebToolsDialogPosition(container);
        };
        document.addEventListener('pointermove', move);
        document.addEventListener('pointerup', up);
      });
    }
  }
  function enhanceWebToolsDialogs(node = document) {
    const dialogs = [];
    if (node.matches?.('ewt-install-dialog')) dialogs.push(node);
    node.querySelectorAll?.('ewt-install-dialog').forEach((dialog) => dialogs.push(dialog));
    dialogs.forEach(enhanceWebToolsDialog);
  }
  function scanClassicWebTools(node = document) {
    if (node.shadowRoot) injectClassicWebToolsStyle(node.shadowRoot);
    if (!node.querySelectorAll) return;
    node.querySelectorAll('*').forEach((item) => {
      if (item.shadowRoot) injectClassicWebToolsStyle(item.shadowRoot);
    });
    forceVisibleWebToolsText(node);
    forceClassicWebToolsProgress(node);
    enhanceWebToolsDialogs(node);
  }
  const nativeAttachShadow = Element.prototype.attachShadow;
  if (!Element.prototype.__meshcoreClassicWebTools) {
    Element.prototype.attachShadow = function(init) {
      const root = nativeAttachShadow.call(this, init);
      setTimeout(() => injectClassicWebToolsStyle(root), 0);
      return root;
    };
    Element.prototype.__meshcoreClassicWebTools = true;
  }
  const webToolsObserver = new MutationObserver(() => scanClassicWebTools());
  webToolsObserver.observe(document.documentElement, { childList: true, subtree: true });
  scanClassicWebTools();
  setTimeout(scanClassicWebTools, 100);
  setTimeout(scanClassicWebTools, 600);
  let installerWarningAccepted = false;
  function closeExperimentalWarning() {
    document.querySelector('.experimental-overlay')?.remove();
  }
  function openExperimentalWarning(installButton) {
    closeExperimentalWarning();
    const overlay = document.createElement('div');
    overlay.className = 'experimental-overlay';
    overlay.innerHTML = `<div class="experimental-dialog" role="dialog" aria-modal="true" aria-label="Experimental firmware warning">
      <div class="experimental-title">Experimental Firmware</div>
      <div class="experimental-body">
        <div class="experimental-icon" aria-hidden="true"></div>
        <div class="experimental-text"><b>This firmware is experimental.</b>
        It can crash, lose settings, or require recovery flashing. Install only if you are prepared to reflash the T-Deck Plus.</div>
      </div>
      <div class="experimental-actions">
        <button type="button" data-warning-cancel>Cancel</button>
        <button type="button" data-warning-install>Install</button>
      </div>
    </div>`;
    document.body.appendChild(overlay);
    const cancel = overlay.querySelector('[data-warning-cancel]');
    const install = overlay.querySelector('[data-warning-install]');
    cancel.addEventListener('click', closeExperimentalWarning);
    install.addEventListener('click', () => {
      closeExperimentalWarning();
      installerWarningAccepted = true;
      try {
        installButton.click();
      } finally {
        setTimeout(() => { installerWarningAccepted = false; scanClassicWebTools(); }, 0);
        setTimeout(scanClassicWebTools, 250);
        setTimeout(scanClassicWebTools, 900);
      }
    });
    overlay.addEventListener('click', (event) => {
      if (event.target === overlay) closeExperimentalWarning();
    });
    cancel.focus();
  }
  document.addEventListener('click', (event) => {
    const installButton = event.target.closest?.('.win-button[slot="activate"]');
    if (!installButton || installerWarningAccepted) return;
    event.preventDefault();
    event.stopPropagation();
    event.stopImmediatePropagation();
    openExperimentalWarning(installButton);
  }, true);
)JS";
    out << "  function loadWindowState() {\n";
    out << "    try { return JSON.parse(localStorage.getItem(windowStateKey) || '{}') || {}; }\n";
    out << "    catch (error) { return {}; }\n";
    out << "  }\n";
    out << "  let savedWindowState = loadWindowState();\n";
    out << "  function windowId(win) { return win?.dataset.window || ''; }\n";
    out << "  function windowById(id) { return desktopWindows.find((win) => windowId(win) === id); }\n";
    out << "  function numericStyle(value, fallback) {\n";
    out << "    const parsed = Number.parseInt(value || '', 10);\n";
    out << "    return Number.isFinite(parsed) ? parsed : fallback;\n";
    out << "  }\n";
    out << "  function persistWindowState() {\n";
    out << "    const state = {};\n";
    out << "    desktopWindows.forEach((win) => {\n";
    out << "      const id = windowId(win);\n";
    out << "      if (!id) return;\n";
    out << "      state[id] = {\n";
    out << "        left: numericStyle(win.style.left, win.offsetLeft),\n";
    out << "        top: numericStyle(win.style.top, win.offsetTop),\n";
    out << "        minimized: win.classList.contains('minimized')\n";
    out << "      };\n";
    out << "    });\n";
    out << "    savedWindowState = state;\n";
    out << "    try { localStorage.setItem(windowStateKey, JSON.stringify(state)); }\n";
    out << "    catch (error) {}\n";
    out << "  }\n";
    out << "  function activeScreen() { return screens.find((screen) => screen.dataset.screen === current); }\n";
    out << "  function show(screenName) {\n";
    out << "    current = screenName || 'Home';\n";
    out << "    screens.forEach((screen) => screen.classList.toggle('active', screen.dataset.screen === current));\n";
    out << "  }\n";
    out << "  function clockText(date) {\n";
    out << "    let hour = date.getHours();\n";
    out << "    const suffix = hour < 12 ? 'AM' : 'PM';\n";
    out << "    hour = hour % 12 || 12;\n";
    out << "    return `${hour}:${String(date.getMinutes()).padStart(2, '0')}${suffix}`;\n";
    out << "  }\n";
    out << "  function updateClock() {\n";
    out << "    const text = clockText(new Date());\n";
    out << "    document.querySelectorAll('.clock').forEach((node) => {\n";
    out << "      node.innerHTML = `${node.dataset.battery || '0'}% <b>${text}</b>`;\n";
    out << "    });\n";
    out << "    if (pageClock) pageClock.textContent = text;\n";
    out << "  }\n";
    out << "  function bringWindowToFront(win) {\n";
    out << "    if (!win || win.classList.contains('minimized')) return;\n";
    out << "    topZ += 1;\n";
    out << "    win.style.zIndex = String(topZ);\n";
    out << "  }\n";
    out << "  function clampWindow(win, left, top) {\n";
    out << "    const taskbar = document.querySelector('.desktop-taskbar');\n";
    out << "    const taskbarHeight = taskbar ? taskbar.offsetHeight : 28;\n";
    out << "    const maxLeft = Math.max(0, window.innerWidth - win.offsetWidth - 2);\n";
    out << "    const maxTop = Math.max(0, window.innerHeight - taskbarHeight - win.offsetHeight - 2);\n";
    out << "    return {left: Math.min(Math.max(0, left), maxLeft), top: Math.min(Math.max(0, top), maxTop)};\n";
    out << "  }\n";
    out << "  function taskButtonFor(win) {\n";
    out << "    if (!taskButtons) return null;\n";
    out << "    let button = Array.from(taskButtons.children).find((node) => node.dataset.taskWindow === win.dataset.window);\n";
    out << "    if (!button) {\n";
    out << "      button = document.createElement('button');\n";
    out << "      button.type = 'button';\n";
    out << "      button.className = 'task-button minimized';\n";
    out << "      button.dataset.taskWindow = win.dataset.window || '';\n";
    out << "      button.textContent = win.dataset.title || 'Window';\n";
    out << "      button.addEventListener('click', () => restoreWindow(win));\n";
    out << "      taskButtons.appendChild(button);\n";
    out << "    }\n";
    out << "    return button;\n";
    out << "  }\n";
    out << "  function minimizeWindow(win, persist = true) {\n";
    out << "    if (!win) return;\n";
    out << "    win.classList.add('minimized');\n";
    out << "    const button = taskButtonFor(win);\n";
    out << "    if (button) button.classList.add('minimized');\n";
    out << "    if (persist) persistWindowState();\n";
    out << "  }\n";
    out << "  function restoreWindow(win, persist = true) {\n";
    out << "    if (!win) return;\n";
    out << "    win.classList.remove('minimized', 'closed');\n";
    out << "    const button = taskButtons ? Array.from(taskButtons.children).find((node) => node.dataset.taskWindow === win.dataset.window) : null;\n";
    out << "    if (button) button.remove();\n";
    out << "    const left = numericStyle(win.style.left, win.offsetLeft);\n";
    out << "    const top = numericStyle(win.style.top, win.offsetTop);\n";
    out << "    const point = clampWindow(win, left, top);\n";
    out << "    win.style.left = point.left + 'px';\n";
    out << "    win.style.top = point.top + 'px';\n";
    out << "    bringWindowToFront(win);\n";
    out << "    if (persist) persistWindowState();\n";
    out << "  }\n";
    out << "  function applySavedWindowState(win) {\n";
    out << "    const state = savedWindowState[windowId(win)];\n";
    out << "    if (!state) { if (win.classList.contains('minimized')) taskButtonFor(win); return; }\n";
    out << "    const left = Number(state.left);\n";
    out << "    const top = Number(state.top);\n";
    out << "    if (Number.isFinite(left) && Number.isFinite(top)) {\n";
    out << "      const point = clampWindow(win, left, top);\n";
    out << "      win.style.left = point.left + 'px';\n";
    out << "      win.style.top = point.top + 'px';\n";
    out << "    }\n";
    out << "    if (state.minimized) minimizeWindow(win, false);\n";
    out << "    else win.classList.remove('minimized', 'closed');\n";
    out << "  }\n";
    out << "  function initMovableWindow(win) {\n";
    out << "    const title = win.querySelector('.window-titlebar');\n";
    out << "    if (!title) return;\n";
    out << "    bringWindowToFront(win);\n";
    out << "    win.querySelector('[data-window-minimize]')?.addEventListener('click', (event) => { event.stopPropagation(); minimizeWindow(win); });\n";
    out << "    win.querySelector('[data-window-close]')?.addEventListener('click', (event) => { event.stopPropagation(); minimizeWindow(win); });\n";
    out << "    win.addEventListener('pointerdown', () => bringWindowToFront(win));\n";
    out << "    title.addEventListener('pointerdown', (event) => {\n";
    out << "      if (event.target.closest('button')) return;\n";
    out << "      event.preventDefault();\n";
    out << "      bringWindowToFront(win);\n";
    out << "      const rect = win.getBoundingClientRect();\n";
    out << "      const offsetX = event.clientX - rect.left;\n";
    out << "      const offsetY = event.clientY - rect.top;\n";
    out << "      const move = (moveEvent) => {\n";
    out << "        const point = clampWindow(win, moveEvent.clientX - offsetX, moveEvent.clientY - offsetY);\n";
    out << "        win.style.left = point.left + 'px';\n";
    out << "        win.style.top = point.top + 'px';\n";
    out << "      };\n";
    out << "      const up = () => {\n";
    out << "        document.removeEventListener('pointermove', move);\n";
    out << "        document.removeEventListener('pointerup', up);\n";
    out << "        persistWindowState();\n";
    out << "      };\n";
    out << "      document.addEventListener('pointermove', move);\n";
    out << "      document.addEventListener('pointerup', up);\n";
    out << "    });\n";
    out << "    applySavedWindowState(win);\n";
    out << "  }\n";
    out << "  window.addEventListener('resize', () => {\n";
    out << "    desktopWindows.forEach((win) => {\n";
    out << "      if (win.classList.contains('minimized')) return;\n";
    out << "      const rect = win.getBoundingClientRect();\n";
    out << "      const point = clampWindow(win, rect.left, rect.top);\n";
    out << "      win.style.left = point.left + 'px';\n";
    out << "      win.style.top = point.top + 'px';\n";
    out << "    });\n";
    out << "    persistWindowState();\n";
    out << "  });\n";
    out << "  function closeEditor() { document.querySelector('.edit-overlay')?.remove(); }\n";
    out << "  function openEditor(label) {\n";
    out << "    closeEditor();\n";
    out << "    const overlay = document.createElement('div');\n";
    out << "    overlay.className = 'edit-overlay';\n";
    out << "    overlay.innerHTML = `<div class=\"edit-dialog\"><div class=\"edit-title\">${label}</div><p>Type on keyboard. Enter=OK Esc=Cancel</p><input autofocus value=\"\"><small>OK applies to firmware settings</small></div>`;\n";
    out << "    document.body.appendChild(overlay);\n";
    out << "    const input = overlay.querySelector('input');\n";
    out << "    input.focus();\n";
    out << "    input.addEventListener('keydown', (event) => { if (event.key === 'Escape' || event.key === 'Enter') closeEditor(); });\n";
    out << "  }\n";
    out << "  function isEditAction(label) { return ['Edit','Name','Secret','Freq','BW','SF','CR','Lat','Lon','PIN','Var'].includes(label); }\n";
    out << "  function runAction(index) {\n";
    out << "    const action = (actions[current] || [])[index];\n";
    out << "    if (!action) return;\n";
    out << "    if (isEditAction(action.label)) { openEditor(action.label); return; }\n";
    out << "    show(action.target);\n";
    out << "  }\n";
    out << "  document.addEventListener('click', (event) => {\n";
    out << "    const desktopWindowIcon = event.target.closest('[data-open-window]');\n";
    out << "    if (desktopWindowIcon) { restoreWindow(windowById(desktopWindowIcon.dataset.openWindow)); return; }\n";
    out << "    const icon = event.target.closest('.deskicon[data-target]');\n";
    out << "    if (icon) { show(icon.dataset.target); return; }\n";
    out << "    if (event.target.closest('.close')) { show('Home'); return; }\n";
    out << "    if (event.target.closest('[data-taskbar-chat]')) { show('Inbox'); return; }\n";
    out << "    const action = event.target.closest('[data-action-index]');\n";
    out << "    if (action) { runAction(Number(action.dataset.actionIndex)); return; }\n";
    out << "  });\n";
    out << "  document.addEventListener('keydown', (event) => {\n";
    out << "    if (event.key === 'Escape') { if (document.querySelector('.edit-overlay')) { closeEditor(); } else { show('Home'); } return; }\n";
    out << "    if (event.key !== 'Enter' && event.key !== ' ') return;\n";
    out << "    const desktopWindowIcon = event.target.closest('[data-open-window]');\n";
    out << "    if (desktopWindowIcon) { event.preventDefault(); restoreWindow(windowById(desktopWindowIcon.dataset.openWindow)); return; }\n";
    out << "    const icon = event.target.closest('.deskicon[data-target]');\n";
    out << "    if (icon) { event.preventDefault(); show(icon.dataset.target); return; }\n";
    out << "    if (event.target.closest('[data-taskbar-chat]')) { event.preventDefault(); show('Inbox'); return; }\n";
    out << "  });\n";
    out << "  document.addEventListener('wheel', (event) => {\n";
    out << "    const screen = activeScreen();\n";
    out << "    const main = screen ? screen.querySelector('main') : null;\n";
    out << "    if (!main || screen.dataset.screen === 'Home') return;\n";
    out << "    main.scrollTop += event.deltaY > 0 ? 28 : -28;\n";
    out << "    event.preventDefault();\n";
    out << "  }, {passive:false});\n";
    out << R"JS(
  const hardwareLog = document.querySelector('[data-hw-log]');
  const hardwareStatus = document.querySelector('[data-hw-status]');
  const hardwareDevice = document.querySelector('[data-hw-device]');
  function hardwareBridgeBase() {
    const saved = localStorage.getItem('meshcore-hardware-bridge-url');
    if (saved) return saved.replace(/\/$/, '');
    const host = location.hostname || 'dev-host.local';
    return `https://${host}:8093`;
  }
  function setHardwareBusy(active, label) {
    document.querySelectorAll('[data-hw-action]').forEach((button) => { button.disabled = active; });
    if (hardwareStatus) hardwareStatus.textContent = active ? label : 'Idle';
  }
  function writeHardwareLog(text) {
    if (!hardwareLog) return;
    hardwareLog.textContent = text;
    hardwareLog.scrollTop = hardwareLog.scrollHeight;
  }
  function appendHardwareLog(text) {
    if (!hardwareLog) return;
    hardwareLog.textContent += `\n${text}`;
    hardwareLog.scrollTop = hardwareLog.scrollHeight;
  }
  function formatHardwareResult(data) {
    const lines = [];
    lines.push(`ok=${data.ok === false ? 'false' : 'true'}`);
    if (data.serial?.path) lines.push(`serial=${data.serial.path} rw=${data.serial.read_write ?? ''}`);
    if (data.firmware?.path) lines.push(`firmware=${data.firmware.path}`);
    if (data.firmware?.sha256) lines.push(`sha256=${data.firmware.sha256}`);
    if (data.device?.health?.lines) lines.push(data.device.health.lines.join('\n'));
    if (data.device?.wifi?.lines) lines.push(data.device.wifi.lines.join('\n'));
    if (data.lines) lines.push(data.lines.join('\n'));
    if (data.output) lines.push(data.output);
    if (data.summary) lines.push(JSON.stringify(data.summary, null, 2));
    if (data.error) lines.push(`error=${data.error}`);
    if (lines.length <= 1) lines.push(JSON.stringify(data, null, 2));
    return lines.join('\n');
  }
  async function callHardware(action) {
    const actions = {
      status: { label: 'Status', method: 'GET', path: '/api/status' },
      wifi: { label: 'WiFi', method: 'POST', path: '/api/wifi/start' },
      deploy: { label: 'Deploy', method: 'POST', path: '/api/deploy' },
      hil: { label: 'HIL Test', method: 'POST', path: '/api/hil' },
      build: { label: 'Build', method: 'POST', path: '/api/build' },
      cycle: { label: 'Cycle', method: 'POST', path: '/api/cycle' }
    };
    const spec = actions[action];
    if (!spec) return;
    const base = hardwareBridgeBase();
    setHardwareBusy(true, spec.label);
    writeHardwareLog(`${spec.label} started\n${base}${spec.path}`);
    try {
      const options = { method: spec.method, mode: 'cors', headers: { 'Content-Type': 'application/json' } };
      if (spec.method !== 'GET') options.body = JSON.stringify({});
      const response = await fetch(base + spec.path, options);
      const data = await response.json();
      if (hardwareDevice && data.serial?.path) hardwareDevice.textContent = data.serial.path;
      writeHardwareLog(formatHardwareResult(data));
      if (hardwareStatus) hardwareStatus.textContent = data.ok === false ? 'Error' : 'Ready';
    } catch (error) {
      writeHardwareLog(`Hardware bridge unavailable.\nOpen ${base}/api/status once if the browser asks to trust the certificate.\n${error}`);
      if (hardwareStatus) hardwareStatus.textContent = 'Offline';
    } finally {
      setHardwareBusy(false, hardwareStatus?.textContent || 'Idle');
    }
  }
  document.querySelectorAll('[data-hw-action]').forEach((button) => {
    button.addEventListener('click', () => callHardware(button.dataset.hwAction));
  });
  window.meshcoreHardwareStatus = () => callHardware('status');
)JS";
    out << "  desktopWindows.forEach(initMovableWindow);\n";
    out << "  window.dispatchEvent(new Event('resize'));\n";
    out << "  updateClock();\n";
    out << "  setInterval(updateClock, 10000);\n";
    out << "  show('Home');\n";
    out << "})();\n";
    out << "</script>\n";
}

}  // namespace

int main() {
    auto snapshot = meshcore::make_mock_snapshot();
    snapshot.state.current_epoch_seconds = static_cast<unsigned>(std::time(nullptr));
    const char* build_dir_env = std::getenv("BUILD_DIR");
    const std::filesystem::path build_dir =
        build_dir_env != nullptr && build_dir_env[0] != '\0' ? build_dir_env : "build";
    const std::filesystem::path out_path = build_dir / "index.html";
    const std::filesystem::path legacy_screens_path = build_dir / "screens.html";
    std::filesystem::create_directories(build_dir);
    const auto downloads = copy_downloads(build_dir);
    write_webflash_assets(build_dir, downloads);

    std::ofstream out(out_path);
    out << "<!doctype html>\n<html><head><meta charset=\"utf-8\">";
    out << "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    out << "<title>MeshCore T-Deck Plus Screens</title>\n";
    out << "<script type=\"module\" src=\"https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module\"></script>\n";
    out << "<style>";
    out << "html,body{width:100%;height:100%;min-height:100%}body{position:relative;margin:0;padding:10px 10px 42px;background:#008080;color:#000;font:11px 'MS Sans Serif',Tahoma,Arial,sans-serif;box-sizing:border-box;overflow:hidden}";
    out << ".page-desktop-icons{position:absolute;left:10px;top:10px;z-index:2;display:flex;flex-direction:column;gap:4px}.page-desktop-icon{width:64px;height:68px;padding:3px 2px;background:transparent;border:0;color:#fff;text-align:center;font:11px 'MS Sans Serif',Tahoma,Arial,sans-serif;cursor:pointer}.page-desktop-icon:focus{outline:1px dotted #fff;outline-offset:-1px}.page-desktop-icon span{display:block}.page-installer-icon{width:32px;height:32px;margin:0 auto 3px;background:url(\"data:image/svg+xml,%3Csvg%20xmlns%3D%22http%3A%2F%2Fwww.w3.org%2F2000%2Fsvg%22%20width%3D%2232%22%20height%3D%2232%22%20viewBox%3D%220%200%2032%2032%22%20shape-rendering%3D%22crispEdges%22%3E%3Crect%20x%3D%227%22%20y%3D%223%22%20width%3D%2218%22%20height%3D%2226%22%20fill%3D%22%23303030%22%2F%3E%3Crect%20x%3D%228%22%20y%3D%224%22%20width%3D%2216%22%20height%3D%2224%22%20fill%3D%22%23d8d8d8%22%2F%3E%3Crect%20x%3D%2210%22%20y%3D%227%22%20width%3D%2212%22%20height%3D%2210%22%20fill%3D%22%23ffffff%22%2F%3E%3Crect%20x%3D%2211%22%20y%3D%229%22%20width%3D%2210%22%20height%3D%222%22%20fill%3D%22%23000080%22%2F%3E%3Crect%20x%3D%2211%22%20y%3D%2213%22%20width%3D%227%22%20height%3D%222%22%20fill%3D%22%231084d0%22%2F%3E%3Crect%20x%3D%2210%22%20y%3D%2221%22%20width%3D%2212%22%20height%3D%224%22%20fill%3D%22%23000080%22%2F%3E%3Crect%20x%3D%2212%22%20y%3D%2222%22%20width%3D%228%22%20height%3D%222%22%20fill%3D%22%239ec3ff%22%2F%3E%3C%2Fsvg%3E\") center/32px 32px no-repeat;image-rendering:pixelated}.page-desktop-icon:active .page-installer-icon,.page-desktop-icon:focus .page-installer-icon{filter:brightness(.85)}";
    out << ".page-hardware-icon{width:32px;height:32px;margin:0 auto 3px;background:url(\"data:image/svg+xml,%3Csvg%20xmlns%3D%22http%3A%2F%2Fwww.w3.org%2F2000%2Fsvg%22%20width%3D%2232%22%20height%3D%2232%22%20viewBox%3D%220%200%2032%2032%22%20shape-rendering%3D%22crispEdges%22%3E%3Crect%20x%3D%224%22%20y%3D%226%22%20width%3D%2224%22%20height%3D%2217%22%20fill%3D%22%23303030%22%2F%3E%3Crect%20x%3D%225%22%20y%3D%227%22%20width%3D%2222%22%20height%3D%2215%22%20fill%3D%22%23d8d8d8%22%2F%3E%3Crect%20x%3D%227%22%20y%3D%229%22%20width%3D%2218%22%20height%3D%2210%22%20fill%3D%22%23000080%22%2F%3E%3Crect%20x%3D%229%22%20y%3D%2211%22%20width%3D%2214%22%20height%3D%222%22%20fill%3D%22%239ec3ff%22%2F%3E%3Crect%20x%3D%229%22%20y%3D%2215%22%20width%3D%229%22%20height%3D%222%22%20fill%3D%22%23ffffff%22%2F%3E%3Crect%20x%3D%2213%22%20y%3D%2223%22%20width%3D%226%22%20height%3D%223%22%20fill%3D%22%23808080%22%2F%3E%3Crect%20x%3D%229%22%20y%3D%2226%22%20width%3D%2214%22%20height%3D%223%22%20fill%3D%22%23303030%22%2F%3E%3Crect%20x%3D%2210%22%20y%3D%2227%22%20width%3D%2212%22%20height%3D%221%22%20fill%3D%22%23c0c0c0%22%2F%3E%3C%2Fsvg%3E\") center/32px 32px no-repeat;image-rendering:pixelated}";
    out << ".movable-window{position:absolute;z-index:10;overflow:hidden}.movable-window.minimized,.movable-window.closed{display:none}.install-window,.mock-window,.hardware-window{background:#c0c0c0;border-top:2px solid #fff;border-left:2px solid #fff;border-right:2px solid #404040;border-bottom:2px solid #404040;box-shadow:1px 1px 0 #000;box-sizing:border-box}.install-window{left:10px;top:10px;width:224px;height:62px}.hardware-window{left:82px;top:10px;width:378px;height:286px}.mock-window{left:calc(50% - 162px);top:86px;width:324px;height:266px}.install-title,.mock-title{height:22px;line-height:22px;background:#000080;color:#fff;font-weight:bold;padding:0 3px 0 6px;display:flex;align-items:center;justify-content:space-between;box-sizing:border-box;cursor:move;user-select:none}.window-controls{display:flex;gap:2px}.window-controls button{width:16px;height:14px;line-height:12px;padding:0;background:#c0c0c0;color:#000;border-top:1px solid #fff;border-left:1px solid #fff;border-right:1px solid #404040;border-bottom:1px solid #404040;font-size:10px;font-weight:bold}.window-controls button:active{border-top-color:#404040;border-left-color:#404040;border-right-color:#fff;border-bottom-color:#fff}.install-body{height:40px;display:flex;align-items:center;justify-content:center;padding:4px;box-sizing:border-box}.mock-body{width:320px;height:240px;padding:0;background:#000;border:0;box-sizing:border-box;overflow:hidden}.mock-viewport{width:320px;height:240px;display:block;overflow:hidden}.vnc-frame{display:block;width:320px;height:240px;border:0;background:#000;overflow:hidden}.mock-window .screen{border:0;box-shadow:none}";
    out << ".desktop-taskbar{position:fixed;left:0;right:0;bottom:0;height:28px;background:#c0c0c0;border-top:1px solid #fff;box-shadow:0 -1px 0 #808080;color:#000;display:flex;align-items:center;gap:4px;padding:3px 5px;box-sizing:border-box;z-index:10000}.start-button,.task-button{height:22px;line-height:20px;background:#c0c0c0;color:#000;border-top:1px solid #fff;border-left:1px solid #fff;border-right:1px solid #404040;border-bottom:1px solid #404040;font:11px 'MS Sans Serif',Tahoma,Arial,sans-serif;box-sizing:border-box}.start-button{width:56px;font-weight:bold}.task-buttons{display:flex;align-items:center;gap:3px;min-width:0;flex:1}.task-button{min-width:96px;max-width:160px;padding:0 8px;text-align:left;overflow:hidden;white-space:nowrap;text-overflow:clip}.task-button:active,.task-button.minimized{border-top-color:#404040;border-left-color:#404040;border-right-color:#fff;border-bottom-color:#fff}.page-clock{height:22px;min-width:72px;line-height:20px;text-align:center;background:#c0c0c0;border-top:1px solid #808080;border-left:1px solid #808080;border-right:1px solid #fff;border-bottom:1px solid #fff;box-sizing:border-box;font-weight:bold}";
    out << ".win-button{display:inline-block;width:auto;min-width:118px;height:22px;line-height:20px;padding:0 8px;background:#c0c0c0;color:#000;border-top:1px solid #fff;border-left:1px solid #fff;border-right:1px solid #404040;border-bottom:1px solid #404040;text-decoration:none;font:11px 'MS Sans Serif',Tahoma,Arial,sans-serif;box-sizing:border-box}.win-button:active{border-top-color:#404040;border-left-color:#404040;border-right-color:#fff;border-bottom-color:#fff}.unsupported{display:inline-block;color:#800000;font-weight:bold}";
    out << ".hardware-title{height:22px;line-height:22px;background:#000080;color:#fff;font-weight:bold;padding:0 3px 0 6px;display:flex;align-items:center;justify-content:space-between;box-sizing:border-box;cursor:move;user-select:none}.hardware-body{height:260px;padding:6px;box-sizing:border-box;background:#c0c0c0;color:#000}.hardware-row{height:18px;line-height:18px;display:flex;justify-content:space-between;gap:8px}.hardware-row span{font-weight:bold}.hardware-buttons{display:grid;grid-template-columns:repeat(3,1fr);gap:4px;margin:5px 0}.hardware-buttons button{width:auto;height:22px;line-height:20px;background:#c0c0c0;color:#000;border-top:1px solid #fff;border-left:1px solid #fff;border-right:1px solid #404040;border-bottom:1px solid #404040;font:11px 'MS Sans Serif',Tahoma,Arial,sans-serif;font-weight:bold}.hardware-buttons button:disabled{color:#808080}.hardware-log{height:172px;margin:0;padding:4px;background:#fff;color:#000;border-top:1px solid #808080;border-left:1px solid #808080;border-right:1px solid #fff;border-bottom:1px solid #fff;box-sizing:border-box;overflow:auto;white-space:pre-wrap;font:10px Consolas,'Courier New',monospace}";
    out << "@keyframes classic-window-pop{from{opacity:0;transform:scale(.96)}to{opacity:1;transform:scale(1)}}.experimental-overlay{position:fixed;inset:0;z-index:30000;background:rgba(0,0,0,.22);display:flex;align-items:center;justify-content:center;color:#000}.experimental-dialog{width:386px;background:#c0c0c0;border-top:2px solid #fff;border-left:2px solid #fff;border-right:2px solid #404040;border-bottom:2px solid #404040;box-shadow:1px 1px 0 #000;box-sizing:border-box;animation:classic-window-pop 120ms steps(4,end)}.experimental-title{height:22px;line-height:22px;background:#000080;color:#fff;font-weight:bold;padding:0 6px;box-sizing:border-box}.experimental-body{display:flex;gap:12px;padding:12px 12px 8px;box-sizing:border-box;line-height:15px}.experimental-icon{width:32px;height:32px;flex:0 0 32px;background:#ffe080;border:1px solid #000;clip-path:polygon(50% 0,100% 100%,0 100%);position:relative}.experimental-icon:after{content:'!';position:absolute;left:13px;top:9px;color:#000;font:bold 18px Arial,sans-serif}.experimental-text b{display:block;margin-bottom:5px}.experimental-actions{display:flex;justify-content:flex-end;gap:6px;padding:0 12px 12px}.experimental-actions button{min-width:76px;height:22px;line-height:20px;background:#c0c0c0;color:#000;border-top:1px solid #fff;border-left:1px solid #fff;border-right:1px solid #404040;border-bottom:1px solid #404040;font:11px 'MS Sans Serif',Tahoma,Arial,sans-serif}.experimental-actions button:active{border-top-color:#404040;border-left-color:#404040;border-right-color:#fff;border-bottom-color:#fff}ewt-install-dialog{font:11px 'MS Sans Serif',Tahoma,Arial,sans-serif!important;color:#000!important;--md-sys-color-primary:#000080;--md-sys-color-surface:#c0c0c0;--md-sys-color-on-surface:#000;--md-sys-color-surface-container-high:#c0c0c0;--md-sys-color-surface-container-highest:#c0c0c0;--md-sys-color-outline:#404040}ewt-install-dialog.webtools-minimized{display:none!important}";
    out << ".screen{display:none;width:320px;height:240px;background:#c0c0c0;border-top:1px solid #fff;border-left:1px solid #fff;border-right:1px solid #404040;border-bottom:1px solid #404040;box-shadow:1px 1px 0 #000;box-sizing:border-box;overflow:hidden}.screen.active{display:block}";
    out << ".top{height:24px;background:linear-gradient(90deg,#000080,#1084d0);display:flex;align-items:center;justify-content:space-between;padding:0 4px 0 6px;box-sizing:border-box;font-size:12px;color:#fff}";
    out << ".top b{font-size:14px}.close{width:16px;height:14px;background:#c0c0c0;color:#000;border-top:1px solid #fff;border-left:1px solid #fff;border-right:1px solid #404040;border-bottom:1px solid #404040;padding:0;line-height:12px;font-size:10px;font-weight:bold}.close:active{border-top-color:#404040;border-left-color:#404040;border-right-color:#fff;border-bottom-color:#fff}.toolbar{height:28px;background:#c0c0c0;display:flex;gap:4px;align-items:center;padding:3px 4px;box-sizing:border-box;border-top:1px solid #fff;border-bottom:1px solid #808080}main{height:160px;padding:8px 10px;box-sizing:border-box;font-size:12px;overflow:hidden;color:#000;background:#c0c0c0}";
    out << "nav{height:28px;background:#c0c0c0;display:flex;gap:4px;align-items:center;padding:3px 4px;box-sizing:border-box;border-top:1px solid #fff}";
    out << "button{width:44px;height:22px;border:1px solid #404040;background:#c0c0c0;color:#000;font-weight:bold;border-radius:0;font-size:10px;cursor:pointer}nav button:first-child{width:50px}.toolbar button{width:54px}.top .close{width:16px;height:14px;background:#c0c0c0;color:#000;border-top:1px solid #fff;border-left:1px solid #fff;border-right:1px solid #404040;border-bottom:1px solid #404040;padding:0;line-height:12px;font-size:10px;font-weight:bold}.tray{margin-left:auto;color:#000;font-size:10px;min-width:70px;text-align:right}.clock b{font-weight:700}";
    out << ".listview{position:relative;height:100%;overflow:hidden}.row{height:32px;background:#fff;border:1px solid #808080;display:flex;align-items:center;justify-content:space-between;gap:8px;padding:2px 4px;box-sizing:border-box;margin-bottom:2px}";
    out << ".row b{display:block;font-size:12px;color:#000}.row small{display:block;color:#404040;font-size:10px}.row span{font-size:12px;color:#000;text-align:right}";
    out << ".metric{display:flex;justify-content:space-between;font-size:16px;margin-bottom:8px}.grid2{display:grid;grid-template-columns:1fr 1fr;gap:8px}.grid2 div{background:#17212b;padding:8px}.grid2 b{display:block;font-size:24px}";
    out << ".preview,.message,.compose{background:#fff;border:1px solid #808080;padding:8px;margin:8px 0}.compose{height:108px}.compose span,.coords{color:#404040}.boot{text-align:center;font-size:28px;margin-top:48px}.boot small{font-size:12px;color:#404040}";
    out << ".home-main{height:240px;padding:0;background:#008080;color:#fff}.desktop{height:212px;display:grid;grid-template-columns:repeat(5,58px);grid-auto-rows:72px;gap:0 4px;padding:8px;box-sizing:border-box;position:relative}.deskicon{width:58px;height:58px;text-align:center;color:#fff;font-size:12px;cursor:pointer}.deskicon:focus,.taskbar>b:focus{outline:1px dotted #fff;outline-offset:-1px}.deskicon .icon{position:relative;width:32px;height:32px;margin:0 auto 3px;background-repeat:no-repeat;box-sizing:border-box}.deskicon .icon i{display:none}.deskicon span{display:block}.desktoptoast{position:absolute;left:6px;bottom:6px;width:308px;height:30px;margin:0;background:#c0c0c0;color:#000;border:1px solid #404040;padding:8px 6px;box-sizing:border-box;overflow:hidden;font-size:12px}.taskbar{height:28px;background:#c0c0c0;border-top:1px solid #fff;color:#000;display:flex;align-items:center;gap:4px;padding:3px 6px;box-sizing:border-box}.taskbar>b{display:inline-block;width:50px;height:22px;line-height:22px;text-align:center;border:1px solid #404040;background:#c0c0c0;font-size:10px;cursor:pointer}.taskbar span{font-size:10px;margin-left:auto}";
    out << ".icon-mail{background:linear-gradient(#9ec3ff,#9ec3ff) 6px 10px/20px 4px,linear-gradient(#000080,#000080) 7px 16px/18px 2px,linear-gradient(#000080,#000080) 9px 19px/14px 2px,linear-gradient(#000080,#000080) 12px 22px/8px 2px,linear-gradient(#303030,#303030) 4px 8px/24px 18px,linear-gradient(#fff,#fff) 5px 9px/22px 16px}";
    out << ".icon-edit{background:linear-gradient(#1084d0,#1084d0) 10px 6px/11px 2px,linear-gradient(#808080,#808080) 10px 11px/11px 1px,linear-gradient(#808080,#808080) 10px 15px/9px 1px,linear-gradient(#303030,#303030) 7px 2px/19px 25px,linear-gradient(#fff,#fff) 8px 3px/17px 23px,linear-gradient(#303030,#303030) 4px 25px/19px 3px,linear-gradient(#f8d35b,#f8d35b) 5px 22px/17px 5px}";
    out << ".icon-list{background:linear-gradient(#000080,#000080) 14px 6px/4px 17px,linear-gradient(#000080,#000080) 7px 13px/18px 3px,linear-gradient(#303030,#303030) 2px 4px/12px 10px,linear-gradient(#9ec3ff,#9ec3ff) 3px 5px/10px 8px,linear-gradient(#303030,#303030) 18px 4px/12px 10px,linear-gradient(#9ec3ff,#9ec3ff) 19px 5px/10px 8px,linear-gradient(#303030,#303030) 10px 19px/12px 10px,linear-gradient(#9ec3ff,#9ec3ff) 11px 20px/10px 8px}";
    out << ".icon-folder{background:linear-gradient(#805a00,#805a00) 4px 6px/13px 7px,linear-gradient(#ffe080,#ffe080) 5px 7px/11px 5px,linear-gradient(#805a00,#805a00) 3px 9px/26px 17px,linear-gradient(#f8d35b,#f8d35b) 4px 10px/24px 15px,linear-gradient(#fff,#fff) 7px 14px/18px 2px,linear-gradient(#1084d0,#1084d0) 7px 18px/15px 2px,linear-gradient(#1084d0,#1084d0) 7px 22px/12px 2px}";
    out << ".icon-nav{background:linear-gradient(#303030,#303030) 4px 5px/22px 22px,linear-gradient(#cfeec7,#cfeec7) 5px 6px/20px 20px,linear-gradient(#5da15d,#5da15d) 5px 12px/20px 2px,linear-gradient(#5da15d,#5da15d) 13px 6px/2px 20px,radial-gradient(circle,#fff 0 2px,#c00000 3px 6px,transparent 7px) 18px 16px/8px 8px,linear-gradient(#c00000,#c00000) 20px 23px/3px 5px}";
    out << ".icon-gear{background:linear-gradient(#303030,#303030) 4px 4px/24px 24px,linear-gradient(#d8d8d8,#d8d8d8) 5px 5px/22px 22px,linear-gradient(#000080,#000080) 9px 10px/14px 2px,linear-gradient(#000080,#000080) 9px 16px/14px 2px,linear-gradient(#000080,#000080) 9px 22px/14px 2px,linear-gradient(#ffe080,#ffe080) 12px 8px/4px 6px,linear-gradient(#ffe080,#ffe080) 18px 14px/4px 6px,linear-gradient(#ffe080,#ffe080) 10px 20px/4px 6px}";
    out << ".icon-radio{background:linear-gradient(#303030,#303030) 15px 3px/2px 14px,linear-gradient(#1084d0,#1084d0) 10px 6px/2px 5px,linear-gradient(#1084d0,#1084d0) 20px 6px/2px 5px,linear-gradient(#1084d0,#1084d0) 6px 2px/2px 10px,linear-gradient(#1084d0,#1084d0) 24px 2px/2px 10px,linear-gradient(#303030,#303030) 6px 16px/20px 12px,linear-gradient(#d8d8d8,#d8d8d8) 7px 17px/18px 10px,linear-gradient(#000080,#000080) 10px 20px/5px 4px,radial-gradient(circle,#c00000 0 3px,transparent 4px) 18px 20px/6px 6px}";
    out << ".icon-drive{background:linear-gradient(#303030,#303030) 7px 3px/19px 26px,linear-gradient(#d8d8d8,#d8d8d8) 8px 4px/17px 24px,linear-gradient(#fff,#fff) 10px 8px/13px 1px,linear-gradient(#fff,#fff) 10px 14px/13px 1px,linear-gradient(#fff,#fff) 10px 20px/13px 1px,linear-gradient(#1084d0,#1084d0) 11px 10px/4px 2px,linear-gradient(#1084d0,#1084d0) 11px 16px/4px 2px,linear-gradient(#1084d0,#1084d0) 11px 22px/4px 2px,linear-gradient(#00a000,#00a000) 19px 10px/2px 2px,linear-gradient(#00a000,#00a000) 19px 16px/2px 2px,linear-gradient(#c00000,#c00000) 19px 22px/2px 2px}";
    out << ".icon-warn{background:linear-gradient(#303030,#303030) 5px 4px/22px 24px,linear-gradient(#ffe080,#ffe080) 6px 5px/20px 22px,linear-gradient(#c00000,#c00000) 9px 8px/14px 3px,linear-gradient(#303030,#303030) 15px 13px/3px 8px,linear-gradient(#303030,#303030) 15px 23px/3px 3px}";
    out << ".icon-mail,.icon-edit,.icon-list,.icon-folder,.icon-nav,.icon-gear,.icon-radio,.icon-drive,.icon-warn{background-repeat:no-repeat;image-rendering:pixelated}";
    out << svg_icon_style("mail", R"ICON(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32" shape-rendering="crispEdges"><rect x="4" y="9" width="24" height="16" fill="#303030"/><rect x="5" y="10" width="22" height="14" fill="#ffffff"/><path d="M5 10h22v4L16 21 5 14z" fill="#9ec3ff"/><path d="M5 24l8-8h6l8 8z" fill="#ffffff"/><path d="M5 10l11 10 11-10v4L16 23 5 14z" fill="none" stroke="#000080" stroke-width="2"/></svg>)ICON");
    out << svg_icon_style("edit", R"ICON(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32" shape-rendering="crispEdges"><rect x="7" y="3" width="18" height="23" fill="#303030"/><rect x="8" y="4" width="16" height="21" fill="#ffffff"/><rect x="11" y="8" width="10" height="2" fill="#1084d0"/><rect x="11" y="13" width="9" height="1" fill="#808080"/><rect x="11" y="17" width="8" height="1" fill="#808080"/><path d="M5 24h15v5H5z" fill="#805a00"/><path d="M6 23h15v5H6z" fill="#f8d35b"/><path d="M20 18h4v4h-4z" fill="#303030"/><path d="M18 20h4v4h-4z" fill="#ffe080"/></svg>)ICON");
    out << svg_icon_style("list", R"ICON(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32" shape-rendering="crispEdges"><rect x="3" y="5" width="10" height="9" fill="#303030"/><rect x="4" y="6" width="8" height="7" fill="#9ec3ff"/><rect x="19" y="5" width="10" height="9" fill="#303030"/><rect x="20" y="6" width="8" height="7" fill="#9ec3ff"/><rect x="11" y="20" width="10" height="9" fill="#303030"/><rect x="12" y="21" width="8" height="7" fill="#9ec3ff"/><rect x="14" y="8" width="4" height="16" fill="#000080"/><rect x="7" y="15" width="18" height="3" fill="#000080"/></svg>)ICON");
    out << svg_icon_style("folder", R"ICON(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32" shape-rendering="crispEdges"><path d="M4 7h11l3 4h10v16H4z" fill="#805a00"/><path d="M5 8h9l3 4h10v14H5z" fill="#f8d35b"/><rect x="7" y="15" width="17" height="2" fill="#ffffff"/><rect x="7" y="19" width="15" height="2" fill="#1084d0"/><rect x="7" y="23" width="12" height="2" fill="#1084d0"/></svg>)ICON");
    out << svg_icon_style("nav", R"ICON(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32" shape-rendering="crispEdges"><rect x="4" y="5" width="23" height="23" fill="#303030"/><rect x="5" y="6" width="21" height="21" fill="#cfeec7"/><rect x="5" y="13" width="21" height="2" fill="#5da15d"/><rect x="14" y="6" width="2" height="21" fill="#5da15d"/><rect x="20" y="18" width="7" height="7" fill="#c00000"/><rect x="22" y="20" width="3" height="3" fill="#ffffff"/><rect x="23" y="24" width="2" height="4" fill="#c00000"/></svg>)ICON");
    out << svg_icon_style("gear", R"ICON(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32" shape-rendering="crispEdges"><rect x="13" y="3" width="6" height="5" fill="#808080"/><rect x="13" y="24" width="6" height="5" fill="#808080"/><rect x="3" y="13" width="5" height="6" fill="#808080"/><rect x="24" y="13" width="5" height="6" fill="#808080"/><rect x="8" y="8" width="16" height="16" fill="#303030"/><rect x="9" y="9" width="14" height="14" fill="#d8d8d8"/><rect x="13" y="13" width="6" height="6" fill="#000080"/><rect x="15" y="15" width="2" height="2" fill="#ffffff"/></svg>)ICON");
    out << svg_icon_style("radio", R"ICON(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32" shape-rendering="crispEdges"><rect x="15" y="3" width="2" height="13" fill="#303030"/><rect x="9" y="5" width="2" height="7" fill="#1084d0"/><rect x="21" y="5" width="2" height="7" fill="#1084d0"/><rect x="5" y="2" width="2" height="11" fill="#1084d0"/><rect x="25" y="2" width="2" height="11" fill="#1084d0"/><rect x="6" y="16" width="20" height="12" fill="#303030"/><rect x="7" y="17" width="18" height="10" fill="#d8d8d8"/><rect x="10" y="20" width="6" height="4" fill="#000080"/><rect x="20" y="20" width="5" height="5" fill="#c00000"/></svg>)ICON");
    out << svg_icon_style("drive", R"ICON(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32" shape-rendering="crispEdges"><rect x="7" y="3" width="19" height="26" fill="#303030"/><rect x="8" y="4" width="17" height="24" fill="#d8d8d8"/><rect x="10" y="8" width="13" height="1" fill="#ffffff"/><rect x="10" y="14" width="13" height="1" fill="#ffffff"/><rect x="10" y="20" width="13" height="1" fill="#ffffff"/><rect x="11" y="10" width="4" height="2" fill="#1084d0"/><rect x="11" y="16" width="4" height="2" fill="#1084d0"/><rect x="11" y="22" width="4" height="2" fill="#1084d0"/><rect x="20" y="10" width="2" height="2" fill="#00a000"/><rect x="20" y="16" width="2" height="2" fill="#00a000"/><rect x="20" y="22" width="2" height="2" fill="#c00000"/></svg>)ICON");
    out << svg_icon_style("warn", R"ICON(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32" shape-rendering="crispEdges"><path d="M16 4 29 28H3z" fill="#303030"/><path d="M16 7 26 26H6z" fill="#ffe080"/><rect x="9" y="11" width="14" height="3" fill="#c00000"/><rect x="15" y="15" width="3" height="7" fill="#303030"/><rect x="15" y="24" width="3" height="3" fill="#303030"/></svg>)ICON");
    out << ".chat-main{padding:0;overflow:hidden}.chatapp{height:160px;position:relative;font-size:9px;line-height:1.15;color:#000}.chatstatus{position:absolute;left:4px;top:4px;width:312px;height:12px;display:flex;justify-content:space-between;align-items:center}.chatstatus span{font-size:9px;color:#004000}.chatstatus .channel{color:#000080}.chatstatus .dm{color:#004000}";
    out << ".chatbody{position:absolute;left:4px;top:18px;width:312px;height:114px;display:grid;grid-template-columns:88px 220px;gap:4px}.roster,.transcript{position:relative;background:#fff;border:1px solid #808080;overflow:hidden;box-sizing:border-box}.scrollbar{position:absolute;width:7px;background:#e6e6e6;border:1px solid #808080;box-sizing:border-box}.scrollbar i{position:absolute;left:1px;width:3px;background:#808080;border:1px solid #404040;box-sizing:border-box}.contact-scroll{right:1px;top:14px;height:46px}.channel-scroll{right:1px;top:75px;height:36px}.thread-scroll{right:1px;top:17px;height:92px}.screen-scroll{right:2px;top:2px;height:154px}.roster b{display:block;height:13px;line-height:13px;background:#000080;color:#fff;font-size:9px;font-weight:bold;padding:0 3px;box-sizing:border-box}.buddy{height:16px;display:grid;grid-template-columns:8px 1fr 18px;gap:2px;align-items:center;padding:1px 9px 1px 2px;box-sizing:border-box}.buddy.on{background:#d7e7ff}.buddy i{width:6px;height:6px;background:#00a000;border:1px solid #007000;box-sizing:border-box}.buddy span{overflow:hidden;white-space:nowrap;text-overflow:clip}.buddy small{font-size:8px;color:#404040;text-align:right}.transcript{padding:2px 9px 2px 2px;box-sizing:border-box}.threadtitle{height:12px;line-height:12px;border-bottom:1px solid #d0d0d0;color:#000080;font-weight:bold;margin-bottom:1px}.chatline{display:grid;grid-template-columns:28px 46px 1fr;gap:2px;height:13px;align-items:center;font-size:9px}.chatline time{color:#606060}.chatline b{color:#000080;overflow:hidden;white-space:nowrap;text-overflow:clip}.chatline.out b{color:#008000}.chatline.sys b{color:#606060}.chatline.selected{background:#000080;color:#fff}.chatline.selected time,.chatline.selected b,.chatline.selected.out b{color:#fff}.chatline span{overflow:hidden;white-space:nowrap;text-overflow:clip}.chatline .marquee{display:block;text-overflow:clip}.sendrow{position:absolute;left:4px;top:134px;width:312px;height:24px;display:grid;grid-template-columns:90px 1fr 0;gap:2px;align-items:center}.sendrow span,.sendbox{height:20px;line-height:18px;background:#fff;border:1px solid #808080;box-sizing:border-box;padding:0 3px;font-size:9px;overflow:hidden;white-space:nowrap;text-overflow:clip}.sendrow span{border:0;background:transparent;padding:0}.sendrow button{display:none}";
    out << ".msgstatus{position:absolute;left:96px;top:121px;width:218px;height:10px;text-align:right;font-size:9px;color:#404040;overflow:hidden;white-space:nowrap}";
    out << ".edit-overlay{position:fixed;inset:0;background:rgba(0,0,0,.35);display:flex;align-items:center;justify-content:center;z-index:10}.edit-dialog{width:284px;height:118px;background:#c0c0c0;border:1px solid #404040;box-sizing:border-box;color:#000;font-size:10px;padding:28px 8px 0;position:relative}.edit-title{position:absolute;left:2px;top:2px;width:278px;height:20px;background:#000080;color:#fff;line-height:20px;padding-left:5px;box-sizing:border-box;font-size:12px}.edit-dialog p{margin:0 0 8px}.edit-dialog input{width:268px;height:26px;box-sizing:border-box;border:1px solid #404040;background:#fff;color:#000;font:12px Arial,sans-serif}.edit-dialog small{display:block;color:#004000;margin-top:8px}";
    out << ".log{font-size:10px;margin:3px 0;color:#000}";
    out << ".map{height:118px;background:linear-gradient(90deg,#173126,#243545);position:relative;border:1px solid #31485b}.map:before,.map:after{content:\"\";position:absolute;background:#466070}.map:before{left:95px;top:0;width:2px;height:118px}.map:after{left:0;top:70px;width:300px;height:2px}.pin{position:absolute;left:152px;top:56px;width:12px;height:12px;background:#f0c35a;border-radius:50%}.map span{position:relative;display:inline-block;margin:18px;color:#edf2f4;font-size:12px}";
    out << "h2{font-size:14px;margin:0 0 8px}";
    out << "</style></head><body>\n";
    out << "<div class=\"page-desktop-icons\"><button class=\"page-desktop-icon\" type=\"button\" data-open-window=\"installer\"><span class=\"page-installer-icon\" aria-hidden=\"true\"></span><span>Installer</span></button><button class=\"page-desktop-icon\" type=\"button\" data-open-window=\"hardware\"><span class=\"page-hardware-icon\" aria-hidden=\"true\"></span><span>Hardware</span></button></div>\n";
    render_downloads(out, downloads);
    out << "<section class=\"mock-window movable-window\" data-window=\"mock\" data-title=\"T-Deck Live Hardware\"><div class=\"mock-title window-titlebar\"><span>T-Deck Live Hardware</span><span class=\"window-controls\"><button type=\"button\" aria-label=\"Minimize\" data-window-minimize>_</button><button type=\"button\" aria-label=\"Close\" data-window-close>x</button></span></div><div class=\"mock-body\"><iframe class=\"vnc-frame\" src=\"hardware-tdeck.html\" title=\"Real T-Deck hardware\"></iframe></div></section>\n";
    out << "<section class=\"hardware-window movable-window minimized\" data-window=\"hardware\" data-title=\"Hardware\"><div class=\"hardware-title window-titlebar\"><span>Real T-Deck Hardware</span><span class=\"window-controls\"><button type=\"button\" aria-label=\"Minimize\" data-window-minimize>_</button><button type=\"button\" aria-label=\"Close\" data-window-close>x</button></span></div><div class=\"hardware-body\"><div class=\"hardware-row\"><span>Bridge</span><span data-hw-status>Idle</span></div><div class=\"hardware-row\"><span>Device</span><span data-hw-device>/dev/tdeck-plus</span></div><div class=\"hardware-buttons\"><button type=\"button\" data-hw-action=\"status\">Status</button><button type=\"button\" data-hw-action=\"wifi\">WiFi</button><button type=\"button\" data-hw-action=\"deploy\">Deploy</button><button type=\"button\" data-hw-action=\"hil\">HIL Test</button><button type=\"button\" data-hw-action=\"build\">Build</button><button type=\"button\" data-hw-action=\"cycle\">Cycle</button></div><pre class=\"hardware-log\" data-hw-log>Hardware bridge: https://dev-host.local:8093/api/status\nUse Status first. Cycle builds 915 MHz, OTA deploys, then runs the real-device HIL checks.</pre></div></section>\n";

    const auto screens = meshcore::all_screens();
    out << "<footer class=\"desktop-taskbar\"><button class=\"start-button\" type=\"button\">Start</button><div class=\"task-buttons\" aria-label=\"Minimized windows\"></div><div class=\"page-clock\" aria-label=\"Clock\"></div></footer>\n";
    render_interaction_script(out);
    out << "</body></html>\n";
    out.close();
    std::filesystem::copy_file(out_path, legacy_screens_path,
                               std::filesystem::copy_options::overwrite_existing);

    std::cout << "Generated " << out_path << " with " << screens.size()
              << " screens at " << meshcore::screen_width << "x" << meshcore::screen_height << "\n";
    return 0;
}
