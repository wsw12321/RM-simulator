#pragma once
#include <cstdint>
#include <string_view>
#include <variant>

// Source-linked 17 mm heat arithmetic, not a referee detector or HP model.
namespace rmuc2027::heat_v1 {
inline constexpr std::string_view MODULE_VERSION = "rmuc2027-heat-v1.0.0-module.1";
inline constexpr std::string_view SOURCE_VERSION = "RMUC2027_CN_V1.0.0_20261015";
inline constexpr std::string_view SOURCE_SHA256 = "9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be";
inline constexpr std::int64_t SHOT_HEAT_TENTHS = 100;
inline constexpr std::int64_t PERMANENT_MARGIN_TENTHS = 1000;
inline constexpr std::int64_t COOLING_PERIOD_MS = 100;
enum class ThresholdPolicy : std::uint8_t {
    StrictRejectUnadjudicated, EngineeringProseStrictGreaterV1,
    EngineeringFlowchartGreaterEqualV1
};
enum class BoundaryPolicy : std::uint8_t {
    StrictRejectUnadjudicated, EngineeringCoolingBeforeShotV1,
    EngineeringShotBeforeCoolingV1
};
enum class DeathPolicy : std::uint8_t {
    StrictRejectCoincidentShot, EngineeringShotBeforeDeathResetV1
};
enum class Error {
    InvalidConfig, NegativeValue, NonIncreasingTime, IntegerOverflow,
    UnadjudicatedThresholdEquality, UnadjudicatedShotCoolingCoincidence,
    UnadjudicatedShotDeathCoincidence
};
std::string_view error_name(Error);
std::string_view policy_name(ThresholdPolicy);
std::string_view policy_name(BoundaryPolicy);
template<class T> using Result = std::variant<T,Error>;
struct Config {
    // Borrowed fixed infantry cooling-priority level1 values. Choosing this
    // fixture, an epoch of0, integer C, and tenths are engineering contracts.
    std::int64_t limit_tenths = 400;
    std::int64_t cooling_per_second = 12;
    ThresholdPolicy threshold = ThresholdPolicy::StrictRejectUnadjudicated;
    BoundaryPolicy boundary = BoundaryPolicy::StrictRejectUnadjudicated;
    DeathPolicy death = DeathPolicy::StrictRejectCoincidentShot;
};
struct Event {
    std::int64_t time_ms;
    // Already detected 17 mm rounds, even if an upstream hardware gate failed.
    // Never use attempted fire requests here. One batch per timestamp.
    std::int64_t detected_shots = 0;
    bool died = false;
};
struct Snapshot {
    std::int64_t time_ms = 0;
    std::int64_t heat_tenths = 0;
    bool overheat_locked = false;
    bool permanent_latched = false;
    std::int64_t detected_shots = 0;
    std::int64_t last_event_ms = -1;
    bool firing_locked() const { return overheat_locked || permanent_latched; }
    bool operator==(const Snapshot&) const = default;
};
class Heat {
public:
    static Result<Heat> start(Config = {});
    Result<Snapshot> apply(Event);
    const Snapshot& snapshot() const noexcept { return state_; }
    const Config& config() const noexcept { return config_; }
private:
    Config config_{};
    Snapshot state_{};
    explicit Heat(Config config) : config_(config) {}
};
} // namespace rmuc2027::heat_v1
