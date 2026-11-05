# Verification record

Date: 2026-11-05. Tested environment: Linux, GCC 14.2.0, Python 3.12, Node 24.22.1.

## Passed

- C++20 release build with `-Wall -Wextra -Wpedantic -Werror`.
- Native core: **6,562 assertions**, covering input rejection without mutation, integer arithmetic, normalized movement, arena bounds, static collision, robot contact, bounded aim slew, projectile hit/cover, cooldown/ammo, simultaneous destruction, terminal freeze, scripted-policy replay and a 3,000-tick deterministic trajectory.
- GCC `-O0` and `-O2`: identical final fixture state hash **2045014215** and identical test output. This is same-machine optimization-level checking, not cross-platform proof.
- AddressSanitizer and UndefinedBehaviorSanitizer: all native assertions passed. LeakSanitizer was disabled because this environment's ptrace restriction prevents it.
- Eight HTTP integration tests: metadata, replay round-trip, invalid-action atomicity, cross-origin rejection, corrupt-replay preservation, wrong build, invalid boolean-as-integer, wrong schema.
- One bridge unit test: a newly launched candidate built from different source is rejected, including empty replay, and the previous engine is retained.
- Node DOM-handler + live-API regression harness: held-key movement/fire survives single-step, all mutation controls disabled during file read/import, programmatic Play ignored while busy, successful import stays paused, failed hash validation preserves match, scope modal opens/closes.
- Python source compilation and Node JavaScript syntax checks.
- Optional WASM C ABI wrapper: native C++ syntax compilation only.

## Blocked / not passed

- **Actual Chromium browser interaction and visual QA:** attempted through Playwright, including a supported elevated retry. Chromium failed before page load with `process_singleton_posix.cc: socket() failed: Operation not permitted`. No screenshots were captured, and no real-browser test is claimed as passed. `tests/browser_smoke.cjs` is supplied for a machine that can launch Chromium. Its test executable path defaults to `/usr/bin/chromium`; adjust to the installed browser path if needed.
- **WebAssembly build:** `em++` is not installed. `tools/build_wasm.sh` exits 2 with that diagnostic. No installer or network download was run. The browser-WASM adapter and cross-target test remain unimplemented.
- Mobile touch controls, actual mobile layout, other browsers/operating systems, performance budget, long-running policy watchdog fault injection and physical fidelity: unverified.
- Official 2027 compliance: **not implemented or tested**. The rules matrix is research input, not evidence that the sandbox implements its rows.

## Reproduce the verified subset

`bash tools/check.sh` builds, executes core/API/unit/DOM-handler checks. Node is required for JavaScript checks, but not to run the simulator itself. It starts an isolated loopback service for the check and stops it afterward.

Run a real browser separately:

1. `python3 server.py --port 8766`
2. `node tests/browser_smoke.cjs http://127.0.0.1:8766`

This needs an already installed Playwright package and Chromium. Neither is installed by the project. Do not treat a missing test dependency as a product test pass.

## Addition: isolated 2027 match formula module

The `rules/rmuc2027_v1` module has 149 scenario/boundary assertions. Coverage includes all 16 ownership combinations; temporary/full control, illegal enemy full-base control, independent 1/3-point rates, externally resolved credit sequencing/sealing, 419999/420000 ms ending boundaries, each ordered tiebreaker, base-priority at timeout, missing inputs, explicit simultaneous-destruction ambiguity, living-robot HP exclusions, duplicate/overflow rejection and terminal immutability.

These tests verify the narrow formula implementation against the cited baseline manual. They do not verify upstream capture, control sampling, damage attribution, referee decisions or the entire competition rule pack. The browser sandbox does not call this module.

For this addition, GCC O0/O2 output was identical, and AddressSanitizer/UBSan passed all 149 assertions. An independent review compared implementation and provenance with the eight cited manual pages and found no actionable issue within this narrow scope. Existing native/API/DOM-handler regressions also passed again.

## Addition: manually adjudicated scenario harness

Date: 2026-11-05. Environment: Linux, GCC 14.2.0, Python 3.12.14, Node 24.22.1.
Harness: `rmuc2027-adjudicated-harness.1`; unchanged rule module:
`rmuc2027-match-v1.0.0-module.1`.
Final harness implementation identity:
`4b787726535cd9ed7d8b81e1eab1b7406f681c577bec4290f7a78a73f06fe76d`.

### Passed on the final sources

- `bash tools/check.sh`: all 6,562 existing C++ core checks, all 149 existing rule
  assertions, **27 new scenario-harness tests**, Python compilation, JavaScript
  syntax, one bridge unit test, eight HTTP integration tests and DOM-handler/API
  regressions passed. The new CLI is separate from the HTTP/browser path.
- `bash -n tools/build.sh tools/check.sh` passed.
- Native adapter compiled with C++20, `-Wall -Wextra -Wpedantic -Werror`, `-O2`.
- All nine bundled scenarios were run repeatedly, verified by full re-execution,
  and compared after canonical scenario serialization/round-trip. Public CLI
  run/verify/extract/re-run emitted identical log bytes, including expected errors.
- Timeout tiers, early base destruction, unknown-value preservation, missing input,
  unsupported referee paths, explicit simultaneity resolution, no implicit accrual,
  same-time caller order, duplicate/gapped credits, full-boundary rollback, terminal
  latch and atomic ledger seal passed. Rejected boundaries preserved exact state.
- Strict schema/numeric/protocol rejection, exact int64/uint64 handling, unknown and
  duplicate keys, floats/NaN, boolean-as-integer, negative/overflow values, event/time
  ordering, depth and resource limits, source-mode conflicts, log tampering,
  recomputed tamper hashes, stale-build rejection **before scenario execution**,
  and no-overwrite behavior passed.
- Unicode output regression: a valid UTF-8 input with 3,000 boundaries and 500 emoji
  per source note would expand beyond the 32 MiB log budget. Exact pre-write byte
  validation now rejects it without partial output. A smaller log below 32 MiB whose
  extracted scenario exceeds 8 MiB is similarly rejected. Inclusive byte-limit
  boundaries and successful multibyte-text round-trips passed. Nesting is explicitly
  capped at 32 container levels, not dependent on Python recursion behavior.
- `-O0` adapter: 26 scenario tests passed, one public-CLI-path test intentionally
  skipped because that test pins the normal `build/rmuc_scenario_driver` path.
  It passed in the normal release/aggregate run. The complete canonical logs for all
  nine scenarios were also compared directly between O0 and O2 and were identical.
- ASan/UBSan adapter: the same 26 applicable scenario tests passed, with the same
  public-CLI-path skip. LeakSanitizer was disabled, as in the prior native checks.
- SHA-256 comparison confirmed original `src/*`, `server.py`, `web/*`, and the rule
  module header/implementation/provenance unchanged. No rule-semantic correction
  was necessary or made.

An initial aggregate run exposed the old test script's fixed 0.4-second server
startup delay under concurrent compilation load. `tools/check.sh` now waits for
loopback metadata readiness, bounded at 10 seconds, rather than assuming readiness
after a fixed sleep. The final complete aggregate run passed. No server/browser
product code was changed.

### Additional native checks, exact build commands

Run from the project root after the normal build:

