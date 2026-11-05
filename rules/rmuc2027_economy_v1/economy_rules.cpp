#include "economy_rules.hpp"
#include <algorithm>
#include <limits>

namespace rmuc2027::economy_v1 {
namespace {
using I = std::int64_t;
constexpr I kMax = std::numeric_limits<I>::max();
constexpr I kMaxRounds = 1'000'000;
bool in_match(I t) { return t >= 0 && t <= MATCH_DURATION_MS; }
I ceil_div(I a, I b) { return (a + b - 1) / b; }
} // namespace

std::string_view error_name(Error e) {
    switch (e) {
    case Error::InvalidChoice: return "InvalidChoice";
    case Error::NegativeValue: return "NegativeValue";
    case Error::NonIncreasingTime: return "NonIncreasingTime";
    case Error::TimeOutsideMatch: return "TimeOutsideMatch";
    case Error::IntegerOverflow: return "IntegerOverflow";
    case Error::InvalidUnit: return "InvalidUnit";
    case Error::InsufficientGold: return "InsufficientGold";
    case Error::TeamCapExceeded: return "TeamCapExceeded";
    case Error::InsufficientAllowance: return "InsufficientAllowance";
    case Error::InvalidLevel: return "InvalidLevel";
    }
    return "InvalidError";
}

Result<I> scheduled_gold_between(I from, I to) {
    if (from < -1 || to < 0 || to > MATCH_DURATION_MS || from > MATCH_DURATION_MS) return Error::TimeOutsideMatch;
    if (to < from) return Error::NonIncreasingTime;
    I gold = 0;
    for (const auto& g : SCHEDULE)
        if (g.elapsed_ms > from && g.elapsed_ms <= to) gold += g.gold;
    return gold;
}

Result<I> periodic_gold_between(I start, I period, I amount, I from, I to) {
    if (period <= 0 || amount < 0 || start < 0) return Error::InvalidChoice;
    if (to < from) return Error::NonIncreasingTime;
    if (!in_match(to) || from < -1) return Error::TimeOutsideMatch;
    // Count k>=1 with from < start+k*period <= to.
    const auto upto = [&](I t) { return t < start + period ? I{0} : (t - start) / period; };
    const I count = upto(to) - upto(std::max(from, start));
    if (amount && count > kMax / amount) return Error::IntegerOverflow;
    return count * amount;
}

Result<I> minimum_unit(Ammo a, Channel c) {
    if (a != Ammo::Mm17 && a != Ammo::Mm42) return Error::InvalidChoice;
    if (c == Channel::Local) return a == Ammo::Mm17 ? I{10} : I{1};
    if (c == Channel::Remote) return a == Ammo::Mm17 ? I{100} : I{10};
    return Error::InvalidChoice;
}

Result<I> ammo_cost(Ammo a, Channel c, I rounds) {
    const auto unit = minimum_unit(a, c);
    if (auto e = std::get_if<Error>(&unit)) return *e;
    if (rounds < 0) return Error::NegativeValue;
    if (rounds == 0 || rounds % std::get<I>(unit) != 0) return Error::InvalidUnit;
    if (rounds > kMaxRounds) return Error::IntegerOverflow;
    // Table 5-7: 17 mm local 10/10, remote 150/100; 42 mm local 10/1, remote 150/10.
    if (a == Ammo::Mm17) return c == Channel::Local ? rounds : rounds / 100 * 150;
    return c == Channel::Local ? rounds * 10 : rounds / 10 * 150;
}

Result<I> remote_hp_cost(I elapsed) {
    if (!in_match(elapsed)) return Error::TimeOutsideMatch;
    return 50 + ceil_div(elapsed * 20, 60000); // ROUNDUP(elapsed_s/60*20)
}

Result<I> instant_revival_cost(I elapsed, I level) {
    if (!in_match(elapsed)) return Error::TimeOutsideMatch;
    if (level < 1 || level > 10) return Error::InvalidLevel;
    return ceil_div(elapsed, 60000) * 80 + level * 20;
}

Result<I> sentry_supply_available(I elapsed, I claimed) {
    if (!in_match(elapsed)) return Error::TimeOutsideMatch;
    if (claimed < 0) return Error::NegativeValue;
    const I entitled = elapsed / SENTRY_SUPPLY_PERIOD_MS * SENTRY_SUPPLY_ROUNDS;
    if (claimed > entitled) return Error::InsufficientAllowance;
    return entitled - claimed;
}

Result<I> TeamWallet::settle(I t) {
    if (!in_match(t)) return Error::TimeOutsideMatch;
    if (t < state_.income_settled_ms) return Error::NonIncreasingTime;
    const I gold = std::get<I>(scheduled_gold_between(state_.income_settled_ms, t));
    state_.gold += gold;
    state_.total_income += gold;
    state_.income_settled_ms = t;
    return gold;
}

Result<WalletSnapshot> TeamWallet::add_gold(I amount) {
    if (amount < 0) return Error::NegativeValue;
    if (state_.gold > kMax - amount || state_.total_income > kMax - amount) return Error::IntegerOverflow;
    state_.gold += amount;
    state_.total_income += amount;
    return state_;
}

Result<Purchase> TeamWallet::buy_ammo(I t, Ammo a, Channel c, I rounds) {
    if (!in_match(t)) return Error::TimeOutsideMatch;
    if (t < state_.income_settled_ms) return Error::NonIncreasingTime;
    const auto cost = ammo_cost(a, c, rounds);
    if (auto e = std::get_if<Error>(&cost)) return *e;
    I& bought = a == Ammo::Mm17 ? state_.purchased_17mm : state_.purchased_42mm;
    const I cap = a == Ammo::Mm17 ? TEAM_PURCHASE_CAP_17MM : TEAM_PURCHASE_CAP_42MM;
    if (bought + rounds > cap) return Error::TeamCapExceeded;
    if (std::get<I>(cost) > state_.gold) return Error::InsufficientGold;
    state_.gold -= std::get<I>(cost);
    state_.total_spent += std::get<I>(cost);
    bought += rounds;
    return Purchase{std::get<I>(cost), rounds, c == Channel::Remote ? t + REMOTE_DELIVERY_DELAY_MS : t};
}

Result<WalletSnapshot> TeamWallet::spend(I amount) {
    if (amount < 0) return Error::NegativeValue;
    if (amount > state_.gold) return Error::InsufficientGold;
    state_.gold -= amount;
    state_.total_spent += amount;
    return state_;
}

Result<RobotAllowance> RobotAllowance::start(I mm17, I mm42) {
    if (mm17 < 0 || mm42 < 0) return Error::NegativeValue;
    if (mm17 > kMaxRounds || mm42 > kMaxRounds) return Error::IntegerOverflow;
    RobotAllowance r;
    r.state_.mm17 = mm17;
    r.state_.mm42 = mm42;
    return r;
}

Result<AllowanceSnapshot> RobotAllowance::settle(I t) {
    if (!in_match(t)) return Error::TimeOutsideMatch;
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    auto s = state_;
    std::vector<std::array<I, 3>> keep;
    for (const auto& p : s.pending) {
        if (p[0] <= t) (p[1] == I(Ammo::Mm17) ? s.mm17 : s.mm42) += p[2];
        else keep.push_back(p);
    }
    s.pending = std::move(keep);
    s.time_ms = t;
    state_ = std::move(s);
    return state_;
}

Result<AllowanceSnapshot> RobotAllowance::deliver(I t, Ammo a, I rounds, I effective) {
    if (!in_match(t)) return Error::TimeOutsideMatch;
    if (t < state_.time_ms || effective < t) return Error::NonIncreasingTime;
    if (a != Ammo::Mm17 && a != Ammo::Mm42) return Error::InvalidChoice;
    if (rounds < 0) return Error::NegativeValue;
    if (rounds > kMaxRounds || state_.pending.size() > 10000) return Error::IntegerOverflow;
    state_.pending.push_back({effective, I(a), rounds});
    return settle(t);
}

Result<AllowanceSnapshot> RobotAllowance::consume(I t, Ammo a, I rounds) {
    if (!in_match(t)) return Error::TimeOutsideMatch;
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    if (rounds < 0) return Error::NegativeValue;
    auto settled = *this;
    (void)settled.settle(t);
    I& have = a == Ammo::Mm17 ? settled.state_.mm17 : settled.state_.mm42;
    if (rounds > have) return Error::InsufficientAllowance;
    have -= rounds;
    *this = std::move(settled);
    return state_;
}

Result<AllowanceSnapshot> RobotAllowance::record_overfire(I t, I rounds) {
    if (!in_match(t)) return Error::TimeOutsideMatch;
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    if (rounds < 0) return Error::NegativeValue;
    state_.overfired_42mm += rounds;
    state_.time_ms = t;
    return state_;
}
} // namespace rmuc2027::economy_v1
