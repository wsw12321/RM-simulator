# Experimental tactical action SDK1 / observation revision2

This teaching interface combines the existing filtered combat snapshot with an
explicitly **public objective-control and score broadcast**. The map and objective
rectangles are placeholder geometry, combat is synthetic two-robot test physics,
and the public broadcasts are a deliberate experimental assumption. None is an
official RMUC sensor, map, tactics model, or referee implementation.

## Build and entrypoints

Include `tactical_mode/policy_sdk.hpp` and keep its relative dependency,
`combat_mode/policy_sdk.hpp`. Both headers use standard-library C++17 value types;
no simulator, capture-rule, or match-rule header is exposed to a controller.

Implement these entrypoints in the `tactical::policy` namespace:

```cpp
Descriptor descriptor();
Action decide(const Observation& observation);
```

`kSdkVersion` is integer `1` for actions and descriptors. `kObservationVersion` is integer `2` for the revised observation layout. `kObservationProfile` is
`experimental-tactical-public-control-v2`. The example's controller identifier
is `public-capture-combat-v1`. `Action`, `Scalar`, `Integer`, `Descriptor`, point,
rectangle, arena, self, enemy, and projectile types are aliases of the combat SDK.
`validate_action`, `aim_direction`, and the integer direction table are reused.
Actions keep the combat contract: integer dx/dy in [-1000,1000], aim coordinates
within [0,12000] x [0,7600], and integer fire in {0,1}. Booleans and floating-point
action values are not silently coerced to valid integers.

For the playable scene, edit `tactical_mode/example_policy.cpp`, stop the current
server, then run from the project root:

```sh
bash tools/build.sh
python3 server.py
```

Open `http://127.0.0.1:8765/tactical.html` and select C++ independently for each
robot. `observation.combat.self_index` lets the entrypoint dispatch to different
local policies. Keep earlier source builds if you need their replays. The source
registry in `tactical_mode/build_id.py` covers its explicitly listed files, not
executable authenticity, compiler flags/toolchains, arbitrary transitive includes,
or external files. Add newly split policy sources to both the build inputs and
registry. There is no online compilation or arbitrary binary plugin loading.

A standalone compile check from the repository root is:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -c tactical_mode/example_policy.cpp -o /tmp/tactical-example-policy.o
```

## Exact owning observation contract

Every top-level field is const and owned by the observation. The aggregate order
is `combat`, `zones`, `score`, `rate`, `qualified_ms`, `duration_ms`,
`credits_enabled`, `self_heat`. A copied observation retains its own strings, vectors and zone
values. There are no references or engine handles.

- `combat`: unchanged `combat::policy::Observation`. It includes current tick,
  own identity/pose/HP/ammo/cooldown, public arena geometry, and only enemy pose
  and projectile kinematics that pass the combat SDK's range/line-of-sight filter.
  Enemy visibility does not reveal whether the enemy is disabled. It never
  includes opponent HP, ammo, cooldown, hit counts, or projectile ownership.
- `zones`: a PUBLIC `std::array<Zone,4>`. Every zone contains only string `id`,
  rectangle `rect`, integer `original_owner` and `controller` (0 red, 1 blue),
  and string `degree` (`full` or `temporary`). The ordered zones are red outpost,
  blue outpost, red base, blue base. These public values are broadcast globally,
  including when their rectangles are outside sensor range or behind a wall.
- `score`, `rate`: PUBLIC integer arrays ordered red then blue. Score is the
  accumulated experimental control credit; rate is the current broadcast control
  rate. Check `credits_enabled` before treating score as an enabled ledger.
- `qualified_ms`, `duration_ms`: public sampled rule clock and run duration.
  The experimental full run lasts 420000 ms.
- `credits_enabled`: whether experimental control credits are enabled.
- `self_heat`: owned `SelfHeat` for this robot only. It has `enabled`, integer
  `heat_tenths`, integer `limit_tenths`, integer `cooling_per_second`, and booleans
  `overheat_locked`, `permanent_latched`, `firing_locked`. Divide heat tenths by10
  only for display; all authority math stays integer. `firing_locked` is the union
  of the two HEAT reasons, not a claim that death, ammo, native shot cooldown or
  another future lock permits firing. No enemy thermal values are supplied.

No capture progress, detector state, grace timers, occupancy, capture-completion
timestamps, opponent HP, event logs, hashes, terminal flag, simulator references,
or history is exposed. The public control change is an intentional observable
fact, not a loophole for leaking the hidden capture state. The nested combat
filter must not be broadened merely because the tactical adapter adds broadcasts.

## Example behavior

The policy is stateless and recomputes a small visibility graph from the public
wall geometry at every movement decision. Robot radius plus a conservative margin
inflates walls for routing; extra corner clearance absorbs integer movement
rounding. A currently visible enemy body is a temporary routing obstacle regardless
of its hidden health, keeping the initial paths from colliding. Red first visits
the public blue-outpost rectangle
(index 1); blue first visits the public red-base rectangle (index 2). Each waits
until its target's broadcast controller matches its own index. It does not use a
capture timer, detector status, or hidden occupancy to decide that capture worked.

Fire stays off until that robot's capture is public and either both demonstration
targets have changed control or tick 1800 has been reached. After capture, both
robots route toward the same alternating public rendezvous. A currently visible
enemy provides aim, while own ammo, cooldown and turret direction gate fire.
Bounded firing pauses are followed by renewed movement, even if the same enemy
pose remains visible; the example cannot detect a disabled enemy using hidden HP.
The 30-second fallback avoids depending indefinitely on another controller's
cooperation, but never bypasses the robot's own initial-capture requirement.

This is a demonstration of capture plus synthetic combat in one run, not an
optimized strategy or a promise of capture, victory, or damage against arbitrary
controllers. In particular, a controller can block or contest the other robot.

## Trust boundary

Policies are **trusted local native C++ in the simulator's shared process**.
Stateless, snapshot-only decisions are the supported contract. Native code can
retain globals, inspect memory, perform I/O, hang, or crash; the API cannot prevent
that. Filtering defines the intended data interface, not security, sandboxing,
process isolation, or enforcement of deterministic behavior.

A candidate-state transaction may protect committed simulator state and replay
records against invalid output or caught exceptions. It cannot undo callback
side effects, file writes, or native global state, and does not contain crashes
or hangs. Reproducibility requires an unchanged controller, inputs, configuration,
and implementation; public broadcasts do not establish official fairness.

## Attack-ledger integration does not broaden observations

The earlier tactical engine v2 added private source-linked damage accounting
and final comparison tiers without broadening observations. Engine v3 now adds
own thermal state under observation revision2; action `kSdkVersion` stays1. No nominal damage, hit ledger, attack total, opponent remaining
HP, settlement status or final result is added to `Observation`. Damage config
and terminal reporting belong to the host UI, not the robot sensor snapshot.
Native administrative output contains private accounting and must not be fed back
into a controller. See README.md for explicit experimental producer assumptions.

## Heat observation and action replay

Observation revision2 adds only the robot's own thermal snapshot; action SDK1
remains the common manual/controller contract. There is no binary ABI promise.
Rebuild after source changes, and keep the matching earlier source for old replays.
A policy may submit fire while heat-locked, but the native authority gates actual
emission. The requested input remains in replay; no-shot attempts consume no ammo
and add no heat. Cooling can release the thermal reason at a future boundary.
The synthetic10-tick weapon interval is separate from the source-backed10Hz
thermal cooling formula. No HP penalty, opponent heat, or first-person video
behavior is added to this SDK. See README.md for explicit clock/profile/order
assumptions and strict Q2-equality handling.
