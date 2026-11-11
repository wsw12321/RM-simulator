#!/usr/bin/env python3
"""Pin the field_sim native identity to every source byte it is compiled from
and to the field data it loads."""
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    'rules/rmuc2027_heat_v1/heat_rules.hpp', 'rules/rmuc2027_heat_v1/heat_rules.cpp',
    'rules/rmuc2027_limits_v1/limits_rules.hpp', 'rules/rmuc2027_limits_v1/limits_rules.cpp',
    'field_sim/json.hpp', 'field_sim/terrain.hpp', 'field_sim/terrain.cpp',
    'field_sim/engine.hpp', 'field_sim/engine.cpp', 'field_sim/cli.cpp', 'field_sim/build_id.py',
    'field_sim/field/rmuc2027_v1/field.json', 'field_sim/field/rmuc2027_v1/terrain.bin',
]


def build_id():
    return hashlib.sha256(b''.join(p.encode() + b'\0' + (ROOT / p).read_bytes() + b'\0'
                                   for p in SOURCES)).hexdigest()


if __name__ == '__main__':
    print(build_id())
