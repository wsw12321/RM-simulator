"""Pinned source identity for the isolated filtered combat wrapper and SDK."""
import hashlib
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['src/sim.hpp', 'src/sim.cpp', 'src/directions.inc',
           'combat_mode/core.hpp', 'combat_mode/core.cpp', 'combat_mode/cli.cpp',
           'combat_mode/policy_sdk.hpp', 'combat_mode/example_policy.cpp',
           'combat_mode/build_id.py', 'combat_mode/bridge.py']
def build_id():
    return hashlib.sha256(b''.join(p.encode() + b'\0' + (ROOT / p).read_bytes() + b'\0'
                                  for p in SOURCES)).hexdigest()
if __name__ == '__main__':
    print(build_id())
