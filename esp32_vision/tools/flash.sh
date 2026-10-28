#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/env.sh
port="${1:-/dev/cu.usbmodem5C930528211}"
if [ "$#" -gt 0 ]; then shift; fi
python tools/flash_app.py --port "$port" "$@"
