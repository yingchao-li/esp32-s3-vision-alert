#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/env.sh
port="${1:?Usage: bash scripts/monitor.sh /dev/cu.usbmodem...}"
idf.py -p "$port" monitor