```bash
ID="$(python3 scenario_harness/cli.py build-id)"
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -O0 \
  "-DSCENARIO_BUILD_ID=\"$ID\"" -Irules/rmuc2027_v1 \
  rules/rmuc2027_v1/match_rules.cpp scenario_harness/driver.cpp \
  -o build/rmuc_scenario_driver_o0
SCENARIO_TEST_DRIVER="$PWD/build/rmuc_scenario_driver_o0" \
  python3 tests/test_scenario_harness.py

g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -O1 -g \
  -fno-omit-frame-pointer -fsanitize=address,undefined -no-pie \
  "-DSCENARIO_BUILD_ID=\"$ID\"" -Irules/rmuc2027_v1 \
  rules/rmuc2027_v1/match_rules.cpp scenario_harness/driver.cpp \
  -o build/rmuc_scenario_driver_sanitized
ASAN_OPTIONS=detect_leaks=0 \
  SCENARIO_TEST_DRIVER="$PWD/build/rmuc_scenario_driver_sanitized" \
  python3 tests/test_scenario_harness.py
```

### Not established by these checks

The results verify this bounded adapter and formula execution, not that supplied
facts are genuinely adjudicated, physically possible or correctly attributed.
Credit timing is not checked against elapsed time; no scoring/capture scheduler,
HP-loss damage inference, or referee penalties were implemented. All examples are
synthetic. Rule provenance and source/log hashes are not official certification,
binary/compiler authentication or tamper-proof signatures.

No new real-browser, WASM, mobile, cross-platform or full-official-match claim is
made. Earlier browser/WASM limitations remain. No package installation, publishing
or external service was used for this addition.

## Addition: isolated activation-delay and capture subset

Date: 2026-11-05. Module: `rmuc2027-capture-v1.0.0-module.1`.
Primary source: RMUC 2027 V1.0.0, printed pp94/96/102 (physical PDF pp95/97/103).
The original PDF SHA-256 and audited matrix SHA-256 were rechecked against the
copied provenance. An independent source reviewer checked the original pages.

### Passed on the final sources

- `bash tools/check.sh`: all 6,562 sandbox checks, 149 match-rule assertions,
  **1,057 new capture scenario/boundary assertions**, **1,033,476 independent
  seeded capture property checks**, the capture demonstration, 27 existing
  scenario-harness tests, one bridge unit test, eight HTTP integration tests,
  Python/JavaScript syntax and DOM-handler/API regressions passed.
- C++20 compilation with `-Wall -Wextra -Wpedantic -Werror` and shell syntax
  checks passed. Both capture suites ran at GCC O0 and O2 with identical output.
  This is same-machine optimization checking, not cross-platform proof.
- Both capture suites passed ASan and UBSan. LeakSanitizer remained disabled
  because ptrace prevents it in this environment.
- Coverage includes initial ownership, all four literal table transitions,
  base full-control restrictions, explicit unsupported actors/states, ordinary
  loss grace, reacquisition, 2 s and 5 s just-before/exact/after boundaries,
  progress decay/floor/resume, contest pause, strict coincidence rejection,
  versioned engineering timer-first opt-in, closed timestamps, exact completion
  and explicit unconsumed intervals, invalid input/time/overflow, atomic state
  and trace rejection, repeated execution, and interval-partition invariance.
- The 3 s detector-active + 2 s grace counterexample uses copies of both objects
  and commits neither on downstream ambiguity. It does not claim separate live
  mutations are an atomic integrated operation. The downstream capture receives
  one logical expiry and adds no second grace period.
- The portable property suite in `tests/capture_rule_property_tests.cpp` uses
  fixed seed 412 to compare long/split advances, states, traces and event outcomes
  plus int64 boundary cases. Its 1,033,476 assertions test implementation
  properties, **not an independent oracle for official referee behavior**.
- Independent code review approved the bounded API, tests, source-linked
  provenance and documented exclusions after the row-1 fact was named
  `AdjudicatedCaptureThresholdReached`, requiring the shared progress/contest
  qualifications rather than treating bare continuous detection as sufficient.
- SHA-256 checks confirmed `src/*`, `web/*`, `server.py`, every existing match
  module file, and the scenario implementation/format/fixtures unchanged.
  The scenario harness identity remains
  `4b787726535cd9ed7d8b81e1eab1b7406f681c577bec4290f7a78a73f06fe76d`.
  No old logs were rewritten, and neither existing runtime calls this module.

Reviewed implementation hashes:

- `capture_rules.hpp`: `f39afc45ef0685fbf02ed5ecd3308dd4e12001315bb92b44d08fc08882463db9`
- `capture_rules.cpp`: `a3d6ae5a126e0d992f2b921de75cd86a0b1c1751eaa72a2183eae79d82f9f422`

### Reproduce additional native checks

After the normal aggregate check, run from the project root:

```bash
for TEST in capture_rule_tests capture_rule_property_tests; do
  g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -O0 \
    -Irules/rmuc2027_capture_v1 rules/rmuc2027_capture_v1/capture_rules.cpp \
    "tests/$TEST.cpp" -o "build/${TEST}_o0"
  "./build/${TEST}_o0"
  g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -O1 -g \
    -fno-omit-frame-pointer -fsanitize=address,undefined -no-pie \
    -Irules/rmuc2027_capture_v1 rules/rmuc2027_capture_v1/capture_rules.cpp \
    "tests/$TEST.cpp" -o "build/${TEST}_sanitized"
  ASAN_OPTIONS=detect_leaks=0 "./build/${TEST}_sanitized"
done
```

### Scope not established by this addition

The timed attempt ends at temporary capture and returns any unconsumed interval.
Temporary-control absence/restoration clocks are not implemented: the interaction
of the continuous-absence row with shared pause/decay text remains unadjudicated.
The pure table mapper requires externally qualified facts. Outpost destruction or
restoration outputs cannot establish a live/rebuilt outpost, HP, or valid sequence
of repeated destructions. The lifecycle flag being false is not certification.

Default behavior rejects capture/expiry coincidence and direct contested-to-none
changes. `EngineeringTimerBeforeInputV1` is a conspicuously versioned opt-in
ordering, not an official ruling, and it does not unlock other unsupported paths.
Sensor detection lag, dead zones, eligibility, robot/card aggregation, forced
invalidation, scoring clocks, damage and referee behavior remain external.

No new persisted log schema, official-referee integration, browser/WASM/mobile
verification, full official-match compliance or physical fidelity claim is made.
No packages were installed, and no publication or external service was used.

## Separate integrated logical scenario milestone · 2026-11-05

This section describes only `integrated_scenario/`, the new manually adjudicated
logical-capture → derived-control → supplied-credit → match-judge experiment.
It does not upgrade the sandbox, legacy scenario harness, physical simulation,
UI/WASM coverage or official-rule completeness claims above.

### Final checks actually completed

- `bash tools/check.sh`: passed on the frozen sources. Existing core 6,562 checks,
  match 149 assertions, capture 1,057 checks, seeded capture property 1,033,476
  assertions, legacy scenario 27 tests, bridge/server tests and DOM-handler/API
  regressions all remained green. The new integration suite passed all 30 tests.
- New tests cover seven reviewable fixtures, capture-derived control credits,
  control victory despite unequal intact base HP, exact temporary endpoints and
  unconsumed intervals, explicit continuation and missing/wrong handoff failures,
  frozen completed attempts, strict 5 s coincidence and contested-to-none,
  rollback at every phase, no implicit accrual, missing tie-break facts, unsupported
  referee ruling, finalized-state immutability, strict closed input timestamps,
  cold log replay, checkpoint restoration and exact one-shot/resumed log equality.
- Input/output regressions include malformed JSON, duplicate keys, type/range/
  version/byte/count limits, unknown authority fields, stale compiled identity,
  rewritten hashes with altered content, canonical false-versus-zero provenance,
  direct-API detached/size guarantees, readable Unicode exact output byte limits,
  `PYTHONIOENCODING=utf-16`, escaped unpaired-surrogate diagnostic keys, and no
  existing-file overwrite. CLI emits the guarded UTF-8 bytes directly.
