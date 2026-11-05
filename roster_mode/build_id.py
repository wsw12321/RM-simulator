#!/usr/bin/env python3
"""Pin roster authority, transport, SDK, synthetic contract and reused rule evidence."""
import hashlib
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    'src/sim.hpp', 'src/sim.cpp', 'src/directions.inc',
    'combat_mode/bridge.py', 'combat_mode/build_id.py', 'objective_mode/objective.hpp',
    'rules/rmuc2027_capture_v1/capture_rules.hpp', 'rules/rmuc2027_capture_v1/capture_rules.cpp',
    'rules/rmuc2027_capture_v1/provenance.json',
    'rules/rmuc2027_v1/match_rules.hpp', 'rules/rmuc2027_v1/match_rules.cpp', 'rules/rmuc2027_v1/provenance.json',
    'rules/rmuc2027_damage_v1/damage_rules.hpp', 'rules/rmuc2027_damage_v1/damage_rules.cpp', 'rules/rmuc2027_damage_v1/provenance.json',
    'rules/rmuc2027_heat_v1/heat_rules.hpp', 'rules/rmuc2027_heat_v1/heat_rules.cpp', 'rules/rmuc2027_heat_v1/provenance.json',
    'tactical_mode/ATTACK_DAMAGE_SOURCES.md', 'tactical_mode/attack_damage_source_audit.json',
    'tactical_mode/HEAT_SOURCES.md', 'tactical_mode/heat_source_audit.json',
    'roster_mode/core.hpp', 'roster_mode/core.cpp', 'roster_mode/policy_sdk.hpp',
    'roster_mode/example_policy.cpp', 'roster_mode/cli.cpp', 'roster_mode/bridge.py',
    'roster_mode/default_config.json', 'roster_mode/CONTRACT.md', 'roster_mode/provenance.json', 'roster_mode/build_id.py',
]
def build_id():
    return hashlib.sha256(b''.join(p.encode() + b'\0' + (ROOT / p).read_bytes() + b'\0' for p in SOURCES)).hexdigest()
if __name__ == '__main__':
    print(build_id())
