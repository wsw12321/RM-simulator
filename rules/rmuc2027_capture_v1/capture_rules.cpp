#include "capture_rules.hpp"
#include <limits>
#include <utility>

namespace rmuc2027::capture_v1 {
namespace {
bool valid(Team t) { return t == Team::Red || t == Team::Blue; }
bool valid(BoundaryPolicy p) {
    return p == BoundaryPolicy::StrictRejectUnadjudicated ||
           p == BoundaryPolicy::EngineeringTimerBeforeInputV1;
}
bool valid(QualifiedDetection d) {
    return d == QualifiedDetection::Present || d == QualifiedDetection::Absent;
}
Team opponent(Team t) { return t == Team::Red ? Team::Blue : Team::Red; }
std::optional<Error> validate(PositionControl p) {
    if (!valid(p.original_owner) || !valid(p.controller)) return Error::InvalidTeam;
    if (p.kind != PositionKind::Outpost && p.kind != PositionKind::Base)
        return Error::InvalidPositionKind;
    if (p.degree != ControlDegree::Full && p.degree != ControlDegree::Temporary)
        return Error::InvalidControlDegree;
    if (p.kind == PositionKind::Base && p.degree == ControlDegree::Full &&
        p.controller != p.original_owner) return Error::InvalidEnemyFullBaseControl;
    return std::nullopt;
}
std::optional<Error> check_time(std::int64_t t, std::int64_t previous) {
    if (t < 0) return Error::NegativeTime;
    if (t <= previous) return Error::TimeNotIncreasing;
    return std::nullopt;
}
bool both(LogicalActivation a) { return a.red && a.blue; }
bool neither(LogicalActivation a) { return !a.red && !a.blue; }
bool active(LogicalActivation a, Team t) { return t == Team::Red ? a.red : a.blue; }
}

std::string_view error_name(Error e) {
    switch (e) {
#define ERR(x) case Error::x: return #x
        ERR(InvalidTeam); ERR(InvalidPositionKind); ERR(InvalidControlDegree);
        ERR(InvalidEnemyFullBaseControl); ERR(InvalidPolicy); ERR(InvalidDetection);
        ERR(InvalidFact); ERR(NegativeTime); ERR(TimeNotIncreasing);
        ERR(IntegerOverflow); ERR(RedundantInput); ERR(UnadjudicatedCoincidence);
        ERR(UnadjudicatedContestExitToNone); ERR(UnsupportedTemporaryTiming);
        ERR(UnsupportedTransition); ERR(AttemptAlreadyCompleted);
        ERR(InputBeyondCaptureEndpoint);
#undef ERR
    }
    return "UnknownError";
}
std::string_view policy_name(BoundaryPolicy p) {
    switch (p) {
        case BoundaryPolicy::StrictRejectUnadjudicated:
            return "strict-reject-unadjudicated-v1";
        case BoundaryPolicy::EngineeringTimerBeforeInputV1:
            return "ENGINEERING-NOT-OFFICIAL-timer-before-input-v1";
    }
    return "INVALID-POLICY";
}
Result<PositionControl> initial_control(PositionKind k, Team owner) {
    PositionControl p{k, owner, owner, ControlDegree::Full};
    if (auto e = validate(p)) return *e;
    return p;
}
Result<MappedTransition> map_qualified_transition(PositionControl p, QualifiedFact f) {
    if (auto e = validate(p)) return *e;
    if (!valid(f.actor)) return Error::InvalidTeam;
    MappedTransition r{p, p, f, {}, false};
    switch (f.kind) {
        case FactKind::AdjudicatedCaptureThresholdReached:
            if (p.degree != ControlDegree::Full || f.actor == p.controller)
                return Error::UnsupportedTransition;
            r.after.controller = f.actor;
            r.after.degree = ControlDegree::Temporary;
            r.source_row = "control.capture_transitions/table5-21/row1";
            break;
        case FactKind::TemporaryControllerAbsentContinuouslyForFiveSeconds:
            if (p.degree != ControlDegree::Temporary || f.actor != opponent(p.controller))
                return Error::UnsupportedTransition;
            r.after.controller = f.actor;
            r.after.degree = ControlDegree::Full;
            r.source_row = "control.capture_transitions/table5-21/row2";
            r.requires_external_outpost_lifecycle_adjudication =
                p.kind == PositionKind::Outpost;
            break;
        case FactKind::ActorDestroyedOutpost:
            if (p.kind != PositionKind::Outpost) return Error::UnsupportedTransition;
            if (p.degree == ControlDegree::Full && f.actor != p.controller) {
                r.after.degree = ControlDegree::Temporary;
                r.source_row = "control.capture_transitions/table5-21/row3";
            } else if (p.degree == ControlDegree::Temporary && f.actor == p.controller) {
                r.after.degree = ControlDegree::Full;
                r.source_row = "control.capture_transitions/table5-21/row4";
            } else return Error::UnsupportedTransition;
            r.requires_external_outpost_lifecycle_adjudication = true;
            break;
        default: return Error::InvalidFact;
    }
    if (auto e = validate(r.after)) return *e;
    return r;
}

ActivationDelay::ActivationDelay(ActivationSnapshot s, BoundaryPolicy p)
    : state_(s), policy_(p) { record(ActivationTraceKind::Initialized, "buff.activation_latency"); }
void ActivationDelay::record(ActivationTraceKind k, std::string_view rule) {
    trace_.push_back({state_.time_ms, k, state_, rule, policy_name(policy_)});
}
Result<ActivationDelay> ActivationDelay::start(std::int64_t t, QualifiedDetection d, BoundaryPolicy p) {
    if (t < 0) return Error::NegativeTime;
    if (!valid(d)) return Error::InvalidDetection;
    if (!valid(p)) return Error::InvalidPolicy;
    return ActivationDelay{{t, d, d == QualifiedDetection::Present, std::nullopt}, p};
}
Result<ActivationSnapshot> ActivationDelay::apply(std::int64_t t, std::optional<QualifiedDetection> edge) {
    if (auto e = check_time(t, state_.time_ms)) return *e;
    if (edge && !valid(*edge)) return Error::InvalidDetection;
    if (edge && *edge == state_.detection) return Error::RedundantInput;
    const bool coincidence = edge && state_.deactivation_at_ms && *state_.deactivation_at_ms == t;
    if (coincidence && policy_ == BoundaryPolicy::StrictRejectUnadjudicated)
        return Error::UnadjudicatedCoincidence;
    // Check before any state/trace update, even if an earlier pending expiry was due.
    if (edge == QualifiedDetection::Absent && t > std::numeric_limits<std::int64_t>::max() - DEACTIVATION_DELAY_MS)
        return Error::IntegerOverflow;
    auto next = *this;
    if (next.state_.deactivation_at_ms && *next.state_.deactivation_at_ms <= t) {
        next.state_.time_ms = *next.state_.deactivation_at_ms;
        if (coincidence) next.record(ActivationTraceKind::EngineeringOrderApplied, "MODEL-02");
        next.state_.deactivation_at_ms.reset();
        next.state_.logically_active = false;
        next.record(ActivationTraceKind::GraceExpired, "buff.activation_latency");
    }
    next.state_.time_ms = t;
    if (edge) {
        next.state_.detection = *edge;
        if (*edge == QualifiedDetection::Present) {
            const bool pending = next.state_.deactivation_at_ms.has_value();
            next.state_.logically_active = true;
            next.state_.deactivation_at_ms.reset();
            next.record(pending ? ActivationTraceKind::GraceCancelledByReacquisition :
                                 ActivationTraceKind::DetectionAcquired,
                        "buff.activation_latency");
        } else {
            next.state_.deactivation_at_ms = t + DEACTIVATION_DELAY_MS;
            next.record(ActivationTraceKind::OrdinaryDetectionLost, "buff.activation_latency");
        }
    }
    *this = std::move(next);
    return state_;
}

CaptureAttempt::CaptureAttempt(CaptureSnapshot s, BoundaryPolicy p)
    : state_(s), policy_(p) { record(CaptureTraceKind::Initialized, "control.positions"); }
void CaptureAttempt::record(CaptureTraceKind k, std::string_view rule) {
    trace_.push_back({state_.time_ms, k, state_, rule, policy_name(policy_)});
}
Result<CaptureAttempt> CaptureAttempt::start(std::int64_t t, PositionControl c,
                                            LogicalActivation a, BoundaryPolicy p) {
    if (t < 0) return Error::NegativeTime;
    if (auto e = validate(c)) return *e;
    if (!valid(p)) return Error::InvalidPolicy;
    if (c.degree != ControlDegree::Full) return Error::UnsupportedTemporaryTiming;
    return CaptureAttempt{{t, c, a, 0, false}, p};
}
Result<CaptureAdvance> CaptureAttempt::apply(std::int64_t t, std::optional<LogicalActivation> input) {
    if (auto e = check_time(t, state_.time_ms)) return *e;
    if (state_.completed) return Error::AttemptAlreadyCompleted;
    if (input && *input == state_.active) return Error::RedundantInput;
    // Shared table note says pause until exactly one side remains. No official
    // rule resolves going directly from both to neither, so reject this path.
    if (input && both(state_.active) && neither(*input))
        return Error::UnadjudicatedContestExitToNone;
    const auto duration = t - state_.time_ms; // Both nonnegative, ordered int64.
    const Team challenger = opponent(state_.control.controller);
    const bool progressing = !both(state_.active) && active(state_.active, challenger);
    const auto needed = CAPTURE_DURATION_MS - state_.progress_ms;
    const bool completes = progressing && duration >= needed;
    const bool coincidence = completes && duration == needed && input.has_value();
    if (completes && duration > needed && input) return Error::InputBeyondCaptureEndpoint;
    if (coincidence && policy_ == BoundaryPolicy::StrictRejectUnadjudicated)
        return Error::UnadjudicatedCoincidence;
    auto next = *this;
    if (completes) {
        // needed <= t-old_time guarantees this sum cannot overflow.
        next.state_.time_ms += needed;
        next.state_.progress_ms = CAPTURE_DURATION_MS;
        if (coincidence) next.record(CaptureTraceKind::EngineeringOrderApplied,
                                    "control.deactivation_capture_boundary;MODEL-02");
        // Retained progress can meet the threshold without an uninterrupted
        // final 5 s stretch. Apply row 1 WITH its shared partial-progress note;
        // do not fabricate a continuous-activation fact for the pure mapper.
        next.state_.control.controller = challenger;
        next.state_.control.degree = ControlDegree::Temporary;
        next.state_.completed = true;
        next.record(CaptureTraceKind::CapturedTemporarily,
                    "control.capture_transitions/table5-21/row1");
    } else {
        if (progressing) next.state_.progress_ms += duration; // < 5000.
        else if (!both(state_.active) && next.state_.progress_ms > 0) {
            // Defender-only and unoccupied states both mean challenger absent.
            if (duration >= next.state_.progress_ms) {
                next.state_.time_ms += next.state_.progress_ms;
                next.state_.progress_ms = 0;
                next.record(CaptureTraceKind::ProgressReachedZero, "control.capture_progress");
            } else next.state_.progress_ms -= duration;
        }
        next.state_.time_ms = t;
    }
    if (input) {
        next.state_.active = *input;
        next.record(CaptureTraceKind::LogicalInputChanged, "control.capture_progress");
    }
    const auto remainder = t - next.state_.time_ms;
    *this = std::move(next);
    return CaptureAdvance{state_, t, remainder};
}
}
