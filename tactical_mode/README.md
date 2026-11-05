# Experimental tactical vertical slice

This isolated native mode puts combat and objectives in one playable scene. It is **not full official RMUC**, a referee simulator, a validated tactics model, or a win-rate estimator. Existing modes and rule implementations stay unchanged.

## Authority and selected assumptions

A single `sandbox::Engine` owns positions, projectiles, damage, disabled robots, and the **60 Hz** clock. The adapter calls the unchanged capture and match-formula modules; it does not run a second independent simulation or clock. Its qualified post-combat timestamps are `ceil(tick * 1000 / 60)` milliseconds. The run ends at **25200 ticks / 420 seconds**, not when a robot is disabled.

The default `EXPERIMENTAL-playable-engineering-v3` preset explicitly selects:

- `ENGINEERING-NOT-OFFICIAL-timer-before-input-v1`: timer completion precedes a coincident input. Strict rejection remains selectable.
- `EXPERIMENTAL-reconciled-projectile-hp-loss-v1`: reconcile only the unchanged sandbox's opposing-projectile events against actual HP deductions. This is a synthetic producer of settled facts, not an official damage/referee model. Select `missing-adjudicated-attack-damage` to keep the prior unresolved-tie behavior.
- `EXPERIMENTAL-one-robot-per-team-connected-not-dismissed-v1`: the two synthetic robots are treated as connected and not dismissed when the existing qualified remaining-HP formula is used. No connection or penalty lifecycle is simulated.
- `EXPERIMENTAL-end-of-second-v1`: after capture processing, credit the current control formula once at each whole second, including second 420. Credits may instead be disabled. This cadence is not an established official rule.
- `EXPERIMENTAL-inclusive-center-rectangle-v1` and `EXPERIMENTAL-one-alive-eligible-robot-per-team-v1`: reuse the four existing objective-mode placeholder rectangles; one living robot per side qualifies by its center, with inclusive edges. No interaction-card, eligibility, or multi-robot model is claimed.
- `EXPERIMENTAL-immediate-death-deactivation-v1`: death immediately resets all of that robot's detector tracks to Absent at the qualified death timestamp and clears pending grace. Death is **not** treated as verified ordinary detection loss. This is an unsupported-official-lifecycle engineering choice.
- `EXPERIMENTAL-latched-temporary-no-lifecycle-v1`: the first captured Temporary control stays latched for the rest of the run. There is no restoration, respawn, rebuild, or base destruction.
- `EXPERIMENTAL-cooling-gate-combat-shot-heat-death-damage-capture-credit-judge-v3`: preview due thermal cooling, gate firing requests, advance combat, settle detected-shot heat and death reset, reconcile private attack damage, then qualified detection/capture, control credit and judgment. Both controllers act from the same frozen pre-step state.

For ordinary movement out of a rectangle, the verified qualified-track 2000 ms grace is applied exactly once. The unchanged capture module applies its 5000 ms progress, reversal and contest rules. A direct contested-to-neither transition remains unresolved and causes an atomic error, even with engineering timer ordering. At death exactly on a capture deadline, engineering mode can complete capture before deactivation; strict mode rejects the entire batch. Death before the endpoint removes activation immediately.

The non-destructible base HP value 1 is only an alive sentinel. Sandbox speed, HP, damage, ammunition, cover and projectiles are unchanged synthetic values, not official robot models. Disabled robots do not move or fire and do not respawn. Their collision bodies can remain under the original physics.

## Verified17mm heat subset and experimental firing gate

The isolated `rules/rmuc2027_heat_v1` module models **heat and firing locks**, not
HP damage. The baseline manual §5.1.3 pp66–67 adds10 heat per detected17mm shot,
cools at10Hz by C/10 per100ms, temporarily locks above Q0 until heat returns to0,
and permanently locks above Q2=Q0+100 for the remainder of the round. Equality
at Q0 does not newly lock. Cooling merely below Q0 does not release a temporary
lock. Death resets heat to0; it does not revoke a round-long permanent firing latch.
Other firing-lock reasons are outside this module and are never cleared by it.

