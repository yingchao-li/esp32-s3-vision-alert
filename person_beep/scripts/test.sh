#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_binary="$(mktemp -t person_beep_alarm)"
trap 'rm -f "$test_binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -I main tests/alarm_policy_test.cpp -o "$test_binary"
"$test_binary"
shasum -a 256 -c models/SHA256SUMS
for script in scripts/*.sh; do bash -n "$script"; done
