# Experimental browser objective teaching mode

**Playable local engineering integration, not full RMUC, physical referee detection,
verified official scoring cadence, or a real-match win-rate model.**

The original `sandbox-v1` combat mode remains separate. This new mode uses its real
60 Hz integer movement and cover/robot collisions, but rejects all fire actions.
Four placeholder rectangles qualify robot-center occupancy and feed the unchanged
source-linked capture and match modules. There are no bullets, HP deductions,
attack attribution, destruction, restoration or rebuilds in this mode.

## Run and try

```bash
bash tools/build.sh
python3 server.py
# Open http://127.0.0.1:8765 and select experimental objective mode.
python3 tests/test_objective_api.py
bash tools/check.sh
```

Choose the visible boundary and scoring options, then apply/reset. Strict timing
and **credits disabled** are the defaults. Enable the clearly labeled
`EXPERIMENTAL-end-of-second-v1` option to run a scored experiment. Choose C++ demo
control and advance 10 seconds: red captures the blue outpost at 7,050 ms, blue captures
the red base at 9,067 ms. Scores are [3, 3] at 10 s and [13, 33] at 20 s. The intentionally
asymmetric teaching routes are not a competitive strategy. Human red/idle blue is
available for exploring zones without interference; hybrid uses human red/demo blue.
The UI can accelerate simulated time without dropping or enlarging physics ticks.

The genuine match-duration boundary is 420,000 ms / 25,200 ticks. With credits enabled,
the verified judge returns BlueWins on the demo route with scores [413, 1233]. The
420th credit is included. With credits disabled, the teaching run instead stops
with `unscored-timeout`; it does not invent a zero control total and announce a
verified match result. An unsupported strict boundary rejects the entire requested
batch and pauses the UI as an invalid experiment, never a team loss.

## Physical input and qualified detection

- Physics time is exactly `tick/60` seconds. Each frame first applies both actions
  and common-state movement/collision rules. Input frames are the replay authority.
- A zone detects a robot iff its center lies in the inclusive rectangle and HP > 0.
  Robot radius does not expand the rectangle. Rectangles are placeholders, not
  official field zones. Walls still constrain movement into them.
- There is exactly one robot per team, assumed alive, normally connected, eligible
  and equipped for detection. No card sensor, detection lag, multi-robot/card
  aggregation, eligibility invalidation or death adapter is implemented. Objective
  HP stays at the sandbox engineering value 100 because combat is disabled.
- The post-move detector sample is qualified at `ceil(tick*1000/60)` milliseconds:
  ticks 1, 2, 3 become 17, 34, 50 ms. `EXPERIMENTAL-ceil-sampled-ms-v1` adds less than 1 ms of
  causal quantization relative to physics. It is not exact rational sensor timing
  or an official detector implementation. Metadata exposes both the rational world
  clock and qualified milliseconds. A sample's occupancy applies from that time,
  never retrospectively to the preceding interval.
- Each zone/team has one `ActivationDelay`. Detection acquired/lost edges are sent
  only when sampled detection changes. The verified 2 s grace is applied exactly once.
  Logical active flags are aggregated atomically for the whole position before
  passing them to `CaptureAttempt`. Raw occupancy never directly sets control.

## Exact boundaries and explicit unsupported continuation

Internal grace deadlines strictly before a sample are processed first. A detection
edge coincident with its grace expiry is passed in the same call, not hidden behind
an earlier no-edge advance. The two detector tracks are aggregated before capture,
so no entity-iteration order can invent a contested intermediate state.

Capture progress runs on the qualified millisecond timeline. If a 5 s endpoint lies
strictly before a new logical input, capture consumes exactly that endpoint, then
the explicit latched strategy handles the residual interval. At equality the input
is passed intact to the module, so the selected policy governs the coincidence.
Strict mode is `strict-reject-unadjudicated-v1`; an explicitly selected
`ENGINEERING-NOT-OFFICIAL-timer-before-input-v1` resolves the supported timer/input
coincidences. Neither resolves contested-to-neither: that unsupported input remains
an atomic error. No silent fallback, arbitrary exit ordering or forged external
adjudication is used.

