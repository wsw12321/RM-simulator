#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if ! command -v em++ >/dev/null; then
  echo "Emscripten em++ is not installed. No installation or network action was attempted." >&2
  exit 2
fi
mkdir -p build/wasm
BUILD_ID="$(python3 -c "import hashlib,pathlib; print(hashlib.sha256(b''.join(pathlib.Path(p).read_bytes() for p in ['src/sim.hpp','src/sim.cpp','src/policy.cpp','src/directions.inc'])).hexdigest())")"
em++ -std=c++20 -O2 "-DSIM_BUILD_ID=\"$BUILD_ID\"" src/sim.cpp src/policy.cpp src/wasm.cpp \
 -sMODULARIZE=1 -sEXPORT_NAME=SandboxModule -sALLOW_MEMORY_GROWTH=1 \
 -sEXPORTED_FUNCTIONS='["_sandbox_reset","_sandbox_state_json","_sandbox_replay_json","_sandbox_frame"]' \
 -sEXPORTED_RUNTIME_METHODS='["cwrap","UTF8ToString"]' -o build/wasm/sandbox.js
printf 'Built experimental C ABI module. Browser transport adapter and cross-target validation remain.\n'
