#!/usr/bin/env python3
"""HTTPS bridge between the browser WebUI and the attached T-Deck Plus.

This is intentionally a local development tool. It exposes serial, OTA, and
hardware-in-loop actions to the web UI so firmware/UI changes can be tested on
real hardware without leaving the browser.
"""

from __future__ import annotations

import argparse
import contextlib
import datetime as dt
import hashlib
import http.server
import json
import os
import re
import ssl
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / 'tools'
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import hil_tdeck_loop as hil  # noqa: E402

DIST_DIR = ROOT / 'dist'
DEFAULT_FIRMWARE = DIST_DIR / 'tdeck-plus-915-firmware.bin'
DEFAULT_OTA_HOST = os.environ.get('MESHCORE_TDECK_HOST', '<TDECK_IP>')
SERIAL_LOCK = threading.Lock()
COMMAND_LOCK = threading.Lock()


def now_id() -> str:
    return dt.datetime.now(dt.UTC).strftime('%Y%m%dT%H%M%SZ')


def sha256_file(path: Path) -> Optional[str]:
    if not path.exists():
        return None
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def firmware_info(path: Path = DEFAULT_FIRMWARE) -> Dict[str, Any]:
    return {
        'path': str(path),
        'exists': path.exists(),
        'size': path.stat().st_size if path.exists() else 0,
        'sha256': sha256_file(path),
    }


def serial_path() -> str:
    return hil.find_serial_port(os.environ.get('MESHCORE_TDECK_SERIAL'))


def ensure_serial_permissions(path: str) -> None:
    if os.access(path, os.R_OK | os.W_OK):
        return
    with contextlib.suppress(Exception):
        subprocess.run(['sudo', 'chmod', '666', path], timeout=5, check=False)


def read_json_body(handler: http.server.BaseHTTPRequestHandler) -> Dict[str, Any]:
    length = int(handler.headers.get('Content-Length', '0') or '0')
    if length <= 0:
        return {}
    raw = handler.rfile.read(length)
    if not raw:
        return {}
    return json.loads(raw.decode('utf-8'))


def run_process(command: Sequence[str], cwd: Path, timeout_s: float, env: Optional[Dict[str, str]] = None) -> Dict[str, Any]:
    started = time.monotonic()
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    try:
        proc = subprocess.run(
            list(command),
            cwd=str(cwd),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            env=merged_env,
            timeout=timeout_s,
            check=False,
        )
        output = (proc.stdout or '').splitlines()
        return {
            'ok': proc.returncode == 0,
            'returncode': proc.returncode,
            'duration_s': round(time.monotonic() - started, 2),
            'command': list(command),
            'output': '\n'.join(output[-700:]),
        }
    except subprocess.TimeoutExpired as exc:
        output = (exc.stdout or '') if isinstance(exc.stdout, str) else (exc.stdout or b'').decode('utf-8', 'replace')
        return {
            'ok': False,
            'returncode': None,
            'duration_s': round(time.monotonic() - started, 2),
            'command': list(command),
            'error': f'timeout after {timeout_s:.0f}s',
            'output': '\n'.join(output.splitlines()[-700:]),
        }


def serial_exchange(command: str, expect: Optional[str] = None, timeout_s: float = 4.0) -> Dict[str, Any]:
    path = serial_path()
    ensure_serial_permissions(path)
    started = time.monotonic()
    with SERIAL_LOCK:
        ser = hil.RawSerial(path)
        try:
            ser.write_line(command)
            lines: List[str] = []
            deadline = time.monotonic() + timeout_s
            while time.monotonic() < deadline:
                got = ser.read_lines()
                if got:
                    lines.extend(got)
                    failure = hil.check_failures(got)
                    if failure:
                        return {'ok': False, 'serial': path, 'command': command, 'lines': lines, 'error': failure}
                    if expect and any(expect in line for line in got):
                        return {'ok': True, 'serial': path, 'command': command, 'lines': lines, 'duration_s': round(time.monotonic() - started, 2)}
                time.sleep(0.05)
            ok = expect is None
            result: Dict[str, Any] = {'ok': ok, 'serial': path, 'command': command, 'lines': lines, 'duration_s': round(time.monotonic() - started, 2)}
            if expect and not ok:
                result['error'] = f"did not see {expect!r}"
            return result
        finally:
            ser.close()


