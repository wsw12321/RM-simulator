#!/usr/bin/env python3
"""Pin tactical authority, its trusted SDK/controller and unchanged source modules."""
import hashlib
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    'src/sim.hpp', 'src/sim.cpp', 'src/directions.inc',
    'combat_mode/core.hpp', 'combat_mode/core.cpp', 'combat_mode/policy_sdk.hpp',
    'combat_mode/bridge.py', 'combat_mode/build_id.py',
    'objective_mode/objective.hpp',
    'rules/rmuc2027_capture_v1/capture_rules.hpp', 'rules/rmuc2027_capture_v1/capture_rules.cpp',
    'rules/rmuc2027_capture_v1/provenance.json',
    'rules/rmuc2027_v1/match_rules.hpp', 'rules/rmuc2027_v1/match_rules.cpp',
    'rules/rmuc2027_v1/provenance.json',
    'rules/rmuc2027_damage_v1/damage_rules.hpp', 'rules/rmuc2027_damage_v1/damage_rules.cpp',
    'rules/rmuc2027_damage_v1/provenance.json',
    'rules/rmuc2027_heat_v1/heat_rules.hpp', 'rules/rmuc2027_heat_v1/heat_rules.cpp',
    'rules/rmuc2027_heat_v1/provenance.json', 'rules/rmuc2027_heat_v1/README.md',
    'tactical_mode/HEAT_SOURCES.md', 'tactical_mode/heat_source_audit.json',
    'tactical_mode/ATTACK_DAMAGE_SOURCES.md', 'tactical_mode/attack_damage_source_audit.json',
    'tactical_mode/core.hpp', 'tactical_mode/core.cpp', 'tactical_mode/policy_sdk.hpp',
    'tactical_mode/example_policy.cpp', 'tactical_mode/cli.cpp',
    'tactical_mode/provenance.json', 'tactical_mode/build_id.py', 'tactical_mode/bridge.py',
    'native_pipe.py',
]
def build_id():
    return hashlib.sha256(b''.join(p.encode() + b'\0' + (ROOT / p).read_bytes() + b'\0'
                                  for p in SOURCES)).hexdigest()
if __name__ == '__main__':
    print(build_id())
