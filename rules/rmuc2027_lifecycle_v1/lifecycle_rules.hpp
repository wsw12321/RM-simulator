#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

// Source-linked ground-robot survival lifecycle: out-of-combat state, death,
// revival readout, invincible/weak states, HP regeneration, remote HP exchange
// and the base-HP logistics coefficient.
// RMUC 2027 V1.0.0 §2 table 2-1 (p16), §5.3 (pp73-75), §5.6.1 table 5-19 (pp94-95).
// It owns no physics, no gold (see economy_v1) and no damage multipliers (buff_v1).
namespace rmuc2027::lifecycle_v1 {
inline constexpr std::string_view MODULE_VERSION = "rmuc2027-lifecycle-v1.0.0-module.1";
inline constexpr std::string_view SOURCE_VERSION = "RMUC2027_CN_V1.0.0_20261015";
inline constexpr std::string_view SOURCE_SHA256 = "9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be";
inline constexpr std::int64_t MATCH_DURATION_MS = 420000;
inline constexpr std::int64_t BASE_MAX_HP = 10000;           // §5.6.1 p94
inline constexpr std::int64_t OUT_OF_COMBAT_MS = 6000;       // §2 p16
inline constexpr std::int64_t LATE_SUPPLY_FROM_MS = 240000;  // "比赛开始 4 分钟后"
inline constexpr std::int64_t REMOTE_HEAL_DELAY_MS = 6000;   // §5.3.1 p73
inline constexpr std::int64_t READOUT_INVINCIBLE_MS = 30000; // §5.3.2 p75
inline constexpr std::int64_t WEAK_CLEAR_INVINCIBLE_MS = 10000;
inline constexpr std::int64_t INSTANT_PROTECTION_MS = 3000;
inline constexpr std::int64_t INSTANT_POWER_MS = 4000;
inline constexpr std::int64_t FAST_READOUT_BASE_HP = 4000;   // base HP below this -> x4

enum class Error {
    InvalidConfig, NegativeValue, NonIncreasingTime, TimeOutsideMatch, IntegerOverflow,
    NotAlive, NotDead, NotOutOfCombat, HealAlreadyPending, InvalidBaseHp, UnsettledTime
};
std::string_view error_name(Error);
template<class T> using Result = std::variant<T, Error>;

// Table 5-19: lambda = 1 - delta/20000, delta = own base HP already lost.
struct Logistics {
    std::int64_t numerator = 20000; // lambda = numerator / 20000
    static constexpr std::int64_t denominator = 20000;
};
Result<Logistics> logistics(std::int64_t base_hp);
// §5.3.2: required = round(10 + (420 - remaining_s)/10 + 20*instant_count), then
// table 5-19 divides by lambda. ENGINEERING: the lambda quotient is rounded half-up
// once more; elapsed time is taken in exact milliseconds.
Result<std::int64_t> revival_readout(std::int64_t elapsed_ms, std::int64_t instant_count, std::int64_t base_hp);
// §5.3.2: +1 per second, +4 while at own supply buff point or own base HP < 4000.
std::int64_t readout_rate(bool in_own_supply, std::int64_t base_hp);
struct RegenInput {
    std::int64_t max_hp = 0;
    std::int64_t elapsed_ms = 0;
    bool out_of_combat = false;
    bool in_own_supply = false;
    bool in_full_own_outpost_buff = false; // own fully controlled outpost buff point
    std::int64_t base_hp = BASE_MAX_HP;
};
// §5.3.1 + table 5-19: supply 10%/s (25%/s after 4 min while out of combat),
// outpost 50% of the current supply rate; times lambda; max of same-type buffs;
// final value rounded half-up (§5.6.4.1 p105).
Result<std::int64_t> regen_per_second(const RegenInput&);
// §5.3.1: remote exchange adds 60% * lambda of the max HP, never above max.
Result<std::int64_t> remote_heal_amount(std::int64_t max_hp, std::int64_t base_hp);

enum class Status : std::uint8_t { Alive, Dead };
enum class RevivalKind : std::uint8_t { None, Readout, Instant };
struct Context {
    bool in_own_supply = false;
    bool in_full_own_outpost_buff = false;
    // Detected own base / own fully-controlled outpost / own supply buff card
    // (§5.3.2 p75): clears "weak". Activation delay is an upstream concern.
    bool weak_clearing_card = false;
    std::int64_t base_hp = BASE_MAX_HP;
};
struct Snapshot {
    std::int64_t time_ms = 0;
    Status status = Status::Alive;
    std::int64_t hp = 0, max_hp = 0;
    std::int64_t last_fire_ms = -1, last_hp_loss_ms = -1;
    std::int64_t death_ms = -1, readout_required = 0, readout_progress = 0;
    std::int64_t instant_revivals = 0, deaths = 0;
    RevivalKind last_revival = RevivalKind::None;
    std::int64_t revived_ms = -1;
    bool weak = false;
    std::int64_t invincible_until_ms = -1;   // exclusive end; -1 none
    std::int64_t weak_until_ms = -1;         // instant revival only; -1 card-cleared only
    std::int64_t power_boost_until_ms = -1;  // instant revival: chassis power x2 (<=200 W)
    std::int64_t pending_heal_due_ms = -1;
    std::int64_t settled_ms = 0;             // time-driven transitions settled up to here
    bool operator==(const Snapshot&) const = default;
    bool alive() const { return status == Status::Alive; }
    bool invincible(std::int64_t t) const { return alive() && invincible_until_ms > t; }
    bool power_boosted(std::int64_t t) const { return alive() && power_boost_until_ms > t; }
    bool out_of_combat(std::int64_t t) const;
    // Weak robots have their launcher locked (§5.3.2). Death also locks it.
    bool launcher_allowed() const { return alive() && !weak; }
};
struct Advance {
    Snapshot state;
    std::int64_t healed = 0;       // HP regenerated or remotely added in this call
    bool revived = false;
};
struct Loss { std::int64_t actual = 0; bool died = false; };
enum class LossKind : std::uint8_t {
    Combat,  // projectile, dart, collision, aerial: blocked by invincibility (§2 p17)
    Other    // penalty, module offline: not blocked by invincibility
};

// One robot's lifecycle. Every mutation is atomic (errors change nothing).
// Contract: `advance(t)` settles time first; every other mutation must then use
// exactly that settled t (else UnsettledTime), so facts of one tick can never
// skip regeneration, readout or expiry instants before them.
class RobotLife {
public:
    static Result<RobotLife> start(std::int64_t max_hp);
    // Settle all time-driven transitions in (last, t]: readout seconds, readout
    // revival, protection expiry, weak-card clearing, remote heal, regeneration
    // at whole match seconds (ENGINEERING settlement instant).
    Result<Advance> advance(std::int64_t t, const Context&);
    Result<Snapshot> record_fire(std::int64_t t);
    Result<Loss> apply_loss(std::int64_t t, std::int64_t amount, LossKind, const Context&);
    Result<Snapshot> heal(std::int64_t t, std::int64_t amount);
    // Level-up / stage change. An increase is added to current HP if alive (§5.5.2).
    Result<Snapshot> set_max_hp(std::int64_t t, std::int64_t max_hp);
    Result<Snapshot> request_remote_heal(std::int64_t t);  // gold handled elsewhere
    Result<Snapshot> instant_revive(std::int64_t t);       // gold handled elsewhere
    const Snapshot& snapshot() const noexcept { return state_; }
private:
    Snapshot state_;
    explicit RobotLife(Snapshot s) : state_(s) {}
    Result<Snapshot> check_time(std::int64_t t) const;
    void die(Snapshot&, std::int64_t t, const Context&) const;
};
} // namespace rmuc2027::lifecycle_v1
