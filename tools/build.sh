#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
CXX="${CXX:-g++}"
BUILD_ID="$(python3 -c "import hashlib,pathlib; print(hashlib.sha256(b''.join(pathlib.Path(p).read_bytes() for p in ['src/sim.hpp','src/sim.cpp','src/policy.cpp','src/directions.inc'])).hexdigest())")"
FLAGS=("-DSIM_BUILD_ID=\"$BUILD_ID\"" -Isrc -std=c++20 -Wall -Wextra -Wpedantic -Werror -O2)
"$CXX" "${FLAGS[@]}" src/sim.cpp src/policy.cpp src/cli.cpp -o build/rmuc_sandbox
"$CXX" "${FLAGS[@]}" src/sim.cpp src/policy.cpp tests/core_tests.cpp -o build/core_tests
./build/core_tests
"$CXX" "${FLAGS[@]}" -Irules/rmuc2027_v1 rules/rmuc2027_v1/match_rules.cpp tests/match_rule_tests.cpp -o build/match_rule_tests
"$CXX" "${FLAGS[@]}" -Irules/rmuc2027_v1 rules/rmuc2027_v1/match_rules.cpp rules/rmuc2027_v1/demo.cpp -o build/match_rules_demo
./build/match_rule_tests
"$CXX" "${FLAGS[@]}" -Irules/rmuc2027_capture_v1 rules/rmuc2027_capture_v1/capture_rules.cpp tests/capture_rule_tests.cpp -o build/capture_rule_tests
"$CXX" "${FLAGS[@]}" -Irules/rmuc2027_capture_v1 rules/rmuc2027_capture_v1/capture_rules.cpp rules/rmuc2027_capture_v1/demo.cpp -o build/capture_rules_demo
./build/capture_rule_tests
"$CXX" "${FLAGS[@]}" -Irules/rmuc2027_capture_v1 rules/rmuc2027_capture_v1/capture_rules.cpp tests/capture_rule_property_tests.cpp -o build/capture_rule_property_tests
./build/capture_rule_property_tests
SCENARIO_BUILD_ID="$(python3 scenario_harness/cli.py build-id)"
"$CXX" "${FLAGS[@]}" "-DSCENARIO_BUILD_ID=\"$SCENARIO_BUILD_ID\"" -Irules/rmuc2027_v1 \
  rules/rmuc2027_v1/match_rules.cpp scenario_harness/driver.cpp -o build/rmuc_scenario_driver
INTEGRATED_BUILD_ID="$(python3 integrated_scenario/cli.py build-id)"
"$CXX" "${FLAGS[@]}" "-DINTEGRATED_BUILD_ID=\"$INTEGRATED_BUILD_ID\"" \
  -Irules/rmuc2027_capture_v1 -Irules/rmuc2027_v1 \
  rules/rmuc2027_capture_v1/capture_rules.cpp rules/rmuc2027_v1/match_rules.cpp \
  integrated_scenario/driver.cpp -o build/rmuc_integrated_driver
OBJECTIVE_BUILD_ID="$(python3 objective_mode/build_id.py)"
OBJECTIVE_FLAGS=("${FLAGS[@]}" "-DOBJECTIVE_BUILD_ID=\"$OBJECTIVE_BUILD_ID\"" -Iobjective_mode -Irules/rmuc2027_capture_v1 -Irules/rmuc2027_v1)
OBJECTIVE_SOURCES=(src/sim.cpp rules/rmuc2027_capture_v1/capture_rules.cpp rules/rmuc2027_v1/match_rules.cpp objective_mode/objective.cpp)
"$CXX" "${OBJECTIVE_FLAGS[@]}" "${OBJECTIVE_SOURCES[@]}" objective_mode/cli.cpp -o build/rmuc_objective
"$CXX" "${OBJECTIVE_FLAGS[@]}" "${OBJECTIVE_SOURCES[@]}" tests/objective_tests.cpp -o build/objective_tests
./build/objective_tests

"$CXX" "${OBJECTIVE_FLAGS[@]}" "${OBJECTIVE_SOURCES[@]}" tests/objective_adapter_tests.cpp -o build/objective_adapter_tests
./build/objective_adapter_tests

