# RMUC2027 17 mm heat module v1

This versioned module implements a **bounded source-backed subset**, not a full referee or an official robot simulation. Source: RMUC2027 Chinese V1.0.0, §5.1.3 printed pp66–67, §5.3.2 p74, infantry cooling-priority table5-18 p94. See `provenance.json` and `../../tactical_mode/HEAT_SOURCES.md` for the independent source audit and pinned PDF.

## Verified arithmetic and firing consequences

- Every **detected** 17 mm round adds10 heat. Attempting to fire, missing, cover hits, or an ineffective later armor hit are not alternative heat inputs. Muzzle speed does not alter the increment.
- Every100ms, cooling subtracts C/10 heat, clamped at0. The implementation stores tenths: one shot adds100, C=12 subtracts12 per100ms. There is no framewise floating-point decay or truncation of1.2 to1.
- Q0 is a threshold, not a storage cap. The crossing shot is counted before lock. A temporary heat lock remains until heat0, even after heat falls below Q0.
- Q2=Q0+100. Permanent firing lock lasts the rest of the round and survives subsequent cooling to0 or death. Death resets heat0 and removes only the temporary heat reason. No other lock cause is represented or cleared here.
- Heat generates **no HP deduction, damage event or attack-damage credit**.

## Deliberately unresolved boundaries

The prose requires Q0<Q1<Q2 for temporary lock and Q1>Q2 for permanent lock. The flowchart uses Q1>=Q2 for permanent lock. Equality is therefore outside **both** prose intervals, not a prose-confirmed temporary case.

- Default `StrictRejectUnadjudicated` rejects a shot that reaches exactQ2, atomically, including an intermediate +10 increment within a multi-shot batch.
- `ENGINEERING-prose-greater-temporary-at-equality-v1` assigns temporary lock at equality and permanent only above. The temporary-at-equality fallback is an explicit engineering choice, not a verified prose ruling.
- `ENGINEERING-flowchart-greater-equal-v1` assigns permanent at equality, explicitly selecting the diagram over the conflicting prose.

The default module rejects coincident shots/cooling, and coincident shots/death. Explicit module policies support cooling-before-shot or shot-before-cooling, and shot-before-death-reset. Cooling epoch0 and one strictly increasing timestamp per atomic event batch are interface choices, not verified referee timing. The initial event may be at0; first cooling is100ms. No call splitting at one timestamp is allowed to bypass a strict boundary.

A detected-shot batch represents a sequence of identical +10 detections at the stated timestamp, with no cooling interleaved. O(1) range checks preserve every intermediate temporary/permanent crossing and reject any exact unresolved threshold. The count is an externally supplied fact, not authorization to shoot. Facts may still be consumed despite an existing lock; an upstream emitter owns whether it can emit another shot. Full physical/referee ordering of simultaneous detections remains outside this module.

All errors preserve the complete prior state. Negative, overflowing, invalid-config and non-increasing-time inputs reject. The ledger-like cumulative detected count is diagnostic, not a score.

## Experimental tactical adapter

Only `tactical_mode` uses this module. Older combat, objective, layered, formal capture/match and sandbox physics remain unchanged. The default tactical preset enables heat, with strictQ2 rejection. Heat may be disabled for controlled comparison. A fixed experimental fixture borrows ONLY level1 cooling-priority infantry heat attributes Q0=40/C=12. It does not relabel the synthetic robot as official infantry or change HP100, geometry, ammunition or projectile physics.

At each60Hz tick, both policies get frozen pre-step snapshots. The adapter forecasts discrete cooling through the tick-end `ceil(tick*1000/60)` timestamp, gates only the effective native `fire` bit when a heat reason remains, runs unchanged native combat, counts actual native shot events, applies those heat facts and the observed death reset, and then reconciles projectile HP loss and advances capture/credit/judgment. This is `EXPERIMENTAL-cooling-before-fire-gate-and-shot-v1` plus `EXPERIMENTAL-observed-shot-before-death-reset-v1`, not official simultaneous-event timing. Requested inputs remain unchanged in replay. Blocked-fire counts count requested-fire ticks suppressed by a heat lock, not failed shot attempts during native cooldown/ammo gating.

Every thermal state, count, policy selection and source is pinned by authoritative hash/build identity. Existing replay schema/profile remain, but engine/preset/phase arev3; replay requires the exact current build/config/observation revision. There is no backwards reinterpretation of old replay hashes.

Action SDK remains1; tactical observation revision2 adds an owning const `self_heat` value with own heat and heat-derived lock state only. Enemy heat and all detected/block counters remain administrative. Administrative output is not policy input; trusted same-process C++ is not a security boundary.

## Unsupported

No first-person video cutoff/recovery or video telemetry is simulated. The manual's temporary video consequence and permanent-branch video ambiguity are documented, not converted to a blank tactical map or lost sensors. No42mm heat, dynamic robot/level/buff profile, generalized fractional/rounded cooling, overspeed/offline/revival lock causes, official detection latency/cooling epoch, full death/revival lifecycle, or untrusted-code/WASM authority is claimed. A temporary lock's release does not guarantee ability to fire: native death, ammo and cooldown still apply. Permanent-limit tests use externally supplied module facts/test fixtures; ordinary gated one-shot tactical play cannot build toQ2 after temporary lock.
