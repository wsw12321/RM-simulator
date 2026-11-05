#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

// Source-linked arithmetic for externally SETTLED referee deductions. This is
// not a collision classifier, damage calculator, penalty scheduler or HP engine.
namespace rmuc2027::damage_v1 {
inline constexpr std::string_view MODULE_VERSION = "rmuc2027-damage-v1.0.0-module.1";
inline constexpr std::string_view SOURCE_VERSION = "RMUC2027_CN_V1.0.0_20261015";
inline constexpr std::string_view SOURCE_SHA256 = "9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be";
enum class Team : std::uint8_t { Red, Blue };
enum class EntityKind : std::uint8_t { Robot, Base, Outpost };
enum class Cause : std::uint8_t {
    Projectile, Dart, AerialImpact, ViolationPenalty, ArmorCollision,
    ModuleOffline, Unadjudicated
};
enum class Error {
    InvalidTeam, InvalidEntityKind, InvalidCause, NegativeValue,
    HpIncreased, MissingAttackCredit, ConflictingAttackCredit,
    UnadjudicatedCause, InvalidSequence, TimeRegression, IntegerOverflow,
    LedgerSealed, UnsupportedCauseForEntity
};
std::string_view error_name(Error);
template<class T> using Result = std::variant<T,Error>;
struct SettledDeduction {
    std::uint64_t sequence;
    std::int64_t time_ms;
    std::uint32_t target_id;
    Team target_team;
    EntityKind target_kind;
    Cause cause;
    // Already rounded, clamped and ordered by the upstream adjudicator. These
    // values describe ONE deduction, not gross HP change containing healing.
    std::int64_t hp_before;
    std::int64_t hp_after;
    // Required for projectile/dart/aerial impact. No friendly-fire attribution
    // is invented. Penalties derive victim's opponent from the explicit rule;
    // if supplied there this recipient must match. Exclusions require nullopt.
    std::optional<Team> attack_credit_team;
};
struct Settlement {
    std::int64_t actual_hp_loss;
    std::int64_t attack_damage_credit;
    std::optional<Team> credited_team;
    bool excluded;
    bool penalty;
};
Result<Settlement> settle(const SettledDeduction&);
struct Totals {
    std::array<std::int64_t,2> attack_damage{}; // indexed by credited team
    std::array<std::int64_t,2> actual_hp_loss{}; // indexed by victim team
    std::array<std::int64_t,2> excluded_hp_loss{}; // indexed by victim team
    std::array<std::int64_t,2> penalty_attack_damage{}; // indexed by credited team
    bool operator==(const Totals&) const = default;
};
class Ledger {
public:
    Result<Settlement> apply(const SettledDeduction&);
    Result<std::vector<Settlement>> apply_batch(std::span<const SettledDeduction>);
    const Totals& totals() const noexcept { return totals_; }
    std::uint64_t last_sequence() const noexcept { return sequence_; }
    std::int64_t last_time_ms() const noexcept { return time_ms_; }
    bool sealed() const noexcept { return sealed_; }
    void seal() noexcept { sealed_=true; }
private:
    Totals totals_{};
    std::uint64_t sequence_{};
    std::int64_t time_ms_{-1};
    bool sealed_{};
};
} // namespace rmuc2027::damage_v1
