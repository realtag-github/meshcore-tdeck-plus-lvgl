#include "wifi_ota_service.h"
#include "app_config.h"

#include <Arduino.h>

#ifndef APP_ENABLE_WIFI_OTA
#define APP_ENABLE_WIFI_OTA 0
#endif

#ifndef APP_WIFI_OTA_SSID
#define APP_WIFI_OTA_SSID "meshcore-tdeck"
#endif

#ifndef APP_WIFI_OTA_HOSTNAME
#define APP_WIFI_OTA_HOSTNAME "meshcore-tdeck"
#endif

#ifndef APP_WIFI_STA_SSID
#define APP_WIFI_STA_SSID ""
#endif

#ifndef APP_WIFI_STA_PASSWORD
#define APP_WIFI_STA_PASSWORD ""
#endif

#if APP_ENABLE_WIFI_OTA && defined(ARDUINO_ARCH_ESP32)
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

namespace {

WebServer ota_server(80);
bool ota_started = false;
bool ota_upload_failed = false;
bool wifi_connecting = false;
uint32_t wifi_connect_started_ms = 0;
uint32_t last_wifi_status_ms = 0;
String configured_ssid;
String configured_password;
String configured_hostname;

const char index_html[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>MeshCore T-Deck OTA</title>
  <style>
    body{font-family:system-ui,sans-serif;margin:2rem;max-width:42rem}
    code{background:#eee;padding:.1rem .3rem}
    input,button{font:inherit;margin:.4rem 0}
  </style>
</head>
<body>
  <h1>MeshCore T-Deck OTA</h1>
  <p>Upload a PlatformIO app image, for example <code>tdeck-plus-915-firmware.bin</code>.</p>
  <form method="POST" action="/update" enctype="multipart/form-data">
    <input type="file" name="firmware" accept=".bin" required>
    <br>
    <button type="submit">Upload and reboot</button>
  </form>
  <p><a href="/health">health</a></p>
</body>
</html>
)HTML";

void send_plain(int code, const String& body) {
    ota_server.send(code, "text/plain", body);
}

void handle_update_upload() {
    HTTPUpload& upload = ota_server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        ota_upload_failed = false;
        Serial.printf("wifi ota: upload start name=%s size=%u\n",
                      upload.filename.c_str(),
                      static_cast<unsigned>(upload.totalSize));
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            ota_upload_failed = true;
            Serial.print("wifi ota: begin failed: ");
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (!ota_upload_failed && Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            ota_upload_failed = true;
            Serial.print("wifi ota: write failed: ");
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (!ota_upload_failed && Update.end(true)) {
            Serial.printf("wifi ota: success bytes=%u rebooting\n", static_cast<unsigned>(upload.totalSize));
        } else {
            ota_upload_failed = true;
            Serial.print("wifi ota: end failed: ");
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        ota_upload_failed = true;
        Update.abort();
        Serial.println("wifi ota: upload aborted");
    }
}

void handle_update_done() {
    if (ota_upload_failed || Update.hasError()) {
        send_plain(500, "OTA failed\n");
        return;
    }
    send_plain(200, "OK rebooting\n");
    delay(250);
    ESP.restart();
}

void start_server() {
    if (ota_started) {
        return;
    }
    ota_server.on("/", HTTP_GET, []() {
        ota_server.send_P(200, "text/html", index_html);
    });
    ota_server.on("/health", HTTP_GET, []() {
        String body;
        body.reserve(160);
        body += "ok\nssid=";
        body += WiFi.SSID();
        body += "\nip=";
        body += WiFi.localIP().toString();
        body += "\nhostname=";
        body += configured_hostname;
        body += "\napp=";
        body += APP_NAME;
        body += "\nversion=";
        body += APP_VERSION;
        body += "\nuptime_ms=";
        body += String(millis());
        body += "\nfree_heap=";
        body += String(ESP.getFreeHeap());
        body += "\n";
        send_plain(200, body);
    });
    ota_server.on("/update", HTTP_POST, handle_update_done, handle_update_upload);
    ota_server.onNotFound([]() {
        send_plain(404, "not found\n");
    });
    ota_server.begin();
    ota_started = true;
}

void stop_server() {
    if (!ota_started) {
        return;
    }
    ota_server.stop();
    ota_started = false;
}

void load_config() {
    configured_ssid = APP_WIFI_STA_SSID;
    configured_password = APP_WIFI_STA_PASSWORD;
    configured_hostname = APP_WIFI_OTA_HOSTNAME;

    Preferences prefs;
    if (prefs.begin("meshcore-wifi", false)) {
        if (prefs.isKey("ssid")) {
            configured_ssid = prefs.getString("ssid", configured_ssid);
        }
        if (prefs.isKey("pass")) {
            configured_password = prefs.getString("pass", configured_password);
        }
        if (prefs.isKey("host")) {
            configured_hostname = prefs.getString("host", configured_hostname);
        }
        prefs.end();
    }

    if (configured_hostname.length() == 0) {
        configured_hostname = APP_WIFI_OTA_HOSTNAME;
    }
}

void begin_connect_wifi() {
    if (configured_ssid.length() == 0) {
        Serial.println("wifi ota: disabled, set credentials with: wifi set <ssid> <password>");
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(configured_hostname.c_str());
    WiFi.begin(configured_ssid.c_str(), configured_password.c_str());
    wifi_connecting = true;
    wifi_connect_started_ms = millis();
    last_wifi_status_ms = 0;
    Serial.printf("wifi ota: connecting ssid=%s hostname=%s\n",
                  configured_ssid.c_str(),
                  configured_hostname.c_str());
}

void poll_connect_wifi() {
    if (!wifi_connecting) {
        return;
    }
    if (WiFi.status() == WL_CONNECTED) {
        wifi_connecting = false;
        if (MDNS.begin(configured_hostname.c_str())) {
            MDNS.addService("http", "tcp", 80);
            Serial.printf("wifi ota: mdns http://%s.local/update\n", configured_hostname.c_str());
        } else {
            Serial.println("wifi ota: mdns failed");
        }
        start_server();
        Serial.printf("wifi ota: ready ip=%s url=http://%s/update\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.localIP().toString().c_str());
        return;
    }
    const uint32_t now = millis();
    if (last_wifi_status_ms == 0 || now - last_wifi_status_ms >= 3000U) {
        last_wifi_status_ms = now;
        Serial.printf("wifi ota: connecting status=%d elapsed=%lus\n",
                      static_cast<int>(WiFi.status()),
                      static_cast<unsigned long>((now - wifi_connect_started_ms) / 1000U));
    }
    if (millis() - wifi_connect_started_ms >= 15000U) {
        wifi_connecting = false;
        Serial.printf("wifi ota: connect failed status=%d\n", static_cast<int>(WiFi.status()));
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
}

}  // namespace
#endif

void wifi_ota_begin() {
#if APP_ENABLE_WIFI_OTA && defined(ARDUINO_ARCH_ESP32)
    load_config();
    if (configured_ssid.length() == 0) {
        Serial.println("wifi ota: idle, set credentials with: wifi set <ssid> <password>");
    } else {
        Serial.printf("wifi ota: idle ssid=%s, use 'wifi start' to connect\n", configured_ssid.c_str());
    }
#else
    Serial.println("wifi ota: disabled");
#endif
}

void wifi_ota_loop() {
#if APP_ENABLE_WIFI_OTA && defined(ARDUINO_ARCH_ESP32)
    poll_connect_wifi();
    if (ota_started) {
        ota_server.handleClient();
    }
#endif
}

void wifi_ota_start() {
#if APP_ENABLE_WIFI_OTA && defined(ARDUINO_ARCH_ESP32)
    if (ota_started || wifi_connecting) {
        Serial.println(wifi_ota_status());
        return;
    }
    load_config();
    begin_connect_wifi();
#else
    Serial.println("wifi ota: disabled");
#endif
}

void wifi_ota_restart() {
#if APP_ENABLE_WIFI_OTA && defined(ARDUINO_ARCH_ESP32)
    wifi_ota_stop();
    load_config();
    begin_connect_wifi();
#else
    Serial.println("wifi ota: disabled");
#endif
}

void wifi_ota_stop() {
#if APP_ENABLE_WIFI_OTA && defined(ARDUINO_ARCH_ESP32)
    wifi_connecting = false;
    last_wifi_status_ms = 0;
    stop_server();
    MDNS.end();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("wifi ota: stopped");
#endif
}

bool wifi_ota_save_credentials(const String& ssid, const String& password, const String& hostname) {
#if APP_ENABLE_WIFI_OTA && defined(ARDUINO_ARCH_ESP32)
    if (ssid.length() == 0) {
        return false;
    }
    Preferences prefs;
    if (!prefs.begin("meshcore-wifi", false)) {
        return false;
    }
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    if (hostname.length() > 0) {
        prefs.putString("host", hostname);
    }
    prefs.end();
    configured_ssid = ssid;
    configured_password = password;
    if (hostname.length() > 0) {
        configured_hostname = hostname;
    }
    return true;
#else
    (void)ssid;
    (void)password;
    (void)hostname;
    return false;
#endif
}

bool wifi_ota_save_hostname(const String& hostname) {
#if APP_ENABLE_WIFI_OTA && defined(ARDUINO_ARCH_ESP32)
    if (hostname.length() == 0) {
        return false;
    }
    Preferences prefs;
    if (!prefs.begin("meshcore-wifi", false)) {
        return false;
    }
    prefs.putString("host", hostname);
    prefs.end();
    configured_hostname = hostname;
    return true;
#else
    (void)hostname;
    return false;
#endif
}

void wifi_ota_clear_credentials() {
#if APP_ENABLE_WIFI_OTA && defined(ARDUINO_ARCH_ESP32)
    Preferences prefs;
    if (prefs.begin("meshcore-wifi", false)) {
        prefs.clear();
        prefs.end();
    }
    configured_ssid = "";
    configured_password = "";
    configured_hostname = APP_WIFI_OTA_HOSTNAME;
    wifi_ota_stop();
#endif
}

String wifi_ota_status() {
#if APP_ENABLE_WIFI_OTA && defined(ARDUINO_ARCH_ESP32)
    String value;
    value.reserve(180);
    value += "wifi ota: ";
    value += ota_started ? "ready" : (wifi_connecting ? "connecting" : "stopped");
    value += " ssid=";
    value += configured_ssid.length() > 0 ? configured_ssid : "(unset)";
    value += " status=";
    value += String(static_cast<int>(WiFi.status()));
    value += " ip=";
    value += WiFi.localIP().toString();
    value += " host=";
    value += configured_hostname.length() > 0 ? configured_hostname : String(APP_WIFI_OTA_HOSTNAME);
    value += " autostart=off";
    return value;
#else
    return "wifi ota: disabled";
#endif
}
