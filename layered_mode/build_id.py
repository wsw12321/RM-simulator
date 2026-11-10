"""Content identity for the isolated layered movement implementation."""
import hashlib
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['layered_mode/core.hpp', 'layered_mode/core.cpp', 'layered_mode/cli.cpp',
           'layered_mode/policy_sdk.hpp', 'layered_mode/policy_adapter.hpp',
           'layered_mode/policy_adapter.cpp', 'layered_mode/example_policy.cpp',
           'layered_mode/bridge.py', 'layered_mode/build_id.py',
           'layered_mode/provenance.json', 'arena_format/core.py', 'native_pipe.py']
def build_id():
    return hashlib.sha256(b''.join(p.encode() + b'\0' + (ROOT / p).read_bytes() + b'\0' for p in SOURCES)).hexdigest()
if __name__ == '__main__':
    print(build_id())
