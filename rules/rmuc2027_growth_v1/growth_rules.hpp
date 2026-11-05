#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

// Source-linked robot performance, level and experience arithmetic.
// RMUC 2027 V1.0.0 §3.1 (printed pp22-26), §5.4.3 table 5-12 (p83),
// §5.5 tables 5-14..5-18 (pp89-94). Pure functions; no clock, physics or referee.
namespace rmuc2027::growth_v1 {
inline constexpr std::string_view MODULE_VERSION = "rmuc2027-growth-v1.0.0-module.1";
inline constexpr std::string_view SOURCE_VERSION = "RMUC2027_CN_V1.0.0_20261015";
inline constexpr std::string_view SOURCE_SHA256 = "9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be";
inline constexpr std::int64_t MAX_LEVEL = 10;

enum class RobotClass : std::uint8_t { Heavy, Infantry, SentryAuto, SentrySemi };
// §5.5.2: chosen before the round; defaults when not chosen.
enum class Chassis : std::uint8_t { PowerFirst, HpFirst };
enum class Launcher : std::uint8_t { BurstFirst, CoolingFirst };
inline constexpr Chassis DEFAULT_CHASSIS = Chassis::HpFirst;
inline constexpr Launcher DEFAULT_LAUNCHER = Launcher::CoolingFirst;
// Table 5-16 rows. Armed = third assembly completed OR modular 42 mm installed.
enum class HeavyStage : std::uint8_t { Default, Assembly1, Assembly2, Armed };
enum class Caliber : std::uint8_t { None, Mm17, Mm42 };
enum class TargetKind : std::uint8_t { Robot, Outpost, Base };
enum class Error {
    InvalidClass, InvalidChoice, InvalidLevel, NegativeValue, IntegerOverflow,
    NoRecipients, NotExperienceRobot, UnadjudicatedNonProjectileKiller
};
std::string_view error_name(Error);
template<class T> using Result = std::variant<T, Error>;

struct Loadout {
    RobotClass robot_class = RobotClass::Infantry;
    Chassis chassis = DEFAULT_CHASSIS;      // infantry only
    Launcher launcher = DEFAULT_LAUNCHER;   // infantry only
    HeavyStage heavy_stage = HeavyStage::Default; // heavy only
};
struct Performance {
    std::int64_t max_hp = 0;
    std::int64_t chassis_power_w = 0;
    Caliber caliber = Caliber::None;
    // Absent for a heavy robot without an installed modular launcher.
    std::optional<std::int64_t> heat_limit;
    std::optional<std::int64_t> cooling_per_second;
    std::optional<std::int64_t> muzzle_speed_limit_mm_s;
    bool operator==(const Performance&) const = default;
};
// Level is ignored for heavy/sentry (they do not use the experience system).
Result<Performance> performance(const Loadout&, std::int64_t level);
// §3.1 tables 3-3/3-5/3-6: initial allowed rounds at round start.
Result<std::int64_t> initial_allowance(RobotClass);
bool uses_experience(RobotClass);          // §5.5.1: infantry only
std::int64_t kill_level(RobotClass, std::int64_t level); // sentry counts as1 (p89)

// Table 5-15. Experience is kept in tenths because shared awards round to0.1.
std::int64_t experience_for_level_tenths(std::int64_t level); // 0 for invalid
Result<std::int64_t> level_for_experience_tenths(std::int64_t experience_tenths,
                                                std::int64_t level_cap = MAX_LEVEL);
// Table 5-12: team level cap before assembly2 is5, after assembly2 is7,
// after assembly3 is10. `highest_assembly` is 0..4 completed difficulty.
Result<std::int64_t> team_level_cap(std::int64_t highest_assembly);
// Heavy robot level after first completion of an assembly difficulty.
Result<std::int64_t> heavy_level(std::int64_t highest_assembly);
struct AssemblyReward {
    std::int64_t heavy_level = 1;
    std::int64_t team_level_cap = 5;
    std::int64_t team_defense_percent = 0;     // ground robots, outpost, base
    std::int64_t gold_per_10s = 0;             // first-completion stream
    bool unlocks_42mm = false;
};
// First-completion benefits of difficulty d (1..4). Repeat completions are
// separate small streams; see repeat_gold_per_10s.
Result<AssemblyReward> first_assembly_reward(std::int64_t difficulty);
Result<std::int64_t> repeat_gold_per_10s(std::int64_t difficulty); // d4: repeat unsupported -> error

// Table 5-14 experience arithmetic, all in tenths.
std::int64_t shot_experience_tenths(RobotClass, std::int64_t launched_rounds);
Result<std::int64_t> damage_experience_tenths(TargetKind, std::int64_t settled_damage);
Result<std::int64_t> kill_experience_tenths(std::int64_t victim_level, std::int64_t killer_level);
inline constexpr std::int64_t TERRAIN_FIRST_CROSSING_TENTHS = 3000; // 300 exp
// Equal split rounded half-up to0.1 (p89 example: 4*120/2=240).
Result<std::int64_t> split_tenths(std::int64_t total_tenths, std::int64_t recipients);
enum class KillerPolicy : std::uint8_t {
    StrictRejectUnadjudicated,
    // Use the same average-level rule for non-projectile kills among living infantry.
    EngineeringAverageLevelForNonProjectileV1
};
struct UnattributedKill {
    std::int64_t victim_level = 1;
    bool projectile = true;
    // Experience totals (tenths) of every eligible living opposing recipient.
    std::span<const std::int64_t> recipient_experience_tenths;
    std::int64_t level_cap = MAX_LEVEL;
    KillerPolicy policy = KillerPolicy::StrictRejectUnadjudicated;
};
// p90 example two: avg exp 2625 -> level5; victim9 -> 50*9*1.8/2 = 405 each.
Result<std::int64_t> unattributed_kill_share_tenths(const UnattributedKill&);
// §5.5.2: a max-HP increase from levelling is added to current HP immediately.
Result<std::int64_t> hp_after_max_change(std::int64_t current_hp, std::int64_t old_max, std::int64_t new_max);
} // namespace rmuc2027::growth_v1
