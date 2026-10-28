#!/usr/bin/env python3
"""Capture real board logs for a bounded duration, without changing firmware."""
import argparse
import time
from pathlib import Path
import serial

p = argparse.ArgumentParser()
p.add_argument("port")
p.add_argument("output", type=Path)
p.add_argument("--seconds", type=float, default=30)
p.add_argument("--reset", action="store_true", help="Reset via UART DTR/RTS before capture")
args = p.parse_args()
args.output.parent.mkdir(parents=True, exist_ok=True)
s = serial.Serial(port=None, baudrate=115200, timeout=0.2, exclusive=True)
s.dtr = False
s.rts = False
s.port = args.port
s.open()
if args.reset:
    # ESP32 UART auto-reset circuit: EN LOW then HIGH, BOOT HIGH throughout.
    s.dtr = False
    s.rts = True
    time.sleep(0.1)
    s.rts = False
    time.sleep(0.1)
end = time.monotonic() + args.seconds
with s, args.output.open("wb") as output:
    while time.monotonic() < end:
        chunk = s.read(s.in_waiting or 1)
        if chunk:
            output.write(chunk)
            output.flush()
print(f"Saved {args.output.stat().st_size} bytes to {args.output}")
