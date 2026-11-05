#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

// Source-linked damage settlement and buff composition.
// RMUC 2027 V1.0.0: §2 (p17), §5.1.1 table 5-2 (pp64-65), §5.2 table 5-5 (p72),
// §5.4.2 (p76), §5.6.1 (p94), §5.6.3 tables 5-22/5-23 (p98), §5.6.4 tables
// 5-25..5-27 (pp102-107), §5.7.1 (p108), §5.7.2 table 5-28 (p109), table 5-30 (p113).
namespace rmuc2027::buff_v1 {
inline constexpr std::string_view MODULE_VERSION = "rmuc2027-buff-v1.0.0-module.1";
inline constexpr std::string_view SOURCE_VERSION = "RMUC2027_CN_V1.0.0_20261015";
inline constexpr std::string_view SOURCE_SHA256 = "9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be";
using I = std::int64_t;

enum class Error {
    InvalidChoice, Unsupported, NegativeValue, NonIncreasingTime, IntegerOverflow,
    OnCooldown, EnhancedExhausted, BlockedInWeakState
};
std::string_view error_name(Error);
template<class T> using Result = std::variant<T, Error>;

// ---- Table 5-2 raw damage (pixels checked, columns 42/17/aerial/dart/collision).
enum class Target : std::uint8_t { RobotArmor, BaseArmor, BaseDartModule, OutpostMiddleArmor, OutpostDartModule };
enum class Source : std::uint8_t { Projectile42, Projectile17, AerialImpact, Dart, Collision };
enum class DartTarget : std::uint8_t { None, Fixed, RandomFixed, RandomMoving, TerminalMoving };
Result<I> raw_damage(Target, Source, DartTarget = DartTarget::None);
// ---- Table 5-5 adaptive armour regions (projectiles only).
enum class ArmorRegion : std::uint8_t { Default, Reinforced, Unreinforced };

// ---- Damage settlement. Same-type buffs take the maximum (p105). The net
// defence is max(defence) - max(vulnerability) and may be negative
// (p104-105 examples). Final value is rounded half-up (p105).
struct DamageInput {
    Target target = Target::RobotArmor;
    Source source = Source::Projectile17;
    DartTarget dart = DartTarget::None;
    ArmorRegion region = ArmorRegion::Default;
    std::span<const I> attack_percent;        // attacker, e.g. {150, 200}; empty = 100%
    std::span<const I> defense_percent;       // victim, e.g. {25, 50}
    std::span<const I> vulnerability_percent; // victim, e.g. {15, 100}
    bool invincible = false;                  // §2 p17: no projectile/dart/collision damage
    bool shield_42mm = false;                 // §5.1.1 / §5.4.2 42 mm shielding
};
Result<I> settle_damage(const DamageInput&);
I net_defense_percent(std::span<const I> defense, std::span<const I> vulnerability);

// ---- Cooling composition (p107 example: 40*5 > 40+75 -> 200; sentry example
// p109-110: max(30*3, 30+75)=105 then *1/3 = 35). Multipliers are in tenths.
struct CoolingInput {
    I base_per_second = 0;
    std::span<const I> multiplier_tenths; // e.g. 15 (tunnel), 20/30/50 (energy), 30 (attack posture)
    std::span<const I> additive;          // e.g. fortress w
    I scale_numerator = 1, scale_denominator = 1; // posture reduction applied afterwards
};
// ENGINEERING: the composed value is rounded half-up to an integer per second.
Result<I> effective_cooling(const CoolingInput&);

// ---- Base and outpost defence (§5.6.1 p94).
inline constexpr I BASE_OUTPOST_DEFENSE = 100; // own outpost position fully own-controlled

// ---- Field buff points (§5.6.4.2-5.6.4.9 pp105-107).
enum class BuffPoint : std::uint8_t {
    OwnBase, OwnHighPoint, CentralHighland, OwnFortress, EnemyFortress, OwnOutpost, OwnSupply, OwnAssembly
};
struct PointEffect { I defense = 0, vulnerability = 0, cooling_add = 0, reserve_rounds = 0; bool exchange = false, heavy_invincible = false; };
struct PointContext {
    I elapsed_ms = 0;
    bool own_outpost_position_own_controlled = true; // own fortress only works when false
    bool enemy_outpost_position_own_controlled = false;
    bool enemy_base_armor_deployed = false;
    bool point_outpost_position_full_own = true;      // OwnOutpost: that position fully own-controlled
    I own_base_lowest_hp = 10000;                     // fortress delta uses the round minimum
    bool heavy = false;
};
// Returns the effect a non-weak robot gets while activating that point.
Result<PointEffect> point_effect(BuffPoint, const PointContext&);
I fortress_cooling(I base_max_minus_lowest);        // floor(delta/80), <=75
I fortress_reserve(I base_max_minus_lowest);        // 100+floor(delta/15), <=500
inline constexpr I FORTRESS_DEPLOY_MS = 10000;      // continuous enemy-fortress activation
inline constexpr I FORTRESS_HOLD_MS = 3000;         // timer kept (paused) after death/lapse

// ---- Terrain crossing buffs (§5.6.4.5 p105-106, table 5-27).
enum class Terrain : std::uint8_t { Highland, Road, TunnelCross, TunnelThrough };
struct TerrainGrant { bool granted = false; bool first_time = false; };
class TerrainBuffs {
public:
    // A completed crossing detected at t. Road cannot repeat within 15 s.
    Result<TerrainGrant> grant(I t, Terrain);
    void clear() noexcept; // death clears all terrain buffs
    I defense_percent(I t) const;          // 25, or 50 while the combined buff lasts
    I cooling_multiplier_tenths(I t) const; // 15 while "穿越隧道" lasts, else 10
    struct State { std::array<I, 4> until{-1, -1, -1, -1}; I combo_until = -1; I last_road_ms = -1000000; std::array<bool, 4> earned{}; };
    const State& state() const noexcept { return state_; }
private:
    State state_;
};

// ---- Big energy mechanism (tables 5-22/5-23). Average ring = ring_sum/hits.
struct EnergyBuff { I attack_percent = 100, defense_percent = 0, cooling_multiplier_tenths = 10, duration_ms = 0; };
Result<EnergyBuff> big_energy_buff(I ring_sum, I hits, I lit_arms);
inline constexpr EnergyBuff SMALL_ENERGY_BUFF{100, 25, 10, 45000};

// ---- Radar mark vulnerability (table 5-30): P in tenths of progress.
I radar_mark_vulnerability(I progress_tenths); // [0,100):0, [100,120):15, [120,150]:20

// ---- Heavy early defence (§5.7.1 p108): 50% in the first 3 min before installation.
I heavy_opening_defense(I elapsed_ms, bool launcher_installed);

// ---- Sentry posture (§5.7.2 table 5-28, p109).
enum class Posture : std::uint8_t { Attack, Defense, Move };
struct PostureEffect {
    I defense = 0, vulnerability = 0;
    I cooling_buff_tenths = 10;              // competes with other cooling buffs (max)
    I cooling_scale_num = 1, cooling_scale_den = 1;
    I power_buff_tenths = 10;                // competes with other power buffs (max)
    I power_scale_num = 1, power_scale_den = 1;
    I power_override_w = 0;                  // enhanced move: limit raised to 200 W
    bool heat_always_zero = false;           // enhanced attack
};
inline constexpr I POSTURE_SWITCH_COOLDOWN_MS = 5000;
inline constexpr I POSTURE_DEGRADE_AFTER_MS = 180000; // "超过 3 分钟"
inline constexpr I ENHANCED_LIMIT_MS = 15000;
class SentryPosture {
public:
    // ENGINEERING: entering/leaving enhanced counts as a switch for the 5 s cooldown;
    // enhanced time also accrues to its base posture (p110 example two).
    Result<I> select(I t, Posture, bool enhanced);
    Result<I> advance(I t); // auto-exits enhanced at its 15 s budget
    PostureEffect effect() const;
    struct State {
        I time_ms = 0, last_switch_ms = -1000000;
        Posture posture = Posture::Move;
        bool enhanced = false;
        std::array<I, 3> accumulated{}, enhanced_used{};
    };
    const State& state() const noexcept { return state_; }
private:
    State state_;
    void accrue(I t);
};

// ---- 42 mm shield (§5.1.1 p65, §5.4.2 p76-77).
class Shield42 {
public:
    // Detected heavy 42 mm launch. allowance_before==0 is an over-fire.
    void launch(I t, bool heavy_alive, I allowance_before);
    void heavy_died(I t);
    // Lift §5.4.2 shields once the heavy is alive with allowance > 0.
    void update(I t, bool heavy_alive, I allowance);
    bool shielded(I t) const;
    struct State { I last_launch_ms = -1; I death_ms = -1; bool dead = false; bool overfire = false; };
    const State& state() const noexcept { return state_; }
private:
    State state_;
};
} // namespace rmuc2027::buff_v1