- Independent review identified four adapter-boundary issues (direct public
  validator bounds/aliases, bool/int identity comparison, environment-dependent
  stdout encoding, malformed-surrogate error reporting). All were fixed and
  have reproducible regressions; final independent review found no blocker.
- GCC O0 and O2 emitted exactly equal canonical full logs for all seven fixtures,
  including rejected records and semantic trace deltas.
- The integration suite also passed with an O0 native driver and an
  AddressSanitizer + UndefinedBehaviorSanitizer native driver. Each alternate
  driver run executed 28 tests and skipped only the two CLI tests intentionally
  pinned to the normal build path. Those two passed in the normal aggregate run.
  Leak detection was disabled (`ASAN_OPTIONS=detect_leaks=0`) for this runner;
  no leak-check claim is made.
- Independent stress review ran the maximum 512 boundaries and 4,096 credits with
  Unicode notes. The emitted 20,257,909-byte log was self-readable and cold-verified.
  Another fixed-seed review pass covered 20 synthetic scenarios / 800 attempts,
  checking complete rollback, terminal immutability and exact resume equivalence.
  These are implementation invariants, not an independent official-rules oracle.
- SHA-256 preservation checks confirmed the sandbox sources/web/server, both
  existing rule implementations and provenance, and the legacy scenario harness
  remained unchanged. Legacy source identity is still
  `4b787726535cd9ed7d8b81e1eab1b7406f681c577bec4290f7a78a73f06fe76d`.

Frozen integration source identity:
`89bd673825cab5dead27a720ea012323869c681f73930eca380def474be876ef`

- `integrated_scenario/cli.py`:
  `7a01969beac529c40228752f2c52a604cd69fd65cbe7c276895cffe6db701ddd`
- `integrated_scenario/driver.cpp`:
  `af431a34212a36a9abcd1485f06fdf69d7f671b59cc5ab3ea380649e59bb2237`

### Reproduce alternate native checks

After `bash tools/check.sh`, run from the project root:

```bash
ID="$(python3 integrated_scenario/cli.py build-id)"
COMMON=(-std=c++20 -Wall -Wextra -Wpedantic -Werror
  "-DINTEGRATED_BUILD_ID=\"$ID\""
  -Irules/rmuc2027_capture_v1 -Irules/rmuc2027_v1
  rules/rmuc2027_capture_v1/capture_rules.cpp
  rules/rmuc2027_v1/match_rules.cpp integrated_scenario/driver.cpp)
g++ "${COMMON[@]}" -O0 -o build/integrated_o0
INTEGRATED_TEST_DRIVER="$PWD/build/integrated_o0" python3 tests/test_integrated_scenario.py
g++ "${COMMON[@]}" -O1 -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -no-pie -o build/integrated_sanitized
ASAN_OPTIONS=detect_leaks=0 INTEGRATED_TEST_DRIVER="$PWD/build/integrated_sanitized" \
  python3 tests/test_integrated_scenario.py
```

### Deliberate limits

Only whole-position adjudicated logical activation enters capture; the single
qualified detector-track `ActivationDelay` is not mistaken for robot/card
aggregation. Temporary restoration, outpost destruction/rebuild/lifecycle,
physical qualification, attack-damage/HP source facts and referee penalties are
not simulated. Each positive unsupported interval requires an exact external
unchanged-control assertion or the entire boundary fails. The 30-credit late-
capture example visibly chooses an author-adjudicated, nonofficial end-of-second
convention; compact partial-credit examples are explicitly unit-only.

The named capture/continuation/credit/judge phase order is an engineering input
policy, not verified official same-time timing. No credit cadence is generated.
Checkpoints replay the complete attempted prefix, O(prefix), and verify canonical
state/log/provenance; no raw-memory/private-state restoration or O(1) claim is made.
Hashes detect drift, not referee authenticity or compiler/binary attestation.
No real referee, physical match, cross-architecture, browser rendering, WASM,
full-match compliance, publication, package installation or external service was
used or newly verified for this milestone.

## 2026-11-05 · Experimental browser objective mode

Scope: `objective-experimental-v1`, a separate movement-only teaching profile.
Real sandbox movement/collision feeds placeholder qualified rectangle detection,
unchanged capture/match modules, named experimental Temporary-control latching and
optional end-of-second credits. This is not full RMUC or official detector timing.

Frozen native objective source identity:
`aaba15cc8d3937a5b5edb37bfab3f71ea13bd8553afea9798efe9495dfd42d12`.

Executed checks:

- 912 native objective checks exercise actual movement into/out of zones, inclusive
  center geometry/alive assumptions, causal ceil clock samples, exactly-once grace,
  capture endpoints, strict expiry/reacquisition rejection, strict capture/expiry
  coincidence, multi-frame rollback, named timer policy, latched continuation,
  batch equivalence, tutorial scores, exact 420 s timeout, terminal rejection,
  disabled-score timeout and reset.
- 681 separate white-box adapter checks use explicitly artificial qualified input
  timelines. They exercise internal deadline subdivision, contested-to-neither
  rejection under both policies, exact off-frame capture at 7,051 ms, current
  detector telemetry after a frozen capture endpoint, partition equivalence,
  full-duration credits and movement-only HP. These are adapter fixtures, not
  physical detector validation or an independent official-rules oracle.
- Five direct bridge/native API tests cover mode isolation, configuration, native
  parser trailing-data rejection, malformed/tampered replay atomicity, stale binary
  rejection, complete 25,200-frame cold replay and deterministic continuation.
- Three objective HTTP tests cover real endpoints, tutorial movement/capture,
  replay/mode isolation, fire/config rejection, Host/origin/header restrictions,
  duplicate keys including lone surrogates, deeply nested malformed JSON and
  preservation of state after rejection. Existing eight sandbox HTTP tests,
  one bridge test and original DOM/API regression suite passed.
- Original native suites passed: 6,562 sandbox checks with unchanged 3,000-tick
  fixture hash 2045014215; 149 match assertions; 1,057 capture assertions and
  1,033,476 capture property checks. Existing scenario suites passed 27 and 30 tests.
- Independent GCC O0/O2 comparisons of the exact frozen source produced identical
  complete 420 s output/state/replay bytes (1,485,867 bytes, SHA-256
  `43442e4e3f109532cde1fd70e46bcf80ba078f0909a3d03496255229f140ce4e`)
  and identical fixed-seed 1,500-batch trajectories (3,672,210 bytes, SHA-256
  `41785c58559a4c07f9d33d22276dc4b239c99a7ecc1f820da240802a140249e1`).
- ASan and UBSan passed both the 912-check movement suite and 681-check adapter
  suite. Leak detection was disabled (`ASAN_OPTIONS=detect_leaks=0`); no leak-check
  claim is made.
- SHA-256 preservation review confirmed all 35 protected standalone rule/scenario
  files unchanged, including both existing scenario source identities. The movement
  EngineOptions refactor preserves defaults/hash semantics but changes sandbox
  source identity: older sandbox replays need their original source build.

The native demo visibly captures the blue outpost at 7,050 qualified ms and red
base at 9,067 qualified ms. With explicitly enabled experimental credits, scores
are [13, 33] at 20 s and [413, 1233] at 420 s, with exactly 420 supplied credits.
Disabled credits produce `unscored-timeout`, never a fabricated official draw.

