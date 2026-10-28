#!/usr/bin/env bash
set -euo pipefail
command -v git >/dev/null
command -v cmake >/dev/null
command -v python3.13 >/dev/null || { echo 'Install Python 3.13 first: brew install python@3.13'; exit 1; }
export IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-$HOME/.espressif}"
esp_idf_dir="$IDF_TOOLS_PATH/esp-idf-v5.4.2"
if [ ! -d "$esp_idf_dir" ]; then
    git clone --branch v5.4.2 --depth 1 --recursive --shallow-submodules \
        https://github.com/espressif/esp-idf.git "$esp_idf_dir"
fi
test "$(git -C "$esp_idf_dir" describe --tags --exact-match)" = v5.4.2
python3.13 "$esp_idf_dir/tools/idf_tools.py" install --targets esp32s3
python3.13 "$esp_idf_dir/tools/idf_tools.py" install-python-env
echo 'Ready. Run: bash scripts/build.sh'
