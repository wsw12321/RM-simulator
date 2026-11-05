#include "limits_rules.hpp"
#include <algorithm>
#include <limits>

namespace rmuc2027::limits_v1 {
namespace {
constexpr I kMax = std::numeric_limits<I>::max();
constexpr I kMaxPowerMw = 10'000'000; // 10 kW sanity bound
I round_half_up(I numerator, I denominator) { return (2 * numerator + denominator) / (2 * denominator); }
}

std::string_view error_name(Error e) {
    switch (e) {
    case Error::InvalidConfig: return "InvalidConfig";
    case Error::NegativeValue: return "NegativeValue";
    case Error::NonIncreasingTime: return "NonIncreasingTime";
    case Error::IntegerOverflow: return "IntegerOverflow";
    case Error::UnadjudicatedThresholdEquality: return "UnadjudicatedThresholdEquality";
    case Error::UnadjudicatedOverlap: return "UnadjudicatedOverlap";
    }
    return "InvalidError";
}

Result<SpeedPenalty> speed_penalty(Caliber c, I limit, I measured) {
    if (limit <= 0 || measured < 0) return Error::NegativeValue;
    if (c == Caliber::Mm17) {
        const I over = measured - limit;
        if (over <= 0) return SpeedPenalty{};
        if (over < 5000) return SpeedPenalty{true, false, 15000};
        if (over < 10000) return SpeedPenalty{true, false, 20000};
        return SpeedPenalty{true, true, 0};
    }
    if (c == Caliber::Mm42) {
        if (measured <= limit) return SpeedPenalty{};
        if (measured <= 18000) return SpeedPenalty{true, false, 15000};
        return SpeedPenalty{true, true, 0};
    }
    return Error::InvalidConfig;
}

Result<SpeedPenalty> SpeedLock::detect(I t, Caliber c, I limit, I measured) {
    if (t < 0) return Error::NegativeValue;
    if (t < last_ms_) return Error::NonIncreasingTime;
    const auto p = speed_penalty(c, limit, measured);
    if (auto e = std::get_if<Error>(&p)) return *e;
    const auto& penalty = std::get<SpeedPenalty>(p);
    if (penalty.locked && !permanent_ && until_ms_ > t && !penalty.permanent &&
        policy_ == OverlapPolicy::StrictRejectUnadjudicated)
        return Error::UnadjudicatedOverlap;
    last_ms_ = t;
    if (penalty.permanent) permanent_ = true;
    else if (penalty.locked) until_ms_ = std::max(until_ms_, t + penalty.duration_ms);
    return penalty;
}

Result<PowerSnapshot> ChassisPower::settle(I t, I draw, I limit) {
    if (t < 0 || draw < 0 || limit < 0) return Error::NegativeValue;
    if (t % POWER_PERIOD_MS != 0) return Error::InvalidConfig;
    if (t <= state_.time_ms) return Error::NonIncreasingTime; // first boundary is 100 ms
    if (draw > kMaxPowerMw || limit > kMaxPowerMw) return Error::IntegerOverflow;
    auto s = state_;
    const I effective = s.chassis_enabled(t) ? draw : 0;
    // Z = Z - (Pr - Pl) * 0.1 s; mW * 0.1 s = 100 uJ per mW.
    s.buffer_uj -= (effective - limit) * 100;
    if (s.buffer_uj <= 0) {
        s.buffer_uj = 0;
        if (s.chassis_enabled(t)) { s.cut_until_ms = t + POWER_CUTOFF_MS; ++s.cutoffs; }
    }
    if (s.buffer_uj > BUFFER_MAX_UJ) s.buffer_uj = BUFFER_MAX_UJ;
    s.time_ms = t;
    state_ = s;
    return s;
}
void ChassisPower::reset_on_death(I) { state_.buffer_uj = BUFFER_MAX_UJ; }

Result<I> effective_power_limit_w(const PowerLimitInput& in) {
    if (in.base_w < 0 || in.scale_num < 0 || in.override_w < 0) return Error::NegativeValue;
    if (in.scale_den <= 0) return Error::InvalidConfig;
    I buff = 10;
    for (I b : in.buff_tenths) {
        if (b < 0) return Error::NegativeValue;
        buff = std::max(buff, b);
    }
    I w = in.override_w ? in.override_w : round_half_up(in.base_w * buff * in.scale_num, 10 * in.scale_den);
    // "提高 1 倍（但不超过 200W）": doubling never lowers an already higher limit.
    if (in.instant_revival_boost) w = std::max(w, std::min<I>(2 * w, 200));
    return w;
}

Result<LauncherHeat> LauncherHeat::start(HeatConfig c) {
    if (c.caliber != Caliber::Mm17 && c.caliber != Caliber::Mm42) return Error::InvalidConfig;
    if (c.limit < 0 || c.limit > 1'000'000 || c.cooling_per_second < 0 || c.cooling_per_second > 1'000'000) return Error::InvalidConfig;
    if (c.threshold != ThresholdPolicy::StrictRejectUnadjudicated && c.threshold != ThresholdPolicy::EngineeringProseStrictGreaterV1 &&
        c.threshold != ThresholdPolicy::EngineeringFlowchartGreaterEqualV1)
        return Error::InvalidConfig;
    LauncherHeat h;
    h.config_ = c;
    h.state_.limit = c.limit;
    h.state_.cooling_per_second = c.cooling_per_second;
    return h;
}

void LauncherHeat::cool_to(HeatSnapshot& s, I t) const {
    const I cycles = t / 100 - s.time_ms / 100;
    const I c = s.cooling_per_second; // C heat/s per 10 periods == C tenths per period
    if (cycles > 0 && c > 0) {
        const I needed = s.heat_tenths / c + (s.heat_tenths % c != 0);
        s.heat_tenths = cycles >= needed ? 0 : s.heat_tenths - cycles * c;
    }
    if (s.heat_tenths == 0) s.overheat_locked = false;
    s.time_ms = t;
}

Result<HeatSnapshot> LauncherHeat::apply(I t, I shots, bool died) {
    if (t < 0 || shots < 0) return Error::NegativeValue;
    if (t <= state_.last_event_ms) return Error::NonIncreasingTime;
    if (shots > kMax / 1000 || state_.detected_shots > kMax - shots) return Error::IntegerOverflow;
    auto s = state_;
    cool_to(s, t);
    if (shots && !s.heat_always_zero) {
        const I step = shot_tenths(), before = s.heat_tenths;
        const I limit = s.limit * 10, permanent = limit + margin_tenths();
        if (before > kMax - shots * step) return Error::IntegerOverflow;
        // Same O(1) intermediate-crossing logic as heat_v1 (batch = sequence of shots).
        if (config_.threshold == ThresholdPolicy::StrictRejectUnadjudicated && permanent > before &&
            (permanent - before) % step == 0 && (permanent - before) / step <= shots)
            return Error::UnadjudicatedThresholdEquality;
        s.heat_tenths += shots * step;
        const I first_temporary = before > limit ? 1 : (limit - before) / step + 1;
        if (first_temporary <= shots) {
            const I intermediate = before + first_temporary * step;
            if (intermediate < permanent ||
                (intermediate == permanent && config_.threshold == ThresholdPolicy::EngineeringProseStrictGreaterV1))
                s.overheat_locked = true;
        }
        if (s.heat_tenths > permanent ||
            (s.heat_tenths == permanent && config_.threshold == ThresholdPolicy::EngineeringFlowchartGreaterEqualV1))
            s.permanent_latched = true;
    }
    s.detected_shots += shots;
    if (died || s.heat_always_zero) { s.heat_tenths = 0; s.overheat_locked = false; }
    s.last_event_ms = t;
    state_ = s;
    return s;
}

Result<HeatSnapshot> LauncherHeat::set_profile(I t, I limit, I cooling, bool zero) {
    if (t < 0 || limit < 0 || cooling < 0) return Error::NegativeValue;
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    if (limit > 1'000'000 || cooling > 1'000'000) return Error::InvalidConfig;
    auto s = state_;
    cool_to(s, t);
    s.limit = limit;
    s.cooling_per_second = cooling;
    s.heat_always_zero = zero;
    if (zero) { s.heat_tenths = 0; s.overheat_locked = false; }
    state_ = s;
    return s;
}
} // namespace rmuc2027::limits_v1
