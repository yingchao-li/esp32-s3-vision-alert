#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
binary="$(mktemp -t esp32_vision_tests)"
trap 'rm -f "$binary"' EXIT
clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -I main tests/algorithms_test.cpp main/object_tracker.cpp \
  main/obstacle_analyzer.cpp -o "$binary"
"$binary"
python3.13 tools/verify_model.py models/person_baseline.json
python3.13 tools/verify_model.py models/yolo11n_coco.json --yolo11
python3.13 -m unittest discover -s tests -p '*_test.py' -v
for script in tools/*.sh; do bash -n "$script"; done
