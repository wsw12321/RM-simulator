# Trusted-local C++ policy SDK v1

This interface lets a locally compiled C++ controller drive the existing single
robot in the synthetic layered mover. It is not an official RMUC sensing model,
fairness model, calibrated robot model, combat engine or referee. The controller
gets full authored geometry, including every layer, solid and portal.

## Edit, build, run

1. Read `layered_mode/policy_sdk.hpp`. It is the standalone C++17 public contract
   and does not include `core.hpp` or expose an `Engine`.
2. Edit `layered_mode/example_policy.cpp`. Implement
   `layered::policy::descriptor()` and
   `layered::policy::decide(const Observation&)`. The descriptor ID is a bounded
   1..64-character ASCII identifier beginning with a letter. The shipped ID is
   `synthetic-ramp-deck-v1`.
3. From the repository root, run `bash tools/build.sh`, then restart
   `python3 server.py`. Both build and restart are required after source edits.
4. Open `/layered.html`, load the synthetic sample, and select the compiled policy
   as the **next batch input source**. Step or run it. The selector pauses/releases
   manual input when changed; it is not a persistent server controller setting.

No HTTP endpoint compiles, uploads, imports or downloads arbitrary controller
code. The browser selects the one locally linked controller. The build compiles
`core.cpp`, `policy_adapter.cpp`, `example_policy.cpp`, and `cli.cpp` together.
GCC/Clang with signed 128-bit support is required by the existing geometry core.

Minimal callback shape:

```cpp
#include "policy_sdk.hpp"
namespace layered::policy {
Descriptor descriptor() { return {kSdkVersion, "my-controller-v1"}; }
Action decide(const Observation& observation) {
    if (observation.robot.x_q1024 < 1500 * kXYScale)
        return {kSdkVersion, Integer{1000}, Integer{0}};
    return {kSdkVersion, Integer{0}, Integer{0}};
}
}
```

This small callback is only an interface example, not a safe planner. Geometry
and contacts determine whether its requested movement is physically accepted.

## Observation contract

`Observation` is an owning value with const top-level fields. The adapter makes
a new snapshot immediately before each simulated tick and passes it as a const
reference to `decide`. It contains no pointers/references to the live engine.
Copying a nested geometry/config value gives a detached copy, never mutable
access to the simulation. Normal C++ const/value semantics are the API contract,
not protection against malicious memory access by native code.

Exposed fields:

- `sdk_version = 1`, `hz = 60`, and current integer pre-step `tick`.
- One robot's exact X/Y in 1/1024 mm and Z in 1/1048576 mm, plus selected support
  surface and layer IDs. There is no heading, velocity, enemy list or weapon.
- Immutable radius/height in mm, speed in mm per tick per axis, slope limit in
  Q1024 and run tick limit. Current pose is exposed; the initial spawn/history is
  not a separate observation field.
- Full geometry in authored order: boundary; surfaces with polygon, affine
  height, traversability and optional ceiling; global solids; scoped annotation
  triggers; explicit zero-step portals with direction flags. Polygon/portal XY
  uses Q1024; volume and ceiling heights use integral mm. A surface plane is
  `z_q1048576 = z_mm*1048576 + dx_q1024*x_q1024 + dy_q1024*y_q1024`.
- Previous completed tick's contacts and events, and current active trigger IDs.
  Tick-zero observations may include initial trigger-enter events. Events are
  not a cumulative log and contacts do not reveal future attempted movement.

Not exposed through this API: source records, URLs or paths, calibration notes,
metadata descriptions, provenance, hashes/build IDs, replay/action history,
external/plugin/account data, other robots or an `Engine` handle. Necessary
authored surface/layer/volume/portal IDs remain available. This input restriction
does **not** prevent trusted same-process C++ from accessing machine/process data.

Bounds come from the independently validated native map loader: 32 surfaces,
64 solids, 32 triggers, 64 portals, 3..16 vertices per polygon, at most 32 scopes
per trigger, and at most 64 bytes per identifier. Contact snapshots are bounded
by 68 entries; latest-tick event snapshots by 3250 entries. Coordinates and
slopes retain the exact limits in `README.md`. The snapshot owns its vectors and
strings; there is no unbounded history stream. These are API data bounds, not
resource limits on arbitrary user-written C++ allocations.

## Action, clock and validation

