#include "lifecycle_rules.hpp"
#include <algorithm>

namespace rmuc2027::lifecycle_v1 {
namespace {
using I = std::int64_t;
constexpr I kMaxHp = 100000;
I round_half_up(I numerator, I denominator) { return (2 * numerator + denominator) / (2 * denominator); }
bool valid_base(I hp) { return hp >= 0 && hp <= BASE_MAX_HP; }
} // namespace

std::string_view error_name(Error e) {
    switch (e) {
    case Error::InvalidConfig: return "InvalidConfig";
    case Error::NegativeValue: return "NegativeValue";
    case Error::NonIncreasingTime: return "NonIncreasingTime";
    case Error::TimeOutsideMatch: return "TimeOutsideMatch";
    case Error::IntegerOverflow: return "IntegerOverflow";
    case Error::NotAlive: return "NotAlive";
    case Error::NotDead: return "NotDead";
    case Error::NotOutOfCombat: return "NotOutOfCombat";
    case Error::HealAlreadyPending: return "HealAlreadyPending";
    case Error::InvalidBaseHp: return "InvalidBaseHp";
    case Error::UnsettledTime: return "UnsettledTime";
    }
    return "InvalidError";
}

Result<Logistics> logistics(I base_hp) {
    if (!valid_base(base_hp)) return Error::InvalidBaseHp;
    // 1 - (10000 - hp)/20000 == (10000 + hp)/20000
    return Logistics{BASE_MAX_HP + base_hp};
}

Result<I> revival_readout(I elapsed_ms, I instant_count, I base_hp) {
    if (elapsed_ms < 0 || elapsed_ms > MATCH_DURATION_MS) return Error::TimeOutsideMatch;
    if (instant_count < 0) return Error::NegativeValue;
    if (instant_count > 1'000'000) return Error::IntegerOverflow;
    const auto l = logistics(base_hp);
    if (auto e = std::get_if<Error>(&l)) return *e;
    // 10 + elapsed_s/10 + 20*count, in units of 1/10000.
    const I x = round_half_up(10 * 10000 + elapsed_ms + 200000 * instant_count, 10000);
    return round_half_up(x * Logistics::denominator, std::get<Logistics>(l).numerator);
}

I readout_rate(bool in_own_supply, I base_hp) { return in_own_supply || base_hp < FAST_READOUT_BASE_HP ? 4 : 1; }

Result<I> regen_per_second(const RegenInput& in) {
    if (in.max_hp < 0 || in.max_hp > kMaxHp) return Error::InvalidConfig;
    if (in.elapsed_ms < 0 || in.elapsed_ms > MATCH_DURATION_MS) return Error::TimeOutsideMatch;
    const auto l = logistics(in.base_hp);
    if (auto e = std::get_if<Error>(&l)) return *e;
    if (!in.in_own_supply && !in.in_full_own_outpost_buff) return I{0};
    const I percent = in.elapsed_ms >= LATE_SUPPLY_FROM_MS && in.out_of_combat ? 25 : 10;
    // Same-type buffs take the maximum; the supply rate always dominates outpost's 50%.
    const I halves = in.in_own_supply ? 2 : 1;
    return round_half_up(in.max_hp * percent * std::get<Logistics>(l).numerator * halves,
                         100 * Logistics::denominator * 2);
}

Result<I> remote_heal_amount(I max_hp, I base_hp) {
    if (max_hp < 0 || max_hp > kMaxHp) return Error::InvalidConfig;
    const auto l = logistics(base_hp);
    if (auto e = std::get_if<Error>(&l)) return *e;
    return round_half_up(max_hp * 60 * std::get<Logistics>(l).numerator, 100 * Logistics::denominator);
}

bool Snapshot::out_of_combat(I t) const {
    if (!alive()) return false;
    const I last = std::max(last_fire_ms, last_hp_loss_ms);
    return last < 0 || t - last >= OUT_OF_COMBAT_MS;
}

Result<RobotLife> RobotLife::start(I max_hp) {
    if (max_hp < 1 || max_hp > kMaxHp) return Error::InvalidConfig;
    Snapshot s;
    s.hp = s.max_hp = max_hp;
    return RobotLife(s);
}

Result<Snapshot> RobotLife::check_time(I t) const {
    if (t < 0 || t > MATCH_DURATION_MS) return Error::TimeOutsideMatch;
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    if (t != state_.settled_ms) return Error::UnsettledTime;
    return state_;
}

void RobotLife::die(Snapshot& s, I t, const Context& c) const {
    s.status = Status::Dead;
    s.hp = 0;
    s.death_ms = t;
    s.readout_required = std::get<I>(revival_readout(t, s.instant_revivals, c.base_hp));
    s.readout_progress = 0;
    s.weak = false;
    s.weak_until_ms = s.invincible_until_ms = s.power_boost_until_ms = -1;
    s.pending_heal_due_ms = -1; // §5.3.1: dying within 6 s voids the exchange, no refund
    ++s.deaths;
}

Result<Advance> RobotLife::advance(I t, const Context& c) {
    if (t < 0 || t > MATCH_DURATION_MS) return Error::TimeOutsideMatch;
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    if (!valid_base(c.base_hp)) return Error::InvalidBaseHp;
    Advance out{state_};
    Snapshot& s = out.state;
    // 1. Readout seconds since death (§5.3.2 p74): one step per whole second.
    if (s.status == Status::Dead) {
        const I rate = readout_rate(c.in_own_supply, c.base_hp);
        // Boundaries up to the previous settlement time were already counted.
        const I first = (s.settled_ms - s.death_ms) / 1000 + 1;
        for (I k = first; s.death_ms + k * 1000 <= t; ++k) {
            s.readout_progress += rate;
            if (s.readout_progress >= s.readout_required) {
                const I at = s.death_ms + k * 1000;
                s.status = Status::Alive;
                s.hp = std::max<I>(1, round_half_up(s.max_hp * 10, 100));
                s.invincible_until_ms = at + READOUT_INVINCIBLE_MS;
                s.weak = true;
                s.weak_until_ms = -1;
                s.last_revival = RevivalKind::Readout;
                s.revived_ms = at;
                out.revived = true;
                break;
            }
        }
    }
    if (s.status == Status::Alive) {
        // 2. Timed weak expiry after instant revival.
        if (s.weak && s.weak_until_ms >= 0 && s.weak_until_ms <= t) { s.weak = false; s.weak_until_ms = -1; }
        // 3. Weak cleared by a qualifying card; readout invincibility then ends at
        //    max(now, revival+10 s) (§5.3.2 p75).
        if (s.weak && c.weak_clearing_card) {
            s.weak = false;
            s.weak_until_ms = -1;
            if (s.last_revival == RevivalKind::Readout && s.invincible_until_ms > t)
                s.invincible_until_ms = std::max(t, std::min(s.invincible_until_ms, s.revived_ms + WEAK_CLEAR_INVINCIBLE_MS));
        }
        // 4. Remote HP exchange takes effect 6 s after confirmation.
        if (s.pending_heal_due_ms >= 0 && s.pending_heal_due_ms <= t) {
            const I add = std::min(std::get<I>(remote_heal_amount(s.max_hp, c.base_hp)), s.max_hp - s.hp);
            s.hp += add;
            out.healed += add;
            s.pending_heal_due_ms = -1;
        }
        // 5. Regeneration settles at whole match seconds (ENGINEERING instant).
        //    Weak robots receive no buff-point effect (§5.3.2 p75).
        for (I second = s.settled_ms / 1000 + 1; second * 1000 <= t; ++second) {
            const I at = second * 1000;
            if (s.weak || at < s.revived_ms) continue;
            const RegenInput in{s.max_hp, at, s.out_of_combat(at), c.in_own_supply, c.in_full_own_outpost_buff, c.base_hp};
            const I add = std::min(std::get<I>(regen_per_second(in)), s.max_hp - s.hp);
            s.hp += add;
            out.healed += add;
        }
    }
    s.settled_ms = t;
    s.time_ms = t;
    state_ = s;
    return out;
}

Result<Snapshot> RobotLife::record_fire(I t) {
    const auto checked = check_time(t);
    if (auto e = std::get_if<Error>(&checked)) return *e;
    if (!state_.alive()) return Error::NotAlive;
    state_.last_fire_ms = t;
    state_.time_ms = t;
    return state_;
}

Result<Loss> RobotLife::apply_loss(I t, I amount, LossKind kind, const Context& c) {
    const auto checked = check_time(t);
    if (auto e = std::get_if<Error>(&checked)) return *e;
    if (amount < 0) return Error::NegativeValue;
    if (!valid_base(c.base_hp)) return Error::InvalidBaseHp;
    if (!state_.alive()) return Error::NotAlive;
    Snapshot s = state_;
    s.time_ms = t;
    Loss loss;
    if (kind == LossKind::Combat && s.invincible(t)) { state_ = s; return loss; }
    loss.actual = std::min(amount, s.hp);
    if (loss.actual > 0) {
        s.hp -= loss.actual;
        s.last_hp_loss_ms = t;
    }
    if (s.hp == 0) { die(s, t, c); loss.died = true; }
    state_ = s;
    return loss;
}

Result<Snapshot> RobotLife::heal(I t, I amount) {
    const auto checked = check_time(t);
    if (auto e = std::get_if<Error>(&checked)) return *e;
    if (amount < 0) return Error::NegativeValue;
    if (!state_.alive()) return Error::NotAlive;
    state_.hp += std::min(amount, state_.max_hp - state_.hp);
    state_.time_ms = t;
    return state_;
}

Result<Snapshot> RobotLife::set_max_hp(I t, I max_hp) {
    const auto checked = check_time(t);
    if (auto e = std::get_if<Error>(&checked)) return *e;
    if (max_hp < 1 || max_hp > kMaxHp) return Error::InvalidConfig;
    Snapshot s = state_;
    if (s.alive() && max_hp > s.max_hp) s.hp += max_hp - s.max_hp;
    s.hp = std::min(s.hp, max_hp);
    s.max_hp = max_hp;
    s.time_ms = t;
    state_ = s;
    return s;
}

Result<Snapshot> RobotLife::request_remote_heal(I t) {
    const auto checked = check_time(t);
    if (auto e = std::get_if<Error>(&checked)) return *e;
    if (!state_.alive()) return Error::NotAlive;
    if (!state_.out_of_combat(t)) return Error::NotOutOfCombat;
    if (state_.pending_heal_due_ms >= 0) return Error::HealAlreadyPending;
    state_.pending_heal_due_ms = t + REMOTE_HEAL_DELAY_MS;
    state_.time_ms = t;
    return state_;
}

Result<Snapshot> RobotLife::instant_revive(I t) {
    const auto checked = check_time(t);
    if (auto e = std::get_if<Error>(&checked)) return *e;
    if (state_.alive()) return Error::NotDead;
    Snapshot s = state_;
    s.status = Status::Alive;
    s.hp = s.max_hp;
    s.weak = true;
    s.weak_until_ms = t + INSTANT_PROTECTION_MS;
    s.invincible_until_ms = t + INSTANT_PROTECTION_MS;
    s.power_boost_until_ms = t + INSTANT_POWER_MS;
    ++s.instant_revivals;
    s.last_revival = RevivalKind::Instant;
    s.revived_ms = t;
    s.time_ms = t;
    state_ = s;
    return s;
}
} // namespace rmuc2027::lifecycle_v1