**Q2 equality is unresolved:** the prose uses > while the flowchart uses ≥, and
the prose's temporary interval also excludes equality. Strict rejection is the
default. Explicit engineering choices may assign temporary or permanent lock at
that equality; neither is presented as an official clarification. The generic
module also refuses unadjudicated coincident shot/cooling or shot/death order.
Its source quotations and other dependencies are in [HEAT_SOURCES.md](HEAT_SOURCES.md).

The default tactical preset enables `EXPERIMENTAL-fixed-level1-cooling-priority-17mm-v1`.
It borrows ONLY the level1 cooling-priority launcher values Q0=40 and C=12/s
from table5-18 p94. HP100, ammunition120, shot damage10, the10-tick firing interval,
geometry and other sandbox values remain synthetic. This does not make either
robot an official infantry robot or implement XP/level/type selection. Heat may
instead be disabled for comparison with the preceding scene.

The tactical adapter makes the following additional choices explicit:

- A native emitted-shot event is treated as one detected17mm round. A requested
  fire bit, an ammo/cooldown refusal, or a projectile impact is not a detected shot.
  A shot immediately stopped by cover still emitted a round and gains heat.
- The thermal clock starts at epoch0 with the first cooling boundary at100ms,
  aligned to every sixth native60Hz tick. Integer tenths represent cooling12/s
  as exactly12 tenths per100ms. There is no floating or continuous frame cooling.
- At a due boundary, preview cooling before the firing gate. Gate each robot's
  requested fire by its own thermal lock, without changing movement, aiming,
  ammunition or the independent native shot-interval counter.
- Advance native combat once, then apply heat from the actual emitted shots.
  A same-tick shot and death use the named engineering order: observed shot first,
  then death resets heat. The pending thermal state is transactional with physics,
  capture, damage accounting and the entire requested batch.
- Replays retain **requested** actions, including held fire while locked, and
  deterministically recompute the accepted native shots and thermal state.
  Holding fire can resume emission after cooling to zero; blocked attempts do not
  spend ammunition, add heat, deduct HP or earn attack damage.

The immediate single-shot gate normally locks by at most Q0+10, so normal play
from zero cannot reach Q0+100. Permanent/equality tests use externally detected
shots or injected rule boundaries, not fabricated normal-playthrough claims.
The generic module can consume genuinely detected rounds despite an existing lock;
it is an accounting observer, not proof that an upstream gate worked.

Only **own heat** enters observation revision2. Enemy heat, locks, event counts,
and private heat histories are excluded. Action SDK version1 and its validator
remain unchanged. Source/profile checks prevent older observation consumers or
replays from being silently reinterpreted. The top-down tactical map remains
available: it is not a first-person video feed. The manual's temporary-video
interruption and ambiguous direct-permanent video consequences are not simulated.
No heat HP-loss or heat attack-score rule is invented.

## Playing and policies

The browser selects this mode separately from sandbox, objective-only, filtered combat, layered, and scenario modes. Each of the two robots may independently use manual input or the compiled `public-capture-combat-v1` example. Switch the observed robot independently of control selection.

The example is objective-aware: red first targets the blue outpost, blue first targets the red base. They use only their supplied snapshots and public authored geometry. Both public capture broadcasts normally occur before firing, after which they seek contact and fight. It is a teaching controller, not competition strategy. See [POLICY_SDK.md](POLICY_SDK.md) to replace the stateless native callback.

`combat::policy::Action` and its validator are shared unchanged by manual and controller inputs. Both include SDK version 1 and integer `(dx,dy,aim_x,aim_y,fire)` values. Float and boolean values are rejected. Both robots' observations are frozen before either callback; both decisions are collected before either action is applied.