`tools/check.sh` includes both DOM/API suites and syntax-checks the optional
`objective_browser_smoke.cjs`. The latter is not a required runtime dependency.
Real Chromium rendering remains blocked by environment kernel socket permissions;
Node stub-DOM tests do not establish rendering, visual layout or browser physics.
No WASM, cross-architecture equality, official geometry/card sensing, temporary
restoration, outpost lifecycle, real combat, official scoring cadence, full-match
compliance, publication or package installation is claimed.

Final full `bash tools/check.sh` passed after the objective DOM fixture explicitly
restored its own strict/credits-disabled starting configuration. That fixture-only
correction prevents prior HTTP tests from leaking their enabled scoring setup into
UI default assertions; product reset semantics were not changed. The aggregate
includes both native objective suites, all legacy suites, direct API and HTTP
checks, JavaScript syntax checks and both live-server DOM/API suites. The objective
DOM suite verifies real movement/capture, current zone telemetry, config/reset,
in-flight mode switches, stale file selection, import busy guards, 12× backpressure
without skipped ticks, error pause/blur release, full 420 s terminal display and a
2,755,681-byte / 25,200-frame replay export/import through the actual UI handlers.
It does not execute a rendered browser.

## Experimental layered arena workbench · 2026-11-05

This milestone adds a separate calibration editor and static geometry validator,
not layered motion to the sandbox. The only shipped map is a deliberately synthetic
bridge/tunnel fixture. No official CAD coordinates, collision polygons or sensing
zones were promoted into either simulation.

New verification:

- 32 format/CLI tests cover strict bounded JSON, duplicate keys, unknown fields,
  nonfinite and boolean numbers, huge integers, references, convex polygons,
  floor/ceiling ranges, stacked surfaces, slopes, static cylinder contact,
  separate informational triggers, explicit portals, mutation safety and CLI errors.
- Six independent review tests include type-directed invalid substitutions across
  schema paths, valid Unicode/text/null roundtrips, numeric extremes, 1,000 exact
  flat rectangle/cylinder oracle cases, 1,000 analytical slope/ceiling cases,
  contact boundaries, and exact 256 KiB acceptance versus one-byte overflow.
- Seven live HTTP tests cover the sample/validate/query API, structured errors,
  malformed Unicode/deep JSON, duplicate keys, exact request envelopes, byte
  limits, Host/origin/client guards, static assets and unchanged native mode states.
- The Node stub-DOM suite drives the real local APIs and actual arena.js handlers.
  It tests stacked support and clearance, collision versus trigger evidence,
  display-only layer/slice selection, explicit support selection, click/numeric
  probes, invalid inputs, current-only export, stale validation/query/sample/import
  responses, two competing imports, byte limits and exact-limit export/reimport,
  API failures, safe text rendering and preserved sandbox/objective hashes.

Review fixes: numeric query bounds now precede float conversion, avoiding an
uncaught OverflowError for huge JSON integers. Exact-limit export omits an optional
trailing newline when necessary. The service resolves its project module when
loaded by the existing file-based tests as well as when launched normally.

Geometry limitations are intentional: each convex support patch is tested alone;
seams are not merged; a static upright cylinder conservatively envelopes the full
slope height range. Triggers and nearby transitions are annotations, not rule
outcomes or certified routes. Source hashes and calibration status are declarations,
not external verification. Polygon holes, swept motion, chassis dynamics, robot
capabilities and automatic calibration are not implemented.

Real rendered-browser/layout/mobile QA remains unverified because the prior
Chromium launch was blocked by kernel socket permissions. The mock-DOM suite is
not a substitute for it. No WASM, publication or new dependency installation was
performed. Official map integration still requires playable-boundary and sloped
frame calibration, complete valid source topology, layer/clearance mapping and
verified rule-zone/dynamic-object semantics.

Final source-tree aggregate `bash tools/check.sh` passed with all legacy native,
rule, scenario, objective, HTTP and DOM suites plus the new workbench suites.
The independent review suite asserts 2,772 expected invalid mutations and 2,000
geometric oracle cases. SHA-256 comparison against the preceding package confirms
all native/rule/scenario implementations unchanged; the sandbox 3,000-tick fixture
still hashes to 2045014215. Only documentation, the local service/check workflow,
main-page navigation/style, and the new standalone workbench files changed.

## Isolated native layered movement · 2026-11-05

Source identity:
`f272f47cdbd3a1c6e149601c2893918cbf2aea57fd8b51f86ede8e9fba73012d`.

The full source-tree `bash tools/check.sh` passed with every existing suite and
these additions:

- Native C++: 15 groups and 2,000 deterministic action pairs, with warning-clean
  `-Wall -Wextra -Wpedantic -Werror` compilation. Tests cover exact convex geometry,
  triangle boundaries/solids, contact, thin swept collisions/triggers, portals,
  ramps, clearances, one-way support, fixed-point bounds and atomic rejection.
- Python adapter: 12 tests for strict data/config validation, source/map/config
  identities, lower and upper sample routes, replay verification and rollback.
- Independent movement review: 12 tests including 100 circle/rectangle oracle
  resets, exact 3-4-5 corner contact, authored event order, tangential movement,
  coordinate extremes, one-way portals, slope/headroom and tamper rejection.
- Two native-profile stress/protocol tests: maximum object counts, up to 16-vertex
  shapes, 600 ticks at speed50, 64 portals, oversized raw CLI input and invalid
  frame/load/action rollback. The stress batch measured 3.741 seconds locally;
  this is an observation, not a universal latency guarantee. Native commands have
  a 20-second failure timeout; timeout invalidates a run rather than judging it.
- Seven live HTTP tests, including raw duplicate keys and decimal literals that
  Python would otherwise round, limits, malformed bodies, origin/Host guards,
  candidate reset/import failure and preservation of both prior simulation modes.
- Actual layered.js handlers with a mocked DOM and live native HTTP: keyboard and
  pointer movement, display-only layers, lower/upper support and clearance,
  785-tick ramp route, native replay bytes and roundtrip, raw JSON imports,
  stale file selections/reads, in-flight serialization, rejected and uncertain
  responses, metadata-failure veil/reconnect, cross-tab export mismatch, bounded
  backlog, input release, terminal limits, safe text rendering and mode isolation.

Independent GCC O0, O2 and UBSan executions passed all 12 review tests. They emitted
byte-identical 410-frame ramp/deck/trigger/random-input replay data and verified
imports, ending with state hash `8441ee8793abd5aa`; UBSan emitted no diagnostics.
This establishes those tested builds in this environment, not cross-architecture
or browser/WASM equality.

Review found and fixed zero-length segment containment (which falsely blocked
stationary spawn), loss of intermediate batch events, raw numeric-token rounding,
UI uncertainty/metadata mismatches, and ceiling contact attribution at seams.
Ceiling contacts now name the actual violated declarations in authored surface
order; this correction preserves the geometric blocking decision. A successful
step now exposes bounded
batch events separately from final-tick hashed state. Existing static arena
queries remain independent and do not acquire this movement model implicitly.

The sample's underpass reaches east_floor at z0. Its explicit west-floor → ramp →
upper-platform → upper-deck route reaches z1000 and only the upper trigger scope.
These are deliberately synthetic millimeter values, not official dimensions.
The README describes conservative two-piece seam support, L1 slope envelopes,
omitted structural undersides, exact fixed-point inputs and all other model limits.

No existing C++ sandbox/objective/rule/scenario implementation or static arena
validator/editor source was changed. Actual rendered-browser/layout/mobile QA and
persistent downloads remain unverified; the handler tests do not establish them.
No official CAD integration, full robot dynamics, combat, scoring, publication,
new dependency installation or WebAssembly path is claimed.

