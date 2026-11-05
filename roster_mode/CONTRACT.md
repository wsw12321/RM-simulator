# Experimental roster native contract v1

All modes outside `roster_mode` are unchanged. This is a SYNTHETIC 1–4 robots per side laboratory, default 2v2, not an official roster or official multi-robot qualification implementation.

## Identity and configuration

Constants: profile `experimental-roster-tactical-v1`; engine `integer-mm-roster-v1`; observation `experimental-roster-range-los-public-v1`; observation_version 1; replay schema `rmuc-experimental-roster-replay.v1`; SDK1; 60Hz; 25200 ticks; maximum batch600.

Config JSON has exactly six keys: `boundary_policy`, `credit_strategy`, `attack_damage_strategy`, `heat_strategy`, `heat_threshold_policy`, `robots`.

First three selectable strings are unchanged from tactical mode. Heat choices: `disabled`, `SYNTHETIC-per-robot-17mm-v1`. Threshold choices unchanged. Default engineering timer-before-input, experimental end-of-second credit, experimental reconciled projectile HP loss, synthetic heat on, strict threshold equality.

`robots` has 2–8 records with exactly `id`, `team`, `profile`, `spawn`, `eligible`. `id` is [A-Za-z0-9][A-Za-z0-9_.-]{0,31}, case sensitive, unique. `team` integer0/1, each has 1–4. Native canonicalizes robots by ASCII lexicographic ID; request and replay input arrays are likewise canonicalized before use. IDs bind actions; ordering never binds identity.

`profile` keys and inclusive integer bounds: `id` (same ID syntax, prefix `SYNTHETIC-`), `max_hp`1..10000, `ammo`0..5000, `radius_mm`100..500, `speed_mm_tick`0..100, `projectile_speed_mm_tick`80..1000, `projectile_damage`1..1000, `cooldown_ticks`1..120, `sensor_range_mm`0..20000, `heat_limit_tenths`0..20000, `cooling_per_second`0..1000. A reused profile ID must have identical values. All numbers remain engineering assumptions; no official robot/class claim.

`spawn` exactly x,y,yaw integers; x,y center inside arena with profile-radius clearance, yaw0..255; no overlapping circles or walls. `eligible` boolean. Fixed rectangle arena12000x7600 and three walls unchanged from sandbox.

Default robots: blue-1 at10400,2900 yaw128 standard; blue-2 at10400,4700 yaw128 scout; red-1 at1600,2900 yaw0 standard; red-2 at1600,4700 yaw0 scout. Standard profile: SYNTHETIC-standard,100,120,280,55,260,10,10,6000,400,12. Scout: SYNTHETIC-scout,80,90,240,65,300,8,9,5500,400,12. Values in profile field order above. All eligible true.

## Native line protocol

Integer tokens are strict signed decimal, not booleans/floats/exponents/plus. Strings use ID syntax; whitespace delimiting. BUILD, META, POLICY_META, EXPORT take no arguments. STATE observer_id. RESET observer_id boundary_index credit_index damage_index heat_index threshold_index N then N robot records:

`id team profile_id max_hp ammo radius_mm speed_mm_tick projectile_speed_mm_tick projectile_damage cooldown_ticks sensor_range_mm heat_limit_tenths cooling_per_second x y yaw eligible_0_or_1`

STEP count observer_id N then N records `id kind sdk dx dy aim_x aim_y fire`. kind is manual/policy; policy uses compiled entrypoint and SDK1, remaining action tokens valid but ignored. Requests must include every configured ID exactly once.

FRAMES count observer_id N then count frames: `tick` followed by N inputs `id kind controller_id_or_dash sdk dx dy aim_x aim_y fire observation_hash`, then `hash`. FRAME is unnecessary. Complete batch atomic; frame array may be reordered by ID, native returns canonical arrays.

Line byte limit1048576. Errors are {error:string}; no mutations on errors, including RESET invalid observer.

## JSON observations/state/replay

