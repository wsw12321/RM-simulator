# Filtered combat policy SDK v1

This isolated experimental mode wraps the unchanged `src/sim.cpp` two-robot
sandbox. It is synthetic test physics, not an official RMUC map, sensing model,
referee rule implementation, fairness guarantee, or full 3D simulator. Existing
sandbox, objective, and layered modes remain separate.

## Trust and reproducibility boundary

Controllers are **trusted local native C++ code**, linked into the native process.
They are not contained or sandboxed. They can read process memory, access files,
retain static data, perform I/O, or fail to return. The API supports only stateless
controllers whose output is determined solely by the supplied snapshot. Filtering
limits the contents of that snapshot; it cannot stop deliberately untrusted native
code from reading other information. There is no deterministic-policy or security
enforcement claim.

Each batch operates on a candidate simulation. Invalid output or a caught exception
leaves the committed simulator and replay journal unchanged. This does **not** undo
external callback side effects, native static/global memory changes, or filesystem
writes. A process crash/hang cannot be recovered inside this native API; the Python
bridge applies its own worker timeout and candidate-commit protection.

## Build and edit

`policy_sdk.hpp` is a standalone C++17 header using only the standard library. It
contains no simulator headers, state pointers, engine handles, mutable references,
logs, or global-state hashes. Copying it is sufficient to compile a controller's
source. The version is `combat::policy::kSdkVersion == 1`.

Edit `example_policy.cpp` and implement:

```cpp
combat::policy::Descriptor combat::policy::descriptor();
combat::policy::Action combat::policy::decide(const Observation& observation);
```

The compiled example is `filtered-cover-v1`. The runner dispatches it independently
for robot 0 and robot 1 according to each input's `manual` or `policy` source. The
C++ wrapper also accepts a separate `Controller` binding for each robot, so tests
and local integrations can supply distinct decision functions. No binary plugin
loader or persistent private policy-state format is provided.

For the complete application, run `bash tools/build.sh`, then
`python3 server.py`, and open the local server's `/combat.html` page.

Or build only this executable from the repository root after the Python bridge is
present:

```sh
mkdir -p build
COMBAT_BUILD_ID="$(python3 combat_mode/build_id.py)"
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  "-DCOMBAT_BUILD_ID=\"$COMBAT_BUILD_ID\"" \
  src/sim.cpp combat_mode/core.cpp combat_mode/example_policy.cpp \
  combat_mode/cli.cpp -o build/rmuc_combat
```

The pinned build identity includes original simulator source/header/direction table,
the wrapper, SDK, compiled controller source, CLI, bridge, and identity script.
It is an identity of the explicitly registered source inputs, not a signed binary
attestation. New transitive includes must be added to `SOURCES` manually. The
registry does not automatically fingerprint arbitrary new dependencies, compiler
flags, toolchain versions, or a modified binary; maintain those separately when
comparing experiments.

## Exact observation contract

Every `Observation` is an owning value with const top-level fields. Both robots'
observations are built from the same pre-step state before **either** controller
is invoked. One decision is collected per selected policy per tick. Both decisions
are collected before either returned action is validated or applied. Invalid
manual requests, invalid source metadata, SDK versions, counts, or missing bindings
are rejected before invoking a controller.

The profile `experimental-range-los-v1` supplies only:

- SDK version, 60 Hz tick rate, current tick, self index, and range 6000 mm.
- Own center x/y in mm, turret yaw in 256 integer headings, HP, ammo and cooldown.
- Static public arena dimensions, robot radius, per-tick robot/projectile speed,
  and three fixed wall rectangles.
- Optional enemy center x/y and turret yaw. It is present exactly when squared
  center distance is at most 6000² and original `sandbox::line_blocked` is false.
  Occlusion uses the original closed conservative Q16 segment/AABB test, including
  wall-boundary tangency. No robot-radius expansion is used for sensor visibility.
- Projectiles with centers passing that same range/LOS test, each containing only
  x/y/vx/vy. Filtering occurs first, then lexicographic sort by those four visible
  fields. Duplicate visible records remain duplicate. Global creation order, IDs,
  owner, and remaining lifetime are not exposed.

Enemy presence does not depend on HP: a disabled robot's body pose remains visible
if its center passes range/LOS. No alive flag, death event or hidden HP is encoded.
The snapshot omits own shots and hits too: a hit counter could reveal an unseen
opponent. It contains no other robot statistics, hidden coordinates, terminal flag,
global counters, state hash, events, previous observations, replay, or provenance.
The terminal flag in the separate host envelope is control-plane match status,
not a policy sensor. Policies are not called after the match has ended.

`make_observation(const sandbox::Engine&, int)` and its deterministic JSON encoder
are testable directly. Hidden-state metamorphic tests mutate opponent statistics,
unseen poses/projectiles, projectile metadata/order, global counters, terminal,
journals and events while preserving the permitted fields. The resulting snapshot
bytes and pure example decisions must remain unchanged.