An additional ASan+UBSan build passed all 15 native groups and 2,000 deterministic
action pairs with no diagnostics. Leak detection was disabled
(`ASAN_OPTIONS=detect_leaks=0`); no leak-check claim is made.

## Trusted-local C++ policy SDK v1 · 2026-11-05

Current layered implementation identity:
`d2211eb2fa9afe458cefd9bf862f1ee02755cac87c1850ca885c20ce1114172c`.

The full source-tree aggregate passed with all earlier suites and the new SDK
native harness, seven policy bridge tests, independent policy integration tests,
five live policy HTTP tests and the actual policy UI handler/live-native suite.
The independent Python file contains nine tests: eight run in the standalone
aggregate, and the optional old/new manual-physics differential skips unless
LAYERED_BASELINE_BINARY names the preceding release. All nine were separately
run and passed with that verified baseline binary.

New evidence:

- The independent C++ harness verifies owning const snapshots after engine
  destruction and nested engine changes; exactly one pre-step decision per tick;
  shared manual/policy validation of integer, boolean, float, extreme and wrong-
  version outputs; and full state/frame/event rollback for fourth-callback
  exceptions, invalid outputs and cumulative event overflow.
- Seven bridge tests cover the shipped route and stop, partition invariance,
  speed17/off-lattice resumption, strict request/source guards, candidate failure,
  timeout/error response preservation, source-labelled replay and run limits.
- Independent Python tests verify no decision or descriptor callback during the
  action-import endpoint, historical source-label handling, stateless continuation,
  replay/source rejection, and candidate failures preserving the original process
  and complete journal. Four fixture configurations and 496 manual batches match
  the pre-SDK engine after excluding legitimate source-dependent hashes.
- Five live HTTP tests exercise version/controller negotiation, the native route,
  resolved-action replay, manual action preservation, strict rejected requests,
  run limits, origin checks and isolation from the sandbox/objective modes.
- Existing layered UI tests and the new policy suite run real JavaScript handlers
  with a mocked DOM and live native HTTP. They cover source switching without
  reset, pause/release/busy barriers, keyboard/touch isolation, the 900-tick route,
  action replay, historical attribution, strict new-response identity/tick checks,
  malformed metadata, stale file choices, uncertain-result recovery and other modes.

Independent O0/O2 executions produced byte-identical states, actions, source
segments and hashes for a 900-tick route across five batch boundaries and a
1,503-frame mixed/manual speed17 off-grid run, including action replay import and
stateless continuation. The shipped example reaches the goal at tick765 with the
standard fixture, then returns zero action. Arbitrary custom policies are not
thereby proven deterministic or stateless.

The native adversarial harness passed ASan+UBSan, also independently reproduced
with halt-on-error. Leak detection was disabled because LeakSanitizer cannot run
under this environment's ptrace restrictions; no leak-check result is claimed.
A genuinely stalled subprocess hit the transport watchdog and was terminated at
20.021 seconds. The watchdog is per native command, not a per-tick CPU budget or
an overall request deadline, and does not contain arbitrary descendant processes.

Measured local costs illustrate history dependence: a 35,400-frame idle prefix
plus policy60 took 0.123 seconds; a fragmented 10,000-frame alternating-action
prefix took 1.188 seconds to reconstruct. The example route in 30-tick batches
measured 0.897 seconds. These are observations, not real-time performance promises.

Replay v2 stores resolved actions and unauthenticated source declarations. The
import endpoint calls neither descriptor nor decision; the page's subsequent
metadata refresh can separately invoke a descriptor in a disposable process.
Registered-source hashing is not executable authenticity and does not automatically
cover transitive includes, toolchains, build flags or external inputs.

No sandboxing or untrusted-code safety is claimed. Trusted compiled C++ can access
files, network and processes beyond the typed observation; candidate rollback
protects simulation state, not external side effects. The supplied observation
contains authored geometry and current robot facts, but no provenance text/paths,
source hashes, replay history, plugin data or mutable engine references.

All protected original sandbox, objective, rule, scenario and calibration sources
remain unchanged. Layered manual behavior was differentially checked. Browser
rendering, actual pointer capture and persistent downloads remain unverified.
No publication, new dependency installation or official-match compliance claim.

## Filtered two-robot combat SDK v1 · 2026-11-05

The new `filtered-combat-v1` profile wraps the existing `src/sim.cpp` unchanged.
Its source registry identity is
`ece6a8131398fda85c922112033f347411248a22650933d990b6930b5faff65d`.
It is a separate native process/API/browser page, not a new official-rules mode.

Verified new coverage:

- Native core tests cover owned const snapshots, hidden-field mutation invariance,
  inclusive 6000 mm sensing and conservative cover boundaries, strict shared
  integer action validation, same-pre-step dispatch, callback exceptions and late
  invalid output rollback, both manual/policy orientations, and replay reconstruction.
- Independent native review additionally checks hidden projectile metadata and
  sorting invariance, enemy-HP independence, exact wall tangency, booleans/floats/
  nonfinite/extreme actions, both exception categories, and 600 unchanged-original-
  physics differential steps. It tests partitioned decisions, historical labels,
  observation/state divergence and stateless continuation.
- Nine independent Python integration tests exercise response field allowlists,
  strict per-robot requests, mixed source logs, action-only import without either
  controller callback, continuation, metadata failure isolation, candidate failures,
  forged native response fields, historical source labels and source freshness.
- Six live HTTP tests exercise filtered perspectives, manual inputs on both robots,
  mixed manual/policy operation, real policy-v-policy combat, malformed input and
  replay rejection, origin guards, full 7200-frame timeout/import, and isolation
  from the original sandbox, objective and layered modes.
- The actual new JavaScript handlers run with a mock DOM against the native HTTP
  server. They cover filtered drawing without stale enemies, perspective loading,
  independent source selection, held-input single-step, mouse/touch event handlers,
  busy barriers, raw replay JSON, historical label text, stale file picks,
  metadata failure with manual availability, exact response source/tick/observer
  checks, uncertain-result resynchronization without replaying the mutation,
  and terminal batches shorter than requested.

The shipped policy pair reaches a first disable at tick139, with native final
state hash `4126628050`. Both policies find contact around the central cover and
land hits. A request for 120 ticks after the initial 120 advances only 19 more,
returns terminal, and subsequent step requests reject. Manual stationary robots
are defeated from either controlled side. These are synthetic demo outcomes, not
measures of competitive strategy strength or real match odds.

Core O0/O2 output comparisons passed. Core and independent adversarial harnesses
passed ASan+UBSan with `ASAN_OPTIONS=detect_leaks=0`. LeakSanitizer could not run
under the environment's ptrace restriction; no leak-check result is claimed.

The precise no-hidden-field claim concerns the supplied immutable observation
and the selected public response only. Two trusted C++ policies share one process;
there is no prevention of global-variable sharing, I/O, or deliberate memory access.
The host's terminal flag is outside the policy observation. The developer replay
reveals both robots' inputs, source declarations and diagnostic hashes. It is not
an authenticated record or a filtered player view. Quantized, state-equivalent
input edits can pass state replay checks; the hashes do not certify original
intent or controller authorship.

No runtime browser or screenshot verification was performed. The established
Chromium process/socket permission blocker was not retried or bypassed. Layout,
actual pointer capture, mobile behavior, download persistence and WASM remain
unverified. There were no new dependencies, publication or official geometry changes.

