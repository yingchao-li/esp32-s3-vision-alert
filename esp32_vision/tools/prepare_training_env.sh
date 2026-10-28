#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3.10 -m venv tools/.training-env
tools/.training-env/bin/python -m pip install --upgrade pip setuptools wheel
mkdir -p tools/vendor
espdet_dir=tools/vendor/esp-detection
if [ ! -d "$espdet_dir/.git" ]; then
    git clone --no-checkout https://github.com/espressif/esp-detection.git "$espdet_dir"
fi
git -C "$espdet_dir" checkout 28de5902a357d99dd9a5db0f52905255641c5a78
tools/.training-env/bin/python -m pip install -r tools/training-requirements.txt
tools/.training-env/bin/python -m pip check
tools/.training-env/bin/python -m pip freeze > docs/logs/training-environment.freeze.txt
tools/.training-env/bin/python tools/train_four_class.py --help
