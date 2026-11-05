#!/usr/bin/env python3
"""Source identity for the objective integration only; does not alter rule identities."""
import hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCES=['src/sim.hpp','src/sim.cpp','src/directions.inc','objective_mode/objective.hpp','objective_mode/objective.cpp','objective_mode/cli.cpp','rules/rmuc2027_capture_v1/capture_rules.hpp','rules/rmuc2027_capture_v1/capture_rules.cpp','rules/rmuc2027_v1/match_rules.hpp','rules/rmuc2027_v1/match_rules.cpp','objective_mode/provenance.json','rules/rmuc2027_capture_v1/provenance.json','rules/rmuc2027_v1/provenance.json']
def build_id():
    return hashlib.sha256(b''.join(p.encode()+b'\0'+(ROOT/p).read_bytes()+b'\0' for p in SOURCES)).hexdigest()
if __name__=='__main__': print(build_id())