Policies are trusted C++ in the same process. The snapshot contract is **not memory or process isolation**. No untrusted-plugin or sandbox-security guarantee is made.

## What a robot may observe

The nested `combat` snapshot reuses the unchanged 6000 mm inclusive range and line-of-sight filter. It owns self state, public walls, optional visible enemy pose, and visible projectiles. Enemy HP, ammunition, cooldown and hidden pose are absent. Visible opponent presence does not depend on its HP.

The tactical additions are deliberately **PUBLIC experimental broadcasts**: all objective rectangle bounds, original owner, current controller and Full/Temporary degree; score and control-rate arrays; qualified and total-duration time; and whether credits are enabled. These broadcasts are explicit assumptions, not locally inferred sensors or verified official telemetry. In C++ a disabled score has a zero array with `credits_enabled=false`; in JSON it is `null`. Nested combat SDK version and tactical SDK version are both 1; the tactical JSON includes its own version metadata.

No observation exposes capture progress, raw occupancy, detection/activation flags, grace deadlines, completion timestamps, global events, opponent health or heat, hit/damage ledgers, or global state hashes. Privacy invariance is conditional on the same allowed public control and score facts: hidden occupancy/progress changes do not affect the snapshot until a permitted public control/score broadcast changes.

Native administrative responses and replay files have hashes and both resolved action streams for diagnostics/reconstruction. They are **not robot observations**. The loopback bridge strips authoritative hashes, the private `damage_admin` and `heat_admin` objects, and frame logs from public state and mutation responses, and the UI displays only validated public facts. Exported replay is an explicit administrative artifact, not an opponent sensor feed.

## Attack damage and timeout

The isolated `rules/rmuc2027_damage_v1` module consumes **externally settled actual
HP deductions**, not nominal hit strength or a difference between arbitrary
health snapshots. Its source-linked definitions come from the baseline manual
§2 printed p19; timeout priority is §5.8 pp114–115. See
[ATTACK_DAMAGE_SOURCES.md](ATTACK_DAMAGE_SOURCES.md) for quotations and exclusions.

- Bullet, dart and recognized aerial-impact HP deductions may count as attack
  damage when their scoring recipient is already adjudicated. Aerial impact is
  not ordinary armor collision.
- Violation-penalty deductions credit the opposing team. The module does not
  invent warning issuance, percentage damage, referee discretion or dismissal.
- Ordinary collision and module-offline HP deductions do not count as attack
  damage. Other causes, friendly-fire attribution, mixed simultaneous causes,
  and the distinction between projectile damage, XP and special reward rules
  are not silently resolved.
- The formal ledger's external facts do not prove that upstream detection,
  effectiveness, armor, buffs, immunity, damage amounts or attribution are correct.

The tactical adapter is narrower. The unchanged sandbox emits hit events with
**nominal** amount 10, then clamps aggregate same-tick damage to remaining HP.
The adapter checks opposing-source projectile evidence and reconciles the total
against each victim's actual before/after HP. It records a single settled deduction
per victim and tick, with the sole opposing recipient. For example, nominal hits
of 20 against 7 HP contribute 7, not 20. This avoids assigning an invented share
to individual simultaneous hits. The event producer, 60 Hz boundary, synthetic
projectile amount, absent official mechanisms and reconciliation strategy are
explicit engineering assumptions. Missing or inconsistent evidence makes the
attack ledger unavailable; it does not fabricate a zero or reclassify all loss
as attack damage.

At 25200 ticks, the same world processes final-tick combat and reconciliation,
then capture, the optional final control credit and the judge. This phase order
is experimental, not an asserted official same-time ruling. Unequal control
scores still win first. If control ties and the selected attack ledger is complete,
the existing judge compares attack totals, then qualified remaining robot HP,
then declares a draw. Disabled robots contribute zero remaining HP; bases and
outposts are never included in that sum. The positive base sentinel is only an
alive flag. With equal initial HP and no healing or non-attack causes, tied attack
totals naturally imply tied remaining HP; an unequal surviving-HP branch needs
external/synthetic fixtures and is not claimed reachable in ordinary current play.

