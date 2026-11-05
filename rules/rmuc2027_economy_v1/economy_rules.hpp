#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

// Source-linked team gold and robot allowed-rounds bookkeeping.
// RMUC 2027 V1.0.0 §5.4.1 tables 5-6/5-7 (pp75-76), §5.4.2 tables 5-8/5-9 (pp76-77),
// §5.7.2 (p108). Location/out-of-combat eligibility is supplied by the caller.
namespace rmuc2027::economy_v1 {
inline constexpr std::string_view MODULE_VERSION = "rmuc2027-economy-v1.0.0-module.1";
inline constexpr std::string_view SOURCE_VERSION = "RMUC2027_CN_V1.0.0_20261015";
inline constexpr std::string_view SOURCE_SHA256 = "9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be";
inline constexpr std::int64_t MATCH_DURATION_MS = 420000;
inline constexpr std::int64_t TEAM_PURCHASE_CAP_17MM = 1000; // via gold only
inline constexpr std::int64_t TEAM_PURCHASE_CAP_42MM = 100;
inline constexpr std::int64_t REMOTE_DELIVERY_DELAY_MS = 6000;
inline constexpr std::int64_t SENTRY_SUPPLY_PERIOD_MS = 60000;
inline constexpr std::int64_t SENTRY_SUPPLY_ROUNDS = 100;
inline constexpr std::int64_t AUTO_SENTRY_COMMAND_COST = 50;

enum class Ammo : std::uint8_t { Mm17, Mm42 };
enum class Channel : std::uint8_t { Local, Remote }; // Local = at an allowed buff point
enum class Error {
    InvalidChoice, NegativeValue, NonIncreasingTime, TimeOutsideMatch, IntegerOverflow,
    InvalidUnit, InsufficientGold, TeamCapExceeded, InsufficientAllowance, InvalidLevel
};
std::string_view error_name(Error);
template<class T> using Result = std::variant<T, Error>;

// Table 5-6: 800 at 06:59, +100 at 05:59..01:59, +300 at 00:59.
struct Grant { std::int64_t elapsed_ms; std::int64_t gold; };
inline constexpr std::array<Grant, 7> SCHEDULE{{
    {0, 800}, {60000, 100}, {120000, 100}, {180000, 100}, {240000, 100}, {300000, 100}, {360000, 300}}};
// Gold of scheduled grants with from < elapsed <= to. Use from = -1 to include the opening grant.
Result<std::int64_t> scheduled_gold_between(std::int64_t from_ms, std::int64_t to_ms);
// Fixed-period stream (e.g. table 5-12 "此后每 10 秒获得 N 金币"): boundaries
// start + k*period for k >= 1 inside (from, to].
Result<std::int64_t> periodic_gold_between(std::int64_t start_ms, std::int64_t period_ms, std::int64_t amount,
                                           std::int64_t from_ms, std::int64_t to_ms);
// Tables 5-8/5-9 minimum units and table 5-7 prices.
Result<std::int64_t> minimum_unit(Ammo, Channel);
Result<std::int64_t> ammo_cost(Ammo, Channel, std::int64_t rounds);
// Table 5-7: 50 + ROUNDUP(elapsed_s/60*20); elapsed in exact ms (ENGINEERING).
Result<std::int64_t> remote_hp_cost(std::int64_t elapsed_ms);
// Table 5-7: ROUNDUP(elapsed_s/60)*80 + level*20.
Result<std::int64_t> instant_revival_cost(std::int64_t elapsed_ms, std::int64_t level);
// §5.4.2: 100 rounds per elapsed minute at own supply, unclaimed amounts accumulate.
Result<std::int64_t> sentry_supply_available(std::int64_t elapsed_ms, std::int64_t claimed_rounds);

struct WalletSnapshot {
    std::int64_t gold = 0;
    std::int64_t income_settled_ms = -1;
    std::int64_t purchased_17mm = 0, purchased_42mm = 0;
    std::int64_t total_income = 0, total_spent = 0;
    bool operator==(const WalletSnapshot&) const = default;
};
struct Purchase {
    std::int64_t cost = 0;
    std::int64_t rounds = 0;
    std::int64_t effective_ms = 0; // remote exchanges become usable 6 s later
};
class TeamWallet {
public:
    // Adds scheduled income in (settled, t]. The opening 800 is credited at t>=0.
    Result<std::int64_t> settle(std::int64_t t);
    Result<WalletSnapshot> add_gold(std::int64_t amount); // assembly / other sourced streams
    Result<Purchase> buy_ammo(std::int64_t t, Ammo, Channel, std::int64_t rounds);
    Result<WalletSnapshot> spend(std::int64_t amount);    // HP, revival, sentry command
    const WalletSnapshot& snapshot() const noexcept { return state_; }
private:
    WalletSnapshot state_;
};

// One robot's allowed rounds (§5.4.2): each launched round consumes one of its
// type; delayed remote deliveries are credited at their effective time.
struct AllowanceSnapshot {
    std::int64_t mm17 = 0, mm42 = 0;
    std::int64_t overfired_42mm = 0; // launches attempted at zero allowance (p76)
    std::vector<std::array<std::int64_t, 3>> pending; // {effective_ms, ammo, rounds}
    std::int64_t time_ms = 0;
    bool operator==(const AllowanceSnapshot&) const = default;
};
class RobotAllowance {
public:
    static Result<RobotAllowance> start(std::int64_t mm17, std::int64_t mm42);
    Result<AllowanceSnapshot> settle(std::int64_t t);
    Result<AllowanceSnapshot> deliver(std::int64_t t, Ammo, std::int64_t rounds, std::int64_t effective_ms);
    // Consumes allowance for detected launches; returns error without mutation if short.
    Result<AllowanceSnapshot> consume(std::int64_t t, Ammo, std::int64_t rounds);
    // Records detected 42 mm launches at zero allowance (does not consume).
    Result<AllowanceSnapshot> record_overfire(std::int64_t t, std::int64_t rounds);
    std::int64_t available(Ammo a) const { return a == Ammo::Mm17 ? state_.mm17 : state_.mm42; }
    const AllowanceSnapshot& snapshot() const noexcept { return state_; }
private:
    AllowanceSnapshot state_;
};
} // namespace rmuc2027::economy_v1
