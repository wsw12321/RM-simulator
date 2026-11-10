#!/usr/bin/env python3
"""Parallel, cached native build for every target that tools/build.sh used to
compile one after another.

Each target keeps exactly the compiler flags of the original serial script, so
binaries and pinned build IDs are unchanged. What changes:

* every unique (compiler, flags, source) translation unit is compiled once and
  shared by all targets that need it (sim.cpp alone used to compile 20+ times);
* compilation, linking and the native test binaries run on JOBS workers
  (default: CPU count); test output is printed in the original order;
* objects are cached under build/obj keyed by a hash of all C/C++ sources, so
  an unchanged tree only re-links.

Environment: CXX (default g++), JOBS (default CPU count; JOBS=1 = serial),
LDFLAGS (extra link-only flags, default none; tools/package_release.py uses it
for static release binaries, objects and build IDs are unaffected).
"""
import concurrent.futures as futures
import hashlib
import os
import pathlib
import shlex
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
OBJ = BUILD / 'obj'
CXX = shlex.split(os.environ.get('CXX', 'g++'))
LDFLAGS = shlex.split(os.environ.get('LDFLAGS', ''))
JOBS = max(1, int(os.environ.get('JOBS', os.cpu_count() or 2)))


def py(*args):
    return subprocess.run([sys.executable, *args], cwd=ROOT, check=True, capture_output=True, text=True).stdout.strip()


def sim_build_id():
    return hashlib.sha256(b''.join((ROOT / p).read_bytes() for p in
                                   ['src/sim.hpp', 'src/sim.cpp', 'src/policy.cpp', 'src/directions.inc'])).hexdigest()


