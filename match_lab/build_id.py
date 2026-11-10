#!/usr/bin/env python3
"""Pin the rules lab native identity to every source byte it is compiled from."""
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    'src/sim.hpp', 'src/sim.cpp', 'src/directions.inc',
    'rules/rmuc2027_buff_v1/buff_rules.hpp', 'rules/rmuc2027_buff_v1/buff_rules.cpp',
    'rules/rmuc2027_capture_v1/capture_rules.hpp', 'rules/rmuc2027_capture_v1/capture_rules.cpp',
    'rules/rmuc2027_damage_v1/damage_rules.hpp', 'rules/rmuc2027_damage_v1/damage_rules.cpp',
    'rules/rmuc2027_dart_v1/dart_rules.hpp', 'rules/rmuc2027_dart_v1/dart_rules.cpp',
    'rules/rmuc2027_economy_v1/economy_rules.hpp', 'rules/rmuc2027_economy_v1/economy_rules.cpp',
    'rules/rmuc2027_energy_v1/energy_rules.hpp', 'rules/rmuc2027_energy_v1/energy_rules.cpp',
    'rules/rmuc2027_growth_v1/growth_rules.hpp', 'rules/rmuc2027_growth_v1/growth_rules.cpp',
    'rules/rmuc2027_heat_v1/heat_rules.hpp', 'rules/rmuc2027_heat_v1/heat_rules.cpp',
    'rules/rmuc2027_lifecycle_v1/lifecycle_rules.hpp', 'rules/rmuc2027_lifecycle_v1/lifecycle_rules.cpp',
    'rules/rmuc2027_limits_v1/limits_rules.hpp', 'rules/rmuc2027_limits_v1/limits_rules.cpp',
    'rules/rmuc2027_position_v1/position_rules.hpp', 'rules/rmuc2027_position_v1/position_rules.cpp',
    'rules/rmuc2027_radar_v1/radar_rules.hpp', 'rules/rmuc2027_radar_v1/radar_rules.cpp',
    'rules/rmuc2027_v1/match_rules.hpp', 'rules/rmuc2027_v1/match_rules.cpp',
    'match_lab/engine.hpp', 'match_lab/engine.cpp', 'match_lab/cli.cpp', 'match_lab/build_id.py',
]


def build_id():
    return hashlib.sha256(b''.join(p.encode() + b'\0' + (ROOT / p).read_bytes() + b'\0'
                                   for p in SOURCES)).hexdigest()


if __name__ == '__main__':
    print(build_id())
