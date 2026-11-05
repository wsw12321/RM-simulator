#pragma once
#include "../rmuc2027_heat_v1/heat_rules.hpp"
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

// Source-linked launcher and chassis limits.
// RMUC 2027 V1.0.0 §5.1.2 table 5-3 (p66), §5.1.3 + figure 5-1 (pp66-67),
// §5.1.4 + figure 5-2 (pp67-68), §5.3.2 (p74), §5.7.2 table 5-28 (p109).
// The 17 mm heat arithmetic generalises rmuc2027_heat_v1 (kept unchanged) to
// 42 mm and to profile changes at level-up; a differential test pins equality.
namespace rmuc2027::limits_v1 {
inline constexpr std::string_view MODULE_VERSION = "rmuc2027-limits-v1.0.0-module.1";
inline constexpr std::string_view SOURCE_VERSION = "RMUC2027_CN_V1.0.0_20261015";
inline constexpr std::string_view SOURCE_SHA256 = "9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be";
using I = std::int64_t;
using ThresholdPolicy = heat_v1::ThresholdPolicy;

enum class Caliber : std::uint8_t { Mm17, Mm42 };
enum class Error { InvalidConfig, NegativeValue, NonIncreasingTime, IntegerOverflow, UnadjudicatedThresholdEquality, UnadjudicatedOverlap };
std::string_view error_name(Error);
template<class T> using Result = std::variant<T, Error>;

// ---- Muzzle speed over-limit (table 5-3). Speeds in mm/s.
struct SpeedPenalty { bool locked = false; bool permanent = false; I duration_ms = 0; };
Result<SpeedPenalty> speed_penalty(Caliber, I limit_mm_s, I measured_mm_s);
enum class OverlapPolicy : std::uint8_t {
    StrictRejectUnadjudicated, // a new violation during an active lock is unresolved ("时间不累加")
    EngineeringMaxDeadlineV1   // non-additive: lock ends at max(current end, now + duration)
};
class SpeedLock {
public:
    explicit SpeedLock(OverlapPolicy p = OverlapPolicy::StrictRejectUnadjudicated) : policy_(p) {}
    Result<SpeedPenalty> detect(I t, Caliber, I limit_mm_s, I measured_mm_s);
    bool locked(I t) const { return permanent_ || until_ms_ > t; }
    bool permanent() const { return permanent_; }
    I until_ms() const { return until_ms_; }
private:
    OverlapPolicy policy_;
    I until_ms_ = -1, last_ms_ = -1;
    bool permanent_ = false;
};

// ---- Chassis power buffer (§5.1.4). Q = 60 J, 10 Hz, 5 s cut-off when exhausted.
inline constexpr I BUFFER_MAX_UJ = 60'000'000;  // micro-joules
inline constexpr I POWER_PERIOD_MS = 100;
inline constexpr I POWER_CUTOFF_MS = 5000;
struct PowerSnapshot {
    I time_ms = 0, buffer_uj = BUFFER_MAX_UJ, cut_until_ms = -1, cutoffs = 0;
    bool operator==(const PowerSnapshot&) const = default;
    bool chassis_enabled(I t) const { return t >= cut_until_ms; }
};
class ChassisPower {
public:
    // One settlement per 100 ms boundary: draw/limit in milliwatts. While cut off,
    // the chassis is unpowered and the module settles a zero draw.
    Result<PowerSnapshot> settle(I t, I draw_mw, I limit_mw);
    void reset_on_death(I t);  // §5.3.2: buffer back to its maximum
    const PowerSnapshot& snapshot() const noexcept { return state_; }
private:
    PowerSnapshot state_;
};
// Limit composition: max(base*buff) then *scale; an override replaces it
// (enhanced move 200 W); instant revival doubles it, capped at 200 W.
struct PowerLimitInput {
    I base_w = 0;
    std::span<const I> buff_tenths;  // e.g. 15 / 12 (move posture)
    I scale_num = 1, scale_den = 1;  // e.g. 1/2 (attack/defence posture)
    I override_w = 0;
    bool instant_revival_boost = false;
};
Result<I> effective_power_limit_w(const PowerLimitInput&);

// ---- Launcher heat for 17 mm (+10, Q2=Q0+100) and 42 mm (+100, Q2=Q0+200).
// Fixed boundary semantics equal heat_v1's EngineeringCoolingBeforeShotV1 and
// EngineeringShotBeforeDeathResetV1; threshold equality policy is selectable.
struct HeatConfig {
    Caliber caliber = Caliber::Mm17;
    I limit = 40;               // Q0 in heat units
    I cooling_per_second = 12;  // effective (already buffed) value
    ThresholdPolicy threshold = ThresholdPolicy::StrictRejectUnadjudicated;
};
struct HeatSnapshot {
    I time_ms = 0, heat_tenths = 0, detected_shots = 0, last_event_ms = -1;
    bool overheat_locked = false, permanent_latched = false, heat_always_zero = false;
    I limit = 0, cooling_per_second = 0;
    bool firing_locked() const { return overheat_locked || permanent_latched; }
    bool operator==(const HeatSnapshot&) const = default;
};
class LauncherHeat {
public:
    static Result<LauncherHeat> start(HeatConfig);
    // Detected shots at t (cooling through t first), optional death reset.
    Result<HeatSnapshot> apply(I t, I detected_shots, bool died = false);
    // Level-up / buff change: cools through t with the old rate, then switches.
    // heat_always_zero models the enhanced sentry attack posture (table 5-28).
    Result<HeatSnapshot> set_profile(I t, I limit, I cooling_per_second, bool heat_always_zero = false);
    const HeatSnapshot& snapshot() const noexcept { return state_; }
private:
    HeatConfig config_;
    HeatSnapshot state_;
    I shot_tenths() const { return config_.caliber == Caliber::Mm17 ? 100 : 1000; }
    I margin_tenths() const { return config_.caliber == Caliber::Mm17 ? 1000 : 2000; }
    void cool_to(HeatSnapshot&, I t) const;
};
} // namespace rmuc2027::limits_v1
