# Capture → control → external credits → match result

**Separate manually adjudicated tactical experiment. Not full RMUC, physical
simulation, an official scorekeeper, UI integration, or WebAssembly.**

This adapter composes the unchanged `rmuc2027_capture_v1::CaptureAttempt`,
`rmuc2027_v1::ExternalControlLedger` and `rmuc2027_v1::MatchJudge`. The original
browser sandbox and `scenario_harness` remain separate and unchanged. Module
provenance remains unchanged; this directory's provenance describes the new
integration rather than rewriting the standalone modules' history.

## Run locally

Requires Python 3.10+, Bash and a C++20 compiler, no packages/network/services.
From the project root:

```bash
bash tools/build.sh
python3 integrated_scenario/cli.py run integrated_scenario/scenarios/capture_credit_timeout.json --output /tmp/integrated-log.json
python3 integrated_scenario/cli.py verify /tmp/integrated-log.json
python3 integrated_scenario/cli.py checkpoint /tmp/integrated-log.json --through 1 --output /tmp/integrated-checkpoint.json
python3 integrated_scenario/cli.py restore /tmp/integrated-checkpoint.json --output /tmp/restored-prefix-log.json
python3 tests/test_integrated_scenario.py
bash tools/check.sh
```

Output filenames must not already exist. Commands run from any current directory;
paths to input/output files are caller-relative. CLI returns 0 on success, 1 for
run/resume with logged rejected boundaries, and 2 for malformed input, stale
build, incompatible/tampered replay or output failure. A logged rule rejection is
a useful experiment result, never silently repaired. `verify` returning 0 proves
reproducibility, not that all boundaries were accepted or the match is terminal.

Read [FORMAT.md](FORMAT.md) for the complete required schema, callable Python API,
phase policy, errors, limits and checkpoint/continuation contract. Reviewable
fixtures live in `scenarios/`; rejected-boundary fixtures intentionally exercise
failure and do not represent playable full matches.

## Authority and boundary discipline

1. Initial control is Full for each original position owner. Supplied activation
   is whole-position **logical** activation. Eligibility, detector delays,
   interaction cards, multiple robots and the 2-second deactivation grace must
   have been adjudicated upstream. `ActivationDelay` is not wired here, because
   its one qualified detector track cannot stand in for that aggregation.
2. The existing capture engine derives progress and Full → Temporary control.
   When it stops, the exact consumed endpoint and residual time are logged. A
   completed capture snapshot remains frozen, even while external adjudication
   extends the validity of its control result.
3. Any positive interval beyond that endpoint requires an explicit, exact-interval
   claim that Temporary control remains unchanged and that no restoration or
   outpost-lifecycle transition occurred. Absent or wrong claims reject atomically.
   V1 cannot restore Full control, restart capture, destroy or rebuild an outpost.
   Inputs on completed attempts and inputs past capture endpoints are errors.
4. Each supplied one-second credit derives its rate from integrated control.
   There is no second manual control map or score-total authority. Time passage
   generates **zero** credits. Authors must provide a complete credit history for
   a meaningful scoring experiment; partial-credit tests are only unit fixtures.
5. Base HP, attack damage and surviving-robot HP totals are externally adjudicated
   complete facts. No attacks, HP bookkeeping or referee penalties are simulated.
   The unchanged match judge picks a result and terminal ledger sealing commits
   in the same atomic transaction.

Every boundary advances all capture candidates, checks external continuation,
then credits and judges. This is explicitly named
`ENGINEERING-NOT-OFFICIAL-capture-continuation-credit-judge-v1`, not an official
same-time ruling. All attempts use strictly increasing times, including rejected
ones, and the initial time 0 is closed. Default native capture policy rejects an
input coincident with the 5-second threshold and contested → neither-active.
No silent timer-before-input fallback exists.

## Determinism, rollback and portable checkpoints

Each boundary uses candidate copies of every component. A capture, continuation,
credit or judge failure leaves **all authoritative state unchanged**, including
accepted clock, semantic traces, control, ledger and terminal latch. Candidate
phase outputs remain in the audit log but are not committed. Finalized runs reject
all later attempts before work. Native output logs only newly emitted capture
traces per boundary, avoiding quadratic trace repetition.

Logs carry versioned schemas, full supplied inputs, rule/source provenance,
compiled source fingerprints, phase outputs, decisions/errors, canonical hashes
and before/after state. UTF-8 output remains readable; exact emitted-byte limits
are checked before writing. Duplicate/unknown keys, unsupported versions and
malformed or excessive input fail closed. A stale native binary is rejected
before scenario execution; rebuild after changing fingerprinted source files.

A checkpoint is a **portable complete attempted-prefix replay record**. It keeps
the original initial conditions and policies, every accepted/rejected input,
provenance, canonical state and full-prefix-log fingerprint. Restore cold-runs
that prefix and verifies all of it. Resume accepts a checkpoint-bound continuation
and reexecutes prefix+suffix. The resulting full log is byte-identical to running
the same complete scenario once; no separate lineage fields weaken equivalence.
This costs O(prefix) execution and is not raw-memory persistence, O(1) snapshotting
or arbitrary restoration of private C++ fields. Keep matching sources to restore
older identities. Hashes are drift checks, not signatures/referee authenticity.

## Not established by these tests

Official referee timing and same-time event ordering, latest official Q&A,
restoration/contested absence semantics, outpost destruction/rebuild lifecycle,
robot/card aggregation, physical detection, attack damage attribution and HP
source facts, referee penalties, real matches, cross-architecture/compiler/WASM
consistency and browser/UI integration remain unimplemented or unverified.
