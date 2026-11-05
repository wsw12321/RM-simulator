#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

// Isolated, source-linked formula module. It is NOT wired into sandbox::Engine.
namespace rmuc2027::match_v1 {
inline constexpr std::string_view MODULE_VERSION = "rmuc2027-match-v1.0.0-module.1";
inline constexpr std::string_view SOURCE_VERSION = "RMUC2027_CN_V1.0.0_20261015";
inline constexpr std::string_view SOURCE_SHA256 = "9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be";
inline constexpr std::int64_t MATCH_DURATION_MS = 420000; // §6.6, printed p121.

enum class Team : std::uint8_t { Red, Blue };
enum class ControlDegree : std::uint8_t { Temporary, Full };
enum class Position : std::uint8_t { RedOutpost, BlueOutpost, RedBase, BlueBase };
struct PositionControl { Team controller; ControlDegree degree; };
struct ControlSnapshot {
    // Already-adjudicated ownership. Do not pass physical occupancy as control.
    std::array<PositionControl, 4> positions;
};
ControlSnapshot initial_control(); // §5.6.2, printed p96.
struct ControlRate {
    std::array<std::int64_t, 2> points_per_second{};
};
enum class Error {
    InvalidTeam, InvalidControlDegree, InvalidEnemyFullBaseControl,
    NegativeValue, InvalidElapsedTime, TimeRegression, AlreadyFinished,
    MissingControlScore, MissingAttackDamage, MissingSurvivingRobotHp,
    UnresolvedSimultaneousDestruction, UnsupportedRefereeRuling,
    DuplicateEntity, InvalidEntityKind, IntegerOverflow,
    InvalidCreditSequence, CreditAfterEnd
};
std::string_view error_name(Error);
template<class T> using Result = std::variant<T, Error>;
Result<ControlRate> control_rate(const ControlSnapshot&); // §5.6.2, printed p97.

// One explicitly resolved one-second credit, supplied by an external adapter.
// This module deliberately does not decide when a credit occurs. It provides no
// global-second/control-relative sampling, fractional accrual, capture or timing.
struct ExternalOneSecondCredit {
    std::uint64_t sequence;
    ControlSnapshot adjudicated_control;
};
class ExternalControlLedger {
public:
    Result<ControlRate> apply(const ExternalOneSecondCredit&);
    void seal() noexcept { sealed_ = true; }
    const std::array<std::int64_t, 2>& totals() const noexcept { return totals_; }
    std::uint64_t last_sequence() const noexcept { return sequence_; }
private:
    std::array<std::int64_t, 2> totals_{};
    std::uint64_t sequence_{};
    bool sealed_{};
};

enum class EntityKind : std::uint8_t { Robot, Base, Outpost };
struct RemainingHpFact {
    std::uint32_t entity_id;
    Team team;
    EntityKind kind;
    std::int64_t current_hp;
    bool main_controller_normally_connected;
    bool dismissed;
};
// Upstream supplies each scoring entity exactly once. Current HP, not max HP.
Result<std::array<std::int64_t, 2>> surviving_robot_hp(std::span<const RemainingHpFact>);

struct TeamTotals {
    std::int64_t base_hp;
    std::optional<std::int64_t> control_score;
    // Must be an externally settled §2 p19 ledger, not all HP deductions.
    std::optional<std::int64_t> attack_damage;
    std::optional<std::int64_t> surviving_robot_hp;
};
enum class RefereeRuling : std::uint8_t { None, RoundForfeit, Other };
struct MatchSnapshot {
    std::int64_t elapsed_ms;
    std::array<TeamTotals, 2> teams;
    RefereeRuling referee_ruling{RefereeRuling::None};
    // Only meaningful if both bases are zero. The module cannot decide whether
    // referee event ordering accepts the two destructions as simultaneous.
    bool simultaneous_destruction_resolved{false};
};
enum class Outcome : std::uint8_t { Ongoing, RedWins, BlueWins, Draw };
enum class Reason : std::uint8_t {
    NotEnded, BaseHp, TimeoutControl, TimeoutAttackDamage,
    TimeoutSurvivingRobotHp, TimeoutEqual, ResolvedBothBasesZero
};
struct Decision {
    Outcome outcome{Outcome::Ongoing};
    Reason reason{Reason::NotEnded};
    std::string_view rule_id;
};
Result<Decision> evaluate(const MatchSnapshot&); // §5.8, printed pp114–115.

// Atomic terminal latch for an already resolved boundary snapshot. It does not
// own physics, clocks, control accrual, capture, penalties or series scheduling.
class MatchJudge {
public:
    Result<Decision> update(const MatchSnapshot&);
    std::int64_t last_elapsed_ms() const noexcept { return last_elapsed_ms_; }
    const std::optional<Decision>& final_decision() const noexcept { return final_; }
    const std::optional<MatchSnapshot>& final_snapshot() const noexcept { return final_snapshot_; }
private:
    std::int64_t last_elapsed_ms_{-1};
    std::optional<Decision> final_;
    std::optional<MatchSnapshot> final_snapshot_;
};
}
