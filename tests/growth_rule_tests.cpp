#include "../rules/rmuc2027_growth_v1/growth_rules.hpp"
#include "rule_check.hpp"
#include <vector>

namespace g = rmuc2027::growth_v1;
using I = std::int64_t;

int main() {
    // --- Table 5-17/5-18 infantry rows, every level (printed pp92-94).
    const I power_hp[] = {150, 175, 200, 225, 250, 275, 300, 325, 350, 400};
    const I power_w[] = {60, 65, 70, 75, 80, 85, 90, 95, 100, 100};
    const I hp_hp[] = {200, 225, 250, 275, 300, 325, 350, 375, 400, 400};
    const I hp_w[] = {45, 50, 55, 60, 65, 70, 75, 80, 90, 100};
    const I burst_q[] = {170, 180, 190, 200, 210, 220, 230, 240, 250, 260};
    const I burst_c[] = {5, 7, 9, 11, 12, 13, 14, 16, 18, 20};
    const I cool_q[] = {40, 48, 56, 64, 72, 80, 88, 96, 104, 120};
    const I cool_c[] = {12, 14, 16, 18, 20, 22, 24, 26, 28, 30};
    for (I level = 1; level <= 10; ++level) {
        const auto i = std::size_t(level - 1);
        for (auto chassis : {g::Chassis::PowerFirst, g::Chassis::HpFirst})
            for (auto launcher : {g::Launcher::BurstFirst, g::Launcher::CoolingFirst}) {
                const auto p = ok(g::performance({g::RobotClass::Infantry, chassis, launcher}, level));
                const bool power = chassis == g::Chassis::PowerFirst, burst = launcher == g::Launcher::BurstFirst;
                CHECK(p.max_hp == (power ? power_hp[i] : hp_hp[i]));
                CHECK(p.chassis_power_w == (power ? power_w[i] : hp_w[i]));
                CHECK(p.heat_limit == (burst ? burst_q[i] : cool_q[i]));
                CHECK(p.cooling_per_second == (burst ? burst_c[i] : cool_c[i]));
                CHECK(p.caliber == g::Caliber::Mm17 && p.muzzle_speed_limit_mm_s == 25000);
            }
    }
    CHECK(is_error(g::performance({}, 0), g::Error::InvalidLevel));
    CHECK(is_error(g::performance({}, 11), g::Error::InvalidLevel));
    CHECK(is_error(g::performance({g::RobotClass::Infantry, static_cast<g::Chassis>(9)}, 1), g::Error::InvalidChoice));
    CHECK(is_error(g::performance({static_cast<g::RobotClass>(9)}, 1), g::Error::InvalidClass));
    // Default selections (§5.5.2): HP-first chassis, cooling-first launcher.
    CHECK(ok(g::performance({}, 1)) == (g::Performance{200, 45, g::Caliber::Mm17, 40, 12, 25000}));

    // --- Heavy table 5-16; heat only after the modular launcher, 16.5 m/s.
    g::Loadout heavy{g::RobotClass::Heavy};
    const std::pair<g::HeavyStage, I> stages[] = {{g::HeavyStage::Default, 400}, {g::HeavyStage::Assembly1, 450},
                                                  {g::HeavyStage::Assembly2, 500}, {g::HeavyStage::Armed, 600}};
    for (auto [stage, hp] : stages) {
        heavy.heavy_stage = stage;
        const auto p = ok(g::performance(heavy, 7)); // level ignored for heavy
        CHECK(p.max_hp == hp && p.chassis_power_w == 50);
        CHECK(p.heat_limit.has_value() == (stage == g::HeavyStage::Armed));
    }
    heavy.heavy_stage = g::HeavyStage::Armed;
    CHECK(ok(g::performance(heavy, 1)) == (g::Performance{600, 50, g::Caliber::Mm42, 300, 100, 16500}));
    // --- Sentry table 3-6.
    CHECK(ok(g::performance({g::RobotClass::SentryAuto}, 1)) == (g::Performance{400, 100, g::Caliber::Mm17, 260, 30, 25000}));
    CHECK(ok(g::performance({g::RobotClass::SentrySemi}, 9)) == (g::Performance{200, 60, g::Caliber::Mm17, 100, 10, 25000}));
    CHECK(ok(g::initial_allowance(g::RobotClass::SentryAuto)) == 300);
    CHECK(ok(g::initial_allowance(g::RobotClass::SentrySemi)) == 300);
    CHECK(ok(g::initial_allowance(g::RobotClass::Heavy)) == 0);
    CHECK(ok(g::initial_allowance(g::RobotClass::Infantry)) == 0);
    CHECK(g::uses_experience(g::RobotClass::Infantry));
    for (auto c : {g::RobotClass::Heavy, g::RobotClass::SentryAuto, g::RobotClass::SentrySemi}) CHECK(!g::uses_experience(c));
    CHECK(g::kill_level(g::RobotClass::SentryAuto, 8) == 1 && g::kill_level(g::RobotClass::Heavy, 7) == 7);

    // --- Table 5-15 thresholds and boundaries in tenths.
    const I thresholds[] = {0, 550, 1100, 1650, 2200, 2750, 3300, 3850, 4400, 5000};
    for (I level = 1; level <= 10; ++level) {
        const I t = thresholds[level - 1] * 10;
        CHECK(g::experience_for_level_tenths(level) == t);
        CHECK(ok(g::level_for_experience_tenths(t)) == level);
        if (level > 1) CHECK(ok(g::level_for_experience_tenths(t - 1)) == level - 1);
    }
    CHECK(ok(g::level_for_experience_tenths(1'000'000)) == 10);
    CHECK(is_error(g::level_for_experience_tenths(-1), g::Error::NegativeValue));
    // Assembly-dependent team cap (table 5-12): 5, then 7, then 10.
    CHECK(ok(g::level_for_experience_tenths(44000, ok(g::team_level_cap(0)))) == 5);
    CHECK(ok(g::level_for_experience_tenths(44000, ok(g::team_level_cap(1)))) == 5);
    CHECK(ok(g::level_for_experience_tenths(44000, ok(g::team_level_cap(2)))) == 7);
    CHECK(ok(g::level_for_experience_tenths(44000, ok(g::team_level_cap(3)))) == 9);
    CHECK(ok(g::team_level_cap(4)) == 10 && is_error(g::team_level_cap(5), g::Error::InvalidChoice));
    CHECK(ok(g::heavy_level(0)) == 1 && ok(g::heavy_level(1)) == 5 && ok(g::heavy_level(2)) == 7);
    CHECK(ok(g::heavy_level(3)) == 10 && ok(g::heavy_level(4)) == 10);

    // --- Assembly rewards (table 5-12).
    auto r1 = ok(g::first_assembly_reward(1)), r3 = ok(g::first_assembly_reward(3)), r4 = ok(g::first_assembly_reward(4));
    CHECK(r1.heavy_level == 5 && r1.gold_per_10s == 40 && !r1.unlocks_42mm && r1.team_defense_percent == 0);
    CHECK(ok(g::first_assembly_reward(2)).team_level_cap == 7);
    CHECK(r3.unlocks_42mm && r3.team_defense_percent == 25 && r3.team_level_cap == 10 && r3.heavy_level == 10);
    CHECK(r4.team_defense_percent == 50 && r4.gold_per_10s == 40);
    CHECK(ok(g::repeat_gold_per_10s(1)) == 4 && ok(g::repeat_gold_per_10s(2)) == 8 && ok(g::repeat_gold_per_10s(3)) == 12);
    CHECK(is_error(g::repeat_gold_per_10s(4), g::Error::InvalidChoice));
    CHECK(is_error(g::first_assembly_reward(0), g::Error::InvalidChoice));

    // --- Table 5-14 experience.
    CHECK(g::shot_experience_tenths(g::RobotClass::Infantry, 3) == 30);
    CHECK(g::shot_experience_tenths(g::RobotClass::SentryAuto, 3) == 0);
    CHECK(g::shot_experience_tenths(g::RobotClass::Heavy, 3) == 0);
    CHECK(ok(g::damage_experience_tenths(g::TargetKind::Robot, 20)) == 800);   // 4/pt
    CHECK(ok(g::damage_experience_tenths(g::TargetKind::Outpost, 20)) == 400); // 2/pt
    CHECK(ok(g::damage_experience_tenths(g::TargetKind::Base, 20)) == 100);    // 1 per 2
    CHECK(ok(g::damage_experience_tenths(g::TargetKind::Base, 21)) == 110);    // odd rounds up
    CHECK(ok(g::damage_experience_tenths(g::TargetKind::Base, 1)) == 10);
    CHECK(ok(g::damage_experience_tenths(g::TargetKind::Base, 0)) == 0);
    CHECK(is_error(g::damage_experience_tenths(g::TargetKind::Robot, -1), g::Error::NegativeValue));
    // p90 example one: level2 killer, level6 victim -> 540.
    CHECK(ok(g::kill_experience_tenths(6, 2)) == 5400);
    CHECK(ok(g::kill_experience_tenths(6, 6)) == 3000);
    CHECK(ok(g::kill_experience_tenths(2, 6)) == 1000); // lower victim: gap treated as0
    CHECK(ok(g::kill_experience_tenths(10, 1)) == 14000);
    CHECK(is_error(g::kill_experience_tenths(0, 1), g::Error::InvalidLevel));
    // p89 example: 120 unattributed 17 mm damage split by two infantry -> 240 each.
    CHECK(ok(g::split_tenths(ok(g::damage_experience_tenths(g::TargetKind::Robot, 120)), 2)) == 2400);
    CHECK(ok(g::split_tenths(100, 3)) == 33);   // 3.33.. -> 3.3
    CHECK(ok(g::split_tenths(5, 2)) == 3);      // 0.25 -> 0.3 (half up)
    CHECK(ok(g::split_tenths(200, 3)) == 67);   // 6.66.. -> 6.7
    CHECK(is_error(g::split_tenths(10, 0), g::Error::NoRecipients));
    // p90 example two: victims level9; recipients 1250 and 4000 (avg 2625 -> level5): 405 each.
    {
        std::vector<I> exp{12500, 40000};
        g::UnattributedKill k{9, true, exp};
        CHECK(ok(g::unattributed_kill_share_tenths(k)) == 4050);
        k.level_cap = 5;
        CHECK(ok(g::unattributed_kill_share_tenths(k)) == 4050);
        k.level_cap = 3; // average level is capped too: 50*9*(1+6*0.2)/2 = 495
        CHECK(ok(g::unattributed_kill_share_tenths(k)) == 4950);
        k.level_cap = 10;
        k.projectile = false;
        CHECK(is_error(g::unattributed_kill_share_tenths(k), g::Error::UnadjudicatedNonProjectileKiller));
        k.policy = g::KillerPolicy::EngineeringAverageLevelForNonProjectileV1;
        CHECK(ok(g::unattributed_kill_share_tenths(k)) == 4050);
        std::vector<I> none;
        CHECK(is_error(g::unattributed_kill_share_tenths({9, true, none}), g::Error::NoRecipients));
        // Exact average threshold: avg exactly 2200 -> level5, 2199.9 -> level4.
        std::vector<I> boundary{22000, 22000}, below{21999, 22000};
        CHECK(ok(g::unattributed_kill_share_tenths({5, true, boundary})) == 1250);    // 50*5/2
        CHECK(ok(g::unattributed_kill_share_tenths({5, true, below})) == 1500);       // 50*5*1.2/2
    }
    // --- Levelling adds the max-HP increase to current HP (§5.5.2).
    CHECK(ok(g::hp_after_max_change(120, 200, 225)) == 145);
    CHECK(ok(g::hp_after_max_change(0, 200, 225)) == 25);
    CHECK(ok(g::hp_after_max_change(400, 400, 400)) == 400); // HP-first level9->10
    CHECK(ok(g::hp_after_max_change(300, 400, 250)) == 250);
    CHECK(is_error(g::hp_after_max_change(201, 200, 225), g::Error::InvalidChoice));
    // A whole level ladder never decreases infantry max HP.
    for (auto chassis : {g::Chassis::PowerFirst, g::Chassis::HpFirst})
        for (I level = 1; level < 10; ++level)
            CHECK(ok(g::performance({g::RobotClass::Infantry, chassis}, level + 1)).max_hp >=
                  ok(g::performance({g::RobotClass::Infantry, chassis}, level)).max_hp);
    return finish("growth rules (tables 3-3..3-6, 5-12, 5-14..5-18)");
}
