#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash tools/build.sh
./build/capture_rules_demo > build/capture_demo.out
python3 -m py_compile native_pipe.py tests/test_native_pipe.py
python3 tests/test_native_pipe.py
python3 -m py_compile server.py tests/test_server.py tests/test_bridge_unit.py scenario_harness/cli.py tests/test_scenario_harness.py
python3 tests/test_scenario_harness.py
python3 -m py_compile integrated_scenario/cli.py tests/test_integrated_scenario.py
python3 tests/test_integrated_scenario.py
python3 -m py_compile objective_mode/build_id.py tests/test_objective_api.py
python3 tests/test_objective_api.py
python3 -m py_compile arena_format/*.py tests/test_arena_format.py tests/test_arena_http.py tests/test_arena_review.py
python3 tests/test_arena_format.py
python3 tests/test_arena_review.py
python3 -m py_compile layered_mode/*.py tests/test_layered_bridge.py tests/test_layered_review.py tests/test_layered_http.py tests/test_layered_profile_limits.py
python3 tests/test_layered_bridge.py
python3 tests/test_layered_review.py
python3 tests/test_layered_profile_limits.py
python3 -m py_compile tests/test_policy_http.py tests/test_policy_review.py tests/test_policy_bridge.py
python3 tests/test_policy_bridge.py
python3 tests/test_policy_review.py
python3 -m py_compile combat_mode/*.py tests/test_combat_http.py tests/test_combat_review.py
python3 tests/test_combat_review.py
node --check web/combat.js
node --check tests/combat_ui_handlers.cjs
python3 -m py_compile tactical_mode/*.py tests/test_tactical_http.py tests/test_tactical_review.py
python3 tests/test_tactical_review.py
python3 -m py_compile tests/test_damage_review.py
python3 tests/test_damage_review.py
python3 -m py_compile tests/test_heat_review.py
python3 tests/test_heat_review.py
python3 -m py_compile roster_mode/*.py tests/test_roster_review.py tests/test_roster_http.py tests/test_roster_bridge.py
python3 tests/test_roster_bridge.py
python3 tests/test_roster_review.py
python3 -m py_compile robot_profiles_v1/*.py tests/test_robot_profiles.py tests/test_robot_profiles_review.py tests/test_sourced_profiles_http.py
python3 tests/test_robot_profiles.py
python3 tests/test_robot_profiles_review.py
python3 -m py_compile tests/test_growth_catalog_crosscheck.py
python3 tests/test_growth_catalog_crosscheck.py
python3 -m py_compile match_lab/*.py tests/test_match_lab.py
python3 tests/test_match_lab.py
node --check web/roster-profiles.js
node --check tests/sourced_profiles_ui_handlers.cjs
node --check web/roster.js
node --check web/lab.js
node --check tests/lab_browser_smoke.cjs
node --check tests/roster_ui_handlers.cjs
node --check web/tactical.js
node --check tests/tactical_ui_handlers.cjs
node --check web/app.js
node --check web/arena.js
node --check tests/arena_ui_handlers.cjs
node --check web/layered.js
node --check tests/layered_ui_handlers.cjs
node --check tests/policy_ui_handlers.cjs
node --check tests/objective_ui_handlers.cjs
node --check tests/objective_browser_smoke.cjs
python3 tests/test_bridge_unit.py
PORT="${TEST_PORT:-18765}"
python3 server.py --port "$PORT" > build/check-server.log 2>&1 &
PID=$!
trap 'kill "$PID" 2>/dev/null || true' EXIT
# Readiness, not a fixed sleep: concurrent native builds can delay Python startup.
if ! python3 - "$PORT" "$PID" <<'PY_READY'
import os, sys, time, urllib.error, urllib.request
port, pid = int(sys.argv[1]), int(sys.argv[2])
deadline = time.monotonic() + 10
while time.monotonic() < deadline:
    try:
        os.kill(pid, 0)
        with urllib.request.urlopen(f'http://127.0.0.1:{port}/api/meta', timeout=.25) as response:
            if response.status == 200:
                os.kill(pid, 0)
                break
    except ProcessLookupError:
        raise SystemExit('Test server exited before readiness')
    except (OSError, urllib.error.URLError):
        time.sleep(.1)
else:
    raise SystemExit('Test server did not become ready within 10 seconds')
PY_READY
then
  cat build/check-server.log >&2
  exit 1
fi
python3 tests/test_server.py "$PORT"
python3 tests/test_objective_http.py "$PORT"
python3 tests/test_arena_http.py "$PORT"
python3 tests/test_layered_http.py "$PORT"
python3 tests/test_policy_http.py "$PORT"
node tests/ui_handlers.cjs "http://127.0.0.1:$PORT"

node tests/objective_ui_handlers.cjs "http://127.0.0.1:$PORT"
node tests/arena_ui_handlers.cjs "http://127.0.0.1:$PORT"
node tests/layered_ui_handlers.cjs "http://127.0.0.1:$PORT"
node tests/policy_ui_handlers.cjs "http://127.0.0.1:$PORT"

python3 tests/test_combat_http.py "$PORT"
node tests/combat_ui_handlers.cjs "http://127.0.0.1:$PORT"

python3 tests/test_tactical_http.py "$PORT"
node tests/tactical_ui_handlers.cjs "http://127.0.0.1:$PORT"

python3 tests/test_roster_http.py "$PORT"
python3 tests/test_lab_http.py "$PORT"
node tests/roster_ui_handlers.cjs "http://127.0.0.1:$PORT"

python3 tests/test_sourced_profiles_http.py "$PORT"
node tests/sourced_profiles_ui_handlers.cjs "http://127.0.0.1:$PORT"