KEY_VALUE_RE = re.compile(r'(\w+)=([^=]*?)(?=\s+\w+=|$)')


def first_matching_line(result: Dict[str, Any], prefix: str) -> str:
    for line in result.get('lines', []):
        if line.startswith(prefix):
            return line
    return ''


def parse_key_values(line: str) -> Dict[str, str]:
    return {match.group(1): match.group(2).strip() for match in KEY_VALUE_RE.finditer(line)}


def parse_screen(line: str) -> str:
    values = parse_key_values(line)
    return values.get('screen', 'Home') or 'Home'


def parse_messages(lines: List[str]) -> List[Dict[str, Any]]:
    messages: List[Dict[str, Any]] = []
    for line in lines:
        if line.startswith('rx ') or line.startswith('tx '):
            parts = line.split(' ', 2)
            if len(parts) < 3:
                continue
            sender = ''
            body = parts[2]
            if ': ' in parts[2]:
                sender, body = parts[2].split(': ', 1)
            messages.append({'direction': parts[0], 'id': parts[1], 'sender': sender, 'body': body, 'raw': line})
    return messages


def parse_channels(lines: List[str]) -> List[Dict[str, Any]]:
    channels: List[Dict[str, Any]] = []
    for line in lines:
        match = re.match(r'^(\d+)\s+([*-])\s+(.+?)(?:\s+secret=(.*))?$', line)
        if not match:
            continue
        channels.append({
            'index': int(match.group(1)),
            'active': match.group(2) == '*',
            'name': match.group(3).strip(),
            'secret': (match.group(4) or '').strip(),
            'raw': line,
        })
    return channels


def parse_nodes(lines: List[str]) -> List[Dict[str, str]]:
    nodes: List[Dict[str, str]] = []
    for line in lines:
        if not line or line.startswith('unknown command') or line.startswith('>'):
            continue
        if line.startswith('node ') or line.startswith('0x') or line.startswith('HIL-'):
            nodes.append({'raw': line})
    return nodes


def snapshot_payload() -> Dict[str, Any]:
    if COMMAND_LOCK.locked():
        return {'ok': False, 'busy': True, 'error': 'hardware bridge is busy'}
    payload: Dict[str, Any] = {
        'ok': True,
        'bridge': 'meshcore hardware bridge',
        'root': str(ROOT),
        'ota_host': DEFAULT_OTA_HOST,
        'firmware': firmware_info(),
        'serial': {'path': None, 'exists': False, 'read_write': False},
    }
    try:
        path = serial_path()
        ensure_serial_permissions(path)
        payload['serial'] = {'path': path, 'exists': os.path.exists(path), 'read_write': os.access(path, os.R_OK | os.W_OK)}
        screen = serial_exchange('ui screen', 'hil: screen=', 2.0)
        health = serial_exchange('hil health', 'hil: health', 2.5)
        state = serial_exchange('hil dump-state', 'hil: state', 2.5)
        status = serial_exchange('status', 'name=', 2.0)
        messages = serial_exchange('messages', None, 0.7)
        channels = serial_exchange('channels', None, 0.7)
        nodes = serial_exchange('nodes', None, 0.7)

        health_line = first_matching_line(health, 'hil: health')
        state_line = first_matching_line(state, 'hil: state')
        status_line = first_matching_line(status, 'name=')
        screen_line = first_matching_line(screen, 'hil: screen=')
        health_values = parse_key_values(health_line)
        state_values = parse_key_values(state_line)
        status_values = parse_key_values(status_line)
        active_screen = screen_line.split('=', 1)[1].strip() if '=' in screen_line else parse_screen(health_line or state_line)
        payload.update({
            'screen': active_screen or health_values.get('screen') or state_values.get('screen') or 'Home',
            'health': {'ok': health.get('ok'), 'line': health_line, 'values': health_values, 'raw': health.get('lines', [])},
            'state': {'ok': state.get('ok'), 'line': state_line, 'values': state_values, 'raw': state.get('lines', [])},
            'status': {'ok': status.get('ok'), 'line': status_line, 'values': status_values, 'raw': status.get('lines', [])},
            'messages': parse_messages(messages.get('lines', [])),
            'channels': parse_channels(channels.get('lines', [])),
            'nodes': parse_nodes(nodes.get('lines', [])),
            'raw': {
                'screen': screen.get('lines', []),
                'messages': messages.get('lines', []),
                'channels': channels.get('lines', []),
                'nodes': nodes.get('lines', []),
            },
        })
    except Exception as exc:  # noqa: BLE001
        payload['ok'] = False
        payload['error'] = str(exc)
    return payload


