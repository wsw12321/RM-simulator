# Experimental deterministic layered movement v1

The versioned trusted-local C++ controller interface is documented in
[`POLICY_SDK.md`](POLICY_SDK.md). It adds programmable input, not official robot
sensing, competition fairness, combat, scoring, or a native-code sandbox.

A new, isolated C++ mover consumes validated `rmuc-layered-arena.v1` documents.
It does not replace the sandbox, objective engine, rule adapters, scenario
harnesses, or the read-only arena calibration query. The shipped ramp/tunnel map
is entirely synthetic. There is no official geometry, robot dynamics, combat,
scoring, match winner, or referee claim here.

## Build and run

From the repository root:

```sh
bash tools/build.sh
python3 server.py
# Open the server's /layered.html page.
```

The standalone binary build is:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  -DLAYERED_BUILD_ID=\"$(python3 layered_mode/build_id.py)\" \
  layered_mode/core.cpp layered_mode/policy_adapter.cpp \
  layered_mode/example_policy.cpp layered_mode/cli.cpp -o build/rmuc_layered
```

GCC/Clang with signed 128-bit integer support is required. No JSON-parser library,
package installation, network service, CAD importer, or WebAssembly compiler is
needed. Python validates data and replays; only C++ advances movement. A new map
that fits the constrained profile loads without source edits or recompilation.

## Authored data and exact native profile

The existing format validator runs first, preserving the complete authored map,
source records, calibration declarations and convex polygons. No convex hull,
bounding-box replacement, silent rounding, source fetch, coordinate transform or
official-map promotion occurs. The native mover additionally requires:

- XY, Z, ceiling and volume coordinates are exactly integral millimeters in
  `[-100000, 100000]`. Integral JSON floats such as `1000.0` are permitted.
- Affine slope components are exact multiples of `1/1024`, with
  `abs(dx) + abs(dy) <= 1`. Thus `0.25` is supported and `0.1` is rejected.
  Every surface vertex's height also stays within the coordinate bound.
- At most 32 surfaces, 64 collision volumes, 32 trigger volumes, 64 portals,
  and 16 vertices per polygon. The base format's 256 KiB map limit still applies.
- Every portal has `max_step_mm == 0`, exactly matching support heights along
  its shared edge and opposite-side surface interiors. Nonzero steps, gaps,
  same-side overlapping connections and approximate-height matches are rejected.
- Surface/layer/volume/portal identifiers remain authored identifiers. Physical
  volumes are global: layer labels never exempt a robot from a collision.

The HTTP layered endpoints reject JSON floating-point tokens that cannot be
represented exactly by Python's binary float before validation. This prevents
underflow or a token such as `0.125000000000000001` silently becoming supported
`0.125`. A direct Python caller supplies already-materialized values, so any
precision lost before calling the adapter cannot be recovered. Validated slopes
and integers are then serialized exactly into a bounded native token protocol.
The C++ loader independently checks bounds, convexity, adjacency, continuity,
references and spawn feasibility before replacing the live engine.

## Configuration

The entire map and configuration are immutable during a run. Reset requires
exactly `{map, config}`. The default configuration is:

```json
{
  "radius_mm": 120,
  "height_mm": 400,
  "speed_mm_per_tick": 20,
  "max_slope_q1024": 512,
  "max_ticks": 36000,
  "start": {"x_mm": 800, "y_mm": 1400, "surface_id": "west_floor"}
}
```

All configuration numbers are integers, not booleans or floating-point values.
Radius is 1..1000 mm, height 1..3000 mm, speed 1..50 mm per tick per axis, slope
limit 0..1024, duration 1..36000 ticks. Start XY is integral mm within the native
coordinate bounds and must name one feasible support surface explicitly.

The slope limit bounds the Euclidean gradient magnitude: a value of 512 means
`hypot(dx,dy) <= 0.5`, not 512 degrees. There is no official mobility calibration.

## Deterministic movement and contact contract

One fixed tick is 1/60 second. Input is a resolved direction `[dx,dy]`, both
integers in -1000..1000. Axis displacement is
`truncate_toward_zero(direction * speed_mm_per_tick * 1024 / 1000)` in units of
1/1024 mm. There is no acceleration, velocity carry, gravity, orientation,
friction, jump, step climbing or collision sliding. Diagonal inputs apply both
axis budgets and can be sqrt(2) times faster than an axis-only input. Very small
inputs may quantize to zero. This is an engineering control model, not physics.

Each tick follows one straight XY segment, divided deterministically into at
most 50 substeps of at most 1 mm **per axis** (sqrt(2) mm Euclidean). A blocked
substep leaves its starting position unchanged, retains earlier accepted
substeps in that tick, discards the remaining requested displacement and records
contacts. Ceiling contacts name every violated participating declaration in authored
surface order; they do not relabel a neighbor ceiling as the current surface.
The tick still advances. Invalid requests do not advance anything.

For every accepted substep:

1. The upright circular footprint must remain within the arena boundary and its
   support. For a single convex polygon, checking both endpoint circles against
   exact half-planes also contains the whole swept capsule.
2. A nearby explicit portal can support a footprint spanning **two** adjacent
   surfaces. Both surfaces' non-seam half-planes and the portal endpoint corridor
   must contain the footprint throughout the substep. This is conservative and
   may reject a physically possible corner or a footprint spanning three pieces.
3. The selected support changes only when the center leaves its current polygon
   through an authorized portal into its declared neighbor. Staying exactly on
   the seam keeps the current support until departure. Bidirectional flags apply
   to center transfer; footprint support across the seam is undirected so the
   circle is supported immediately after a one-way crossing. Reverse center
   travel remains blocked. Overlapping XY alone never selects another layer.
4. Support height is the selected plane at the center. The upright envelope uses
   a conservative full-circle support range for each participating plane:
   `z_center +/- radius * (abs(dx) + abs(dy))`. Across a seam it takes the range
   of both planes at both endpoint centers. This overbounds the true circular
   plane extrema and may extrapolate a plane beyond its support piece. It is
   not a wheel-contact or tilted-body model. Height is added to the maximum.
5. The envelope top must be strictly below every participating declared ceiling.
   A footprint fully contained in its current surface, including a tangent to a
   seam, uses only that surface's ceiling. A neighboring ceiling becomes active
   when the footprint extends across the seam. A physical ceiling requiring
   closed edge contact must also be encoded as a global solid.
6. Collision checks use an exact XY capsule against every global convex prism,
   plus the conservative swept vertical range. No thin positive-width prism can
   be skipped between substeps. Touching a solid side or underside blocks.
   Resting on a solid top is allowed only when the entire lower envelope is at
   or above its top. Broad vertical ranges can conservatively block some
   otherwise passable sloped corners; they never waive a declared solid.

The sample deliberately leaves the ramp connector underside unmodeled rather
than inventing a slab that prevents this conservative crest crossing. A smaller
interior slab under the upper deck demonstrates lower clearance and top-rest
behavior. These omissions and invented dimensions are explicit in calibration
notes; the sample is not a physical structural design.

All geometric decisions use bounded integers and signed 128-bit intermediates.
At the maximum coordinate span, XY cross products are below about 8.4e16 fixed
units; their squares are below 7.1e33, well below signed 128-bit capacity
(about 1.7e38). Height and slope products fit signed 64-bit. Comparisons require
no square roots or floating-point arithmetic. Display-only `*_mm` JSON numbers
are derived from exact fixed-point integers; state hashes use the integers.

## Triggers and event order

Triggers are informational overlaps, never solids or rule execution. Only a
trigger listing the selected surface can be active, so a tunnel trigger does
not fire merely because the upper deck shares its XY. Swept checks use the
starting/destination support scopes when a portal is crossed, exact XY capsules
and conservative vertical ranges. This can conservatively report a short seam
annotation; it does not certify an official detector activation.

A trigger crossed entirely during one tick still emits enter/exit evidence.
Within each accepted substep, event order is: transition, exits in authored
trigger order, enters in authored order, then transient enter/exit pairs in
that order. Events contain the resulting tick and zero-based substep. Reset
can emit initial enter events at tick 0. Blocked motion emits no fictional
crossing events.

`state.events` describes only the latest tick; `state.contacts` is latest-tick
blocked evidence, not a collision history. A successful step response also
includes `batch_events` covering the complete request, with original timestamps.
This separate presentation field is not substituted into the hashed final state.
At most 8192 events may be returned by one batch; exceeding that bound rejects
that whole request atomically, and the caller can reduce `count`.

## APIs and journal

Python: `from layered_mode.bridge import LayeredProcess, sample`.
`LayeredProcess(binary=None)` starts the built-in sample. Methods are `meta()`,
`state()`, `replay()`, `reset(data)`, `step(data)`, `policy_step(data)`,
`import_replay(data)`, `close()`.
The loopback server exposes:

- GET `/api/layered/sample`: `{map, config}` for the new synthetic fixture.
- GET `/api/layered/meta`: immutable map/config, identities, 60 Hz, bounds and caveats.
- GET `/api/layered/state`: current exact/display coordinates, selected support,
  layer, conservative support/envelope range, declared ceiling, clearance,
  contacts, active triggers, latest-tick events and state hash.
- POST `/api/layered/reset`: exactly `{map, config}`.
- POST `/api/layered/step`: exactly `{count, action: [dx,dy]}`; count 1..600.
- POST `/api/layered/policy-step`: exactly `{count, sdk_version: 1,
  controller_id: "synthetic-ramp-deck-v1"}` for the shipped controller.
- GET `/api/layered/replay`; POST `/api/layered/import` with that replay object.

The replay schema is `rmuc-layered-replay.v2`, profile `layered-native-v1`, and
`official: false`. It embeds the full immutable map/config, SHA-256 identities
for their canonical JSON and source build, frames `[pre_step_tick, dx, dy,
post_step_hash]`, source segments, and final hash. Source identity includes native implementation,
SDK, adapter, example controller, bridge/serializer, provenance and base validator. Map identity includes source
and calibration declarations. Those declarations are preserved, not authenticated.
The state hash is versioned FNV-1a 64-bit deterministic evidence, not a
cryptographic authenticity or anti-cheat claim.

Source segments describe recorded manual/policy inputs with SDK/controller IDs;
they are unauthenticated declarations, not proof of controller execution or
determinism. Import invokes neither the decision nor descriptor callback. It
validates the schema, bounds and identities, runs a fresh candidate native
process, checks every sequential resolved frame's state hash and the final hash, and swaps
it into the session only after success. Running and candidate binary builds must
both match. Empty replays receive the same checks. Invalid map/config/replay or
a stale binary leaves the previous run intact. Runtime process failure/timeout
invalidates the attempted experiment rather than fabricating a loss or terminal judgement.
Policy batches run in disposable candidates, so their failures preserve the
previous world and journal. Old v1 replays require their original source/binary;
this build rejects them rather than relabeling or silently migrating their hashes.

Replays are limited to 36000 frames and 12 MiB. Reaching configured `max_ticks`
sets `status: "limit_reached"`; further advancement is rejected without changing
state or journal. It does not declare a winner. A batch that would exceed the
remaining duration is rejected whole.

The native CLI has no JSON parser. `LOAD` is a single bounded line generated by
the validated adapter, followed by `STATE`, `BUILD`, `ADVANCE count dx dy`, or
`FRAME expected_pre_step_tick dx dy`, `POLICY_META`, or
`POLICY count sdk_version controller_id`. LOAD validates a candidate before commit.
ADVANCE/FRAME return native `{state, frames, batch_events}`. The bridge owns full
map/config provenance serialization and candidate replay validation.

## Reproducible code-control routes

This manual route uses resolved actions directly and never invokes the compiled
controller callback. The separate policy path executes trusted locally edited C++:

```python
from layered_mode.bridge import LayeredProcess, sample
engine = LayeredProcess()
try:
    # Lower route: x800 -> x6800 at y1400, z0, west -> tunnel -> east.
    lower = engine.step({'count': 300, 'action': [1000, 0]})
    assert lower['robot']['surface_id'] == 'east_floor'
    assert lower['robot']['z_q1048576'] == 0
    assert any(e['id'] == 'tunnel_annotation' for e in lower['batch_events'])

    engine.reset(sample())
    # Move to ramp lane; ascend west -> ramp -> upper platform.
    engine.step({'count': 155, 'action': [0, 1000]})   # y4500
    engine.step({'count': 310, 'action': [1000, 0]})  # x7000, z1000
    engine.step({'count': 150, 'action': [0, -1000]}) # y1500
    top = engine.step({'count': 150, 'action': [-1000, 0]}) # x4000
    assert top['robot']['surface_id'] == 'upper_deck'
    assert top['robot']['z_q1048576'] == 1000 * 1048576
    assert 'deck_annotation' in top['active_trigger_ids']
    saved = engine.replay()
    assert engine.import_replay(saved)['hash'] == top['hash']