Observation exactly: sdk_version,observation_version,profile,hz,tick,self_id,self_team,sensor_range_mm,self,arena,roster,contacts,projectiles,self_heat,zones,score,rate,qualified_ms,duration_ms,credits_enabled.

self exactly x,y,yaw,hp,ammo,cooldown. arena exactly width,height,walls (wall objects x1,y1,x2,y2). roster equals canonical config.robots (explicitly PUBLIC static spawn/profile/eligibility, not current hidden stats). contacts [{id,team,x,y,yaw}] include every other robot whose current center is in self range and wall LOS, including same-team and disabled poses without HP/death flags. Sorted ID. projectiles [{x,y,vx,vy}] filtered point range/LOS, lexicographically sorted, no owner/ID/lifetime/damage. self_heat exactly enabled,heat_tenths,limit_tenths,cooling_per_second,overheat_locked,permanent_latched,firing_locked. Only own dynamic heat. Zones/score/rate/qualified_ms/duration_ms/credits_enabled exactly same public tactical fields.

State exactly build,schema(1),profile,observation_profile,observation_version(1),official(false),observer(string),tick,terminal(0/1),hash,config,result,damage_admin,heat_admin,observation,last_sources,frames. last_sources null initially else canonical [{id,kind,sdk_version,controller_id}]. frames empty unless STEP/FRAMES. Filtered bridge MUST strip hash, frames, damage_admin, heat_admin; no administrative diagnostics in player responses.

Frame exactly tick,inputs,hash. Each input exactly id,kind,sdk_version,controller_id,action(array5),observation_hash. controller_id null for manual; policy controller IDs use [A-Za-z][A-Za-z0-9_.-]{0,63}. Hashes uint32. Action unchanged SDK1. Replay exactly schema,profile,build,engine,observation_profile,observation_version,official,config,initial_hash,frames,final_hash. Same source build/config/schema required; deterministic state hash covers config and every dynamics/rule state. These diagnostics are not cryptographic authentication.

Damage admin exactly schema,module,strategy,complete,reason,attack_damage,remaining_robot_hp,actual_hp_loss,excluded_hp_loss,penalty_attack_damage,nominal_projectile_damage,last_sequence. Values per team like tactical, totals bounded by configured summed HP/ammo/damage, not100. Heat admin exactly schema,module,strategy,threshold_policy,boundary_policy,robots; per robot exact id,heat_tenths,overheat_locked,permanent_latched,firing_locked,detected_shots,blocked_fire_ticks. No public other-robot dynamics.

## Named synthetic semantics

Individual qualified detector per robot per zone; ordinary leave receives verified2s grace once per individual; individual death immediately clears only that robot's detector. Team logical activation is OR of eligible living individual logical activations; no new grace after aggregation. This preserves teammates' continuity and does not claim official qualification. Simultaneous whole-position snapshot passed to verified capture; unsupported contest-to-none still errors atomically. Temporary capture remains latched.

Movement proposals all computed from frozen pre-step state; wall axis-slide then conservative swept circle pair blocking. All conflicted translations reject together, repeat until stable against newly stationary bodies. Initially tangent bodies may separate or move tangentially. Disabled bodies remain movement obstacles. No collision HP damage. Heading/cooldown still update on blocked movement. All living actors emit before damage commits. Friendly projectiles pass through own team, including shooter. Projectiles collide only with walls and living enemies; disabled enemy bodies are projectile-transparent. Earliest Q16 hit wins, wall wins exact tie, then smallest canonical victim ID. Same-tick hits accumulate per victim before HP clamp; actual loss credited to opposing TEAM only, no individual overkill allocation. Every actual emitted round consumes ammo and heat even if immediately blocked by cover. Simultaneous deaths possible. New projectile slots cannot introduce cap priority: bounded settings make2048 active cap unreachable, violation errors atomically.

All owning observations frozen before any callbacks. Trusted same-process stateless C++ only, no malicious code isolation. Heat cooling precedes firing gate; actual shots then death reset; strict threshold ambiguity preserved. 420s judge uses non-destructible base sentinel1, configured robots as normally connected/not dismissed. No respawn or penalties.