The first verified Full→Temporary capture completes that zone's attempt. Its
capture snapshot remains frozen. **`EXPERIMENTAL-latched-temporary-no-lifecycle-v1`
holds that Temporary control for the remainder of this teaching run**, even after
the robot leaves. Current detector/active/grace telemetry continues independently.
This is deliberately not the official Temporary-state rule and not a claim of
external lifecycle adjudication. No new capture attempt, restoration, destruction
or rebuild can occur in that zone until reset.

## Score and judge boundary

The verified formula awards 1 point/s if one side controls both outpost positions,
and 3 points/s if it controls both base positions. `rate` exposes that formula even
when credit generation is disabled; it is not an accrued score.

Only explicit `EXPERIMENTAL-end-of-second-v1` opt-in produces credits. At physical
whole seconds 1 through 420, after same-time activation/capture, the adapter supplies
one sequential credit using current experimentally latched control. The unchanged
ledger only calculates the credit; it does not verify this cadence. The adapter
then invokes the unchanged match judge. This ordering is an engineering strategy,
not a referee ruling. No partial/fractional or control-relative credits are used.

The supplied attack ledger is exactly 0 because attacks do not exist. It is **not**
computed from HP loss. Base HP 1 is only an alive sentinel; no official base HP or
base combat is simulated. Surviving HP 100 per robot is a teaching placeholder and
both robots remain alive. Enabled-score outcomes therefore validate composition of
the selected limited model, not a complete RMUC match.

## API, isolation and replay

`GET /api/objective/meta`, `/state`, `/replay`; `POST /api/objective/reset`, `/step`,
`/import`. They use a distinct native process and do not mutate `/api/*` sandbox
state. The existing loopback Host/origin/header restrictions apply. No publication,
cloud service, package install or external request is needed.

Reset requires exactly `boundary_policy` and `credit_strategy`. Step requires
exactly `count` 1..600, `mode` 0..2 and action `[dx,dy,aim_x,aim_y,0]`. Mode 0 means human
red/idle blue, 1 scripted both, 2 human red/scripted blue. Normal sandbox step limits,
fire, 120 s duration and death termination are unchanged. Every objective native
batch, even if it fails after several proposed frames, is a candidate-copy
transaction over physics, clocks, detectors, capture, credits, judge and journal.
The rejected batch leaves the entire prior run unchanged. A batch reaching 420 s
commits only the prefix through timeout; later steps are rejected.

State uses the sandbox display shape plus `profile:objective-experimental-v1` and
an `objective` object. Top-level `build`/`hash` are objective identities; the
`movement_build` field identifies the movement source. Zone telemetry includes
rectangles, initial/controller teams (0 red, 1 blue), degree, sampled detection,
logical activity, pending grace deadlines, progress, completion and exact endpoint.
`score` is null when disabled. Events are empty because combat events do not exist.

Objective replay schema 1 / engine `objective-native-v1` pins the full configuration,
source identity, sequential resolved actions, each canonical state hash, and final
hash. The native source fingerprint includes movement, adapter, both unchanged
rule modules and their provenance. It is distinct from sandbox/scenario identities.
The bridge rejects stale binaries before use. Import cold-replays into a candidate
native process and swaps it in only after complete hash verification. Wrong mode,
build, policy, clock/lifecycle strategy, malformed input, duplicate keys, bad
sequence or any divergence leaves the current run unchanged. Importing into the
other mode is rejected. Stateless demonstration policy resumes deterministically
from the imported world state. Full 420 s exports fit the 4 MB objective body bound.

Hashes are deterministic drift checks, not cryptographic signatures or referee
proof. Objective hashing uses FNV-1a over canonical little-endian 64-bit fields,
including the original movement hash and authoritative rule snapshots/configuration.
Replay is complete-prefix re-execution, not O(1) memory snapshotting or video.

The configurable `sandbox::EngineOptions` defaults preserve original sandbox
behavior and canonical fixture hash 2045014215 at 3,000 ticks. Its source fingerprint
necessarily changed; **old sandbox replays require their original source build**.
The rule and standalone scenario source identities remain unchanged.

## Verification limits

Native movement/boundary tests, strict/timer tests, whole-batch rollback,
complete 420 s score/timeout, replay round-trip/continuation, malformed API/mode
isolation, plus existing regressions are automated. See `docs/VERIFICATION.md` for
final executed checks. Chromium rendering remains blocked by this environment's
kernel socket permissions; Node DOM/API checks are not rendered-browser QA.
WASM, cross-architecture equality, official detector timing and full RMUC lifecycle
are not established.
