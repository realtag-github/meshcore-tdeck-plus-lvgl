#!/usr/bin/env python3
"""Real T-Deck Plus hardware-in-loop firmware cycle.

Builds the 915 MHz firmware, optionally deploys it over WiFi OTA or USB,
then drives the standalone firmware serial HIL console and writes artifacts.
"""

from __future__ import annotations

import argparse
import datetime as dt
import glob
import hashlib
import json
import os
import shutil
import subprocess
import sys
import termios
import time
import urllib.request
from pathlib import Path
from typing import Iterable, List, Optional, Sequence, Tuple

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE_DIR = ROOT / "firmware"
DIST_DIR = ROOT / "dist"
DEFAULT_FIRMWARE = DIST_DIR / "tdeck-plus-915-firmware.bin"
FAIL_PATTERNS = (
    "Guru Meditation",
    "panic'ed",
    "Backtrace:",
    "StoreProhibited",
    "LoadProhibited",
    "assert failed",
    "abort()",
    "watchdog",
)
BOOT_MARKERS = (
    "boot: setup complete",
    "boot: MeshService started with hardware backend",
)
DEFAULT_COMMANDS = (
    ("hil ping", "hil: pong"),
    ("hil health", "hil: health"),
    ("hil dump-state", "hil: state"),
    ("ble test query", "last_tx=0x0d/"),
    ("ble test app", "last_tx=0x05/"),
    ("ble test contacts", "contacts_total="),
    ("ble test sync", "sync_resp="),
    ("ui home", "hil: ui scheduled=Home"),
    ("ui screen", "hil: screen=Home"),
    ("ui show Inbox", "hil: ui scheduled=Inbox"),
    ("ui screen", "hil: screen=Inbox"),
    ("ui home", "hil: ui scheduled=Home"),
    ("ui screen", "hil: screen=Home"),
    ("ui show Nodes", "hil: ui scheduled=Nodes"),
    ("ui screen", "hil: screen=Nodes"),
    ("ui show Diagnostics", "hil: ui scheduled=Diagnostics"),
    ("ui screen", "hil: screen=Diagnostics"),
    ("mesh inject-node HIL-A Hardware Loop", "hil: injected node"),
    ("mesh inject-direct HIL-A hello-from-hil", "hil: injected direct"),
    ("ui show Inbox", "hil: ui scheduled=Inbox"),
    ("ui screen", "hil: screen=Inbox"),
    ("messages", "hello-from-hil"),
    ("mesh inject-channel test HIL-A hello-test", "hil: injected channel"),
    ("channels", "test"),
    ("status", "name="),
)


