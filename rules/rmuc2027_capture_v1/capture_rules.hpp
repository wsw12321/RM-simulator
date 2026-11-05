#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

// Independent source-linked subset. No dependency on the sandbox, match judge,
// or scenario harness. Times are exact supplied milliseconds, not sensor samples.
namespace rmuc2027::capture_v1 {
inline constexpr std::string_view MODULE_VERSION = "rmuc2027-capture-v1.0.0-module.1";
inline constexpr std::string_view SOURCE_VERSION = "RMUC2027_CN_V1.0.0_20261015";
inline constexpr std::string_view SOURCE_SHA256 = "9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be";
inline constexpr std::int64_t DEACTIVATION_DELAY_MS = 2000; // §5.6.4.1 p102.
inline constexpr std::int64_t CAPTURE_DURATION_MS = 5000; // Table 5-21 p96.

enum class Team : std::uint8_t { Red, Blue };
enum class PositionKind : std::uint8_t { Outpost, Base };
enum class ControlDegree : std::uint8_t { Full, Temporary };
struct PositionControl {
    PositionKind kind;
    Team original_owner; // Immutable position owner, NOT outpost-object ownership.
    Team controller;
    ControlDegree degree;
    bool operator==(const PositionControl&) const = default;
};
enum class Error : std::uint8_t {
    InvalidTeam, InvalidPositionKind, InvalidControlDegree,
    InvalidEnemyFullBaseControl, InvalidPolicy, InvalidDetection, InvalidFact,
    NegativeTime, TimeNotIncreasing, IntegerOverflow, RedundantInput,
    UnadjudicatedCoincidence, UnadjudicatedContestExitToNone,
    UnsupportedTemporaryTiming, UnsupportedTransition,
    AttemptAlreadyCompleted, InputBeyondCaptureEndpoint
};
std::string_view error_name(Error);
template<class T> using Result = std::variant<T, Error>;

// Engineering policy, never an official referee ordering. Default rejects an
// input at the exact deactivation-expiry or capture-completion deadline.
// Reaching the clamped zero-progress floor is commuting, not an ambiguity.
// Opt-in is per instance.
enum class BoundaryPolicy : std::uint8_t {
    StrictRejectUnadjudicated,
    EngineeringTimerBeforeInputV1
};
std::string_view policy_name(BoundaryPolicy);

// Pure table mapper. Facts MUST already be adjudicated by the caller; the mapper
// does not prove their durations, eligible actors, hit attribution or lifecycle.
enum class FactKind : std::uint8_t {
    // Adjudicated row 1 threshold INCLUDING the shared contest/partial-progress
    // note. Five seconds of actor detection alone is NOT sufficient evidence.
    AdjudicatedCaptureThresholdReached,
    TemporaryControllerAbsentContinuouslyForFiveSeconds,
    ActorDestroyedOutpost
};
struct QualifiedFact {
    FactKind kind;
    Team actor; // Gaining side for absence; destroyer for destruction.
};
struct MappedTransition {
    PositionControl before;
    PositionControl after;
    QualifiedFact fact;
    std::string_view source_row;
    // No output certifies a live/rebuilt outpost. Destruction and outpost
    // restoration need external lifecycle adjudication before further use.
    bool requires_external_outpost_lifecycle_adjudication;
};
Result<PositionControl> initial_control(PositionKind, Team original_owner);
Result<MappedTransition> map_qualified_transition(PositionControl, QualifiedFact);

// ONE already-qualified detector track. This is not position-level aggregation,
// raw physical occupancy, an eligibility check, a death or forced-invalidation
// adapter. Acquire means detection has ALREADY happened. No detection lag invented.
enum class QualifiedDetection : std::uint8_t { Present, Absent };
struct ActivationSnapshot {
    std::int64_t time_ms;
    QualifiedDetection detection;
    bool logically_active;
    std::optional<std::int64_t> deactivation_at_ms;
    bool operator==(const ActivationSnapshot&) const = default;
};
enum class ActivationTraceKind : std::uint8_t {
    Initialized, DetectionAcquired, OrdinaryDetectionLost,
    GraceCancelledByReacquisition, GraceExpired, EngineeringOrderApplied
};
struct ActivationTrace {
    std::int64_t time_ms;
    ActivationTraceKind kind;
    ActivationSnapshot after;
    std::string_view rule_id;
    std::string_view policy;
    bool operator==(const ActivationTrace&) const = default;
};
class ActivationDelay {
public:
    static Result<ActivationDelay> start(std::int64_t start_ms,
        QualifiedDetection initial,
        BoundaryPolicy = BoundaryPolicy::StrictRejectUnadjudicated);
    // A call closes timestamp t, even if it has no edge. Later calls must use
    // strictly later t. This prevents advance(t); edge(t) bypassing strict mode.
    // Failure leaves snapshot and trace unchanged. Equal-state edges are invalid.
    Result<ActivationSnapshot> apply(std::int64_t t,
        std::optional<QualifiedDetection> edge = std::nullopt);
    const ActivationSnapshot& snapshot() const noexcept { return state_; }
    const std::vector<ActivationTrace>& trace() const noexcept { return trace_; }
    BoundaryPolicy policy() const noexcept { return policy_; }
private:
    ActivationDelay(ActivationSnapshot, BoundaryPolicy);
    void record(ActivationTraceKind, std::string_view);
    ActivationSnapshot state_;
    BoundaryPolicy policy_;
    std::vector<ActivationTrace> trace_;
};

// Already-qualified position-level LOGICAL states, with the upstream 2 s grace
// already applied exactly once. No robot/card aggregation is provided here.
struct LogicalActivation {
    bool red;
    bool blue;
    bool operator==(const LogicalActivation&) const = default;
};
struct CaptureSnapshot {
    std::int64_t time_ms; // Last consumed time, NOT an unconsumed request horizon.
    PositionControl control;
    LogicalActivation active;
    std::int64_t progress_ms; // [0, 5000], progress of opponent of full controller.
    bool completed;
    bool operator==(const CaptureSnapshot&) const = default;
};
enum class CaptureTraceKind : std::uint8_t {
    Initialized, LogicalInputChanged, ProgressReachedZero,
    CapturedTemporarily, EngineeringOrderApplied
};
struct CaptureTrace {
    std::int64_t time_ms;
    CaptureTraceKind kind;
    CaptureSnapshot after;
    std::string_view rule_id;
    std::string_view policy;
    bool operator==(const CaptureTrace&) const = default;
};
struct CaptureAdvance {
    CaptureSnapshot state;
    std::int64_t requested_time_ms;
    // If nonzero, caller MUST route [state.time_ms, requested_time_ms] elsewhere.
    // This module never claims to consume time beyond temporary capture.
    std::int64_t unconsumed_ms;
};
class CaptureAttempt {
public:
    // Only an already-adjudicated Full state is accepted. Its opponent is the
    // sole challenger for this attempt. Temporary-state timing is unsupported.
    static Result<CaptureAttempt> start(std::int64_t start_ms,
        PositionControl full_control, LogicalActivation initial,
        BoundaryPolicy = BoundaryPolicy::StrictRejectUnadjudicated);
    // Whole-position snapshots are already adjudicated atomic logical inputs.
    // No same-time sequence, arbitrary entity iteration, sensor sampling or
    // pending extra grace exists here. All rejected calls are atomic.
    // A no-input advance may end early at capture and returns unconsumed_ms.
    // An input beyond that endpoint is rejected rather than dropped. At an exact
    // endpoint, only explicit TimerBeforeInputV1 can commit input after capture.
    Result<CaptureAdvance> apply(std::int64_t t,
        std::optional<LogicalActivation> input = std::nullopt);
    const CaptureSnapshot& snapshot() const noexcept { return state_; }
    const std::vector<CaptureTrace>& trace() const noexcept { return trace_; }
    BoundaryPolicy policy() const noexcept { return policy_; }
private:
    CaptureAttempt(CaptureSnapshot, BoundaryPolicy);
    void record(CaptureTraceKind, std::string_view);
    CaptureSnapshot state_;
    BoundaryPolicy policy_;
    std::vector<CaptureTrace> trace_;
};
}
