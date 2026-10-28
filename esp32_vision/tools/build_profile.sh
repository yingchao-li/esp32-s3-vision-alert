#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/env.sh
case "${1:-pipeline}" in
  pipeline) idf.py -DIDF_TARGET=esp32s3 build ;;
  dual) idf.py -B build-dual -DSDKCONFIG="$PWD/sdkconfig.dual" \
          -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.dual" -DIDF_TARGET=esp32s3 build ;;
  *) echo 'Usage: bash tools/build_profile.sh pipeline|dual' >&2; exit 2 ;;
esac