LAYERED_BUILD_ID="$(python3 layered_mode/build_id.py)"
LAYERED_FLAGS=(-std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror "-DLAYERED_BUILD_ID=\"$LAYERED_BUILD_ID\"")
"$CXX" "${LAYERED_FLAGS[@]}" layered_mode/core.cpp layered_mode/policy_adapter.cpp layered_mode/example_policy.cpp layered_mode/cli.cpp -o build/rmuc_layered
"$CXX" "${LAYERED_FLAGS[@]}" layered_mode/core.cpp tests/layered_core_tests.cpp -o build/layered_core_tests
./build/layered_core_tests
"$CXX" "${LAYERED_FLAGS[@]}" layered_mode/core.cpp layered_mode/policy_adapter.cpp tests/policy_review_tests.cpp -o build/policy_review_tests
./build/policy_review_tests

COMBAT_BUILD_ID="$(python3 combat_mode/build_id.py)"
COMBAT_FLAGS=(-std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror "-DCOMBAT_BUILD_ID=\"$COMBAT_BUILD_ID\"")
COMBAT_SOURCES=(src/sim.cpp combat_mode/core.cpp)
"$CXX" "${COMBAT_FLAGS[@]}" "${COMBAT_SOURCES[@]}" combat_mode/example_policy.cpp combat_mode/cli.cpp -o build/rmuc_combat
"$CXX" "${COMBAT_FLAGS[@]}" "${COMBAT_SOURCES[@]}" combat_mode/example_policy.cpp tests/combat_core_tests.cpp -o build/combat_core_tests
./build/combat_core_tests
"$CXX" "${COMBAT_FLAGS[@]}" "${COMBAT_SOURCES[@]}" tests/combat_review_tests.cpp -o build/combat_review_tests
./build/combat_review_tests

TACTICAL_BUILD_ID="$(python3 tactical_mode/build_id.py)"
TACTICAL_FLAGS=(-std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror "-DTACTICAL_BUILD_ID=\"$TACTICAL_BUILD_ID\"" -Isrc -Irules/rmuc2027_capture_v1 -Irules/rmuc2027_v1)
TACTICAL_SOURCES=(src/sim.cpp combat_mode/core.cpp rules/rmuc2027_capture_v1/capture_rules.cpp rules/rmuc2027_v1/match_rules.cpp rules/rmuc2027_damage_v1/damage_rules.cpp rules/rmuc2027_heat_v1/heat_rules.cpp tactical_mode/core.cpp)
"$CXX" "${TACTICAL_FLAGS[@]}" "${TACTICAL_SOURCES[@]}" tactical_mode/example_policy.cpp tactical_mode/cli.cpp -o build/rmuc_tactical
"$CXX" "${TACTICAL_FLAGS[@]}" "${TACTICAL_SOURCES[@]}" tactical_mode/example_policy.cpp tests/tactical_core_tests.cpp -o build/tactical_core_tests
./build/tactical_core_tests
"$CXX" "${TACTICAL_FLAGS[@]}" "${TACTICAL_SOURCES[@]}" tactical_mode/example_policy.cpp tests/tactical_review_tests.cpp -o build/tactical_review_tests
./build/tactical_review_tests

"$CXX" "${FLAGS[@]}" rules/rmuc2027_damage_v1/damage_rules.cpp tests/damage_rule_tests.cpp -o build/damage_rule_tests
./build/damage_rule_tests
"$CXX" "${TACTICAL_FLAGS[@]}" "${TACTICAL_SOURCES[@]}" tactical_mode/example_policy.cpp tests/tactical_damage_tests.cpp -o build/tactical_damage_tests
./build/tactical_damage_tests
"$CXX" "${TACTICAL_FLAGS[@]}" "${TACTICAL_SOURCES[@]}" tactical_mode/example_policy.cpp tests/damage_review_tests.cpp -o build/damage_review_tests
./build/damage_review_tests

"$CXX" "${FLAGS[@]}" rules/rmuc2027_heat_v1/heat_rules.cpp tests/heat_rule_tests.cpp -o build/heat_rule_tests
./build/heat_rule_tests
"$CXX" "${TACTICAL_FLAGS[@]}" "${TACTICAL_SOURCES[@]}" tactical_mode/example_policy.cpp tests/tactical_heat_tests.cpp -o build/tactical_heat_tests
./build/tactical_heat_tests
"$CXX" "${TACTICAL_FLAGS[@]}" "${TACTICAL_SOURCES[@]}" tactical_mode/example_policy.cpp tests/heat_review_tests.cpp -o build/heat_review_tests
./build/heat_review_tests

