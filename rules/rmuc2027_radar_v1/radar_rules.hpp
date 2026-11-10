#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <variant>

// Source-linked radar marking arithmetic: the per-robot "marked progress" P
// (table 5-29), its vulnerability thresholds (table 5-30), the "double
// vulnerability" charge and the jammer difficulty comparison.
// RMUC 2027 V1.0.0 §5.7.4 (printed pp112-114).
// Input is already adjudicated: each received coordinate is classified by its
// distance to the true position. It owns no vision, positioning or radio model.
namespace rmuc2027::radar_v1 {
inline constexpr std::string_view MODULE_VERSION = "rmuc2027-radar-v1.0.0-module.1";
inline constexpr std::string_view SOURCE_VERSION = "RMUC2027_CN_V1.0.0_20261015";
inline constexpr std::string_view SOURCE_SHA256 = "9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be";
using I = std::int64_t;

// P and x are kept in tenths: every step of table 5-29 is a multiple of 0.1.
inline constexpr I PROGRESS_MAX_TENTHS = 1500;
inline constexpr I ACCURATE_BELOW_MM = 800;      // d < 0.8 m
inline constexpr I SEMI_BELOW_MM = 1600;         // 0.8 m <= d < 1.6 m
inline constexpr I INTERRUPTION_MS = 500;        // each 0.5 s without data = one "wrong"
inline constexpr I DOUBLE_CHARGE_MS = 60000;     // one chance per minute of enemy vulnerability
inline constexpr I DOUBLE_DURATION_MS = 30000;
inline constexpr I DOUBLE_MAX_TRIGGERS = 2;      // per round
inline constexpr I JAMMER_MAX_LEVEL = 3;         // "三个难度等级"

enum class Mark : std::uint8_t { Accurate, SemiAccurate, Wrong };
enum class Last : std::uint8_t { None, Good, Bad }; // accurate-or-semi / wrong-or-interrupted
enum class Team : std::uint8_t { Red, Blue };
enum class Error { InvalidChoice, NegativeValue, NonIncreasingTime, NoChance, AlreadyActive };
std::string_view error_name(Error);
template<class T> using Result = std::variant<T, Error>;

// d = distance between the sent coordinate and the robot's real plane position.
Result<Mark> classify(I error_mm);
// Table 5-30, opposing ground robots: [0,100) 0, [100,120) 15, [120,150] 20.
I mark_vulnerability_percent(I progress_tenths);
// Radar vulnerability actually applied. `doubled`: a double-vulnerability
// period is running. `effective`: the marker's radar is not suppressed by the
// jammer difficulty comparison (see Jammers).
I vulnerability_percent(I progress_tenths, bool doubled, bool effective);

struct MarkSnapshot {
    I time_ms = 0;
    I progress_tenths = 0;    // P in [0, 1500]
    I influence_tenths = 0;   // x
    Last last = Last::None;
    I silence_from_ms = 0;    // last received data (or round start / last interruption verdict)
    I reports = 0, interruptions = 0;
    bool operator==(const MarkSnapshot&) const = default;
};
// One marked robot. Interruption verdicts fall due exactly every 0.5 s after
// the last data and are settled BEFORE a report of the same millisecond
// (ENGINEERING timer-before-input, as elsewhere in this repository).
class MarkProgress {
public:
    Result<MarkSnapshot> advance(I t);
    Result<MarkSnapshot> report(I t, Mark);
    // p113: after revival P is kept and the current x is reset to 0.
    Result<MarkSnapshot> revived(I t);
    const MarkSnapshot& snapshot() const noexcept { return state_; }
private:
    MarkSnapshot state_;
    static void apply(MarkSnapshot&, Mark);
    static void settle(MarkSnapshot&, I t);
};

struct DoubleSnapshot {
    I time_ms = 0;
    I charge_ms = 0;          // accumulated enemy-vulnerable time toward the next chance
    I chances = 0, triggers = 0;
    I active_until_ms = -1;   // exclusive
    bool operator==(const DoubleSnapshot&) const = default;
    bool active(I t) const { return active_until_ms > t; }
};
// One team's radar. Chances earned plus triggers used never exceed two
// (ENGINEERING reading of "至多可以触发 2 次").
class DoubleVulnerability {
public:
    // (last, t] passed; `enemy_vulnerable` says whether at least one opposing
    // robot held an effective radar vulnerability during it (several robots do
    // not add up, p113).
    Result<DoubleSnapshot> advance(I t, bool enemy_vulnerable);
    // Radar command: spend one chance, doubling radar vulnerability for 30 s.
    Result<DoubleSnapshot> trigger(I t);
    const DoubleSnapshot& snapshot() const noexcept { return state_; }
private:
    DoubleSnapshot state_;
};

// Jammer difficulty (p114). level(team) is the difficulty of the jammer placed
// on `team`'s own radar base, i.e. the wave carrying the opponent's key that
// `team`'s radar decodes; it starts at 1 and rises by one per successful decode.
// ENGINEERING reading of "当对方干扰波难度高于己方": a team that has decoded
// further than its opponent is not made vulnerable by the opponent's radar.
class Jammers {
public:
    I level(Team t) const noexcept { return level_[t == Team::Blue ? 1 : 0]; }
    // `decoder`'s radar submitted the correct key. Saturates at level 3.
    I decoded(Team decoder) noexcept;
    // Do marks sent by `marker`'s radar produce vulnerability / double-vulnerability progress?
    bool marks_effective(Team marker) const noexcept {
        return level(marker == Team::Red ? Team::Blue : Team::Red) <= level(marker);
    }
private:
    std::array<I, 2> level_{1, 1};
};
} // namespace rmuc2027::radar_v1