def run(cmd: Sequence[str], cwd: Path, log_path: Path) -> None:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("ab") as log:
        log.write(("$ " + " ".join(cmd) + "\n").encode())
        proc = subprocess.Popen(cmd, cwd=str(cwd), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert proc.stdout is not None
        for chunk in iter(lambda: proc.stdout.readline(), b""):
            log.write(chunk)
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
        rc = proc.wait()
        if rc != 0:
            raise RuntimeError(f"command failed rc={rc}: {' '.join(cmd)}")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def find_serial_port(preferred: Optional[str]) -> str:
    if preferred:
        return preferred
    env_port = os.environ.get("MESHCORE_TDECK_SERIAL")
    if env_port:
        return env_port
    candidates: List[str] = []
    for pattern in ("/dev/tdeck-plus", "/dev/serial/by-id/*", "/dev/ttyACM*", "/dev/ttyUSB*", "/dev/cu.usbmodem*", "/dev/cu.usbserial*"):
        candidates.extend(sorted(glob.glob(pattern)))
    if not candidates:
        raise RuntimeError("no serial port found; set --serial or MESHCORE_TDECK_SERIAL")
    return candidates[0]


def wait_for_serial_port(path: str, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if os.path.exists(path):
            return path
        time.sleep(0.25)
    raise TimeoutError(f"serial port did not appear: {path}")


class RawSerial:
    def __init__(self, path: str, baud: int = 115200) -> None:
        self.path = path
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self.fd)
        speed = getattr(termios, f"B{baud}")
        attrs[0] = 0
        attrs[1] = 0
        attrs[2] = termios.CLOCAL | termios.CREAD | termios.CS8
        attrs[3] = 0
        attrs[4] = speed
        attrs[5] = speed
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 1
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        termios.tcflush(self.fd, termios.TCIOFLUSH)
        self._buf = b""

    def close(self) -> None:
        os.close(self.fd)

    def write_line(self, line: str) -> None:
        os.write(self.fd, (line + "\n").encode("utf-8", "replace"))

    def read_lines(self) -> List[str]:
        lines: List[str] = []
        while True:
            try:
                chunk = os.read(self.fd, 4096)
            except BlockingIOError:
                break
            if not chunk:
                break
            self._buf += chunk
        while b"\n" in self._buf:
            raw, self._buf = self._buf.split(b"\n", 1)
            lines.append(raw.rstrip(b"\r").decode("utf-8", "replace"))
        return lines


def append_lines(log_path: Path, lines: Iterable[str]) -> None:
    with log_path.open("a", encoding="utf-8") as log:
        for line in lines:
            print(line)
            log.write(line + "\n")


def check_failures(lines: Iterable[str]) -> Optional[str]:
    for line in lines:
        for pattern in FAIL_PATTERNS:
            if pattern in line:
                return f"serial failure pattern {pattern!r}: {line}"
    return None


def wait_for_markers(ser: RawSerial, markers: Sequence[str], timeout_s: float, log_path: Path) -> None:
    remaining = list(markers)
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline and remaining:
        lines = ser.read_lines()
        if lines:
            append_lines(log_path, lines)
            failure = check_failures(lines)
            if failure:
                raise RuntimeError(failure)
            for line in lines:
                remaining = [marker for marker in remaining if marker not in line]
        time.sleep(0.05)
    if remaining:
        raise TimeoutError("missing boot markers: " + ", ".join(remaining))


def drain_serial(ser: RawSerial, log_path: Path, quiet_s: float = 0.25, max_s: float = 1.5) -> None:
    deadline = time.monotonic() + max_s
    quiet_deadline = time.monotonic() + quiet_s
    drained_any = False
    while time.monotonic() < deadline:
        lines = ser.read_lines()
        if lines:
            drained_any = True
            append_lines(log_path, lines)
            quiet_deadline = time.monotonic() + quiet_s
            continue
        if drained_any and time.monotonic() >= quiet_deadline:
            return
        time.sleep(0.05)


def send_command(
    ser: RawSerial,
    command: str,
    expect: str,
    timeout_s: float,
    log_path: Path,
    attempts: int = 2,
) -> List[str]:
    seen: List[str] = []
    last_timeout: Optional[TimeoutError] = None
    for attempt in range(max(1, attempts)):
        drain_serial(ser, log_path)
        with log_path.open("a", encoding="utf-8") as log:
            log.write(f"> {command}\n")
        print(f"> {command}")
        ser.write_line(command)
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            lines = ser.read_lines()
            if lines:
                seen.extend(lines)
                append_lines(log_path, lines)
                failure = check_failures(lines)
                if failure:
                    raise RuntimeError(failure)
                if any(expect in line for line in lines):
                    post_deadline = time.monotonic() + 0.35
                    while time.monotonic() < post_deadline:
                        extra = ser.read_lines()
                        if extra:
                            seen.extend(extra)
                            append_lines(log_path, extra)
                            failure = check_failures(extra)
                            if failure:
                                raise RuntimeError(failure)
                        time.sleep(0.05)
                    return seen
            time.sleep(0.05)
        last_timeout = TimeoutError(
            f"command did not produce {expect!r}: {command}"
            + (f" (attempt {attempt + 1}/{attempts})" if attempts > 1 else "")
        )
    raise last_timeout or TimeoutError(f"command did not produce {expect!r}: {command}")


def upload_wifi(host: str, firmware: Path, timeout_s: int, log_path: Path) -> None:
    boundary = "----meshcore-hil-boundary"
    data = firmware.read_bytes()
    body = (
        f"--{boundary}\r\n"
        f"Content-Disposition: form-data; name=\"firmware\"; filename=\"{firmware.name}\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n"
    ).encode() + data + f"\r\n--{boundary}--\r\n".encode()
    url = f"http://{host}/update"
    req = urllib.request.Request(url, data=body, method="POST", headers={"Content-Type": f"multipart/form-data; boundary={boundary}"})
    with log_path.open("a", encoding="utf-8") as log:
        log.write(f"POST {url} firmware={firmware} bytes={firmware.stat().st_size}\n")
    with urllib.request.urlopen(req, timeout=timeout_s) as response:
        text = response.read().decode("utf-8", "replace")
    with log_path.open("a", encoding="utf-8") as log:
        log.write(text + "\n")
    print(text.strip())


def publish_artifacts(run_dir: Path, summary_path: Path, serial_log: Path) -> None:
    build_dir = ROOT / "simulator" / "build"
    build_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(summary_path, build_dir / "hil-latest.json")
    if serial_log.exists():
        shutil.copy2(serial_log, build_dir / "hil-latest-serial.log")
    webroot = Path("/tmp/meshcore-8092-web")
    if webroot.exists():
        shutil.copy2(summary_path, webroot / "hil-latest.json")
        if serial_log.exists():
            shutil.copy2(serial_log, webroot / "hil-latest-serial.log")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build/deploy/test MeshCore T-Deck Plus 915 MHz on real hardware")
    parser.add_argument("--serial", help="serial device, defaults to MESHCORE_TDECK_SERIAL or auto-detect")
    parser.add_argument("--ota-host", default=os.environ.get("MESHCORE_TDECK_HOST", "meshcore-tdeck.local"), help="WiFi OTA host or IP")
    parser.add_argument("--upload", choices=("wifi", "usb", "none"), default=os.environ.get("HIL_UPLOAD", "wifi"))
    parser.add_argument("--no-build", action="store_true", help="skip PlatformIO build/package")
    parser.add_argument("--firmware", type=Path, default=DEFAULT_FIRMWARE)
    parser.add_argument("--boot-timeout", type=float, default=90.0)
    parser.add_argument("--command-timeout", type=float, default=4.0)
    parser.add_argument("--artifacts-dir", type=Path, default=ROOT / "artifacts" / "hil")
    parser.add_argument("--no-publish", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    started = dt.datetime.now(dt.UTC).strftime("%Y%m%dT%H%M%SZ")
    run_dir = args.artifacts_dir / started
    run_dir.mkdir(parents=True, exist_ok=True)
    build_log = run_dir / "build.log"
    serial_log = run_dir / "serial.log"
    summary_path = run_dir / "summary.json"
    summary = {
        "started_utc": started,
        "upload": args.upload,
        "ota_host": args.ota_host,
        "serial": None,
        "firmware": str(args.firmware),
        "firmware_sha256": None,
        "passed": False,
        "steps": [],
        "artifacts": str(run_dir),
    }

    try:
        if not args.no_build:
            os.environ["BUILD_ENVS"] = "tdeck-plus-915"
            run(["pio", "run", "-e", "tdeck-plus-915"], FIRMWARE_DIR, build_log)
            run([str(ROOT / "tools" / "package_firmware.sh")], ROOT, build_log)
            summary["steps"].append("built tdeck-plus-915")
        if not args.firmware.exists():
            raise FileNotFoundError(args.firmware)
        summary["firmware_sha256"] = sha256_file(args.firmware)
        serial_path = find_serial_port(args.serial)
        summary["serial"] = serial_path

        if args.upload == "wifi":
            upload_wifi(args.ota_host, args.firmware, 45, run_dir / "ota-upload.log")
            summary["steps"].append("uploaded over wifi ota")
            time.sleep(1.0)
        elif args.upload == "usb":
            cmd = ["pio", "run", "-e", "tdeck-plus-915", "-t", "upload", "--upload-port", serial_path]
            run(cmd, FIRMWARE_DIR, run_dir / "usb-upload.log")
            summary["steps"].append("uploaded over usb")

        wait_for_serial_port(serial_path, 20)
        ser = RawSerial(serial_path)
        try:
            if args.upload == "none":
                try:
                    send_command(ser, "hil ping", "hil: pong", min(args.command_timeout, 5.0), serial_log)
                    summary["steps"].append("serial console already responsive")
                    commands = DEFAULT_COMMANDS[1:]
                except Exception:
                    try:
                        drain_serial(ser, serial_log)
                        send_command(ser, "hil ping", "hil: pong", min(args.command_timeout, 5.0), serial_log)
                        summary["steps"].append("serial console responsive after drain")
                        commands = DEFAULT_COMMANDS[1:]
                    except Exception:
                        wait_for_markers(ser, BOOT_MARKERS, args.boot_timeout, serial_log)
                        summary["steps"].append("boot markers observed")
                        commands = DEFAULT_COMMANDS
            else:
                wait_for_markers(ser, BOOT_MARKERS, args.boot_timeout, serial_log)
                summary["steps"].append("boot markers observed")
                commands = DEFAULT_COMMANDS
            for command, expect in commands:
                send_command(ser, command, expect, args.command_timeout, serial_log)
                summary["steps"].append(command)
        finally:
            ser.close()
        summary["passed"] = True
        return 0
    except Exception as exc:  # noqa: BLE001 - this is a test runner boundary
        summary["error"] = str(exc)
        print(f"HIL FAILED: {exc}", file=sys.stderr)
        return 1
    finally:
        summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        if not args.no_publish:
            try:
                publish_artifacts(run_dir, summary_path, serial_log)
            except Exception as exc:  # noqa: BLE001
                print(f"publish failed: {exc}", file=sys.stderr)
        print(f"summary: {summary_path}")


if __name__ == "__main__":
    raise SystemExit(main())