`Action {sdk_version, dx, dy}` uses tagged integer/double/bool values so the
validator can reject accidental boolean or floating-point outputs instead of
silently converting them. Version must be integer 1; each component must be an
integer in `[-1000,1000]`. No extra control channels are supported. A zero action
still advances one tick. Wrong request/descriptor/observation/action versions
are rejected; there is no best-effort SDK compatibility fallback.

Both manual input and policy output pass through the exact same native
`policy::validate_action` function before the existing `Engine::advance` path.
The Python manual API also rejects JSON booleans/floats before native transport.
The adapter never clamps invalid outputs or normalizes a diagonal.

For every accepted tick:

1. Snapshot the candidate's current pre-step state.
2. Call `decide` exactly once.
3. Validate the returned action.
4. Apply the original fixed-point movement/portal/collision/trigger algorithm.
5. Record the resolved action and post-step hash.

The simulation rate is exactly 60 ticks per simulated second, not a wall-clock
scheduling or real-time guarantee. A batch contains 1..600 ticks, with at most
36000 ticks per run. Each axis displacement remains
`truncate_toward_zero(component*speed_mm_per_tick*1024/1000)`. Diagonal speed,
substeps, contacts, conservative support/ceiling checks and trigger order are
unchanged from manual movement.

Only **stateless**, snapshot-only callbacks are supported. The example contains
no static/global mutable state, time source, random source, file access or hidden
memory. Continuing it after a manual step or action-replay import uses only the
current snapshot. There is no serialization/resumption promise for custom
static variables, object state, external state or RNG. Pure callbacks are a
programming contract, not something this tool can enforce for arbitrary C++.

## API and source attribution

Python:

```python
from layered_mode.bridge import LayeredProcess, sample

engine = LayeredProcess()
try:
    policy = engine.meta()['policy']
    request = {'sdk_version': policy['sdk_version'],
               'controller_id': policy['controller_id']}
    engine.reset(sample())
    engine.policy_step({**request, 'count': 600})
    state = engine.policy_step({**request, 'count': 165})
    assert state['tick'] == 765
    assert state['robot']['surface_id'] == 'upper_deck'
    assert state['robot']['x_q1024'] == 4000 * 1024
    assert state['robot']['y_q1024'] == 1500 * 1024
    assert state['robot']['z_q1048576'] == 1000 * 1048576
    replay = engine.replay()
    assert engine.import_replay(replay)['hash'] == state['hash']
finally:
    engine.close()
```

HTTP keeps manual `POST /api/layered/step` exactly `{count, action:[dx,dy]}`.
Policy is explicit `POST /api/layered/policy-step` with exactly
`{count, sdk_version:1, controller_id:"synthetic-ramp-deck-v1"}`. The ID must match
the compiled descriptor for each new policy request. `GET /api/layered/meta`
provides `policy` with `sdk_version`, `controller_id`, `execution`, `observation`,
`state_mode`, `decision_hz`, and `watchdog_seconds`. Invalid/unavailable metadata
is reported as `policy:null` with `policy_error`; policy requests fail closed,
never switch to manual input silently.

Each state/step response adds `action_source`, null for an empty/reset world or
`{kind, sdk_version, controller_id}` for the **last committed input**. Manual IDs
are null. It does not choose the next input source. Switching the UI selector
does not mutate the world, erase the journal or create cross-tab controller state.
Reset/import are paused in the UI; they do not execute policy decisions. The UI
labels its next-batch selection separately from recorded historical source.

Replay v2 contains the immutable map/config, identities, final hash, resolved
`[pre_step_tick, dx, dy, post_step_hash]` frames, and contiguous `action_sources`
segments `{start_tick,count,kind,sdk_version,controller_id}` covering every frame.
Source labels are structurally validated but **unauthenticated declarations**.
Someone can relabel a controller ID without changing the motion hashes. They
are not proof that any controller produced these actions.

The import endpoint/reconstruction calls neither `descriptor` nor `decide`; it advances only the resolved
actions, checking every frame/hash and final hash in a candidate before swapping.
This verifies action replay, not policy determinism. Loading a trusted executable
can still execute its ordinary C++ startup code; this is not hostile-code isolation.
The UI separately refreshes `/meta` after import/reconnection, and that metadata
request may call `descriptor` in a disposable process. Thus the complete UI flow
is not a promise that no controller code executes. Descriptor and decision
callbacks should both be deterministic and free of external side effects; the
disposable process cannot undo arbitrary descriptor file/network activity.
The shipped example is separately tested for repeated and differently partitioned
execution. Those tests do not certify arbitrary edited policies.

