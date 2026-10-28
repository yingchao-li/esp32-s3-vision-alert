#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/env.sh
port="${1:?Usage: bash scripts/flash.sh /dev/cu.usbmodem...}"
# 115200 was selected because this board's CH343 link dropped packets at
# 460800/921600 during full-flash readback. Do not erase the whole flash.
idf.py -p "$port" -b 115200 flash
