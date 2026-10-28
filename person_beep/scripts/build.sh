#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/env.sh
idf.py -DIDF_TARGET=esp32s3 build