Select the missing strategy, or encounter unsupported settlement, and a control
tie remains `unresolved-timeout-tie` / `missing-adjudicated-attack-damage`.
An unavailable attack ledger does not prevent a valid higher-priority control
result. Credits disabled still produce `unscored-timeout`.

Attack totals, nominal damage, loss accounting and opponent remaining HP are
private native administrative data. They do not enter controller observations or
ordinary HTTP/UI responses. A terminal result discloses its comparison basis,
not those numeric totals. This is deliberate end-of-run reporting, outside the
SDK's sensing contract. Developer replay already reveals both input streams and
can reconstruct private state; it is not a filtered player artifact.

## Native protocol and replay

`build/rmuc_tactical` is line-oriented JSON output. It supports:

- `BUILD`, `META`, `POLICY_META`, `EXPORT`.
- `RESET observer boundary_index credit_index damage_index heat_index threshold_index`. Boundary0 is strict,1 engineering; credit0 disables,1 enables; damage0 keeps the ledger missing,1 enables reconciliation; heat0 disables,1 enables the borrowed heat profile; threshold0 rejects Q2 equality,1 makes it temporary under an engineering interpretation,2 makes it permanent under the flowchart interpretation. Every selection is required and explicit.
- `STATE observer`.
- `STEP count observer source0 sdk0 dx0 dy0 aim_x0 aim_y0 fire0 source1 sdk1 dx1 dy1 aim_x1 aim_y1 fire1`, where source is `manual` or `policy`, and count is 1..600.
- `FRAMES count observer` followed by each resolved frame. Frame grammar: tick; for each side kind, controller ID (`-` for manual), SDK and five action integers; then authoritative hash and both observation hashes. `FRAME tick observer ...` also supports one frame.

Each STEP and FRAMES batch runs on a candidate copy. Errors commit no physics, objective, score or journal change. Controller exceptions are sanitized. Replay invokes no controller callbacks or descriptor entrypoints. Full import/prefix seeking use callback-free batches of up to 600, and the bridge commits a replacement only after all replay validation succeeds. Individual native lines are bounded at 512 KiB; replay imports are bounded at 48 MiB and 25200 frames.

The frame schema remains `rmuc-experimental-tactical-replay.v1`, while the engine identity is now `integer-mm-tactical-v3`. Earlier engine/config/build replays require their earlier source version and are rejected here. Replay pins the entire selected configuration, tactical observation profile, initial and final hashes, and a build hash covering all used native/rule/SDK/bridge sources. Both teams' pre-action observation hashes and resolved inputs are recorded on each frame. The authoritative state hash also covers private thermal state and private attack-ledger totals, sequence/time, completeness, HP baseline and sealing; replay recomputes that state from recorded actions rather than trusting supplied totals. A valid import restores the configuration recorded in that replay, even if the current run uses another supported preset. Altered or inconsistent configuration/hash evidence and changed source builds reject rather than silently reinterpret recorded actions. Local hashes are deterministic regression checks, not cryptographic authenticity guarantees.

## Verification

`tests/tactical_core_tests.cpp` checks default policy-versus-policy capture, positive control credit, combat damage/disable, actual 420-second timeout, and full callback-free replay in the **same run**, plus action-validator parity, batch/exception rollback, hidden-data invariance, common pre-step broadcasts, no respawn/early KO, replay corruption and missing-ledger outcomes. `tests/tactical_review_tests.cpp` independently exercises manual play, death boundaries, strict atomic rejection, final credit order and additional privacy mutations.

The implementation is native C++20; the standalone SDK/example support C++17. No tactical WASM build, production deployment, official-match equivalence, or rendered-browser verification is claimed here.