def status_payload() -> Dict[str, Any]:
    payload: Dict[str, Any] = {
        'ok': True,
        'bridge': 'meshcore hardware bridge',
        'root': str(ROOT),
        'ota_host': DEFAULT_OTA_HOST,
        'firmware': firmware_info(),
        'serial': {'path': None, 'exists': False, 'read_write': False},
    }
    try:
        path = serial_path()
        ensure_serial_permissions(path)
        payload['serial'] = {'path': path, 'exists': os.path.exists(path), 'read_write': os.access(path, os.R_OK | os.W_OK)}
        ping = serial_exchange('hil ping', 'hil: pong', 2.5)
        health = serial_exchange('hil health', 'hil: health', 3.0) if ping.get('ok') else {'ok': False, 'lines': []}
        wifi = serial_exchange('wifi status', 'wifi ota:', 2.0)
        payload['device'] = {'ping': ping, 'health': health, 'wifi': wifi}
    except Exception as exc:  # noqa: BLE001
        payload['ok'] = False
        payload['error'] = str(exc)
    return payload


def start_wifi() -> Dict[str, Any]:
    result = serial_exchange('wifi start', 'wifi ota: ready', 22.0)
    if not result.get('ok'):
        status = serial_exchange('wifi status', 'wifi ota:', 4.0)
        result['status'] = status
        result['ok'] = bool(status.get('ok') and any('ready' in line for line in status.get('lines', [])))
    return result


def deploy_ota(host: str, firmware: Path) -> Dict[str, Any]:
    if not firmware.exists():
        return {'ok': False, 'error': f'firmware missing: {firmware}', 'firmware': firmware_info(firmware)}
    wifi = start_wifi()
    log_path = ROOT / 'artifacts' / 'web-hardware' / f'ota-{now_id()}.log'
    log_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        hil.upload_wifi(host, firmware, 60, log_path)
        return {'ok': True, 'host': host, 'firmware': firmware_info(firmware), 'wifi': wifi, 'log': str(log_path), 'output': log_path.read_text(encoding='utf-8', errors='replace')}
    except Exception as exc:  # noqa: BLE001
        return {'ok': False, 'host': host, 'firmware': firmware_info(firmware), 'wifi': wifi, 'log': str(log_path), 'error': str(exc), 'output': log_path.read_text(encoding='utf-8', errors='replace') if log_path.exists() else ''}


def run_hil(host: str, upload: str = 'wifi') -> Dict[str, Any]:
    path = serial_path()
    ensure_serial_permissions(path)
    wifi: Optional[Dict[str, Any]] = None
    if upload == 'wifi':
        wifi = start_wifi()
    result = run_process(
        ['python3', str(TOOLS / 'hil_tdeck_loop.py'), '--upload', upload, '--no-build', '--serial', path, '--ota-host', host],
        ROOT,
        180,
    )
    if wifi is not None:
        result['wifi'] = wifi
    return result


def run_build() -> Dict[str, Any]:
    build = run_process(['pio', 'run', '-e', 'tdeck-plus-915'], ROOT / 'firmware', 420)
    if not build.get('ok'):
        return build
    package = run_process([str(TOOLS / 'package_firmware.sh')], ROOT, 240, {'BUILD_ENVS': 'tdeck-plus-915'})
    package['build_output'] = build.get('output', '')
    return package


