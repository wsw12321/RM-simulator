#include "radar_rules.hpp"
#include <algorithm>

namespace rmuc2027::radar_v1 {
std::string_view error_name(Error e) {
    switch (e) {
    case Error::InvalidChoice: return "InvalidChoice";
    case Error::NegativeValue: return "NegativeValue";
    case Error::NonIncreasingTime: return "NonIncreasingTime";
    case Error::NoChance: return "NoChance";
    case Error::AlreadyActive: return "AlreadyActive";
    }
    return "InvalidError";
}

Result<Mark> classify(I error_mm) {
    if (error_mm < 0) return Error::NegativeValue;
    if (error_mm < ACCURATE_BELOW_MM) return Mark::Accurate;
    if (error_mm < SEMI_BELOW_MM) return Mark::SemiAccurate;
    return Mark::Wrong;
}

I mark_vulnerability_percent(I progress_tenths) {
    if (progress_tenths >= 1200) return 20;
    if (progress_tenths >= 1000) return 15;
    return 0;
}

I vulnerability_percent(I progress_tenths, bool doubled, bool effective) {
    if (!effective) return 0;
    return mark_vulnerability_percent(progress_tenths) * (doubled ? 2 : 1);
}

// Table 5-29: update x first, then P = clamp(P + x, 0, 150).
void MarkProgress::apply(MarkSnapshot& s, Mark m) {
    if (m == Mark::Wrong) {
        s.influence_tenths = s.last == Last::Bad ? s.influence_tenths - 8 : -8;
        s.last = Last::Bad;
    } else {
        const I step = m == Mark::Accurate ? 10 : 5;
        s.influence_tenths = s.last == Last::Good ? s.influence_tenths + step : step;
        s.last = Last::Good;
    }
    s.progress_tenths = std::clamp<I>(s.progress_tenths + s.influence_tenths, 0, PROGRESS_MAX_TENTHS);
}

void MarkProgress::settle(MarkSnapshot& s, I t) {
    while (s.silence_from_ms + INTERRUPTION_MS <= t) {
        s.silence_from_ms += INTERRUPTION_MS;
        ++s.interruptions;
        apply(s, Mark::Wrong);
    }
    s.time_ms = t;
}

Result<MarkSnapshot> MarkProgress::advance(I t) {
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    settle(state_, t);
    return state_;
}

Result<MarkSnapshot> MarkProgress::report(I t, Mark m) {
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    if (m != Mark::Accurate && m != Mark::SemiAccurate && m != Mark::Wrong) return Error::InvalidChoice;
    settle(state_, t);
    state_.silence_from_ms = t;
    ++state_.reports;
    apply(state_, m);
    return state_;
}

Result<MarkSnapshot> MarkProgress::revived(I t) {
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    settle(state_, t);
    state_.influence_tenths = 0;
    state_.last = Last::None;
    return state_;
}

Result<DoubleSnapshot> DoubleVulnerability::advance(I t, bool enemy_vulnerable) {
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    DoubleSnapshot& s = state_;
    if (enemy_vulnerable && s.chances + s.triggers < DOUBLE_MAX_TRIGGERS) {
        s.charge_ms += t - s.time_ms;
        while (s.charge_ms >= DOUBLE_CHARGE_MS && s.chances + s.triggers < DOUBLE_MAX_TRIGGERS) {
            s.charge_ms -= DOUBLE_CHARGE_MS;
            ++s.chances;
        }
        if (s.chances + s.triggers >= DOUBLE_MAX_TRIGGERS) s.charge_ms = 0; // nothing left to earn
    }
    s.time_ms = t;
    return s;
}

Result<DoubleSnapshot> DoubleVulnerability::trigger(I t) {
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    if (state_.chances <= 0) return Error::NoChance;
    if (state_.active(t)) return Error::AlreadyActive; // ENGINEERING: periods do not overlap
    --state_.chances;
    ++state_.triggers;
    state_.active_until_ms = t + DOUBLE_DURATION_MS;
    return state_;
}

I Jammers::decoded(Team decoder) noexcept {
    I& level = level_[decoder == Team::Blue ? 1 : 0];
    level = std::min(JAMMER_MAX_LEVEL, level + 1);
    return level;
}
} // namespace rmuc2027::radar_v1
