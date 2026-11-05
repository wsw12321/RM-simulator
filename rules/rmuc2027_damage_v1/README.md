# Settled attack-damage ledger

Version `rmuc2027-damage-v1.0.0-module.1`, C++20, no dependencies. This is a
source-linked accounting module, **not a damage calculator or referee simulator**.
Only the separate experimental tactical adapter uses it. Older modes are unchanged.

The pinned source is RMUC 2027 Chinese V1.0.0 (2026-10-15), SHA-256
`9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be`.
See [provenance](provenance.json), the [source audit](../../tactical_mode/ATTACK_DAMAGE_SOURCES.md),
and [official source hub](https://bbs.robomaster.com/wiki/20204847/1969149?source=7).
Printed page numbers are one less than physical PDF pages. Latest official Q&A is
not certified by this implementation.

## What the source supports

- §2 table 2-1, p19 defines attack damage as **HP deducted** by projectile, dart,
  or aerial impact on a robot/base/outpost. Nominal hit strength, hit count, damage
  before clamping, and generic HP difference are not interchangeable with this.
- p19 explicitly credits **violation-penalty HP deductions** to the victim team's
  opponent. A module-offline automatic deduction is not such a violation deduction.
- p19 excludes ordinary armor-collision and referee-module-offline loss on robots.
  Supporting mechanics on pp65/69/70 do not establish these deductions for bases
  or outposts; those entity/cause pairs are rejected. A Robot fact additionally
  asserts upstream ground-robot applicability where the source requires it.
- Integer rounding, buffs, shield validity, warning floors and card triggers are
  upstream. The ledger neither repeats them nor generates them. A settled yellow
  warning from 5 HP to 1 credits 4; a settled red-card deduction from 5 to 0 credits 5.
  The yellow-warning floor is not imposed on all penalties.

## Explicit externally settled input

`SettledDeduction` has consecutive sequence, nonnegative time, target entity/team/
kind, adjudicated cause, HP before/after, and optional **adjudicated scoring team**.
It represents one deduction, after any rounding, clamping and ordering, with no
interleaved healing. The actual loss is `hp_before - hp_after`. HP increases,
negative values, unknown causes, unsupported pairs and invalid enums fail.

Projectile, dart and aerial facts require `attack_credit_team`. It is an upstream
assertion, **not an inferred attacker**. The module has no attacker field. In
particular p89's friendly/unknown-source XP rule does not settle team attack-score
attribution, and p111's moving-dart group wording remains unresolved. Without
external attribution, such events must stay unadjudicated. A named recipient does
not prove the adjudication is official; it only makes the accounting input explicit.
Violation penalties derive the opponent; a conflicting supplied recipient fails.
Excluded causes must not claim a recipient. Physical collision is not sufficient
classification: p65 notes that the referee can classify some collisions as projectile.

`settle()` is pure. `Ledger::apply()` validates consecutive sequence and monotonic
(non-decreasing) time, then atomically adds victim loss, credited attack loss,
excluded victim loss and penalty-credit subtotals. `apply_batch()` is all-or-nothing,
including sequence, timestamp and overflow failures. Seal rejects later events.
The ledger does not own entity HP continuity, trigger validity or timeline completeness:
its producer must prove these. Reusing a fact under a new sequence is an upstream
error, not something integer accounting can detect.

A generic penalty event must never substitute for an administrative forfeit override.
Official pre-match forfeits prescribe score/HP resets, including zero attack damage;
the unchanged match module rejects unimplemented referee rulings.

## Minimal C++ use

```cpp
namespace d = rmuc2027::damage_v1;
d::Ledger ledger;
// Upstream has ALREADY adjudicated this red robot's warning and settled HP5→1.
// This code does not issue a warning or calculate its percentage/floor.
d::SettledDeduction event{1, 123000, 17, d::Team::Red, d::EntityKind::Robot,
    d::Cause::ViolationPenalty, 5, 1, std::nullopt};
auto result = ledger.apply(event);
if (const auto* error = std::get_if<d::Error>(&result)) {
    const auto diagnostic = d::error_name(*error); // Pass to your host logger.
    (void)diagnostic; // The ledger is unchanged on failure.
} else {
    // ledger.totals().attack_damage == {0, 4}.
}
```

Include `damage_rules.hpp` and compile `damage_rules.cpp` with C++20. The full
project build runs both module and integration tests; no online dependency is used.

## Tactical adapter and remaining gaps

The unchanged native sandbox emits `{tick,kind,actor,target,amount}`. Its kind-2
amount is nominal `10`, and all hits commit simultaneously before aggregate HP is
clamped to zero. It provides no bullet identity, official referee settlement,
armor/shield validity, penalty/offline cause or per-hit overkill attribution.

`EXPERIMENTAL-reconciled-projectile-hp-loss-v1` therefore supports only that closed
synthetic model:

1. Check HP continuity against the last committed native HP baseline.
2. Validate event shapes, timestamps and opposing source/target identities.
3. Group all same-tick hits by victim. Every hit on a victim has the same opponent.
4. Verify loss equals `min(before_hp, sum(nominal_hit_amounts))` and disable events
   agree with the HP transition.
5. Enter one settled projectile deduction per changed victim, atomically. Nominal
   amount is a separate private diagnostic and never a score. No individual-hit
   overkill allocation is invented.
6. Any mismatch or unsupported cause permanently marks the ledger incomplete.
   Prefix totals remain diagnostics only. Required missing attack damage cannot
   become zero, a remaining-HP decision or a draw. Higher control can still decide.

The default playable preset enables this explicitly experimental adapter. Selecting
`missing-adjudicated-attack-damage` preserves conservative unresolved ties and does
not collect a substitute ledger. Neither selection claims full official settlement.
No penalties, collisions causing HP loss, offline state, darts, aerial attacks,
friendly fire, healing, revival or base destruction are generated by the sandbox.
These require a new explicit producer/schema, not inferred event meanings.

At tick25200, synthetic combat and its ledger settle before capture/control-credit/
420000ms judgment; the complete tick commits and seals atomically. Source p65 allows
delayed referee damage settlement and does not establish this exact cutoff ordering.
The adapter and phase identifiers, config, source/build identity, private totals,
completeness, baseline, and seal state participate in replay/hash verification.

The unchanged match judge applies control, then attack damage, then current HP of
surviving robots, then draw. Current-HP aggregation uses the existing verified helper,
with an explicit experimental assumption of one connected, non-dismissed robot per
team. Damage totals and enemy HP are private administrative facts, never policy input.
Trusted C++ controllers share a process: filtered observations are not isolation.