# The isolated roster authority reuses compiled rule objects within this build.
# Stop running servers before rebuilding executables; live test runs use frozen builds.
ROSTER_BUILD_ID="$(python3 roster_mode/build_id.py)"
ROSTER_FLAGS=(-std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror "-DROSTER_BUILD_ID=\"$ROSTER_BUILD_ID\"" -Isrc -Irules/rmuc2027_capture_v1 -Irules/rmuc2027_v1)
ROSTER_SOURCES=(src/sim.cpp rules/rmuc2027_capture_v1/capture_rules.cpp rules/rmuc2027_v1/match_rules.cpp rules/rmuc2027_damage_v1/damage_rules.cpp rules/rmuc2027_heat_v1/heat_rules.cpp roster_mode/core.cpp roster_mode/example_policy.cpp)
ROSTER_OBJECTS=()
mkdir -p build/roster-objects
for source in "${ROSTER_SOURCES[@]}"; do
    object="build/roster-objects/${source//\//_}.o"
    "$CXX" "${ROSTER_FLAGS[@]}" -c "$source" -o "$object"
    ROSTER_OBJECTS+=("$object")
done
"$CXX" "${ROSTER_FLAGS[@]}" "${ROSTER_OBJECTS[@]}" roster_mode/cli.cpp -o build/rmuc_roster
"$CXX" "${ROSTER_FLAGS[@]}" "${ROSTER_OBJECTS[@]}" tests/roster_core_tests.cpp -o build/roster_core_tests
./build/roster_core_tests
"$CXX" "${ROSTER_FLAGS[@]}" "${ROSTER_OBJECTS[@]}" tests/roster_review_tests.cpp -o build/roster_review_tests
./build/roster_review_tests

# Source-linked 2027 robot growth module (performance tables, levels, experience).
"$CXX" "${FLAGS[@]}" rules/rmuc2027_growth_v1/growth_rules.cpp tests/growth_rule_tests.cpp -o build/growth_rule_tests
./build/growth_rule_tests
"$CXX" "${FLAGS[@]}" rules/rmuc2027_growth_v1/growth_rules.cpp rules/rmuc2027_growth_v1/dump.cpp -o build/growth_rules_dump
"$CXX" "${FLAGS[@]}" rules/rmuc2027_lifecycle_v1/lifecycle_rules.cpp tests/lifecycle_rule_tests.cpp -o build/lifecycle_rule_tests
./build/lifecycle_rule_tests
"$CXX" "${FLAGS[@]}" rules/rmuc2027_economy_v1/economy_rules.cpp tests/economy_rule_tests.cpp -o build/economy_rule_tests
./build/economy_rule_tests
"$CXX" "${FLAGS[@]}" rules/rmuc2027_buff_v1/buff_rules.cpp tests/buff_rule_tests.cpp -o build/buff_rule_tests
./build/buff_rule_tests
"$CXX" "${FLAGS[@]}" rules/rmuc2027_heat_v1/heat_rules.cpp rules/rmuc2027_limits_v1/limits_rules.cpp tests/limits_rule_tests.cpp -o build/limits_rule_tests
./build/limits_rule_tests
"$CXX" "${FLAGS[@]}" rules/rmuc2027_position_v1/position_rules.cpp tests/position_rule_tests.cpp -o build/position_rule_tests
./build/position_rule_tests

# Integrated 2027 rules lab: every source-linked module in one deterministic match.
MATCH_LAB_BUILD_ID="$(python3 match_lab/build_id.py)"
MATCH_LAB_SOURCES=(src/sim.cpp rules/rmuc2027_buff_v1/buff_rules.cpp rules/rmuc2027_capture_v1/capture_rules.cpp
  rules/rmuc2027_damage_v1/damage_rules.cpp rules/rmuc2027_economy_v1/economy_rules.cpp rules/rmuc2027_growth_v1/growth_rules.cpp
  rules/rmuc2027_heat_v1/heat_rules.cpp rules/rmuc2027_lifecycle_v1/lifecycle_rules.cpp rules/rmuc2027_limits_v1/limits_rules.cpp
  rules/rmuc2027_position_v1/position_rules.cpp rules/rmuc2027_v1/match_rules.cpp match_lab/engine.cpp)
MATCH_LAB_FLAGS=(-std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -Isrc "-DMATCH_LAB_BUILD_ID=\"$MATCH_LAB_BUILD_ID\"")
"$CXX" "${MATCH_LAB_FLAGS[@]}" "${MATCH_LAB_SOURCES[@]}" match_lab/cli.cpp -o build/rmuc_match_lab
"$CXX" "${MATCH_LAB_FLAGS[@]}" "${MATCH_LAB_SOURCES[@]}" tests/match_lab_tests.cpp -o build/match_lab_tests
./build/match_lab_tests
