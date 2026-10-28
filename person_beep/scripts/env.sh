#!/usr/bin/env bash
# Source this file from bash or zsh.
export IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-$HOME/.espressif}"
export IDF_PATH="${IDF_PATH:-$IDF_TOOLS_PATH/esp-idf-v5.4.2}"
if [ -z "${IDF_PYTHON_ENV_PATH:-}" ]; then
    export IDF_PYTHON_ENV_PATH="$IDF_TOOLS_PATH/python_env/idf5.4_py3.13_env"
fi
if [ ! -x "$IDF_PYTHON_ENV_PATH/bin/python" ]; then
    echo "Missing IDF Python environment. Run: bash scripts/setup.sh" >&2
    return 1
fi
. "$IDF_PYTHON_ENV_PATH/bin/activate"
. "$IDF_PATH/export.sh"
