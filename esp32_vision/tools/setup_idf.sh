#!/usr/bin/env bash
set -euo pipefail
command -v cmake >/dev/null
command -v python3.13 >/dev/null
export IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-$HOME/.espressif}"
vision_idf_dir="$IDF_TOOLS_PATH/esp-idf-v5.4.2"
if [ ! -d "$vision_idf_dir/.git" ]; then
    git clone --branch v5.4.2 --depth 1 --recursive --shallow-submodules \
      https://github.com/espressif/esp-idf.git "$vision_idf_dir"
fi
test "$(git -C "$vision_idf_dir" describe --tags --exact-match)" = v5.4.2
python3.13 "$vision_idf_dir/tools/idf_tools.py" install --targets esp32s3
python3.13 "$vision_idf_dir/tools/idf_tools.py" install-python-env
