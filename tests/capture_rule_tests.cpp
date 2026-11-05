#include "capture_rules.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <utility>
using namespace rmuc2027::capture_v1;
namespace {
int checks = 0;
constexpr auto MAX = std::numeric_limits<std::int64_t>::max();
constexpr auto PRESENT = QualifiedDetection::Present;
constexpr auto ABSENT = QualifiedDetection::Absent;
constexpr auto ENGINEERING = BoundaryPolicy::EngineeringTimerBeforeInputV1;
constexpr LogicalActivation NONE{false, false}, RED{true, false}, BLUE{false, true}, BOTH{true, true};
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) { std::cerr << "FAIL capture rule: " << label << '\n'; std::exit(1); }
}
template<class T> T value(Result<T> result) {
    if (auto e = std::get_if<Error>(&result)) {
        std::cerr << "Unexpected error: " << error_name(*e) << '\n';
        check(false, "expected successful result");
    }
    ++checks;
    return std::get<T>(std::move(result));
}
template<class T> void error(const Result<T>& result, Error expected) {
    check(std::holds_alternative<Error>(result) && std::get<Error>(result) == expected,
          "expected explicit error");
}
PositionControl initial(PositionKind kind = PositionKind::Outpost, Team owner = Team::Red) {
    return value(initial_control(kind, owner));
}
CaptureAttempt attempt(LogicalActivation a = BLUE,
                       BoundaryPolicy p = BoundaryPolicy::StrictRejectUnadjudicated,
                       std::int64_t start = 0) {
    return value(CaptureAttempt::start(start, initial(), a, p));
}
ActivationDelay delay(QualifiedDetection d = PRESENT,
                      BoundaryPolicy p = BoundaryPolicy::StrictRejectUnadjudicated,
                      std::int64_t start = 0) {
    return value(ActivationDelay::start(start, d, p));
}
template<class C, class F> void atomic_error(C& c, F action, Error expected) {
    const auto before = c.snapshot();
    const auto trace = c.trace();
    error(action(), expected);
    check(c.snapshot() == before && c.trace() == trace, "rejection preserves state AND trace");
}
void mappings() {
    for (auto kind : {PositionKind::Outpost, PositionKind::Base}) {
        for (auto owner : {Team::Red, Team::Blue}) {
            const auto other = owner == Team::Red ? Team::Blue : Team::Red;
            const auto p = initial(kind, owner);
            check(p.controller == owner && p.degree == ControlDegree::Full, "official initial full control");
            const auto capture = value(map_qualified_transition(p,
                {FactKind::AdjudicatedCaptureThresholdReached, other}));
            check(capture.after.controller == other && capture.after.degree == ControlDegree::Temporary &&
                  capture.after.original_owner == owner, "row 1 changes controller, not original owner");
            const auto restore = value(map_qualified_transition(capture.after,
                {FactKind::TemporaryControllerAbsentContinuouslyForFiveSeconds, owner}));
            check(restore.after == p, "row 2 restores opposite temporary controller");
            check(restore.requires_external_outpost_lifecycle_adjudication == (kind == PositionKind::Outpost),
                  "outpost absence mapping does not certify lifecycle");
            error(map_qualified_transition(p,
                {FactKind::AdjudicatedCaptureThresholdReached, owner}), Error::UnsupportedTransition);
            error(map_qualified_transition(p,
                {FactKind::TemporaryControllerAbsentContinuouslyForFiveSeconds, other}), Error::UnsupportedTransition);
            error(map_qualified_transition(capture.after,
                {FactKind::TemporaryControllerAbsentContinuouslyForFiveSeconds, other}), Error::UnsupportedTransition);
        }
    }
    auto p = initial();
    auto destroyed = value(map_qualified_transition(p, {FactKind::ActorDestroyedOutpost, Team::Blue}));
    check(destroyed.after.controller == Team::Red && destroyed.after.degree == ControlDegree::Temporary,
          "row 3 destroy-first does not give destroyer control");
    check(destroyed.requires_external_outpost_lifecycle_adjudication, "destroy-first lifecycle explicit");
    auto promoted = value(map_qualified_transition(destroyed.after, {FactKind::ActorDestroyedOutpost, Team::Red}));
    check(promoted.after == p && promoted.requires_external_outpost_lifecycle_adjudication,
          "row 4 literal map only; no alive or rebuilt assertion");
    // Mapping qualified facts independently does NOT establish that two outpost
    // destructions can really occur consecutively. No lifecycle sequence engine.
    auto after_capture = value(map_qualified_transition(p,
        {FactKind::AdjudicatedCaptureThresholdReached, Team::Blue}));
    auto capture_then_destroy = value(map_qualified_transition(after_capture.after,
        {FactKind::ActorDestroyedOutpost, Team::Blue}));
    check(capture_then_destroy.after.controller == Team::Blue &&
          capture_then_destroy.after.degree == ControlDegree::Full &&
          capture_then_destroy.after.original_owner == Team::Red &&
          capture_then_destroy.requires_external_outpost_lifecycle_adjudication,
          "capture-first/destroy table result does not manufacture 500 HP rebuild");
    auto restore_not_original = value(map_qualified_transition(destroyed.after,
        {FactKind::TemporaryControllerAbsentContinuouslyForFiveSeconds, Team::Blue}));
    check(restore_not_original.after.controller == Team::Blue,
          "absence maps to opponent, not original or last full owner");
    error(map_qualified_transition(p, {FactKind::ActorDestroyedOutpost, Team::Red}), Error::UnsupportedTransition);
    error(map_qualified_transition(destroyed.after, {FactKind::ActorDestroyedOutpost, Team::Blue}), Error::UnsupportedTransition);
    error(map_qualified_transition(initial(PositionKind::Base),
        {FactKind::ActorDestroyedOutpost, Team::Blue}), Error::UnsupportedTransition);
    auto base = initial(PositionKind::Base); base.degree = ControlDegree::Temporary;
    error(map_qualified_transition(base,
        {FactKind::TemporaryControllerAbsentContinuouslyForFiveSeconds, Team::Blue}), Error::InvalidEnemyFullBaseControl);
    p.controller = static_cast<Team>(9);
    error(map_qualified_transition(p, {FactKind::ActorDestroyedOutpost, Team::Blue}), Error::InvalidTeam);
    error(initial_control(static_cast<PositionKind>(9), Team::Red), Error::InvalidPositionKind);
    error(initial_control(PositionKind::Outpost, static_cast<Team>(9)), Error::InvalidTeam);
    p = initial(); p.original_owner = static_cast<Team>(9);
    error(map_qualified_transition(p, {FactKind::ActorDestroyedOutpost, Team::Blue}), Error::InvalidTeam);
    p = initial(); p.degree = static_cast<ControlDegree>(9);
    error(map_qualified_transition(p, {FactKind::ActorDestroyedOutpost, Team::Blue}), Error::InvalidControlDegree);
    p = initial();
    error(map_qualified_transition(p, {static_cast<FactKind>(9), Team::Blue}), Error::InvalidFact);
    error(map_qualified_transition(p, {FactKind::ActorDestroyedOutpost, static_cast<Team>(9)}), Error::InvalidTeam);
    base = initial(PositionKind::Base); base.controller = Team::Blue;
    error(map_qualified_transition(base, {FactKind::ActorDestroyedOutpost, Team::Red}), Error::InvalidEnemyFullBaseControl);
}
void activation() {
    auto a = delay();
    value(a.apply(1000, ABSENT));
    check(a.snapshot().logically_active && a.snapshot().deactivation_at_ms == 3000, "ordinary loss starts one grace");
    value(a.apply(2999)); check(a.snapshot().logically_active, "active 1 ms before expiry");
    value(a.apply(3000)); check(!a.snapshot().logically_active, "inactive exactly at uncontested expiry");
    value(a.apply(3001)); check(!a.snapshot().logically_active, "inactive 1 ms after expiry");
    atomic_error(a, [&] { return a.apply(3001, PRESENT); }, Error::TimeNotIncreasing);
    value(a.apply(3002, PRESENT)); check(a.snapshot().logically_active, "qualified reacquisition no invented latency");
    auto b = delay(); value(b.apply(1000, ABSENT)); value(b.apply(2999, PRESENT));
    check(!b.snapshot().deactivation_at_ms && b.snapshot().logically_active, "reacquisition before expiry cancels grace");
    value(b.apply(3000, ABSENT)); value(b.apply(4999));
    check(b.snapshot().logically_active, "new ordinary loss gets new 2 s grace");
    value(b.apply(5000)); check(!b.snapshot().logically_active, "new grace expires only once");
    auto c = delay(); value(c.apply(1000, ABSENT));
    atomic_error(c, [&] { return c.apply(3000, PRESENT); }, Error::UnadjudicatedCoincidence);
    value(c.apply(3000));
    atomic_error(c, [&] { return c.apply(3000, PRESENT); }, Error::TimeNotIncreasing);
    auto eng = delay(PRESENT, ENGINEERING); value(eng.apply(1000, ABSENT)); value(eng.apply(3000, PRESENT));
    check(eng.snapshot().logically_active && !eng.snapshot().deactivation_at_ms, "explicit timer-first reacquires after expiry");
    const auto& tr = eng.trace();
    check(tr.size() == 5 && tr[2].kind == ActivationTraceKind::EngineeringOrderApplied &&
          tr[3].kind == ActivationTraceKind::GraceExpired &&
          tr[4].kind == ActivationTraceKind::DetectionAcquired &&
          tr[3].policy == "ENGINEERING-NOT-OFFICIAL-timer-before-input-v1", "policy and ordering recorded in trace");
    auto later = delay(); value(later.apply(1000, ABSENT)); value(later.apply(3001, PRESENT));
    check(later.trace()[2].time_ms == 3000 && later.trace()[3].time_ms == 3001, "expiry not rounded to observation time");
    auto inactive = delay(ABSENT); value(inactive.apply(99));
    check(!inactive.snapshot().logically_active && !inactive.snapshot().deactivation_at_ms, "initial absent has no fabricated grace");
    value(inactive.apply(100, PRESENT)); check(inactive.snapshot().logically_active, "acquisition already qualified upstream");
    atomic_error(inactive, [&] { return inactive.apply(101, PRESENT); }, Error::RedundantInput);
    atomic_error(inactive, [&] { return inactive.apply(101, static_cast<QualifiedDetection>(99)); }, Error::InvalidDetection);
    atomic_error(inactive, [&] { return inactive.apply(-1); }, Error::NegativeTime);
    atomic_error(inactive, [&] { return inactive.apply(99); }, Error::TimeNotIncreasing);
    error(ActivationDelay::start(-1, PRESENT), Error::NegativeTime);
    error(ActivationDelay::start(0, static_cast<QualifiedDetection>(99)), Error::InvalidDetection);
    error(ActivationDelay::start(0, PRESENT, static_cast<BoundaryPolicy>(99)), Error::InvalidPolicy);
    auto overflow = delay(PRESENT, BoundaryPolicy::StrictRejectUnadjudicated, MAX - 2001);
    atomic_error(overflow, [&] { return overflow.apply(MAX - 1999, ABSENT); }, Error::IntegerOverflow);
    value(overflow.apply(MAX - 2000, ABSENT)); value(overflow.apply(MAX));
    check(!overflow.snapshot().logically_active && overflow.snapshot().time_ms == MAX, "last representable deadline works");
    auto huge = delay(ABSENT); value(huge.apply(MAX));
    check(huge.snapshot().time_ms == MAX, "huge quiet interval cannot overflow");
}
void capture() {
    auto c = attempt(); value(c.apply(4999));
    check(c.snapshot().progress_ms == 4999 && !c.snapshot().completed, "1 ms before capture");
    auto at = value(c.apply(5000));
    check(at.state.completed && at.state.control.controller == Team::Blue &&
          at.state.control.degree == ControlDegree::Temporary && at.unconsumed_ms == 0, "exact 5 s uncontested capture");
    atomic_error(c, [&] { return c.apply(5000, NONE); }, Error::TimeNotIncreasing);
    atomic_error(c, [&] { return c.apply(5001); }, Error::AttemptAlreadyCompleted);
    auto overshoot = attempt(); auto after = value(overshoot.apply(5001));
    check(after.state.time_ms == 5000 && after.requested_time_ms == 5001 && after.unconsumed_ms == 1,
          "1 ms after completion is explicitly unconsumed");
    auto long_span = attempt(); after = value(long_span.apply(MAX));
    check(after.state.time_ms == 5000 && after.unconsumed_ms == MAX - 5000, "huge remainder without overflow or hidden restoration");
    auto late_input = attempt();
    atomic_error(late_input, [&] { return late_input.apply(5001, NONE); }, Error::InputBeyondCaptureEndpoint);
    auto near = attempt(); value(near.apply(4999, NONE)); value(near.apply(5000));
    check(near.snapshot().progress_ms == 4998 && !near.snapshot().completed, "deactivation 1 ms before capture immediately reverses logical progress");
    auto equal = attempt();
    atomic_error(equal, [&] { return equal.apply(5000, NONE); }, Error::UnadjudicatedCoincidence);
    auto opt = attempt(BLUE, ENGINEERING); value(opt.apply(5000, NONE));
    check(opt.snapshot().completed && opt.snapshot().active == NONE &&
          opt.trace()[1].kind == CaptureTraceKind::EngineeringOrderApplied &&
          opt.trace()[2].kind == CaptureTraceKind::CapturedTemporarily &&
          opt.trace()[3].kind == CaptureTraceKind::LogicalInputChanged,
          "explicit timer-first completes then records coincident logical loss");
    auto rollback = attempt(); value(rollback.apply(3000, NONE)); value(rollback.apply(5000));
    check(rollback.snapshot().progress_ms == 1000, "3 s partial progress loses exactly 2 s on logical absence");
    value(rollback.apply(5500, BLUE)); value(rollback.apply(9999));
    check(rollback.snapshot().progress_ms == 4999, "reactivation resumes retained 0.5 s progress");
    value(rollback.apply(10000)); check(rollback.snapshot().completed, "retained progress may finish without final continuous 5 s stretch");
    auto floor = attempt(); value(floor.apply(1000, NONE)); value(floor.apply(1999));
    check(floor.snapshot().progress_ms == 1, "decay just before zero");
    value(floor.apply(2000)); value(floor.apply(2001));
    check(floor.snapshot().progress_ms == 0 && floor.trace()[2].time_ms == 2000,
          "decay clamps to zero at exact endpoint");
    auto floor_input = attempt(); value(floor_input.apply(1000, NONE)); value(floor_input.apply(2000, BLUE));
    check(floor_input.snapshot().progress_ms == 0 && floor_input.snapshot().active == BLUE,
          "input exactly at clamped zero floor commutes and need not be rejected");
    auto defender = attempt(); value(defender.apply(3000, RED)); value(defender.apply(5000));
    check(defender.snapshot().progress_ms == 1000, "defender alone does not accrue challenger progress");
    auto paused = attempt(); value(paused.apply(2000, BOTH)); value(paused.apply(100000));
    check(paused.snapshot().progress_ms == 2000 && !paused.snapshot().completed, "contest pauses without cap or accrual");
    atomic_error(paused, [&] { return paused.apply(100001, NONE); }, Error::UnadjudicatedContestExitToNone);
    value(paused.apply(100001, BLUE)); value(paused.apply(103001));
    check(paused.snapshot().completed, "one remaining challenger resumes frozen progress");
    auto contested_defender = attempt(); value(contested_defender.apply(2000, BOTH));
    value(contested_defender.apply(3000, RED)); value(contested_defender.apply(4000));
    check(contested_defender.snapshot().progress_ms == 1000, "one remaining defender resumes decay");
    auto opt_contest = attempt(BOTH, ENGINEERING);
    atomic_error(opt_contest, [&] { return opt_contest.apply(1, NONE); }, Error::UnadjudicatedContestExitToNone);
    auto no_progress = attempt(NONE); value(no_progress.apply(MAX));
    check(no_progress.snapshot().progress_ms == 0, "absent challenger never acquires control");
    auto reverse = value(CaptureAttempt::start(0, initial(PositionKind::Base, Team::Blue), RED));
    value(reverse.apply(5000));
    check(reverse.snapshot().control.controller == Team::Red && reverse.snapshot().control.degree == ControlDegree::Temporary,
          "enemy base only temporary; both teams symmetric");
    auto invalid = attempt();
    atomic_error(invalid, [&] { return invalid.apply(1, BLUE); }, Error::RedundantInput);
    atomic_error(invalid, [&] { return invalid.apply(-1); }, Error::NegativeTime);
    atomic_error(invalid, [&] { return invalid.apply(0, NONE); }, Error::TimeNotIncreasing);
    auto temp = initial(); temp.degree = ControlDegree::Temporary;
    error(CaptureAttempt::start(0, temp, NONE), Error::UnsupportedTemporaryTiming);
    error(CaptureAttempt::start(-1, initial(), NONE), Error::NegativeTime);
    error(CaptureAttempt::start(0, initial(), NONE, static_cast<BoundaryPolicy>(9)), Error::InvalidPolicy);
    auto invalid_base = initial(PositionKind::Base); invalid_base.controller = Team::Blue;
    error(CaptureAttempt::start(0, invalid_base, NONE), Error::InvalidEnemyFullBaseControl);
    auto last = attempt(BLUE, BoundaryPolicy::StrictRejectUnadjudicated, MAX - 5000);
    value(last.apply(MAX)); check(last.snapshot().completed, "last representable capture threshold");
    auto too_late = attempt(BLUE, BoundaryPolicy::StrictRejectUnadjudicated, MAX - 4999);
    value(too_late.apply(MAX)); check(!too_late.snapshot().completed && too_late.snapshot().progress_ms == 4999,
                                   "unreachable next threshold does not wrap");
}
// Test-only coordinator: copy BOTH components; feed all generated logical edges
// at their exact times; commit neither if downstream rejects. Only one already-
// qualified blue track and an uncontested red-owned position are modeled here.
// This is not an official multi-robot/card aggregator or reusable production API.
struct Pair {
    ActivationDelay activation;
    CaptureAttempt capture;
};
Result<CaptureAdvance> step(Pair& pair, std::int64_t t,
                           std::optional<QualifiedDetection> edge = std::nullopt) {
    Pair candidate = pair;
    const auto trace_count = candidate.activation.trace().size();
    auto ar = candidate.activation.apply(t, edge);
    if (auto e = std::get_if<Error>(&ar)) return *e;
    // The integration examples avoid expiry+reacquisition at one instant.
    // General same-time event aggregation is deliberately not implemented.
    std::optional<CaptureAdvance> last;
    for (std::size_t i = trace_count; i < candidate.activation.trace().size(); ++i) {
        const auto& event = candidate.activation.trace()[i];
        if (event.after.logically_active != candidate.capture.snapshot().active.blue) {
            auto cr = candidate.capture.apply(event.time_ms, LogicalActivation{false, event.after.logically_active});
            if (auto e = std::get_if<Error>(&cr)) return *e;
            last = std::get<CaptureAdvance>(cr);
        }
    }
    if (candidate.capture.snapshot().time_ms < t) {
        auto cr = candidate.capture.apply(t);
        if (auto e = std::get_if<Error>(&cr)) return *e;
        last = std::get<CaptureAdvance>(cr);
    }
    if (!last) last = CaptureAdvance{candidate.capture.snapshot(), t, 0};
    // No supported integrated advance may silently consume a capture remainder.
    if (last->unconsumed_ms != 0) return Error::InputBeyondCaptureEndpoint;
    pair = std::move(candidate);
    return *last;
}
void composition() {
    Pair p{delay(), attempt()}; value(step(p, 3000, ABSENT)); value(step(p, 4500));
    check(p.capture.snapshot().progress_ms == 4500 && p.activation.snapshot().logically_active,
          "3 s detector-active +1.5 s grace gives 4.5 s capture, not immediate rollback");
    const auto before_a = p.activation.snapshot(); const auto before_c = p.capture.snapshot();
    const auto trace_a = p.activation.trace(); const auto trace_c = p.capture.trace();
    error(step(p, 5000), Error::UnadjudicatedCoincidence);
    check(p.activation.snapshot() == before_a && p.capture.snapshot() == before_c &&
          p.activation.trace() == trace_a && p.capture.trace() == trace_c,
          "3+2 s ambiguous boundary rolls back BOTH components transactionally");
    Pair eng{delay(PRESENT, ENGINEERING), attempt(BLUE, ENGINEERING)};
    value(step(eng, 3000, ABSENT)); value(step(eng, 5000));
    check(!eng.activation.snapshot().logically_active && eng.capture.snapshot().completed,
          "same boundary only resolves with recorded explicit engineering timer-first policy");
    Pair earlier{delay(), attempt()}; value(step(earlier, 2000, ABSENT)); value(step(earlier, 4000));
    check(earlier.capture.snapshot().progress_ms == 4000 && !earlier.activation.snapshot().logically_active,
          "logical expiry delivered once, without second two-second grace");
    value(step(earlier, 5000));
    check(earlier.capture.snapshot().progress_ms == 3000, "decay starts at logical expiry, not physical loss");
    Pair reactivation{delay(), attempt()}; value(step(reactivation, 1000, ABSENT));
    value(step(reactivation, 2500, PRESENT)); value(step(reactivation, 5000));
    check(reactivation.capture.snapshot().completed, "reacquisition within grace preserves logical capture continuity");
}
void invariance() {
    auto long_a = delay(); auto split_a = delay();
    value(long_a.apply(1000, ABSENT)); value(split_a.apply(1000, ABSENT));
    value(long_a.apply(7000));
    for (auto t : {1500, 2500, 2999, 3000, 3001, 6000, 7000}) value(split_a.apply(t));
    check(long_a.snapshot() == split_a.snapshot() && long_a.trace() == split_a.trace(),
          "activation interval partition invariance including exact expiry");
    auto long_c = attempt(); auto split_c = attempt();
    auto l = value(long_c.apply(6000)); value(split_c.apply(1000)); value(split_c.apply(4999));
    auto s = value(split_c.apply(6000));
    check(long_c.snapshot() == split_c.snapshot() && long_c.trace() == split_c.trace() &&
          l.unconsumed_ms == s.unconsumed_ms && l.unconsumed_ms == 1000,
          "capture interval partition invariant state/trace/explicit residual");
    for (int repetition = 0; repetition < 30; ++repetition) {
        auto coarse = attempt(); auto fine = attempt();
        const auto timed_input = [&](std::int64_t t, LogicalActivation input) {
            if (fine.snapshot().time_ms + 1 < t) value(fine.apply(fine.snapshot().time_ms + 1));
            if (fine.snapshot().time_ms + 47 < t) value(fine.apply(fine.snapshot().time_ms + 47));
            value(coarse.apply(t, input)); value(fine.apply(t, input));
        };
        timed_input(2000, NONE); timed_input(4500, BLUE); // decay passes through zero at 4000.
        timed_input(5500, BOTH); timed_input(9000, BLUE); // contest pause.
        value(coarse.apply(14000)); value(fine.apply(10000)); value(fine.apply(12999));
        value(fine.apply(14000));
        check(coarse.snapshot() == fine.snapshot() && coarse.trace() == fine.trace(),
              "repeated complex scenario deterministic and partition invariant");
    }
    // A pure advance closes the timestamp; an equally timed input can never be
    // used as a second phase to bypass strict coincidence detection.
    auto closed = attempt(); value(closed.apply(5000));
    atomic_error(closed, [&] { return closed.apply(5000, NONE); }, Error::TimeNotIncreasing);
    auto simultaneous = attempt();
    atomic_error(simultaneous, [&] { return simultaneous.apply(5000, BOTH); }, Error::UnadjudicatedCoincidence);
}
}
int main() {
    mappings(); activation(); capture(); composition(); invariance();
    std::cout << "capture rule checks passed: " << checks << '\n';
}
