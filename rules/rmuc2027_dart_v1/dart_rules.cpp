#include "dart_rules.hpp"
#include <algorithm>
#include <limits>

namespace rmuc2027::dart_v1 {
namespace {
constexpr I NEVER = std::numeric_limits<I>::max();
bool valid_target(Target t) { return std::uint8_t(t) <= std::uint8_t(Target::BaseTerminalMoving); }
}

std::string_view error_name(Error e) {
    switch (e) {
    case Error::InvalidChoice: return "InvalidChoice";
    case Error::NonIncreasingTime: return "NonIncreasingTime";
    case Error::GateNotClosed: return "GateNotClosed";
    case Error::GateCoolingDown: return "GateCoolingDown";
    case Error::NoChance: return "NoChance";
    case Error::GateNotOpen: return "GateNotOpen";
    case Error::NoDarts: return "NoDarts";
    case Error::SelectionLocked: return "SelectionLocked";
    case Error::NoDartInFlight: return "NoDartInFlight";
    case Error::NegativeValue: return "NegativeValue";
    }
    return "InvalidError";
}

std::string_view target_name(Target t) {
    switch (t) {
    case Target::Outpost: return "outpost";
    case Target::BaseFixed: return "base-fixed";
    case Target::BaseRandomFixed: return "base-random-fixed";
    case Target::BaseRandomMoving: return "base-random-moving";
    case Target::BaseTerminalMoving: return "base-terminal-moving";
    }
    return "invalid";
}

Result<I> max_hp_loss(I max_hp, I percent) {
    if (max_hp < 0 || percent < 0) return Error::NegativeValue;
    if (max_hp > 1'000'000 || percent > 100) return Error::InvalidChoice;
    return (2 * max_hp * percent + 100) / 200;
}

void DartStation::settle(Snapshot& s, I t, std::vector<Event>& events) {
    for (;;) {
        const I stage = s.gate == Gate::Closed ? NEVER : s.gate_until_ms;
        const I chance = s.chances_granted < I(GATE_CHANCE_MS.size()) ? GATE_CHANCE_MS[std::size_t(s.chances_granted)] : NEVER;
        const I next = std::min(stage, chance);
        if (next > t) break;
        if (stage == next) {
            if (s.gate == Gate::Opening) {
                s.gate = Gate::Open;
                s.gate_until_ms = next + LAUNCH_WINDOW_MS;
                s.detect_from_ms = next; // the opposing detection module's window is refreshed
                s.detect_until_ms = next + DETECTION_WINDOW_MS;
                events.push_back({next, EventKind::GateOpen, s.openings});
            } else if (s.gate == Gate::Open) {
                s.gate = Gate::Closing;
                s.gate_until_ms = next + GATE_CLOSING_MS;
                events.push_back({next, EventKind::GateClosing, s.openings});
            } else {
                s.gate = Gate::Closed;
                s.gate_until_ms = -1;
                s.cooldown_until_ms = next + GATE_COOLDOWN_MS;
                events.push_back({next, EventKind::GateClosed, s.openings});
            }
        } else {
            ++s.chances;
            ++s.chances_granted;
            events.push_back({next, EventKind::ChanceGranted, s.chances});
        }
    }
    s.time_ms = t;
}

Result<Step> DartStation::advance(I t) {
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    Step out{state_, {}};
    settle(out.state, t, out.events);
    state_ = out.state;
    return out;
}

Result<Step> DartStation::select_target(I t, Target target) {
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    if (!valid_target(target)) return Error::InvalidChoice;
    Step out{state_, {}};
    Snapshot& s = out.state;
    settle(s, t, out.events);
    if (s.gate != Gate::Closed) {
        const bool allowed = s.target == Target::Outpost && is_base(target) && s.outpost_hit_this_opening && !s.switched_this_opening;
        if (!allowed) return Error::SelectionLocked;
        s.switched_this_opening = true;
    }
    s.target = target;
    out.events.push_back({t, EventKind::TargetSelected, I(target)});
    state_ = s;
    return out;
}

Result<Step> DartStation::open_gate(I t) {
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    Step out{state_, {}};
    Snapshot& s = out.state;
    settle(s, t, out.events);
    if (s.gate != Gate::Closed) return Error::GateNotClosed;
    if (t < s.cooldown_until_ms) return Error::GateCoolingDown;
    if (s.chances <= 0) return Error::NoChance;
    --s.chances;
    ++s.openings;
    s.gate = Gate::Opening;
    s.gate_until_ms = t + GATE_OPENING_MS;
    s.outpost_hit_this_opening = s.switched_this_opening = false;
    out.events.push_back({t, EventKind::GateOpening, s.openings});
    state_ = s;
    return out;
}

Result<Step> DartStation::launch(I t) {
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    Step out{state_, {}};
    Snapshot& s = out.state;
    settle(s, t, out.events);
    if (s.gate != Gate::Open) return Error::GateNotOpen;
    if (s.darts_left <= 0) return Error::NoDarts;
    --s.darts_left;
    ++s.launched;
    out.events.push_back({t, EventKind::Launched, s.launched});
    state_ = s;
    return out;
}

Result<Step> DartStation::missed(I t) {
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    Step out{state_, {}};
    Snapshot& s = out.state;
    settle(s, t, out.events);
    if (s.resolved >= s.launched) return Error::NoDartInFlight;
    ++s.resolved;
    state_ = s;
    return out;
}

Result<HitStep> DartStation::hit(I t, Struck struck, bool module_static) {
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    if (struck != Struck::OutpostModule && struck != Struck::BaseModule) return Error::InvalidChoice;
    HitStep out{state_, {}, {}};
    Snapshot& s = out.state;
    settle(s, t, out.events);
    if (s.resolved >= s.launched) return Error::NoDartInFlight;
    ++s.resolved;
    HitEffect& e = out.effect;
    e.target = s.target;
    if (s.detect_from_ms < 0 || t < s.detect_from_ms || t >= s.detect_until_ms) e.miss = Miss::OutsideDetectionWindow;
    else if (t < s.blackout_until_ms) e.miss = Miss::HitBlackout;
    else if ((struck == Struck::BaseModule) != is_base(s.target)) e.miss = Miss::NotSelectedTarget;
    else if (s.target != Target::Outpost && s.target != Target::BaseFixed && !module_static) e.miss = Miss::ModuleMoving;
    if (e.miss == Miss::None) {
        e.valid = true;
        e.hit_number = ++s.hits;
        s.blackout_until_ms = t + HIT_BLACKOUT_MS;
        (struck == Struck::BaseModule ? s.base_point_disabled_until_ms : s.outpost_point_disabled_until_ms) = t + POINT_DISABLE_MS;
        const I by_order = OCCLUSION_MS[std::size_t(std::min<I>(e.hit_number, 4) - 1)];
        bool deploy = false;
        switch (s.target) {
        case Target::Outpost:
            e.occlusion_ms = by_order;
            s.outpost_hit_this_opening = true;
            break;
        case Target::BaseFixed:
            e.occlusion_ms = by_order;
            e.experience_tenths = FIXED_EXPERIENCE_TENTHS;
            deploy = ++s.fixed_base_hits >= DARTS_PER_ROUND;
            break;
        case Target::BaseRandomFixed:
            e.occlusion_ms = by_order;
            e.experience_tenths = RANDOM_FIXED_EXPERIENCE_TENTHS;
            e.buff_suspend_ms = by_order;
            deploy = ++s.fixed_base_hits >= DARTS_PER_ROUND;
            break;
        case Target::BaseRandomMoving:
        case Target::BaseTerminalMoving:
            e.occlusion_ms = MOVING_OCCLUSION_MS;
            e.experience_tenths = MOVING_EXPERIENCE_TENTHS;
            e.buffs_voided = true;
            e.max_hp_loss_percent = s.target == Target::BaseRandomMoving ? RANDOM_MOVING_HP_PERCENT : TERMINAL_MOVING_HP_PERCENT;
            deploy = true;
            break;
        }
        // Consecutive hits add up (p111 "若连续命中，则被遮挡时间叠加计算").
        s.occlusion_until_ms = std::max(s.occlusion_until_ms, t) + e.occlusion_ms;
        if (e.buff_suspend_ms) s.buff_suspended_until_ms = std::max(s.buff_suspended_until_ms, t) + e.buff_suspend_ms;
        if (deploy && !s.base_armor_deployed) { s.base_armor_deployed = true; e.base_armor_deployed = true; }
    }
    state_ = s;
    return out;
}
} // namespace rmuc2027::dart_v1