`rmuc-layered-replay.v1` requires its original source/binary and is explicitly
rejected by this build. Current v2 replays also reject source-build mismatch,
even if the map is unchanged. No old hashes are silently upgraded.

The build identity includes the named SDK, adapter, controller, native core/CLI,
Python bridge, provenance and base map validator files. It does not automatically
discover arbitrary added headers/libraries, external data, compiler flags,
compiler or standard-library versions, and is not a binary digest or signature.
If adding local controller dependencies, add their source paths to
`layered_mode/build_id.py` and their link inputs to `tools/build.sh`. Neither a
claimed build ID nor source labels provide cryptographic authenticity or anti-cheat.

## Candidate atomicity and trust limits

Each policy request creates a disposable native candidate, restores the existing
resolved action prefix, and computes the requested batch there. Identical
historical actions are grouped into `ADVANCE` calls of at most 600 ticks, with
every returned frame checked. This is still O(history) native work per request,
not a constant-time checkpoint or replay of policy decisions.

Within the native adapter, the engine is copied before the batch. A bad SDK,
count, controller ID, invalid output, standard/nonstandard exception, duration
overflow or cumulative event overflow rejects the whole batch. No partial world
or journal is committed. The Python bridge commits a candidate only after the
complete response is validated and the source is still current. A failed policy
experiment preserves the prior validated world and journal; it is not a match
loss, winner, referee decision or successful zero-action fallback.

The Python transport has a 20-second watchdog **per native command**, including
one `POLICY` batch. It is not a per-tick CPU quota, an overall prefix-restore/request
deadline, or an in-process interruption mechanism. Direct CLI/library calls do
not have this watchdog. A hung/crashed candidate is discarded while the original
process remains available. A runtime failure of the live process during a manual
operation is still an invalid run, not a fabricated result.

The controller is **trusted local C++ linked into a native process**, not sandboxed
code. It can perform I/O, read files/process memory, spawn processes, allocate
unbounded resources, corrupt memory or terminate the process. Output validation
does not contain hostile code. Killing a known candidate does not reliably kill
arbitrary descendants, undo file/network writes or roll back external/static
effects. Use only code you trust, under an OS account appropriate for it.

## Shipped fixture route and verification

The example is a small feedback controller for the authored IDs and geometry in
`synthetic_ramp_tunnel.json`, not a general planner. From the default start and
speed, its route is exactly:

- Tick 0: `(800,1400,0)` on `west_floor`.
- Tick 155: `(800,4500,0)`; aligned safely inside the ramp portal lane.
- Tick 465: `(7000,4500,1000)` on `upper_platform`, clear of the crest corner.
- Tick 615: `(7000,1500,1000)`; aligned with the deck portal.
- Tick 765: `(4000,1500,1000)` on `upper_deck`, in `deck_annotation`.
- Later ticks request `[0,0]`, remaining at the goal while the tick count advances.

Proportional final approach and a 1 mm waypoint tolerance avoid exact-equality
oscillation for non-divisor speeds or off-lattice manual positions. Default
waypoints remain exact. A speed-17 run after an off-lattice manual input is tested
to stop within 1 mm of `(4000,1500)`. Arbitrary edits to geometry, slope/radius,
spawn or other constraints can block this fixture-specific route. Unsupported
surface IDs produce zero input; this is explicit example behavior, not a planner
claim or a silent fallback in the policy API.

Tests: `tests/policy_review_tests.cpp` covers detached/immutable observations,
per-tick sensing, shared validators, typed invalid output, SDK/ID rejection,
standard and nonstandard exceptions, mid-batch rollback and aggregate event caps.
`tests/test_policy_bridge.py` covers routes, batch partitioning, manual action
equivalence, source guards, process-failure preservation, replay tampering and
action-only import. HTTP and UI handler checks are separate from visual browser QA.

Measured on the development container: default 765-tick route in 600+165 batches
about 0.082 seconds; the same route in 30-tick batches about 0.897 seconds;
restoring 35400 idle frames and applying the next 60 policy ticks about 0.123
seconds. These small-fixture observations are not latency guarantees, worst-case
bounds, real-time scheduling claims or benchmarks of untrusted controllers.
The original maximum-count geometry stress test remains separate.
