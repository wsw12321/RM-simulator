#include "buff_rules.hpp"
#include <algorithm>
#include <limits>

namespace rmuc2027::buff_v1 {
namespace {
constexpr I kMaxScalar = 1'000'000;
I round_half_up(I numerator, I denominator) { return (2 * numerator + denominator) / (2 * denominator); }
template<class Span> Result<I> maximum(Span values, I floor_value, I upper) {
    I m = floor_value;
    for (I v : values) {
        if (v < 0) return Error::NegativeValue;
        if (v > upper) return Error::InvalidChoice;
        m = std::max(m, v);
    }
    return m;
}
bool is_projectile(Source s) { return s == Source::Projectile17 || s == Source::Projectile42; }
} // namespace

std::string_view error_name(Error e) {
    switch (e) {
    case Error::InvalidChoice: return "InvalidChoice";
    case Error::Unsupported: return "Unsupported";
    case Error::NegativeValue: return "NegativeValue";
    case Error::NonIncreasingTime: return "NonIncreasingTime";
    case Error::IntegerOverflow: return "IntegerOverflow";
    case Error::OnCooldown: return "OnCooldown";
    case Error::EnhancedExhausted: return "EnhancedExhausted";
    case Error::BlockedInWeakState: return "BlockedInWeakState";
    }
    return "InvalidError";
}

Result<I> raw_damage(Target t, Source s, DartTarget d) {
    switch (t) {
    case Target::RobotArmor:
        if (s == Source::Projectile42) return I{200};
        if (s == Source::Projectile17) return I{20};
        if (s == Source::Collision) return I{2};
        return Error::InvalidChoice; // aerial impact / dart: "-"
    case Target::BaseArmor:
    case Target::OutpostMiddleArmor:
        if (s == Source::Projectile42) return I{200};
        if (s == Source::Projectile17) return I{20};
        if (s == Source::AerialImpact) return I{200};
        return Error::InvalidChoice;
    case Target::BaseDartModule:
        if (s != Source::Dart) return Error::InvalidChoice;
        switch (d) {
        case DartTarget::Fixed: return I{200};
        case DartTarget::RandomFixed: return I{300};
        case DartTarget::RandomMoving: return I{800};
        case DartTarget::TerminalMoving: return I{1250};
        case DartTarget::None: break;
        }
        return Error::InvalidChoice;
    case Target::OutpostDartModule:
        if (s == Source::Dart) return I{1000};
        return Error::InvalidChoice;
    }
    return Error::InvalidChoice;
}

I net_defense_percent(std::span<const I> defense, std::span<const I> vulnerability) {
    I d = 0, v = 0;
    for (I x : defense) d = std::max(d, x);
    for (I x : vulnerability) v = std::max(v, x);
    return d - v;
}

Result<I> settle_damage(const DamageInput& in) {
    const auto raw = raw_damage(in.target, in.source, in.dart);
    if (auto e = std::get_if<Error>(&raw)) return *e;
    const auto attack = maximum(in.attack_percent, 100, kMaxScalar);
    const auto defense = maximum(in.defense_percent, 0, 100);
    const auto vulnerability = maximum(in.vulnerability_percent, 0, kMaxScalar);
    for (const auto* r : {&attack, &defense, &vulnerability})
        if (auto e = std::get_if<Error>(r)) return *e;
    if (in.region != ArmorRegion::Default && in.region != ArmorRegion::Reinforced && in.region != ArmorRegion::Unreinforced)
        return Error::InvalidChoice;
    if (in.invincible) return I{0};
    if (in.shield_42mm && in.source == Source::Projectile42) return I{0};
    // Table 5-5 multiplies the projectile's raw damage only.
    I region_halves = 2;
    if (is_projectile(in.source))
        region_halves = in.region == ArmorRegion::Reinforced ? 1 : in.region == ArmorRegion::Unreinforced ? 3 : 2;
    // Attack buffs raise projectile / aerial damage; defence (and its negative,
    // vulnerability) applies to projectiles, collision and aerial, not darts.
    const I attack_percent = in.source == Source::Dart || in.source == Source::Collision ? 100 : std::get<I>(attack);
    const I net = in.source == Source::Dart ? 0 : std::min<I>(100, std::get<I>(defense) - std::get<I>(vulnerability));
    const I numerator = std::get<I>(raw) * region_halves * attack_percent * (100 - net);
    return round_half_up(numerator, 2 * 100 * 100);
}

Result<I> effective_cooling(const CoolingInput& in) {
    if (in.base_per_second < 0 || in.scale_numerator < 0) return Error::NegativeValue;
    if (in.scale_denominator <= 0) return Error::InvalidChoice;
    if (in.base_per_second > kMaxScalar) return Error::IntegerOverflow;
    const auto mult = maximum(in.multiplier_tenths, 10, 1000);
    const auto add = maximum(in.additive, 0, kMaxScalar);
    if (auto e = std::get_if<Error>(&mult)) return *e;
    if (auto e = std::get_if<Error>(&add)) return *e;
    const I tenths = std::max(in.base_per_second * std::get<I>(mult), (in.base_per_second + std::get<I>(add)) * 10);
    return round_half_up(tenths * in.scale_numerator, 10 * in.scale_denominator);
}

I fortress_cooling(I delta) { return std::clamp<I>(delta / 80, 0, 75); }
I fortress_reserve(I delta) { return std::clamp<I>(100 + std::max<I>(delta, 0) / 15, 100, 500); }

Result<PointEffect> point_effect(BuffPoint p, const PointContext& c) {
    if (c.elapsed_ms < 0 || c.own_base_lowest_hp < 0 || c.own_base_lowest_hp > 10000) return Error::InvalidChoice;
    PointEffect e;
    switch (p) {
    case BuffPoint::OwnBase: e.defense = 50; e.exchange = true; return e;            // p105
    case BuffPoint::OwnHighPoint: e.defense = 50; e.exchange = true; return e;       // p105
    case BuffPoint::CentralHighland: e.defense = 25; return e;                       // p105
    case BuffPoint::OwnFortress:                                                     // p107
        if (!c.own_outpost_position_own_controlled) {
            const I delta = 10000 - c.own_base_lowest_hp;
            e.defense = 50;
            e.cooling_add = fortress_cooling(delta);
            e.reserve_rounds = fortress_reserve(delta);
        }
        return e;
    case BuffPoint::EnemyFortress:                                                   // p107
        if (c.elapsed_ms >= 180000 && c.enemy_outpost_position_own_controlled && !c.enemy_base_armor_deployed)
            e.vulnerability = 100;
        return e;
    case BuffPoint::OwnOutpost: e.exchange = c.point_outpost_position_full_own; return e; // p106
    case BuffPoint::OwnSupply: e.exchange = true; e.heavy_invincible = c.heavy; return e; // p107
    case BuffPoint::OwnAssembly: e.heavy_invincible = c.heavy; return e;                 // p106
    }
    return Error::InvalidChoice;
}

// ---- Terrain buffs.
namespace {
constexpr std::array<I, 4> kTerrainMs{30000, 5000, 30000, 120000};
constexpr I kRoadRepeatMs = 15000;
std::size_t category(Terrain t) { return t == Terrain::Highland ? 0 : t == Terrain::Road ? 1 : 2; }
}
Result<TerrainGrant> TerrainBuffs::grant(I t, Terrain kind) {
    const auto k = std::size_t(kind);
    if (k > 3) return Error::InvalidChoice;
    if (t < 0) return Error::NegativeValue;
    if (kind == Terrain::Road && t - state_.last_road_ms < kRoadRepeatMs) return TerrainGrant{};
    I remaining = 0;
    bool other = false;
    for (std::size_t i = 0; i < 4; ++i)
        if (state_.until[i] > t) {
            remaining = std::max(remaining, state_.until[i] - t);
            other = other || i != k;
        }
    if (state_.combo_until > t) remaining = std::max(remaining, state_.combo_until - t);
    State next = state_;
    next.until[k] = std::max(next.until[k], t + kTerrainMs[k]);
    if (other) next.combo_until = t + std::max(remaining, kTerrainMs[k]);
    if (kind == Terrain::Road) next.last_road_ms = t;
    TerrainGrant g{true, !next.earned[category(kind)]};
    next.earned[category(kind)] = true;
    state_ = next;
    return g;
}
void TerrainBuffs::clear() noexcept {
    const auto earned = state_.earned;
    const auto road = state_.last_road_ms;
    state_ = {};
    state_.earned = earned; // "first time" experience is per round, not per life
    state_.last_road_ms = road;
}
I TerrainBuffs::defense_percent(I t) const {
    if (state_.combo_until > t) return 50;
    for (std::size_t i = 0; i < 3; ++i) if (state_.until[i] > t) return 25;
    return 0;
}
I TerrainBuffs::cooling_multiplier_tenths(I t) const { return state_.until[3] > t ? 15 : 10; }

Result<EnergyBuff> big_energy_buff(I ring_sum, I hits, I arms) {
    if (hits <= 0 || ring_sum < hits || ring_sum > 10 * hits) return Error::InvalidChoice;
    I duration = 0;
    switch (arms) {
    case 5: duration = 30000; break;
    case 6: duration = 35000; break;
    case 7: duration = 40000; break;
    case 8: duration = 45000; break;
    case 9: duration = 50000; break;
    case 10: duration = 60000; break;
    default: return Error::InvalidChoice;
    }
    if (ring_sum <= 3 * hits) return EnergyBuff{150, 25, 10, duration};
    if (ring_sum <= 7 * hits) return EnergyBuff{150, 25, 20, duration};
    if (ring_sum <= 8 * hits) return EnergyBuff{200, 25, 20, duration};
    if (ring_sum <= 9 * hits) return EnergyBuff{200, 25, 30, duration};
    return EnergyBuff{300, 50, 50, duration};
}

I radar_mark_vulnerability(I p) { return p < 1000 ? 0 : p < 1200 ? 15 : 20; }
I heavy_opening_defense(I elapsed, bool installed) { return elapsed < 180000 && !installed ? 50 : 0; }

// ---- Sentry posture.
void SentryPosture::accrue(I t) {
    const auto p = std::size_t(state_.posture);
    state_.accumulated[p] += t - state_.time_ms;
    if (state_.enhanced) state_.enhanced_used[p] += t - state_.time_ms;
    state_.time_ms = t;
}
Result<I> SentryPosture::advance(I t) {
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    const auto p = std::size_t(state_.posture);
    if (state_.enhanced && t - state_.time_ms >= ENHANCED_LIMIT_MS - state_.enhanced_used[p]) {
        accrue(state_.time_ms + ENHANCED_LIMIT_MS - state_.enhanced_used[p]);
        state_.enhanced = false; // p110 example two: returns to the base posture
    }
    accrue(t);
    return t;
}
Result<I> SentryPosture::select(I t, Posture p, bool enhanced) {
    if (std::size_t(p) > 2) return Error::InvalidChoice;
    const State before = state_;
    const auto settled = advance(t);
    if (auto e = std::get_if<Error>(&settled)) { state_ = before; return *e; }
    if (state_.posture == p && state_.enhanced == enhanced) return t;
    if (t - state_.last_switch_ms < POSTURE_SWITCH_COOLDOWN_MS) { state_ = before; return Error::OnCooldown; }
    if (enhanced && state_.enhanced_used[std::size_t(p)] >= ENHANCED_LIMIT_MS) { state_ = before; return Error::EnhancedExhausted; }
    state_.posture = p;
    state_.enhanced = enhanced;
    state_.last_switch_ms = t;
    return t;
}
PostureEffect SentryPosture::effect() const {
    const bool degraded = state_.accumulated[std::size_t(state_.posture)] > POSTURE_DEGRADE_AFTER_MS;
    PostureEffect e;
    switch (state_.posture) {
    case Posture::Attack:
        e.vulnerability = 25;
        e.power_scale_den = 2;
        if (state_.enhanced) e.heat_always_zero = true;
        else e.cooling_buff_tenths = degraded ? 20 : 30;
        break;
    case Posture::Defense:
        e.defense = state_.enhanced ? 99 : degraded ? 25 : 50;
        e.power_scale_den = 2;
        e.cooling_scale_den = 3;
        break;
    case Posture::Move:
        e.vulnerability = 25;
        e.cooling_scale_den = 3;
        if (state_.enhanced) e.power_override_w = 200;
        else e.power_buff_tenths = degraded ? 12 : 15;
        break;
    }
    return e;
}

// ---- 42 mm shield.
void Shield42::launch(I t, bool alive, I allowance_before) {
    if (alive && allowance_before <= 0) state_.overfire = true;
    else if (alive) state_.last_launch_ms = t;
}
void Shield42::heavy_died(I t) { state_.dead = true; state_.death_ms = t; }
void Shield42::update(I, bool alive, I allowance) {
    if (alive && allowance > 0) { state_.dead = false; state_.overfire = false; }
}
bool Shield42::shielded(I t) const {
    if (state_.overfire) return true;
    if (state_.dead && t - state_.death_ms >= 3000) return true;
    return state_.last_launch_ms < 0 || t - state_.last_launch_ms >= 4000;
}
} // namespace rmuc2027::buff_v1