The final source-tree aggregate `bash tools/check.sh` passed all old and new suites.
The existing optional layered pre-SDK differential remains opt-in and skips unless
its prior-release binary is supplied; this is not a new combat-test omission.
Independent O0/O2 complete combat JSON, resolved actions, observation hashes and
replays match exactly. The actual stalled-process watchdog terminated after
20.021 seconds. All 71 prior implementation files under src/rules/scenario/
integrated/objective/arena/layered were verified unchanged.

A local run under concurrent test load took 21.341 seconds to execute 59 batches
of 120 idle ticks (7080 total), repeatedly reconstructing the prefix. One mixed
manual/policy tick at that prefix took 0.409 seconds; its compact replay was
1,693,193 bytes. These measurements show the known O(prefix) cost, not a real-time
performance guarantee. The default combat example is much shorter.

## Unified tactical combat/objective slice · 2026-11-05

Tactical source identity:
`7865d1c72bf90f2674df59c4e322554027ab505692dfba5c68cb3fb2b91a6598`.

This is one authoritative native world, not synchronized independent demos.
Combat, qualified occupancy, activation/capture, experimental one-second credits
and 420 s ending are advanced from the same 60 Hz tick. Original physics, filtered
combat observation/validator and verified capture/match modules are unchanged.

New evidence:

- The native core suite runs the shipped two-policy scene through both captures,
  real projectile damage, robot disable, continued 25200-tick timeout and complete
  callback-free action replay. Both captures and damage have occurred by tick 600;
  red is disabled by tick 1200 without ending the match. Final experimental control
  score is `[412,1236]`, outcome blue by control, state hash `3092190057`.
- The independent native suite passes 1,158 assertions, including a separate real
  manual route that captures, earns credit, fires projectiles, disables its opponent,
  continues to 420 s and reconstructs the entire same run. It tests immediate death
  versus ordinary 2 s grace, death before/exactly at capture completion, capture before
  the final second's credit, two-frame strict rejection rollback, simultaneous-death
  contested-to-neither rejection under both boundary policies, and terminal refusal.
- Privacy tests make 200 hidden-state mutations while holding permitted public
  control/score fixed, checking unchanged observation bytes and pure example
  decisions. Hidden capture progress, detector/grace state, enemy statistics,
  invisible projectiles and global/private fields do not enter the SDK snapshot.
  Both robots receive the same pre-action public broadcast alongside their own
  local filtered combat snapshot.
- Nine independent bridge groups verify all public response/config combinations,
  strict scalars, mixed sources, replay batches longer than 600 frames without any
  controller callback, stateless continuation, candidate/descriptor failures,
  forbidden hidden fields, late divergence/config corruption, valid-but-wrong
  native restoration config, public-score/result consistency, historical source
  declarations and source dependency guards.
- Five live HTTP tests pass, including the entire default 25200-tick capture/combat/
  score/timeout run, full import and action equality, independent source actions,
  preset restoration, duplicate-key/type/source/config/hash rejection, origin guards
  and unchanged sandbox/objective/layered/filtered-combat states. This initial
  live suite took 50.214 s under concurrent work, not a real-time promise.
- The new actual JavaScript handlers run with a mock DOM and live native HTTP.
  Coverage includes selected-only combat drawing plus public objective ownership,
  real manual capture→score→fire→disable in one scene, disabled robot controls with
  continued time advancement, all 420 s control-win/tie/unscored outcomes, bounded
  acceleration without skipped ticks, reset-only configuration, strict privacy
  field rejection, inherited input/source/perspective/replay race guards and
  uncertain-response resynchronization without mutation retry.

O0/O2 complete 25200-frame replay exports are byte-identical, not merely equal at
termination. The captured comparison JSON SHA-256 is
`f7cb843a1ae400d9b871d1f4c4f3fd223abfbfc0e569cb2f9277c8fa0b34823c`;
this identifies that test artifact, not a signed replay authenticity guarantee.
The standalone SDK/example compiles as C++17 with strict warnings. Native core
and independent 1,158-assertion review suites pass ASan+UBSan with leak detection
disabled. LeakSanitizer cannot run under this environment's ptrace restriction;
no leak assessment is claimed.

Supported facts and deliberate assumptions remain separate. Rectangular detection,
alive eligibility, immediate death deactivation, ceil-ms sampling, timer-before-
input ordering, latched Temporary control, public score/control broadcasts and
end-of-second credit cadence are named engineering choices. Death is not passed
through ordinary-loss grace. No sandbox damage is promoted to an official settled
attack ledger; equal control at timeout is explicitly unresolved. Strict and
contested-to-neither ambiguities reject the complete batch rather than fabricating
an adjudication. The initial source comparison confirms all 79 prior implementation
files unchanged.

Trusted policies share one native process. Snapshot filtering is not protection
against malicious memory access, shared globals or I/O. Replay reveals both action
streams and diagnostic hashes, not authenticated intent. Candidate rollback does
not undo arbitrary external side effects. Prefix reconstruction remains O(history),
so long-run browser responsiveness is not guaranteed 60 Hz wall time.

No actual browser rendering, pointer capture, mobile device behavior, download
persistence or WASM was verified. The existing Chromium socket-permission blocker
was not retried or bypassed. No publication or dependency installation occurred.

## Settled damage ledger and tactical timeout v2 · 2026-11-05

The new source-linked module is `rmuc2027-damage-v1.0.0-module.1`. Tactical engine
identity is `integer-mm-tactical-v2`; the SDK observation profile and action frame
schema are unchanged, but configuration and build identity reject old-source replays.
Current tactical source identity:
`2ab0a493e120afbd3fc172678b442306857aeeb5ee75f67e87167769a0b562cd`.

The pinned manual PDF hash, 17 quoted source substrings and printed-to-PDF page
mapping were independently rechecked. The source audit also visually inspected
five relevant pages. It distinguishes actual settled HP deduction, explicit
opponent attribution for violation penalties, robot-only collision/offline
exclusions, friendly/unknown-source XP versus team-score attribution, delayed
referee settlement and unresolved simultaneous/dart/overkill dependencies.

New deterministic coverage includes:

- Formal module: qualifying attack causes and entity kinds, externally supplied
  recipients, penalty direction and settled yellow/red-card differences, supported
  non-attack exclusions, unsupported building/cause pairs, invalid enums/values,
  monotonic sequence/time, sealed ledgers, int64 overflow and whole-batch rollback.
- Native adapter: multiple same-tick opposing hits are reconciled with clamped
  actual loss, without allocating overkill to individual projectiles. Nominal110
  versus actual100, simultaneous mutual knockout, unexplained HP changes, invalid
  event attribution and private-ledger atomicity are exercised.
- An action-only playable route moves and fires, leaves control scores `[0,0]`,
  and reaches actual tick25200 with red attack damage100 versus0. The verified
  comparison chooses `red-wins` / `timeout-attack-damage`; all25200 recorded action
  frames reconstruct the same private accounting and final state.
- No-hit complete runs reach a genuine all-equal draw. Explicit missing-ledger or
  unsupported settlement stays unresolved when attack comparison is required.
  Higher control priority and scoring-disabled endings do not invent a later tier.
- Final-tick projectile settlement is included before timeout under the named
  experimental phase order; post-terminal mutation is rejected and ledgers seal.
  Boundary injections in white-box tests are synthetic fixtures, not official
  evidence that impacts at the real-world horn should count.
- Filtered observations remain byte-invariant under changed private accounting.
  Native administrative damage/remaining-HP fields are validated and stripped from
  ordinary API responses. Terminal UI reports only result and comparison basis.

