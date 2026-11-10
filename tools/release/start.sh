#!/usr/bin/env bash
# RMUC Tactics Lab release launcher for Linux / macOS / WSL. Finds Python 3;
# tools/release/launch.py then checks version and platform, starts the
# local-only server and opens the browser once it answers.
#   bash start.sh                 # http://127.0.0.1:8765
#   bash start.sh --port 8766
#   bash start.sh --no-browser
# Must stay compatible with bash 3.2 (macOS /bin/bash).
set -euo pipefail
cd "$(dirname "$0")"

PYTHON="${PYTHON:-python3}"
if ! command -v "$PYTHON" >/dev/null 2>&1; then
  echo "未找到 Python 3（需要 3.10 或更高版本，只用标准库，不需要 pip 安装任何包）。" >&2
  echo "  Ubuntu / Debian / WSL：sudo apt install python3" >&2
  echo "  macOS：brew install python，或从 https://www.python.org/downloads/ 安装" >&2
  exit 1
fi
exec "$PYTHON" tools/release/launch.py "$@"