finally:
    engine.close()
```

The manual route is a scripted test of this invented fixture. The shipped
feedback controller reaches the same default endpoint after 765 ticks, using
one immutable pre-step snapshot per tick. See `POLICY_SDK.md` for its exact route,
1 mm waypoint tolerance, fixture-specific limitations and runnable example.
Neither is general path planning or an official battlefield policy.

## Verification

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  layered_mode/core.cpp tests/layered_core_tests.cpp -o build/layered_core_tests
build/layered_core_tests
python3 tests/test_layered_bridge.py
python3 tests/test_layered_review.py
python3 tests/test_layered_profile_limits.py
python3 tests/test_policy_bridge.py
# With server.py already listening on port 8765:
python3 tests/test_layered_http.py 8765
# Or run the repository aggregate (starts its own test server):
bash tools/check.sh
```

Coverage includes native invalid-load/action atomicity, fixed tick/direction
quantization, boundary tangency, exact convex geometry, thin solids/triggers,
zero-length sweeps, bidirectional/one-way/absent portals, ramp access, global
height-aware solids/top-rest, ceiling and slope rejection, extreme coordinates,
deterministic repeated actions, replay tampering, reset/import rollback and
sample routes. Browser handler/HTTP tests are separate from real rendered-browser
verification; no native test proves browser layout or downloads.

The supported maximum-count stress fixture (32 surfaces, 64 solids, 32 triggers,
64 portals and 16-vertex volumes) completes a 600-tick, 50-mm-per-axis batch in
about 3.6 seconds on the development container. This is measured test evidence,
not a cross-machine real-time guarantee; the adapter has a 20-second command
timeout. The stress test asserts completion and resulting state, not wall time.