GCC O0/O2 full default-scene command outputs, including all25200 action frames,
final private state and exported replay, are byte-identical. Comparison artifact
SHA-256: `6a0c235b28adb194d6055850a709506bb03d1d148778cde93fe1dc7f31cdc5ba`.
The same default policy run retains control `[412,1236]`, private attack `[80,100]`,
remaining robot HP `[0,20]` and a blue control win. Its new state hash is1639330461;
the state hash changed because private accounting and config are now included.

The surviving-HP victory branch is tested with externally settled standalone
facts. It is not naturally reachable after tied attack totals in the current
symmetric100HP, no-healing, opposing-projectile-only scene. The formal ledger
accepts upstream settled facts but does not authenticate them, generate penalties,
resolve friendly fire, calculate XP, implement dart splash, or establish official
armor/detection/buff/rounding/timing mechanics.

The 79 pre-existing implementation files in src, older rule modules, scenario,
integrated, objective, arena, layered and filtered combat remain unchanged. This
milestone adds no dependency installation, publication or official arena geometry.
Real Chromium execution remains blocked by the previously established socket
permission failure; it was not retried or bypassed. JavaScript handler tests with
a mock DOM and live loopback API do not establish rendered layout, actual pointer
capture, mobile behavior or download persistence. WASM remains unbuilt/unverified.

The independent damage review passes **178 native checks** at O0 and O2 and under
AddressSanitizer+UBSan, plus **five Python integration tests** against the pinned
production binary. Review found impossible nominal-damage diagnostics accepted
by the bridge; complete-mode positive/multiple-of10 and profile bounds now reject
them. Late callback failure after a successful damage tick verifies full private
ledger rollback. Leak detection was disabled because LeakSanitizer cannot run
under ptrace in this environment; no leak-check claim is made.

The new formal damage suite and tactical-damage suite also pass ASan+UBSan with
leak detection disabled. The final test-only posthumous-projectile regression is
included: an already fired projectile may deduct HP after its owner is disabled;
no unsupported source-alive scoring gate was introduced.

Focused production checks passed: **10 tactical bridge/transaction tests**,
**six live HTTP tests**, and the actual `tactical.js` handler suite with a mock DOM
and live native HTTP. Full420s cases cover unscored, control win, attack win, draw,
missing-ledger unresolved, and replay reconstruction. Selector changes, busy/file
read barriers, stale/invalid metadata, uncertain responses, bounded backlog and
mode isolation are retained. New private counter bounds and inconsistent result
reasons are rejected before any candidate commit. The documented minimal C++
ledger example also compiles and runs with strict warnings.

Final complete source-tree `bash tools/check.sh` passed with exit0, preserving all
older native, formula, scenario, arena, layered, filtered-combat, API and handler
regressions. Its optional pre-policy layered differential normally skips without
a historical binary; it was separately rerun with the retained baseline and all
nine tests passed. The final JS suite includes the added no-progress assertions.

A clean extracted source package was rebuilt independently. `bash tools/build.sh`
passed every native suite, followed by all10 tactical bridge tests, allfive
independent damage integration tests and allsix live tactical HTTP tests, including
the final HTTP no-progress guards and full-duration replay outcomes. Documentation
was then finalized and the source archive/manifest reverified. The extracted check
is a native build plus focused tactical tests, not a second complete aggregate run.
No build output or test cache is included in the delivered source archive.

## 17 mm heat rules and tactical integration v3 · 2026-11-05

The new source-linked module is `rmuc2027-heat-v1.0.0-module.1`. Tactical engine
identity is `integer-mm-tactical-v3`; action SDK 1 is unchanged, while tactical
observation revision 2 adds only the robot's own thermal state. Final tactical
source identity:
`cf44da44d2bc7eb2732a6ecc1f5e778c0fee5cc128ec804ef3a30435605679cd`.

The pinned PDF checksum, all 94 quoted source fragments and printed/PDF page
mapping were independently rechecked. The source audit visually inspected 25
pages and all 20 infantry heat-profile rows. It identifies a real prose/diagram
conflict at Q0+100, not an implementation rounding issue. The default module
rejects this equality, coincident shot/cooling and shot/death ambiguities. Named
engineering alternatives are explicit configuration, not official adjudications.
The tactical adapter explicitly chooses cooling-before-fire-gate/shot and
shot-before-death-reset, with strict Q0+100 equality handling by default.

Verified arithmetic uses exact integer tenths: a detected 17 mm round adds 10 heat,
cooling settles at 10 Hz, and temporary lock persists until zero. Permanent lock
survives cooling/death until the next round. Heat never deducts HP, creates a
projectile damage event or awards attack-damage credit. The tactical heat fixture
borrows only infantry level 1 cooling-priority Q0=40/C=12; synthetic HP 100 and
other physics remain synthetic. First-person video behavior is not simulated.
In particular, a direct permanent branch's video consequence is unspecified.

New module, tactical and independent review coverage includes:

- Exact lower/equality/upper thresholds, sequential detections, intermediate
  crossings inside a multi-shot batch, strict atomic failure, integer overflow,
  skipped cooling periods, event timestamp ordering, independent lock reasons,
  temporary release and persistent permanent lock through death/zero heat.
- Same final heat with different lock outcomes under explicit shot/cooling order;
  batch/sequence equivalence for 1–25 detected rounds; a 270-tick independent
  arithmetic oracle against actual native shot emission.
- Gate only effective native fire, preserving requested manual/policy actions in
  replay. Native cooldown/ammo/death blocks and thermal blocks do not add heat or
  spend ammunition. An emitted projectile immediately blocked by cover does add
  heat. Held requests resume after cooling to zero without blanket input disable.
- Two-tick transactional failure after actual projectile damage restores native
  physics/projectiles/IDs, HP, damage ledger/sequence, thermal states/counters,
  journals and hashes. Heat-disabled comparison leaves thermal counters zero.
- Own-only filtered observations and strict public/native thermal allowlists;
  17 malformed thermal payload cases; six heat/configuration variants; exact
  source/config/observation pins and callback-free replay reconstruction.

The native heat module, tactical heat suite and independent heat review pass
AddressSanitizer plus UndefinedBehaviorSanitizer with leak detection disabled.
LeakSanitizer is unavailable under this environment's ptrace restriction; no leak
assessment is claimed. The independent review also passed a separate UBSan run.
The optional historical layered differential was separately run with its retained
baseline binary: all nine tests passed, including the one that normally skips.

GCC O0/O2 full default heated-scene outputs, including all 25,200 recorded action
frames, private final state and exported replay, are byte-identical: 15,108,848
bytes, SHA-256
`89efd1da48a18ab7e2a56121c7ff215aa4121e2441216d1a498e5ff8af4e0c71`.
That policy run ends at tick 25,200 with control `[412,1236]`, private attack
`[80,100]`, surviving HP `[0,20]`, heat `[0,0]`, a blue control win and diagnostic
state hash 2168488852. The SHA identifies test output, not signed replay
provenance or a cross-platform determinism proof.

A separate default heated manual-fire 420 s HTTP run spends exactly 120 emitted
rounds, retains held requests during thermal lock, ends with zero heat, and
replays all 25,200 ticks. Firing into cover leaves both HP 100 and attack totals 0.
Thus thermal tests cover sustained fire rather than relying solely on a policy
that avoids requesting fire while locked. Permanent-threshold/equality cases
are module or injected fixtures, not naturally reachable gated scene behavior.

