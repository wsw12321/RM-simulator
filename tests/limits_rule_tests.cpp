#include "../rules/rmuc2027_limits_v1/limits_rules.hpp"
#include "rule_check.hpp"
#include <vector>

namespace m = rmuc2027::limits_v1;
namespace h = rmuc2027::heat_v1;
using I = std::int64_t;

int main() {
    using C = m::Caliber;
    // --- Table 5-3 muzzle speed penalties (V0 = 25 m/s for 17 mm, 16.5 m/s for 42 mm).
    const auto pen = [](C c, I v0, I v1) { return ok(m::speed_penalty(c, v0, v1)); };
    CHECK(!pen(C::Mm17, 25000, 25000).locked);
    CHECK(pen(C::Mm17, 25000, 25001).duration_ms == 15000 && pen(C::Mm17, 25000, 29999).duration_ms == 15000);
    CHECK(pen(C::Mm17, 25000, 30000).duration_ms == 20000 && pen(C::Mm17, 25000, 34999).duration_ms == 20000);
    CHECK(pen(C::Mm17, 25000, 35000).permanent && pen(C::Mm17, 25000, 60000).permanent);
    CHECK(!pen(C::Mm42, 16500, 16500).locked && pen(C::Mm42, 16500, 16501).duration_ms == 15000);
    CHECK(pen(C::Mm42, 16500, 18000).duration_ms == 15000 && !pen(C::Mm42, 16500, 18000).permanent);
    CHECK(pen(C::Mm42, 16500, 18001).permanent);
    CHECK(is_error(m::speed_penalty(C::Mm17, 0, 1), m::Error::NegativeValue));
    {
        m::SpeedLock strict;
        ok(strict.detect(1000, C::Mm17, 25000, 26000));
        CHECK(strict.locked(15999) && !strict.locked(16000));
        CHECK(is_error(strict.detect(2000, C::Mm17, 25000, 26000), m::Error::UnadjudicatedOverlap));
        CHECK(strict.until_ms() == 16000);
        ok(strict.detect(16000, C::Mm17, 25000, 26000)); // after expiry: fine
        CHECK(strict.until_ms() == 31000);
        m::SpeedLock eng(m::OverlapPolicy::EngineeringMaxDeadlineV1);
        ok(eng.detect(1000, C::Mm17, 25000, 31000));   // 20 s -> 21000
        ok(eng.detect(2000, C::Mm17, 25000, 26000));   // 15 s -> 17000 < 21000, not added
        CHECK(eng.until_ms() == 21000);
        ok(eng.detect(10000, C::Mm17, 25000, 26000));  // 25000 > 21000
        CHECK(eng.until_ms() == 25000);
        ok(eng.detect(11000, C::Mm17, 25000, 40000));
        CHECK(eng.permanent() && eng.locked(400000));
        CHECK(is_error(eng.detect(500, C::Mm17, 25000, 25000), m::Error::NonIncreasingTime));
    }
    // --- §5.1.4 chassis power buffer.
    {
        m::ChassisPower p;
        for (I t = 100; t <= 2900; t += 100) ok(p.settle(t, 80000, 60000)); // -2 J per period
        CHECK(p.snapshot().buffer_uj == 2'000'000 && p.snapshot().chassis_enabled(2900));
        ok(p.settle(3000, 80000, 60000));
        CHECK(p.snapshot().buffer_uj == 0 && p.snapshot().cut_until_ms == 8000 && p.snapshot().cutoffs == 1);
        CHECK(!p.snapshot().chassis_enabled(7999) && p.snapshot().chassis_enabled(8000));
        ok(p.settle(3100, 200000, 60000)); // unpowered: draw is zero, +6 J
        CHECK(p.snapshot().buffer_uj == 6'000'000);
        for (I t = 3200; t <= 5000; t += 100) ok(p.settle(t, 200000, 60000));
        CHECK(p.snapshot().buffer_uj == m::BUFFER_MAX_UJ); // capped at 60 J
        CHECK(p.snapshot().cutoffs == 1);
        p.reset_on_death(6000);
        CHECK(p.snapshot().buffer_uj == m::BUFFER_MAX_UJ);
        CHECK(is_error(p.settle(5000, 0, 0), m::Error::NonIncreasingTime));
        CHECK(is_error(p.settle(6050, 0, 0), m::Error::InvalidConfig));
        // Exactly reaching zero also cuts the chassis (figure 5-2: Z <= 0).
        m::ChassisPower q;
        for (I t = 100; t <= 3000; t += 100) ok(q.settle(t, 80000, 60000));
        CHECK(q.snapshot().cutoffs == 1);
    }
    {
        std::vector<I> move{15}, degraded{12};
        CHECK(ok(m::effective_power_limit_w({100, move})) == 150);
        CHECK(ok(m::effective_power_limit_w({100, degraded})) == 120);
        CHECK(ok(m::effective_power_limit_w({100, {}, 1, 2})) == 50);
        CHECK(ok(m::effective_power_limit_w({100, move, 1, 1, 200})) == 200);
        CHECK(ok(m::effective_power_limit_w({60, {}, 1, 1, 0, true})) == 120);
        CHECK(ok(m::effective_power_limit_w({150, {}, 1, 1, 0, true})) == 200);
        CHECK(ok(m::effective_power_limit_w({100, move, 1, 1, 0, true})) == 200);
        CHECK(ok(m::effective_power_limit_w({45, {}, 1, 1, 0, true})) == 90);
    }
    // --- 42 mm heat: +100 per shot, Q2 = Q0 + 200 (§5.1.3).
    {
        m::HeatConfig cfg{C::Mm42, 300, 100, h::ThresholdPolicy::EngineeringProseStrictGreaterV1};
        auto q = ok(m::LauncherHeat::start(cfg));
        ok(q.apply(0, 3));
        CHECK(q.snapshot().heat_tenths == 3000 && !q.snapshot().firing_locked()); // Q1 = Q0 is not > Q0
        ok(q.apply(1, 1));
        CHECK(q.snapshot().overheat_locked && !q.snapshot().permanent_latched);
        ok(q.apply(2, 1)); // 500 = Q2: prose policy keeps it temporary
        CHECK(!q.snapshot().permanent_latched);
        ok(q.apply(3, 1));
        CHECK(q.snapshot().permanent_latched);
        ok(q.apply(6000, 0)); // 100/s -> 600 heat cools in 6 s
        CHECK(q.snapshot().heat_tenths == 0 && !q.snapshot().overheat_locked && q.snapshot().firing_locked());
        auto strict = ok(m::LauncherHeat::start({C::Mm42, 300, 0}));
        ok(strict.apply(0, 4));
        CHECK(is_error(strict.apply(1, 1), m::Error::UnadjudicatedThresholdEquality));
        CHECK(strict.snapshot().heat_tenths == 4000);
    }
    // --- Profile change at level-up; enhanced attack posture keeps heat at 0.
    {
        auto q = ok(m::LauncherHeat::start({C::Mm17, 40, 12, h::ThresholdPolicy::EngineeringProseStrictGreaterV1}));
        ok(q.apply(0, 4));                    // 40
        ok(q.set_profile(1000, 48, 14));      // 10 periods at 12 -> 400-120 = 280 tenths
        CHECK(q.snapshot().heat_tenths == 280 && q.snapshot().limit == 48);
        ok(q.apply(1100, 3));                 // 280-14+300 = 566 > 480 -> locked
        CHECK(q.snapshot().heat_tenths == 566 && q.snapshot().overheat_locked);
        ok(q.set_profile(1200, 48, 14, true));
        CHECK(q.snapshot().heat_tenths == 0 && !q.snapshot().overheat_locked);
        ok(q.apply(1300, 50));
        CHECK(q.snapshot().heat_tenths == 0 && q.snapshot().detected_shots == 57 && !q.snapshot().firing_locked());
        CHECK(is_error(q.set_profile(1000, 48, 14), m::Error::NonIncreasingTime));
        CHECK(is_error(m::LauncherHeat::start({C::Mm17, -1, 0}), m::Error::InvalidConfig));
    }
    // --- Differential test: 17 mm with a fixed profile equals the unchanged heat_v1.
    {
        std::uint64_t seed = 0xC0FFEEull;
        auto next = [&] { seed = seed * 6364136223846793005ull + 1442695040888963407ull; return seed >> 33; };
        long compared = 0;
        for (auto policy : {h::ThresholdPolicy::StrictRejectUnadjudicated, h::ThresholdPolicy::EngineeringProseStrictGreaterV1,
                            h::ThresholdPolicy::EngineeringFlowchartGreaterEqualV1})
            for (int run = 0; run < 300; ++run) {
                const I limit = I(next() % 300), cooling = I(next() % 40);
                h::Config hc{limit * 10, cooling, policy, h::BoundaryPolicy::EngineeringCoolingBeforeShotV1,
                             h::DeathPolicy::EngineeringShotBeforeDeathResetV1};
                auto ref = std::get<h::Heat>(h::Heat::start(hc));
                auto gen = ok(m::LauncherHeat::start({C::Mm17, limit, cooling, policy}));
                I t = 0;
                for (int i = 0; i < 300; ++i) {
                    t += I(next() % 400) + (i ? 1 : 0);
                    const I shots = next() % 3 ? I(next() % 4) : I(next() % 40);
                    const bool died = next() % 50 == 0;
                    const auto a = ref.apply({t, shots, died});
                    const auto b = gen.apply(t, shots, died);
                    CHECK(std::holds_alternative<h::Error>(a) == std::holds_alternative<m::Error>(b));
                    const auto& r = ref.snapshot();
                    const auto& g = gen.snapshot();
                    CHECK(r.heat_tenths == g.heat_tenths && r.overheat_locked == g.overheat_locked &&
                          r.permanent_latched == g.permanent_latched && r.detected_shots == g.detected_shots &&
                          r.last_event_ms == g.last_event_ms);
                    ++compared;
                }
            }
        CHECK(compared == 270000);
    }
    return finish("limits rules (table 5-3, figures 5-1/5-2, heat_v1 differential)");
}
