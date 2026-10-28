#!/usr/bin/env python3
"""Guarded application-only flash. Never erases NVS or writes a partition table
unless the new layout is an explicit, additive app-partition growth approved
with --approve-layout-change."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

EXPECTED_MAC = "44:b1:76:c7:ed:58"
# 0x3f0000 is the historical layout (stable firmware). 0x6f0000 is the route-A
# layout that fits the 2.86 MB embedded YOLO11n COCO model; app offset and the
# nvs/phy partitions are unchanged.
ALLOWED_APP_SIZES = (0x3F0000, 0x6F0000)


def entries(table):
    result = []
    for offset in range(0, len(table) - 31, 32):
        magic, kind, subtype, address, size, label, flags = struct.unpack_from("<HBBII16sI", table, offset)
        if magic != 0x50AA:
            break
        result.append({"kind": kind, "subtype": subtype, "address": address,
                       "size": size, "label": label.rstrip(b"\0")})
    return result


def app_region(table):
    apps = [e for e in entries(table) if e["kind"] == 0]
    if len(apps) != 1:
        raise ValueError("Unexpected partition layout; explicit user authorization required before any layout change")
    app = apps[0]
    if app["subtype"] != 0 or app["address"] != 0x10000 or app["label"] != b"factory":
        raise ValueError("Unexpected partition layout; explicit user authorization required before any layout change")
    if app["size"] not in ALLOWED_APP_SIZES:
        raise ValueError("Unexpected application partition size; explicit user authorization required")
    return app["address"], app["size"]


def layout_upgrade_ok(current, expected):
    """Only allow the single approved change: growing the app partition in place.

    nvs/phy offsets and sizes must not change, the app offset must stay 0x10000,
    and the new app partition must stay inside the 8 MB flash.
    """
    cur, exp = entries(current), entries(expected)
    if len(cur) != len(exp) or len(exp) == 0:
        return False
    for c, e in zip(cur, exp):
        if (c["label"], c["kind"], c["subtype"], c["address"]) != \
           (e["label"], e["kind"], e["subtype"], e["address"]):
            return False
        if c["kind"] == 0:
            if e["size"] < c["size"]:
                return False
        elif c["size"] != e["size"]:
            return False
        if e["address"] + e["size"] > 8 * 1024 * 1024:
            return False
    return True


def validate_image(image, size):
    if len(image) < 24 or image[0] != 0xE9 or struct.unpack_from("<H", image, 12)[0] != 9:
        raise ValueError("Not an ESP32-S3 firmware image")
    if (len(image) + 4095) // 4096 * 4096 > size:
        raise ValueError("Firmware including erase-sector rounding exceeds existing application partition")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", default="/dev/cu.usbmodem5C930528211")
    p.add_argument("--expected-mac", default=EXPECTED_MAC)
    p.add_argument("--image", type=Path, default=Path("build/esp32_vision.bin"))
    p.add_argument("--approve-layout-change", action="store_true",
                   help="Write the reviewed partition table at 0x8000 when only the app partition grows")
    args = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    report_path = root / "docs/logs/flash-result.json"
    report_path.write_text(json.dumps({"status": "NOT FLASHED", "reason": "Current attempt precheck in progress"}) + "\n")
    image = (root / args.image).resolve() if not args.image.is_absolute() else args.image
    reference = root / "build/partition_table/partition-table.bin"
    if not reference.is_file():
        reference = root / "firmware/partition-table-reference.bin"
    expected = reference.read_bytes()
    address, size = app_region(expected)
    blob = image.read_bytes()
    validate_image(blob, size)
    command = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", args.port, "-b", "115200"]

    def run(extra):
        result = subprocess.run(command + extra, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        print(result.stdout, flush=True)
        if result.returncode:
            raise RuntimeError(f"esptool failed ({result.returncode}); no automatic erase/repartition retry")
        return result.stdout

    probe = run(["flash_id"])
    if not re.search(r"Chip is ESP32-S3\b", probe) or f"MAC: {args.expected_mac.lower()}" not in probe:
        raise ValueError("Connected chip/MAC differs from intended Freenove board")
    if "Detected flash size: 8MB" not in probe:
        raise ValueError("Flash capacity is not the verified 8MB")
    table_written = False
    with tempfile.TemporaryDirectory(prefix="vision_flash_") as temp:
        table = Path(temp) / "partitions.bin"
        run(["read_flash", "0x8000", "0x1000", str(table)])
        current = table.read_bytes()
        app_region(current)  # refuse an unexpected on-device layout outright
        if current[:len(expected)] != expected:
            if not args.approve_layout_change:
                raise ValueError(
                    "On-device partition table differs from this build. Review partitions.csv and re-run with "
                    "--approve-layout-change. Only the app partition may grow; nvs/phy/app offsets stay unchanged.")
            if not layout_upgrade_ok(current, expected):
                raise ValueError("Refusing layout change: not the approved additive app-partition growth")
            new_table = Path(temp) / "partitions_new.bin"
            new_table.write_bytes(expected)
            run(["write_flash", "--flash_size", "keep", "0x8000", str(new_table)])
            verify = Path(temp) / "partitions_verify.bin"
            run(["read_flash", "0x8000", "0x1000", str(verify)])
            if verify.read_bytes()[:len(expected)] != expected:
                raise ValueError("Partition table readback mismatch after write")
            table_written = True
    print(f"PRECHECK PASS: ESP32-S3 {args.expected_mac}, 8MB; app offset={address:#x}, "
          f"size={size:#x}, image={len(blob)}; partition_table_written={table_written}; APPLICATION ONLY.",
          flush=True)
    run(["write_flash", "--flash_size", "keep", hex(address), str(image)])
    record = {"status": "FLASH PASS", "port": args.port, "mac": args.expected_mac, "image": str(image),
              "image_sha256": hashlib.sha256(blob).hexdigest(),
              "image_bytes": len(blob), "offset": address, "partition_bytes": size,
              "partition_table_written": table_written, "bootloader_written": False, "nvs_erased": False,
              "hardware_function_test": "NOT PERFORMED; USER RESPONSIBILITY"}
    report_path.write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record, indent=2), flush=True)


if __name__ == "__main__":
    main()