All 83 pre-existing implementation files in the original sandbox, prior rule
modules, scenario/integrated harnesses, objective, arena, layered and filtered
combat modes are unchanged. No publication, dependency installation or official
arena integration occurred. Trusted policies still share a native process;
filtered observations are not isolation against malicious code. Full-prefix
candidate reconstruction remains O(history), not a 60 Hz wall-clock guarantee.
Real Chromium rendering/interaction remains blocked by the previously established
socket-permission failure, with no new retry or bypass. Mock-DOM/live-API tests do
not establish real pointer capture, mobile behavior, rendered layout or download
persistence. WASM remains unbuilt and unverified.

Focused final-source checks pass: 10 tactical bridge/transaction tests, five
independent damage integration tests, five independent heat integration tests,
all eight live tactical HTTP suites, and the complete actual `tactical.js`
handler suite with a mock DOM and live native HTTP. The handler suite exercises
thermal telemetry, held-fire recovery, movement during lock, private-field
rejection, heat replay, every reset-only heat choice and prior full-duration
control/attack/draw/unresolved/unscored outcomes. A permanent-lock presentation
fixture tests wording only; it does not claim ordinary play can reach that state.
One concurrent development HTTP run was interrupted by executable replacement;
that case passed after freezing builds. No product assertion or source fix was
needed. The independent aggregate uses the frozen executable.

The clean extracted package rebuilt every native suite successfully and passed
all 20 focused tactical/damage/heat bridge-review tests. Only documentation was
changed afterward; compiled sources, tests and production source identity stayed
identical. The source archive includes no executables, build output, test caches,
manual PDF or CAD file.

Final source-tree `bash tools/check.sh` passed with exit0: every native suite,
all earlier formula/scenario/arena/layered/filtered-combat regressions, Python and
JavaScript checks, eight tactical HTTP suites and the full handler suite. The
clean extracted package additionally passed all eight tactical HTTP suites,
including default heated 420 s firing and replay plus legacy timeout outcomes.
The extracted run is a native rebuild plus focused integration/HTTP verification,
not a second complete aggregate. All temporary test listeners were stopped.
Documentation was then finalized and both source/extracted manifests and the
archive integrity were reverified. Only the three explanatory documentation files
changed after the clean extraction; no tested implementation or test changed.

## Configurable synthetic multi-robot roster v1 · 2026-11-05

The new isolated scene is `experimental-roster-tactical-v1`, native engine
`integer-mm-roster-v1`, action SDK1 and observation revision1. Default synthetic
2v2 supports bounded 1–4 robots per side with stable ASCII IDs and per-robot
profiles. It does not replace or reinterpret the previous tactical scene.
Final source identity:
`2ee07a70036b4759ea409c704f95ba2de41a02794087c634fed744c943f82f37`.

The configuration and per-ID request/replay contract is in
`roster_mode/CONTRACT.md`. Configured starting roster, profiles, spawn poses and
eligibility are explicitly public. Current other-robot HP/heat/ammo, projectile
ownership, hidden poses, capture progress and damage accounting remain excluded
from policy observations and ordinary HTTP/UI state. Team members use the same
range/LOS pose filter; no implicit teammate telemetry was added.

Independent review passed 477 native assertions, covering frozen same-tick
callbacks, stable-ID and request-order equivalence, late callback/heat/capture
rollback, independent teammate grace/death continuity, simultaneous four-victim
overkill, team ownership/attribution, visibility boundaries, heterogeneous
profiles, 4v4 stress, collision closure, disabled bodies and full-duration replay.
Review found a tangent-contact movement deadlock and missing bridge LOS/pose
checks; both were fixed and regressed before the final implementation freeze.

Final bridge validation also requires exact config-derived tick0 state, not just
valid numeric ranges. An intercepted own x+1 reset was previously plausible but
incorrect; final checks reject it across constructor, RESET, STATE and empty replay.
Ten independent adversarial Python tests and eleven fast bridge tests pass on the
final pin, including seven tick0 corruption families across candidate operations,
live-process/replay preservation and candidate cleanup. All 16 supported team-size
combinations are exercised. Seven live HTTP suites pass with duplicate-key,
malformed-query, config/source/replay tampering, mixed per-ID controls, synthetic
heat/capture, 1v1/2v2/4v4 and earlier-mode isolation coverage.

The actual default all-policy 2v2 run completes 25,200 ticks: all four objectives
are captured, all four robots fire, 221 actual shots are emitted, two robots are
disabled and two survive. It finishes with control `[0,0]`, private attack damage
`[140,140]`, remaining team HP `[40,40]`, all heat zero and a `timeout-equal` draw.
The symmetric objective swaps do not give either side both outposts or both bases;
zero control score is the verified formula, not a missing scoring implementation.
A documented asymmetric run (red policies, stationary blue manual robots) reaches
control `[133,0]` with rate `[4,0]` at tick2400, confirming visible score growth.

Final-pinned GCC O0/O2 complete command output is byte-identical, including all
25,200 recorded per-ID action frames, private final state and replay export:
32,747,646 bytes, SHA-256
`bf619941aa6879dbad29473de529299b99bb539ed9f1812493e0268e7389797a`.
Final diagnostic state hash:652702532. This is same-machine compiler optimization
checking, not cross-platform determinism or replay authentication.

Both roster native suites pass AddressSanitizer plus UndefinedBehaviorSanitizer.
Those runs used diagnostic pin
`e03c91cdc74e25c9f633d972ef39940acddb3202b7406997393d58146edeaf01`;
the later pin changes were confined to Python exact-reset validation, with native
C++/SDK sources unchanged. LeakSanitizer cannot operate under this executor's
ptrace restriction and was disabled; no leak-check pass is claimed. The standalone
SDK and documented minimal controller compile as C++17 with warnings as errors.

Final JavaScript guard tests with a mock DOM and live final-pinned native HTTP
pass: raw staged config retains duplicate keys for server rejection, a64KiB draft
limit, seven malformed reset-response recovery cases, filtered-payload validation,
per-ID source/perspective changes, busy/stale file guards, uncertain reset fallback
and bounded simulation backlog. JavaScript source SHA-256:
`12bb62e90d83d935ad1ddc62fab730b4a20a941d09c70c02a3e12013897ff5f6`.
The earlier full UI run also exercised own heat/movement, the entire420s finish,
replay and older-mode isolation; final full-suite results are recorded below.

All 101 earlier implementation files under src, rule modules, scenario/integrated,
objective, arena, layered, filtered-combat and previous tactical directories remain
byte-identical. Root server routing, navigation and build/check scripts add the new
mode without replacing earlier authorities. No dependency installation or
publication occurred. There is no official lineup/map/complete match claim, no
untrusted-code isolation and no verified WASM or actual-browser rendering/pointer/
mobile/download behavior. The existing Chromium restriction was not retried.

The final frozen source package was extracted into a clean directory and the
complete `bash tools/check.sh` finished with exit0 there. This includes every
native suite, all prior scenario/arena/layered/combat/tactical regressions, the
final 11 fast and 10 independent roster bridge tests, all seven roster HTTP tests,
and the **full final JavaScript suite without FAST mode**. The final UI run passed
own-heat/movement behavior, 25,200-tick termination, complete replay round-trip and
older-mode isolation. The optional historical layered differential was enabled
using the retained earlier binary, and all nine tests passed; that historical
binary is not included in the source-only package.

An earlier development aggregate correctly rejected a stale native source pin
while the final Python reset hardening was landing. Its failed log is not treated
as a pass. The clean extracted aggregate above rebuilt and verified the complete
final pin, with no implementation or test edits during the run. Source/extracted
manifests matched afterward. Only this verification record was then finalized,
and the final ZIP plus both manifests were reverified. The archive contains 195
UTF-8 source/documentation files plus its manifest, with no compiled executable,
object, test cache, credential file, manual PDF or CAD asset.
