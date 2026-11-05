#include "../rules/rmuc2027_buff_v1/buff_rules.hpp"
#include "rule_check.hpp"
#include <vector>

namespace b = rmuc2027::buff_v1;
using I = std::int64_t;
using V = std::vector<I>;

static I hit(b::Target t, b::Source s, V atk = {}, V def = {}, V vul = {}, b::ArmorRegion r = b::ArmorRegion::Default) {
    b::DamageInput in;
    in.target = t; in.source = s; in.region = r;
    in.attack_percent = atk; in.defense_percent = def; in.vulnerability_percent = vul;
    return ok(b::settle_damage(in), "settle");
}

int main() {
    using T = b::Target; using S = b::Source;
    // --- Table 5-2 every defined cell, and "-" cells rejected.
    CHECK(ok(b::raw_damage(T::RobotArmor, S::Projectile42)) == 200 && ok(b::raw_damage(T::RobotArmor, S::Projectile17)) == 20);
    CHECK(ok(b::raw_damage(T::RobotArmor, S::Collision)) == 2);
    CHECK(is_error(b::raw_damage(T::RobotArmor, S::AerialImpact), b::Error::InvalidChoice));
    CHECK(is_error(b::raw_damage(T::RobotArmor, S::Dart), b::Error::InvalidChoice));
    for (auto t : {T::BaseArmor, T::OutpostMiddleArmor}) {
        CHECK(ok(b::raw_damage(t, S::Projectile42)) == 200 && ok(b::raw_damage(t, S::Projectile17)) == 20);
        CHECK(ok(b::raw_damage(t, S::AerialImpact)) == 200);
        CHECK(is_error(b::raw_damage(t, S::Dart), b::Error::InvalidChoice) && is_error(b::raw_damage(t, S::Collision), b::Error::InvalidChoice));
    }
    CHECK(ok(b::raw_damage(T::BaseDartModule, S::Dart, b::DartTarget::Fixed)) == 200);
    CHECK(ok(b::raw_damage(T::BaseDartModule, S::Dart, b::DartTarget::RandomFixed)) == 300);
    CHECK(ok(b::raw_damage(T::BaseDartModule, S::Dart, b::DartTarget::RandomMoving)) == 800);
    CHECK(ok(b::raw_damage(T::BaseDartModule, S::Dart, b::DartTarget::TerminalMoving)) == 1250);
    CHECK(is_error(b::raw_damage(T::BaseDartModule, S::Dart), b::Error::InvalidChoice));
    CHECK(ok(b::raw_damage(T::OutpostDartModule, S::Dart)) == 1000);
    CHECK(is_error(b::raw_damage(T::OutpostDartModule, S::Projectile17), b::Error::InvalidChoice));

    // --- §5.6.4.1 worked examples (p104-105).
    CHECK(hit(T::RobotArmor, S::Projectile42, {200}, {25}, {15}) == 360);
    CHECK(hit(T::RobotArmor, S::Projectile42, {200}, {}, {15}) == 460);
    CHECK(b::net_defense_percent(V{25, 50}, V{}) == 50 && b::net_defense_percent(V{25, 50}, V{15}) == 35);
    CHECK(b::net_defense_percent(V{25}, V{100}) == -75 && b::net_defense_percent(V{25}, V{100, 15}) == -75);
    // Same-type buffs take the maximum, never a sum.
    CHECK(hit(T::RobotArmor, S::Projectile17, {150, 200}) == 40);
    CHECK(hit(T::RobotArmor, S::Projectile17, {}, {25, 50}) == 10);
    // Rounding half-up of the final value.
    CHECK(hit(T::RobotArmor, S::Projectile17, {150}, {25}) == 23); // 22.5
    CHECK(hit(T::RobotArmor, S::Projectile17, {}, {99}) == 0);     // 0.2
    CHECK(hit(T::RobotArmor, S::Projectile17, {}, {}, {100}) == 40);
    // Table 5-5 adaptive armour regions (projectiles only).
    CHECK(hit(T::RobotArmor, S::Projectile17, {}, {}, {}, b::ArmorRegion::Unreinforced) == 30);
    CHECK(hit(T::RobotArmor, S::Projectile17, {}, {}, {}, b::ArmorRegion::Reinforced) == 10);
    CHECK(hit(T::RobotArmor, S::Projectile42, {}, {}, {}, b::ArmorRegion::Reinforced) == 100);
    CHECK(hit(T::RobotArmor, S::Collision, {}, {}, {}, b::ArmorRegion::Unreinforced) == 2);
    CHECK(hit(T::RobotArmor, S::Projectile17, {200}, {50}, {}, b::ArmorRegion::Unreinforced) == 30); // 20*1.5*2*0.5
    // Base 100% defence (outpost held) -> no damage; vulnerability reopens it.
    CHECK(hit(T::BaseArmor, S::Projectile42, {}, {b::BASE_OUTPOST_DEFENSE}) == 0);
    CHECK(hit(T::BaseArmor, S::Projectile17, {}, {100}, {15}) == 3);
    // Defence never applies to darts; attack buffs never apply to darts.
    {
        b::DamageInput dart;
        dart.target = T::BaseDartModule; dart.source = S::Dart; dart.dart = b::DartTarget::RandomMoving;
        V def{100}, atk{300};
        dart.defense_percent = def; dart.attack_percent = atk;
        CHECK(ok(b::settle_damage(dart)) == 800);
        dart.invincible = true;
        CHECK(ok(b::settle_damage(dart)) == 0);
    }
    {   // Invincibility and the 42 mm shield.
        b::DamageInput in;
        in.source = S::Projectile42; in.shield_42mm = true;
        CHECK(ok(b::settle_damage(in)) == 0);
        in.source = S::Projectile17;
        CHECK(ok(b::settle_damage(in)) == 20);
        in.invincible = true;
        CHECK(ok(b::settle_damage(in)) == 0);
        V bad{101};
        in.defense_percent = bad;
        CHECK(is_error(b::settle_damage(in), b::Error::InvalidChoice));
        V negative{-5};
        in.defense_percent = {};
        in.attack_percent = negative;
        CHECK(is_error(b::settle_damage(in), b::Error::NegativeValue));
    }

    // --- Cooling composition examples (p107, p109-110).
    {
        V x50{50}, add75{75}, x30{30}, x15{15};
        CHECK(ok(b::effective_cooling({40, x50, add75})) == 200);
        CHECK(ok(b::effective_cooling({30, x30, add75, 1, 3})) == 35);
        CHECK(ok(b::effective_cooling({5, x15, {}})) == 8);   // 7.5 -> 8
        CHECK(ok(b::effective_cooling({12, {}, {}})) == 12);
        CHECK(ok(b::effective_cooling({30, {}, {}, 1, 3})) == 10);
        CHECK(is_error(b::effective_cooling({30, {}, {}, 1, 0}), b::Error::InvalidChoice));
    }
    // --- Field buff points.
    {
        b::PointContext c;
        CHECK(ok(b::point_effect(b::BuffPoint::OwnBase, c)).defense == 50);
        CHECK(ok(b::point_effect(b::BuffPoint::OwnHighPoint, c)).defense == 50);
        CHECK(ok(b::point_effect(b::BuffPoint::CentralHighland, c)).defense == 25);
        CHECK(ok(b::point_effect(b::BuffPoint::OwnFortress, c)).defense == 0); // own outpost still held
        c.own_outpost_position_own_controlled = false;
        c.own_base_lowest_hp = 4000;
        auto f = ok(b::point_effect(b::BuffPoint::OwnFortress, c));
        CHECK(f.defense == 50 && f.cooling_add == 75 && f.reserve_rounds == 500);
        c.own_base_lowest_hp = 9200;
        f = ok(b::point_effect(b::BuffPoint::OwnFortress, c));
        CHECK(f.cooling_add == 10 && f.reserve_rounds == 153);
        CHECK(b::fortress_cooling(0) == 0 && b::fortress_reserve(0) == 100 && b::fortress_cooling(79) == 0);
        CHECK(ok(b::point_effect(b::BuffPoint::EnemyFortress, c)).vulnerability == 0); // before 3 min
        c.elapsed_ms = 180000;
        CHECK(ok(b::point_effect(b::BuffPoint::EnemyFortress, c)).vulnerability == 0); // enemy outpost not held
        c.enemy_outpost_position_own_controlled = true;
        CHECK(ok(b::point_effect(b::BuffPoint::EnemyFortress, c)).vulnerability == 100);
        c.enemy_base_armor_deployed = true;
        CHECK(ok(b::point_effect(b::BuffPoint::EnemyFortress, c)).vulnerability == 0);
        c.point_outpost_position_full_own = false;
        CHECK(!ok(b::point_effect(b::BuffPoint::OwnOutpost, c)).exchange);
        c.heavy = true;
        CHECK(ok(b::point_effect(b::BuffPoint::OwnSupply, c)).heavy_invincible);
    }
    // --- Terrain crossing buffs (§5.6.4.5, table 5-27).
    {
        b::TerrainBuffs t;
        auto g = ok(t.grant(1000, b::Terrain::Highland));
        CHECK(g.granted && g.first_time && t.defense_percent(30999) == 25 && t.defense_percent(31000) == 0);
        g = ok(t.grant(40000, b::Terrain::Road));
        CHECK(g.granted && g.first_time && t.defense_percent(44999) == 25 && t.defense_percent(45000) == 0);
        CHECK(!ok(t.grant(54999, b::Terrain::Road)).granted);        // within 15 s
        CHECK(ok(t.grant(55000, b::Terrain::Road)).granted);
        CHECK(!ok(t.grant(55000 + 15000, b::Terrain::Road)).first_time);
        // Holding road (5 s) then gaining tunnel-cross: 50% for max(remaining, 30 s).
        b::TerrainBuffs c;
        ok(c.grant(0, b::Terrain::Road));
        ok(c.grant(2000, b::Terrain::TunnelCross));
        CHECK(c.defense_percent(31999) == 50 && c.defense_percent(32000) == 0);
        // Holding highland (30 s) then a road: remaining 25 s dominates road's 5 s.
        b::TerrainBuffs h;
        ok(h.grant(0, b::Terrain::Highland));
        ok(h.grant(5000, b::Terrain::Road));
        CHECK(h.defense_percent(29999) == 50 && h.defense_percent(30000) == 0);
        // Tunnel-through gives 1.5x cooling for 120 s, and counts toward the combo.
        b::TerrainBuffs u;
        ok(u.grant(0, b::Terrain::TunnelThrough));
        CHECK(u.cooling_multiplier_tenths(119999) == 15 && u.cooling_multiplier_tenths(120000) == 10);
        CHECK(u.defense_percent(10) == 0);
        ok(u.grant(1000, b::Terrain::Highland));
        CHECK(u.defense_percent(119999) == 50 && u.defense_percent(120000) == 0);
        // Death clears buffs but keeps the per-round first-time flags.
        u.clear();
        CHECK(u.defense_percent(2000) == 0 && u.cooling_multiplier_tenths(2000) == 10);
        CHECK(!ok(u.grant(3000, b::Terrain::TunnelCross)).first_time); // tunnel category already earned
    }
    // --- Big energy tiers (table 5-22) and durations (table 5-23).
    CHECK(ok(b::big_energy_buff(30, 10, 5)).attack_percent == 150 && ok(b::big_energy_buff(30, 10, 5)).cooling_multiplier_tenths == 10);
    CHECK(ok(b::big_energy_buff(31, 10, 5)).cooling_multiplier_tenths == 20);
    CHECK(ok(b::big_energy_buff(80, 10, 8)).attack_percent == 200 && ok(b::big_energy_buff(80, 10, 8)).duration_ms == 45000);
    CHECK(ok(b::big_energy_buff(81, 10, 9)).cooling_multiplier_tenths == 30);
    {
        const auto top = ok(b::big_energy_buff(91, 10, 10));
        CHECK(top.attack_percent == 300 && top.defense_percent == 50 && top.cooling_multiplier_tenths == 50 && top.duration_ms == 60000);
    }
    CHECK(ok(b::big_energy_buff(6, 1, 6)).duration_ms == 35000 && ok(b::big_energy_buff(6, 1, 7)).duration_ms == 40000);
    CHECK(is_error(b::big_energy_buff(5, 1, 4), b::Error::InvalidChoice) && is_error(b::big_energy_buff(11, 1, 5), b::Error::InvalidChoice));
    CHECK(b::SMALL_ENERGY_BUFF.defense_percent == 25 && b::SMALL_ENERGY_BUFF.duration_ms == 45000);
    // --- Radar mark (table 5-30) and heavy opening defence (§5.7.1).
    CHECK(b::radar_mark_vulnerability(999) == 0 && b::radar_mark_vulnerability(1000) == 15);
    CHECK(b::radar_mark_vulnerability(1199) == 15 && b::radar_mark_vulnerability(1200) == 20 && b::radar_mark_vulnerability(1500) == 20);
    CHECK(b::heavy_opening_defense(179999, false) == 50 && b::heavy_opening_defense(180000, false) == 0);
    CHECK(b::heavy_opening_defense(0, true) == 0);

    // --- Sentry posture (table 5-28).
    {
        b::SentryPosture p;
        auto e = p.effect(); // default move posture
        CHECK(e.vulnerability == 25 && e.power_buff_tenths == 15 && e.cooling_scale_den == 3 && e.defense == 0);
        ok(p.select(1000, b::Posture::Defense, false));
        e = p.effect();
        CHECK(e.defense == 50 && e.power_scale_den == 2 && e.cooling_scale_den == 3 && e.vulnerability == 0);
        CHECK(is_error(p.select(5999, b::Posture::Attack, false), b::Error::OnCooldown));
        CHECK(p.state().posture == b::Posture::Defense);
        ok(p.select(6000, b::Posture::Attack, false));
        e = p.effect();
        CHECK(e.cooling_buff_tenths == 30 && e.vulnerability == 25 && e.power_scale_den == 2 && !e.heat_always_zero);
        ok(p.select(11000, b::Posture::Attack, true));
        CHECK(p.effect().heat_always_zero);
        ok(p.advance(25999));
        CHECK(p.state().enhanced);
        ok(p.advance(26000)); // 15 s budget spent: back to default attack
        CHECK(!p.state().enhanced && p.state().enhanced_used[0] == 15000);
        CHECK(is_error(p.select(40000, b::Posture::Attack, true), b::Error::EnhancedExhausted));
        CHECK(p.state().accumulated[2] == 1000 && p.state().accumulated[1] == 5000 && p.state().accumulated[0] == 20000); // failed select rolled back
        // p110 example two: 2:50 in move, then enhanced move -> degraded move after 15 s.
        b::SentryPosture m;
        ok(m.advance(170000));
        ok(m.select(170000, b::Posture::Move, true));
        CHECK(m.effect().power_override_w == 200);
        ok(m.advance(185000));
        CHECK(!m.state().enhanced && m.state().accumulated[2] == 185000 && m.effect().power_buff_tenths == 12);
        b::SentryPosture d;
        ok(d.select(0, b::Posture::Defense, true));
        CHECK(d.effect().defense == 99);
        ok(d.advance(200000));
        CHECK(d.effect().defense == 25); // > 3 min accumulated: degraded
        CHECK(is_error(d.advance(1), b::Error::NonIncreasingTime));
    }
    // --- 42 mm shield (§5.1.1, §5.4.2).
    {
        b::Shield42 s;
        CHECK(s.shielded(0)); // no 42 mm launch yet
        s.launch(1000, true, 5);
        CHECK(!s.shielded(4999) && s.shielded(5000));
        s.launch(9000, true, 4);
        s.heavy_died(10000);
        CHECK(!s.shielded(12999) && s.shielded(13000));
        s.update(30000, true, 0);  // revived, but no allowance yet: still shielded
        CHECK(s.state().dead && s.shielded(30000));
        s.update(31000, true, 10);
        s.launch(31000, true, 10);
        CHECK(!s.shielded(31000));
        s.launch(32000, true, 0);  // over-fire
        CHECK(s.shielded(32000));
        s.update(33000, true, 3);
        s.launch(33000, true, 3);
        CHECK(!s.shielded(33000));
    }
    return finish("buff and damage rules (tables 5-2, 5-5, 5-22..5-28, 5-30)");
}
