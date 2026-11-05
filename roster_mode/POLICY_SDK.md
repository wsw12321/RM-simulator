# Trusted C++ roster policy SDK v1

`policy_sdk.hpp` is a standalone standard-library C++17 header. It exposes a
synthetic tactical observation, not an official robot sensor or referee API.
Action SDK1 and roster observation revision1 are separate version markers. The
older combat/tactical SDKs keep their own contracts and are not interchangeable.

## Compile and use

Edit `roster_mode/example_policy.cpp`. Stop the running server, run
`bash tools/build.sh`, then restart `python3 server.py`. Select C++ control for
any desired robot in `/roster.html`. Each selection is independent. The browser
uses one compiled `descriptor()` / `decide()` entrypoint for all policy-controlled
robots, called separately with each robot's snapshot. Branch on `self_id`, team,
public roster or profile to implement different roles. This is not runtime plugin
loading, and the browser does not compile or execute submitted source text.

```cpp
#include "policy_sdk.hpp"

namespace roster::policy {
Descriptor descriptor() { return {kSdkVersion, "my-roster-controller-v1"}; }

Action decide(const Observation& o) {
    Action a;
    a.aim_x = o.self.x;
    a.aim_y = o.self.y;
    if (o.self.hp <= 0) return a;
    for (const auto& contact : o.contacts) {
        if (contact.team == o.self_team) continue;
        a.aim_x = contact.x;
        a.aim_y = contact.y;
        if (o.self.ammo > 0 && o.self.cooldown == 0 &&
            !o.self_heat.firing_locked)
            a.fire = Integer{1};
        break;
    }
    return a;
}
}
```

This minimal example stays stationary and aims at the first visible opponent in
stable ID order. It is not a strong policy, a targeting guarantee or an official
firing model. The supplied `example_policy.cpp` also illustrates movement and
objective selection.

The native engine API supports a collection of per-ID `Controller` records with
separate callbacks/descriptors. Bind by stable ID, not incoming vector position.
All configured IDs must supply an action source exactly once. A policy-controlled
robot requires a valid corresponding controller; callback errors invalidate the
candidate step rather than award a match loss.

## Observation contract

The entire observation is an owning immutable value, with no engine pointers or
handles. It contains:

- `self_id`, `self_team`, `tick`, `hz`, sensor range and version markers.
- `self`: own pose, HP, ammunition and native cooldown.
- `self_heat`: own thermal values and thermal-only firing lock reasons.
- `arena` and `roster`: deliberately public static experiment configuration,
  including all initial spawn poses, synthetic profiles and eligibility.
- `contacts`: other robot ID/team/pose only within own center-range and wall LOS.
  This also applies to teammates. Visibility does not publish current HP, heat,
  ammunition, cooldown or an alive/dead flag.
- `projectiles`: visible positions/velocities only, without owner, damage, global
  ID or lifetime. Values are sorted by visible fields.
- `zones`, `score`, `rate`, match clock and credit-enabled flag: deliberately public
  objective broadcasts, not a claim about official communications.

Every robot's snapshot is frozen before the first callback at that tick. Callback
order cannot make a later policy observe already-applied movement or damage.
Distinct IDs use distinct own snapshots even when roster/request order changes.
No hidden positions, dynamic HP/heat of other robots, damage accounting, capture
progress, qualified detector state, grace deadlines or global hashes are exposed.
Public starting configuration must not be mistaken for current hidden telemetry.

## Actions and failures

`Action` values use `Scalar = variant<int64_t, double, bool>` so type mistakes are
rejected instead of silently converted. Set integral fields with `Integer{...}`:

- `sdk_version`: integer1.
- `dx`, `dy`: integers in [-1000,1000]; engine normalizes movement intent using the
  configured synthetic speed.
- `aim_x`, `aim_y`: integers within [0,12000] × [0,7600].
- `fire`: integer0 or1, not a boolean.

Manual and policy inputs use the same action validator. Actual emission still
requires a living robot, available ammunition, native cooldown and no thermal
lock. Requested fire is retained in replay even when the gate blocks emission.
Changing a profile changes the experiment and replay configuration; it does not
change the shared action shape.

A batch validates and commits atomically. A late invalid/throwing callback, rule
ambiguity, divergent replay or invalid native response preserves the accepted
world, scores, heat and recorded inputs. The core cannot roll back arbitrary I/O
or other external side effects of controller code.

## Trust and reproducibility

Only stateless, snapshot-dependent local C++ is supported. Do not use wall time,
random devices, mutable global state, unordered external iteration, file/network
input or hidden-engine access if you want repeatable policy decisions. Purity and
security isolation are not enforced; a compiled controller has the native process's
capabilities. A timeout is a failed candidate, not a sandboxed policy runtime.

Replay imports execute recorded per-ID requested actions, not policy callbacks or
descriptor functions. Source labels and diagnostic hashes help compare runs but
do not authenticate intent or make the log tamper-proof. The log reveals all robot
inputs and is explicitly a developer artifact. Use the exact source build and
configuration that created it.
