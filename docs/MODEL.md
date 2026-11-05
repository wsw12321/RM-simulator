# Sandbox-v1 model boundary

Nothing in this active profile is asserted to be an official RMUC rule.

## Engineering parameters

| Parameter | Sandbox value | Meaning |
|---|---:|---|
| Step rate | 60 Hz | One authoritative transaction per integer tick |
| Arena | 12,000 × 7,600 mm | Arbitrary flat demonstration rectangle |
| Robot radius | 280 mm | Circular movement collider |
| Movement cap | 55 mm/tick | About 3.3 m/s, integer normalized input |
| Gimbal headings | 256 per turn | Checked-in integer lookup, scale 1000 |
| Gimbal slew | 4 headings/tick | Independent aim; no chassis rotation model |
| Health | 100 | Aggregate body health; no armor panels |
| Ammunition | 120 per robot | No refill or official allowance accounting |
| Damage | 10 per hit | No shields, defenses or vulnerability |
| Projectile motion | 260 mm/tick | About 15.6 m/s before vector quantization |
| Cooldown | 10 ticks | At most six shots per second |
| Projectile lifetime | 180 ticks | Three seconds |
| Duration | 7,200 ticks | Two-minute test cap |

Directions truncate integer products toward zero. Positions are integer millimetres. Circle projectile target checks use radius 310 mm (280 + 30); obstacle intersection uses the projectile center line. This asymmetric approximation must not be mistaken for calibrated ballistics. Swept intersection samples Q16 time with integer-coordinate rounding. Tangency can differ by about one millimetre.

Walls: `[5250,2800,6750,4800]`, `[2600,1350,3800,1950]`, `[8200,5650,9400,6250]`, in millimetres. These are not official obstacles.

## Tick phases

1. Validate the complete action pair; invalid input does not mutate engine state.
2. Both policies observe the same prior boundary state.
3. Move robots with axis sliding; constrain arena and static walls. Resolve proposed robot overlap by cancelling both translations. No acceleration/inertia or mechanical power model.
4. Apply bounded gimbal slew and firing/cooldown/ammo. Both robots alive at the start of the damage phase may fire.
5. Sweep all projectile segments against static cover and robots at their post-motion positions. Cover wins exact ties. Sum damage, then commit simultaneous damage.
6. End the sandbox when either robot is disabled or 7,200 ticks elapse. This is not official victory logic.
7. Commit canonical state and record accepted actions plus hash.

The ordering is an explicit simulation convention. It is not sourced as referee event priority. Dead robots remain physical colliders. Motion is bounded so the supplied static obstacles cannot be crossed in one step. There is no general convex/3D physics solver.

## Implemented vs absent

Implemented: two experimental robots; manual/full-code/hybrid control; movement, walls and robot contact; aim/fire; swept 2D projectile/cover checks; aggregate damage and terminal pause; per-tick authoritative action journal; cold replay validation; bounded local API.

Absent: official roster and map, ramps/tunnels/elevation, full referee state machines, adaptive armor, heat/power penalties, experience, economy, supply and revival, heavy assembly, outposts/base/control, aerial/dart/radar, restricted sensing, communications, event provenance for official rules, untrusted strategy isolation, deterministic fuel, snapshots/branching, multiplayer, in-browser C++ compiler, pure-browser runtime, official compliance or physical validation.

## Security and privacy scope

Only trusted locally compiled policies. No uploads to third parties; the browser talks to its own machine. A file selected for replay is posted only to the local bridge. The bridge accepts only loopback Host, same-origin when supplied, and its custom request header. No CORS grants, remote bind option, analytics or credential collection. This is not authentication against other local processes or a production security boundary. Multiple browser tabs share one engine; use one tab per server instance.


## Separate objective teaching profile

The original sandbox assumptions above describe `sandbox-v1` only. The separate
`objective-experimental-v1` browser mode now connects movement to unchanged
qualified-capture and match formulas using explicitly experimental geometry,
sampling, latched control and optional end-of-second credits. It disables combat
and does not implement full RMUC. See [the objective contract](../objective_mode/README.md).

## Separate integrated rules lab profile

`rmuc2027-rules-lab-v1` (`match_lab/`) is a further separate, headless profile.
Its robot attributes, levels, experience, revival, economy, damage multipliers,
heat, chassis power, position lifecycle and match judgment come from the
source-linked `rules/` modules; its field geometry, robot sizes and speeds,
speed-from-power model, muzzle speeds, fire intervals, dispersion, sensing,
assembly durations and scripted AI are synthetic. It shares no state or replay
format with `sandbox-v1`. See [the lab README](../match_lab/README.md).
