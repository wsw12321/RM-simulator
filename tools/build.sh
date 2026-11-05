#!/usr/bin/env bash
# Builds every native target and runs every native test binary.
# The target list and the exact per-target compiler flags live in
# tools/native_build.py, which compiles each unique translation unit once,
# caches objects by source hash under build/obj and works in parallel.
#   CXX=clang++ bash tools/build.sh     # another compiler
#   JOBS=1 bash tools/build.sh          # serial
set -euo pipefail
cd "$(dirname "$0")/.."
exec python3 tools/native_build.py