def run_cycle(host: str) -> Dict[str, Any]:
    path = serial_path()
    ensure_serial_permissions(path)
    wifi = start_wifi()
    result = run_process(
        [str(TOOLS / 'dev_cycle.sh'), '--upload', 'wifi', '--serial', path, '--ota-host', host],
        ROOT,
        600,
        {'FAST_VERIFY': '0'},
    )
    result['wifi'] = wifi
    return result


class HardwareHandler(http.server.BaseHTTPRequestHandler):
    server_version = 'MeshCoreHardwareBridge/1.0'

    def log_message(self, fmt: str, *args: Any) -> None:
        sys.stderr.write('[hardware-bridge] ' + fmt % args + '\n')

    def send_json(self, payload: Dict[str, Any], status: int = 200) -> None:
        raw = json.dumps(payload, indent=2).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(raw)))
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()
        self.wfile.write(raw)

    def do_OPTIONS(self) -> None:
        self.send_response(204)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

    def do_GET(self) -> None:
        try:
            path = self.path.split('?', 1)[0]
            if path in ('/', '/api/status', '/api/health'):
                if path == '/':
                    self.send_json({'ok': True, 'service': 'meshcore hardware bridge', 'status': '/api/status'})
                else:
                    self.send_json(status_payload())
                return
            if path == '/api/snapshot':
                self.send_json(snapshot_payload())
                return
            if path == '/api/latest-hil':
                summary = ROOT / 'simulator' / 'build' / 'hil-latest.json'
                serial = ROOT / 'simulator' / 'build' / 'hil-latest-serial.log'
                self.send_json({
                    'ok': summary.exists(),
                    'summary': json.loads(summary.read_text()) if summary.exists() else None,
                    'serial_log': serial.read_text(encoding='utf-8', errors='replace')[-20000:] if serial.exists() else '',
                })
                return
            self.send_json({'ok': False, 'error': 'not found'}, 404)
        except Exception as exc:  # noqa: BLE001
            self.send_json({'ok': False, 'error': str(exc)}, 500)

    def do_POST(self) -> None:
        if not COMMAND_LOCK.acquire(blocking=False):
            self.send_json({'ok': False, 'error': 'hardware bridge is busy'}, 409)
            return
        try:
            body = read_json_body(self)
            host = str(body.get('host') or DEFAULT_OTA_HOST)
            firmware = Path(body.get('firmware') or DEFAULT_FIRMWARE)
            if not firmware.is_absolute():
                firmware = ROOT / firmware

            if self.path == '/api/command':
                command = str(body.get('command') or 'hil ping')
                expect = body.get('expect')
                timeout_s = float(body.get('timeout_s') or 4.0)
                self.send_json(serial_exchange(command, str(expect) if expect else None, timeout_s))
                return
            if self.path == '/api/wifi/start':
                self.send_json(start_wifi())
                return
            if self.path == '/api/deploy':
                self.send_json(deploy_ota(host, firmware))
                return
            if self.path == '/api/hil':
                self.send_json(run_hil(host, str(body.get('upload') or 'wifi')))
                return
            if self.path == '/api/build':
                self.send_json(run_build())
                return
            if self.path == '/api/cycle':
                self.send_json(run_cycle(host))
                return
            self.send_json({'ok': False, 'error': 'not found'}, 404)
        except Exception as exc:  # noqa: BLE001
            self.send_json({'ok': False, 'error': str(exc)}, 500)
        finally:
            COMMAND_LOCK.release()


def main() -> None:
    parser = argparse.ArgumentParser(description='MeshCore T-Deck Plus hardware bridge')
    parser.add_argument('--port', type=int, default=int(os.environ.get('HARDWARE_BRIDGE_PORT', '8081')))
    parser.add_argument('--cert', required=True)
    parser.add_argument('--key', required=True)
    args = parser.parse_args()

    server = http.server.ThreadingHTTPServer(('', args.port), HardwareHandler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certfile=args.cert, keyfile=args.key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    print(f'hardware bridge listening on https://0.0.0.0:{args.port}', flush=True)
    server.serve_forever()


if __name__ == '__main__':
    main()