The SDK's public direction table and `aim_direction` helper match the original
integer heading selection. Its precondition is bounded arena coordinates:
x in `[0,12000]`, y in `[0,7600]`. It is not a general-purpose extreme-int64 geometry
routine. Coordinates supplied by this sensor and accepted aim actions meet that
precondition.

## Shared action validation

An action has SDK version 1 and five integer components:

| Component | Range |
|---|---|
| dx, dy | -1000 through 1000 |
| aim_x | 0 through 12000 mm |
| aim_y | 0 through 7600 mm |
| fire | integer 0 or 1 |

The tagged `Scalar = variant<int64_t,double,bool>` preserves type errors until
`validate_action` rejects them. Boolean, floating-point, NaN/infinity, out-of-range
and wrong-SDK actions fail without coercion. Manual actions and controller results
use this **same function**, then convert to original simulator integer inputs.

The underlying engine still handles movement normalization, sliding, simultaneous
robot movement, turret slew, cooldown, ammunition, swept projectiles and damage.
Nothing in this wrapper changes those equations. A successful step batch may be
shorter than requested if its first disable or duration limit ends combat. A new
step after terminal is rejected; reset starts a new run.

## Example controller

Without contact, the example uses tick/self-index patrol goals in the known
synthetic arena. It replans a deterministic visibility graph around robot-radius
expanded public wall rectangles rather than querying hidden opponent position.
With a visible target, it uses that pose for aim and range decisions, own ammo and
cooldown for firing, and own low HP for retreat. It never fires at an unseen target.
It uses no opponent HP/ammo/cooldown, history, simulator calls, or external state.

This is a useful starting controller, not a claim of tactical strength. Tests show
that the two example controllers find contact around cover, both land hits, and
reach a disable; they also defeat a stationary manual robot from either side.

## Native protocol and audit replay

One whitespace-delimited command is read per line, one JSON response is emitted.
Integer tokens are parsed strictly without float/bool coercion. Commands are bounded
to 100000 bytes; mutation batches contain 1 through 120 ticks/frames.

- `BUILD`: source identity.
- `POLICY_META`: compiled descriptor only. This is the only metadata inspection
  command that invokes `descriptor`; live policy steps also invoke it for binding.
- `STATE observer`: filtered snapshot for observer 0 or 1, empty new-frame list.
- `RESET observer`: reset then state; no policy callbacks.
- `STEP count observer source0 sdk0 dx0 dy0 aimx0 aimy0 fire0 source1 sdk1 dx1 dy1 aimx1 aimy1 fire1`:
  `source` is `manual` or `policy`. Policy action placeholders are ignored except
  the requested SDK version; manual components are validated and repeated over
  the batch. Policy actions are recomputed once per pre-step snapshot.
- `FRAME tick observer source0 id0 sdk0 dx0 dy0 aimx0 aimy0 fire0 source1 id1 sdk1 dx1 dy1 aimx1 aimy1 fire1 expected_hash observation_hash0 observation_hash1`:
  validate and apply one resolved historical frame, without any controller calls.
- `FRAMES count observer <frame payloads>`: atomic multi-frame reconstruction. Each
  payload is the `FRAME` payload **without observer**. Manual controller ID is `-`;
  historical policy IDs are bounded ASCII identifiers, not a request to load code.
- `EXPORT`: the full developer audit artifact, not a policy observation.

`STATE`, `RESET`, `FRAME`, `FRAMES`, and `EXPORT` never call `descriptor` or `decide`.
Replay does not execute controllers even for frames tagged as policy decisions.

The host state envelope carries build/profile, observer, tick, terminal, native
state hash, filtered observation, last committed source tags, and only the new
batch's frames. The public bridge strips the global hash and frame details.

Replay schema is `rmuc-filtered-combat-replay.v1`, engine
`integer-mm-combat-v1`, profile `filtered-combat-v1`. Each frame records:

- Pre-step tick.
- Two resolved input records in robot-index order, with kind, SDK version,
  nullable controller ID, and the five action integers.
- Two pre-step observation hashes in robot-index order.
- Post-step native state hash.

Observation hashes are 32-bit FNV-1a over the UTF-8 bytes of the native fixed-order
`observation_json` encoding. Native state hashes retain the original simulator
algorithm. They are deterministic, non-cryptographic diagnostic checks, not proof
of authenticity, freedom from collisions, or truthful source attribution. Source
tags are unauthenticated historical labels. Build pinning and full replay checks
reject accidental incompatibility/tampering but are not an adversarial integrity
system. The audit intentionally includes both robots' resolved inputs and global
hashes; never pass audit fields back into `Observation` or call it a filtered
player view.

## Verification

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  src/sim.cpp combat_mode/core.cpp combat_mode/example_policy.cpp \
  tests/combat_core_tests.cpp -o build/combat_core_tests
./build/combat_core_tests
```

Core tests cover observation ownership, hidden-field invariance, range/cover edges,
shared validator equivalence, simultaneous pre-tick collection, full batch rollback,
manual/policy dispatch, tactical outcomes and resolved-input replay. Independent
review tests and Python bridge/HTTP tests cover additional boundaries. Full original
mode regressions remain necessary after any implementation change.