def plan():
    """(output, run_as_test, flags, sources) in the original build.sh order."""
    flags = [f'-DSIM_BUILD_ID="{sim_build_id()}"', '-Isrc', '-std=c++20', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-O2']
    match = 'rules/rmuc2027_v1/match_rules.cpp'
    capture = 'rules/rmuc2027_capture_v1/capture_rules.cpp'
    damage = 'rules/rmuc2027_damage_v1/damage_rules.cpp'
    heat = 'rules/rmuc2027_heat_v1/heat_rules.cpp'
    T = []
    T += [('rmuc_sandbox', False, flags, ['src/sim.cpp', 'src/policy.cpp', 'src/cli.cpp']),
          ('core_tests', True, flags, ['src/sim.cpp', 'src/policy.cpp', 'tests/core_tests.cpp'])]
    mf = flags + ['-Irules/rmuc2027_v1']
    T += [('match_rule_tests', True, mf, [match, 'tests/match_rule_tests.cpp']),
          ('match_rules_demo', False, mf, [match, 'rules/rmuc2027_v1/demo.cpp'])]
    cf = flags + ['-Irules/rmuc2027_capture_v1']
    T += [('capture_rule_tests', True, cf, [capture, 'tests/capture_rule_tests.cpp']),
          ('capture_rules_demo', False, cf, [capture, 'rules/rmuc2027_capture_v1/demo.cpp']),
          ('capture_rule_property_tests', True, cf, [capture, 'tests/capture_rule_property_tests.cpp'])]
    T.append(('rmuc_scenario_driver', False, flags + [f'-DSCENARIO_BUILD_ID="{py("scenario_harness/cli.py", "build-id")}"', '-Irules/rmuc2027_v1'],
              [match, 'scenario_harness/driver.cpp']))
    T.append(('rmuc_integrated_driver', False, flags + [f'-DINTEGRATED_BUILD_ID="{py("integrated_scenario/cli.py", "build-id")}"',
                                                       '-Irules/rmuc2027_capture_v1', '-Irules/rmuc2027_v1'],
              [capture, match, 'integrated_scenario/driver.cpp']))
    of = flags + [f'-DOBJECTIVE_BUILD_ID="{py("objective_mode/build_id.py")}"', '-Iobjective_mode', '-Irules/rmuc2027_capture_v1', '-Irules/rmuc2027_v1']
    osrc = ['src/sim.cpp', capture, match, 'objective_mode/objective.cpp']
    T += [('rmuc_objective', False, of, osrc + ['objective_mode/cli.cpp']),
          ('objective_tests', True, of, osrc + ['tests/objective_tests.cpp']),
          ('objective_adapter_tests', True, of, osrc + ['tests/objective_adapter_tests.cpp'])]
    lf = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', f'-DLAYERED_BUILD_ID="{py("layered_mode/build_id.py")}"']
    T += [('rmuc_layered', False, lf, ['layered_mode/core.cpp', 'layered_mode/policy_adapter.cpp', 'layered_mode/example_policy.cpp', 'layered_mode/cli.cpp']),
          ('layered_core_tests', True, lf, ['layered_mode/core.cpp', 'tests/layered_core_tests.cpp']),
          ('policy_review_tests', True, lf, ['layered_mode/core.cpp', 'layered_mode/policy_adapter.cpp', 'tests/policy_review_tests.cpp'])]
    bf = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', f'-DCOMBAT_BUILD_ID="{py("combat_mode/build_id.py")}"']
    bsrc = ['src/sim.cpp', 'combat_mode/core.cpp']
    T += [('rmuc_combat', False, bf, bsrc + ['combat_mode/example_policy.cpp', 'combat_mode/cli.cpp']),
          ('combat_core_tests', True, bf, bsrc + ['combat_mode/example_policy.cpp', 'tests/combat_core_tests.cpp']),
          ('combat_review_tests', True, bf, bsrc + ['tests/combat_review_tests.cpp'])]
    tf = ['-std=c++20', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', f'-DTACTICAL_BUILD_ID="{py("tactical_mode/build_id.py")}"',
          '-Isrc', '-Irules/rmuc2027_capture_v1', '-Irules/rmuc2027_v1']
    tsrc = ['src/sim.cpp', 'combat_mode/core.cpp', capture, match, damage, heat, 'tactical_mode/core.cpp', 'tactical_mode/example_policy.cpp']
    T += [('rmuc_tactical', False, tf, tsrc + ['tactical_mode/cli.cpp']),
          ('tactical_core_tests', True, tf, tsrc + ['tests/tactical_core_tests.cpp']),
          ('tactical_review_tests', True, tf, tsrc + ['tests/tactical_review_tests.cpp']),
          ('damage_rule_tests', True, flags, [damage, 'tests/damage_rule_tests.cpp']),
          ('tactical_damage_tests', True, tf, tsrc + ['tests/tactical_damage_tests.cpp']),
          ('damage_review_tests', True, tf, tsrc + ['tests/damage_review_tests.cpp']),
          ('heat_rule_tests', True, flags, [heat, 'tests/heat_rule_tests.cpp']),
          ('tactical_heat_tests', True, tf, tsrc + ['tests/tactical_heat_tests.cpp']),
          ('heat_review_tests', True, tf, tsrc + ['tests/heat_review_tests.cpp'])]
    rf = ['-std=c++20', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', f'-DROSTER_BUILD_ID="{py("roster_mode/build_id.py")}"',
          '-Isrc', '-Irules/rmuc2027_capture_v1', '-Irules/rmuc2027_v1']
    rsrc = ['src/sim.cpp', capture, match, damage, heat, 'roster_mode/core.cpp', 'roster_mode/example_policy.cpp']
    T += [('rmuc_roster', False, rf, rsrc + ['roster_mode/cli.cpp']),
          ('roster_core_tests', True, rf, rsrc + ['tests/roster_core_tests.cpp']),
          ('roster_review_tests', True, rf, rsrc + ['tests/roster_review_tests.cpp'])]
    growth = 'rules/rmuc2027_growth_v1/growth_rules.cpp'
    T += [('growth_rule_tests', True, flags, [growth, 'tests/growth_rule_tests.cpp']),
          ('growth_rules_dump', False, flags, [growth, 'rules/rmuc2027_growth_v1/dump.cpp']),
          ('lifecycle_rule_tests', True, flags, ['rules/rmuc2027_lifecycle_v1/lifecycle_rules.cpp', 'tests/lifecycle_rule_tests.cpp']),
          ('economy_rule_tests', True, flags, ['rules/rmuc2027_economy_v1/economy_rules.cpp', 'tests/economy_rule_tests.cpp']),
          ('buff_rule_tests', True, flags, ['rules/rmuc2027_buff_v1/buff_rules.cpp', 'tests/buff_rule_tests.cpp']),
          ('limits_rule_tests', True, flags, [heat, 'rules/rmuc2027_limits_v1/limits_rules.cpp', 'tests/limits_rule_tests.cpp']),
          ('position_rule_tests', True, flags, ['rules/rmuc2027_position_v1/position_rules.cpp', 'tests/position_rule_tests.cpp'])]
    buffs = 'rules/rmuc2027_buff_v1/buff_rules.cpp'
    energy, radar, dart = ('rules/rmuc2027_energy_v1/energy_rules.cpp', 'rules/rmuc2027_radar_v1/radar_rules.cpp',
                           'rules/rmuc2027_dart_v1/dart_rules.cpp')
    # The three suites also cross-check their tables against the buff_v1 copies.
    T += [('energy_rule_tests', True, flags, [buffs, energy, 'tests/energy_rule_tests.cpp']),
          ('radar_rule_tests', True, flags, [buffs, radar, 'tests/radar_rule_tests.cpp']),
          ('dart_rule_tests', True, flags, [buffs, dart, 'tests/dart_rule_tests.cpp'])]
    lab = ['-std=c++20', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-Isrc', f'-DMATCH_LAB_BUILD_ID="{py("match_lab/build_id.py")}"']
    lsrc = ['src/sim.cpp', buffs, capture, damage, dart, 'rules/rmuc2027_economy_v1/economy_rules.cpp', energy, growth, heat, radar,
            'rules/rmuc2027_lifecycle_v1/lifecycle_rules.cpp', 'rules/rmuc2027_limits_v1/limits_rules.cpp',
            'rules/rmuc2027_position_v1/position_rules.cpp', match, 'match_lab/engine.cpp']
    T += [('rmuc_match_lab', False, lab, lsrc + ['match_lab/cli.cpp']),
          ('match_lab_tests', True, lab, lsrc + ['tests/match_lab_tests.cpp'])]
    return T


def tree_hash():
    h = hashlib.sha256()
    h.update(' '.join(CXX).encode())
    for path in sorted(ROOT.rglob('*')):
        if path.suffix in ('.cpp', '.hpp', '.h', '.inc') and BUILD not in path.parents and path.is_file():
            h.update(str(path.relative_to(ROOT)).encode() + b'\0' + path.read_bytes() + b'\0')
    return h.hexdigest()


def run(cmd, cwd=ROOT):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def main():
    start = time.monotonic()
    BUILD.mkdir(exist_ok=True)
    OBJ.mkdir(exist_ok=True)
    targets = plan()
    tree = tree_hash()
    objects = {}  # key -> (flags, source, object path)
    links = []
    for out, test, flags, sources in targets:
        objs = []
        for src in sources:
            key = hashlib.sha256('\0'.join([tree, *CXX, *flags, src]).encode()).hexdigest()[:32]
            obj = OBJ / f'{key}.o'
            objects.setdefault(key, (flags, src, obj))
            objs.append(obj)
        links.append((out, test, flags, objs))
    todo = [(f, s, o) for f, s, o in objects.values() if not o.exists()]
    failures = []
    with futures.ThreadPoolExecutor(JOBS) as pool:
        def compile_one(item):
            flags, src, obj = item
            tmp = obj.with_suffix('.tmp.o')
            r = run([*CXX, *flags, '-c', src, '-o', str(tmp)])
            if r.returncode == 0:
                tmp.replace(obj)
            return src, r
        for src, r in pool.map(compile_one, todo):
            if r.returncode:
                failures.append(f'compile {src}:\n{r.stderr}')
        if failures:
            sys.stderr.write('\n'.join(failures))
            return 1
        def link_one(item):
            out, _, flags, objs = item
            return out, run([*CXX, *flags, *map(str, objs), *LDFLAGS, '-o', str(BUILD / out)])
        for out, r in pool.map(link_one, links):
            if r.returncode:
                failures.append(f'link {out}:\n{r.stderr}')
        if failures:
            sys.stderr.write('\n'.join(failures))
            return 1
        built = time.monotonic()
        tests = [out for out, test, _, _ in links if test]
        results = list(pool.map(lambda t: (t, run([str(BUILD / t)])), tests))
    for name, r in results:  # original order, complete output
        sys.stdout.write(r.stdout)
        sys.stderr.write(r.stderr)
        if r.returncode:
            failures.append(name)
    keep = {o.name for _, _, o in objects.values()}
    for stale in OBJ.glob('*.o'):
        if stale.name not in keep:
            stale.unlink()
    if failures:
        sys.stderr.write(f'FAILED native tests: {", ".join(failures)}\n')
        return 1
    print(f'native build: {len(objects)} translation units ({len(todo)} compiled), {len(links)} targets, '
          f'{len(tests)} test binaries passed; {built - start:.1f}s build + {time.monotonic() - built:.1f}s tests, JOBS={JOBS}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
