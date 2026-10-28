#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/env.sh
python tools/flash_app.py --port "${1:-/dev/cu.usbmodem5C930528211}" --image ../stable_firmware/person_beep.bin
