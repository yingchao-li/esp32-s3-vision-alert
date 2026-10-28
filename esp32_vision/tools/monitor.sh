#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/env.sh
idf.py -p "${1:-/dev/cu.usbmodem5C930528211}" monitor
