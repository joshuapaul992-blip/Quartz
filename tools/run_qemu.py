#!/usr/bin/env python3
"""Quartz dev-loop QEMU driver.

Boots the Quartz dev images headlessly, lets the guest run for a bit, captures a
screenshot of the emulated framebuffer via the QEMU HMP monitor, converts it to
PNG, and shuts the VM down. Used for fast visual verification during development.
"""
import argparse
import os
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def hmp(sock_path, command, timeout=10):
    """Send one HMP command over the QEMU monitor unix socket, return reply text."""
    deadline = time.time() + timeout
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    while True:
        try:
            s.connect(sock_path)
            break
        except (FileNotFoundError, ConnectionRefusedError):
            if time.time() > deadline:
                raise
            time.sleep(0.1)
    s.settimeout(timeout)
    # drain the banner
    try:
        time.sleep(0.2)
        s.recv(65536)
    except socket.timeout:
        pass
    s.sendall((command + "\n").encode())
    time.sleep(0.4)
    out = b""
    try:
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            out += chunk
            if b"(qemu)" in chunk:
                break
    except socket.timeout:
        pass
    s.close()
    return out.decode(errors="replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--boot", default=os.path.join(ROOT, "dist", "boot.img"))
    ap.add_argument("--disk", default=os.path.join(ROOT, "dist", "disk.img"))
    ap.add_argument("--out", default=os.path.join(ROOT, "dist", "screen.png"))
    ap.add_argument("--wait", type=float, default=18.0,
                    help="seconds to let the guest boot/run before screenshot")
    ap.add_argument("--mem", default="64")
    ap.add_argument("--vga", default="std", help="qemu -vga type (std gives Bochs VBE+LFB)")
    ap.add_argument("--shots", type=int, default=1, help="number of screenshots")
    ap.add_argument("--interval", type=float, default=3.0, help="seconds between shots")
    ap.add_argument("--serial", default=os.path.join(ROOT, "dist", "serial.log"))
    args = ap.parse_args()

    tmpd = tempfile.mkdtemp(prefix="quartz-qemu-")
    monsock = os.path.join(tmpd, "mon.sock")

    cmd = [
        "qemu-system-i386",
        "-m", args.mem,
        "-vga", args.vga,
        "-display", "none",
        "-rtc", "base=localtime",
        "-boot", "a",
        "-monitor", f"unix:{monsock},server,nowait",
        "-serial", f"file:{args.serial}",
        "-no-reboot",
    ]
    if os.path.exists(args.boot):
        cmd += ["-fda", args.boot]
    if args.disk and os.path.exists(args.disk):
        cmd += ["-hda", args.disk]

    print("launching:", " ".join(cmd), flush=True)
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    shots = []
    try:
        time.sleep(args.wait)
        for i in range(args.shots):
            ppm = os.path.join(tmpd, f"shot{i}.ppm")
            reply = hmp(monsock, f"screendump {ppm}")
            if os.path.exists(ppm) and os.path.getsize(ppm) > 0:
                out = args.out if args.shots == 1 else args.out.replace(".png", f"_{i}.png")
                to_png(ppm, out)
                shots.append(out)
                print(f"captured {out} ({os.path.getsize(out)} bytes)", flush=True)
            else:
                print(f"screendump {i} produced nothing; monitor said: {reply!r}", flush=True)
            if i + 1 < args.shots:
                time.sleep(args.interval)
        hmp(monsock, "quit")
    finally:
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
    if not shots:
        sys.exit(2)


def to_png(ppm_path, png_path):
    """Convert PPM (P6) to PNG with no external deps."""
    with open(ppm_path, "rb") as f:
        data = f.read()
    assert data[:2] == b"P6", "not a P6 ppm"
    # parse header: P6 <w> <h> <maxval>\n<pixels>
    idx = 2
    fields = []
    while len(fields) < 3:
        while idx < len(data) and data[idx] in b" \t\r\n":
            idx += 1
        if idx < len(data) and data[idx:idx+1] == b"#":
            while idx < len(data) and data[idx] not in b"\r\n":
                idx += 1
            continue
        start = idx
        while idx < len(data) and data[idx] not in b" \t\r\n":
            idx += 1
        fields.append(int(data[start:idx]))
    idx += 1  # single whitespace after maxval
    w, h, _maxv = fields
    pixels = data[idx:idx + w * h * 3]
    write_png(png_path, w, h, pixels)


def write_png(path, w, h, rgb):
    import struct
    import zlib

    def chunk(typ, payload):
        c = struct.pack(">I", len(payload)) + typ + payload
        return c + struct.pack(">I", zlib.crc32(typ + payload) & 0xFFFFFFFF)

    raw = bytearray()
    stride = w * 3
    for y in range(h):
        raw.append(0)
        raw += rgb[y * stride:(y + 1) * stride]
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(sig)
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


if __name__ == "__main__":
    main()
