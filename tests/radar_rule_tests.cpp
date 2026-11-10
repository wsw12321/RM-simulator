#include "../rules/rmuc2027_buff_v1/buff_rules.hpp"
#include "../rules/rmuc2027_radar_v1/radar_rules.hpp"
#include "rule_check.hpp"

namespace r = rmuc2027::radar_v1;
namespace buff = rmuc2027::buff_v1;
using I = std::int64_t;
using r::Mark;

static I P(r::MarkProgress& m, I t, Mark mark) { return ok(m.report(t, mark), "report").progress_tenths; }

int main() {
    {   // d classes: accurate < 0.8 m, semi-accurate < 1.6 m, else wrong (p112).
        CHECK(ok(r::classify(0)) == Mark::Accurate && ok(r::classify(799)) == Mark::Accurate);
        CHECK(ok(r::classify(800)) == Mark::SemiAccurate && ok(r::classify(1599)) == Mark::SemiAccurate);
        CHECK(ok(r::classify(1600)) == Mark::Wrong && ok(r::classify(50000)) == Mark::Wrong);
        CHECK(is_error(r::classify(-1), r::Error::NegativeValue));
    }
    {   // The manual's worked example (p113), step by step.
        r::MarkProgress m;
        // 5 Hz, 100% accurate: 1, 3, 6, 10, 15 within the first second ...
        const I first[5] = {10, 30, 60, 100, 150};
        for (int i = 0; i < 5; ++i) CHECK(P(m, 200 * (i + 1), Mark::Accurate) == first[i]);
        // ... and 120 at 3.0 s.
        for (int i = 6; i <= 15; ++i) P(m, 200 * i, Mark::Accurate);
        CHECK(m.snapshot().progress_tenths == 1200 && m.snapshot().influence_tenths == 150 && m.snapshot().time_ms == 3000);
        // 5 Hz, 0% accurate in 3.0-4.0 s: 119.2, 117.6, 115.2, 112, 108.
        const I wrong[5] = {1192, 1176, 1152, 1120, 1080};
        for (int i = 0; i < 5; ++i) CHECK(P(m, 3000 + 200 * (i + 1), Mark::Wrong) == wrong[i]);
        // 1.0 s without data: two more consecutive "wrong" verdicts -> 103.2, 97.6.
        CHECK(ok(m.advance(4499)).progress_tenths == 1080 && m.snapshot().interruptions == 0);
        CHECK(ok(m.advance(4500)).progress_tenths == 1032);
        CHECK(ok(m.advance(4999)).progress_tenths == 1032);
        CHECK(ok(m.advance(5000)).progress_tenths == 976 && m.snapshot().interruptions == 2);
        // Two correct coordinates: 98.6, 100.6.
        CHECK(P(m, 5200, Mark::Accurate) == 986 && P(m, 5400, Mark::Accurate) == 1006);
        CHECK(m.snapshot().reports == 22 && m.snapshot().influence_tenths == 20);
    }
    {   // x rules of table 5-29 in isolation.
        r::MarkProgress m;
        CHECK(P(m, 100, Mark::SemiAccurate) == 5 && m.snapshot().influence_tenths == 5);   // otherwise x = 0.5
        CHECK(P(m, 200, Mark::SemiAccurate) == 15 && m.snapshot().influence_tenths == 10); // after semi: x + 0.5
        CHECK(P(m, 300, Mark::Accurate) == 35 && m.snapshot().influence_tenths == 20);     // after semi: x + 1.0
        CHECK(P(m, 400, Mark::SemiAccurate) == 60 && m.snapshot().influence_tenths == 25); // after accurate: x + 0.5
        CHECK(P(m, 500, Mark::Wrong) == 52 && m.snapshot().influence_tenths == -8);        // after a good mark: x = -0.8
        CHECK(P(m, 600, Mark::Wrong) == 36 && m.snapshot().influence_tenths == -16);
        CHECK(P(m, 700, Mark::Accurate) == 46 && m.snapshot().influence_tenths == 10);     // after wrong: x = 1.0
        CHECK(P(m, 800, Mark::Wrong) == 38 && P(m, 900, Mark::SemiAccurate) == 43);        // after wrong: x = 0.5
        CHECK(is_error(m.report(899, Mark::Accurate), r::Error::NonIncreasingTime));
        CHECK(is_error(m.report(900, static_cast<Mark>(5)), r::Error::InvalidChoice));
        CHECK(is_error(m.advance(100), r::Error::NonIncreasingTime));
    }
    {   // Clamps at 0 and 150; an interruption verdict precedes a report of the same millisecond.
        r::MarkProgress m;
        CHECK(P(m, 100, Mark::Wrong) == 0 && m.snapshot().influence_tenths == -8);
        for (int i = 2; i <= 40; ++i) P(m, 100 * i, Mark::Accurate);
        CHECK(m.snapshot().progress_tenths == r::PROGRESS_MAX_TENTHS);
        CHECK(P(m, 4100, Mark::Accurate) == 1500);
        const auto s = ok(m.report(4600, Mark::Accurate)); // exactly 0.5 s later
        CHECK(s.interruptions == 1 && s.influence_tenths == 10 && s.progress_tenths == 1500); // 150 - 0.8 + 1.0, clamped
        r::MarkProgress silent; // no radar at all: P stays 0
        CHECK(ok(silent.advance(420000)).progress_tenths == 0 && silent.snapshot().interruptions == 840);
        CHECK(P(silent, 420100, Mark::Accurate) == 10);
    }
    {   // Revival keeps P and resets x to 0 (p113).
        r::MarkProgress m;
        for (int i = 1; i <= 10; ++i) P(m, 200 * i, Mark::Accurate);
        CHECK(m.snapshot().progress_tenths == 550 && m.snapshot().influence_tenths == 100);
        const auto s = ok(m.revived(2100));
        CHECK(s.progress_tenths == 550 && s.influence_tenths == 0);
        CHECK(P(m, 2200, Mark::Accurate) == 560 && P(m, 2400, Mark::Accurate) == 580);
        ok(m.revived(2450));
        CHECK(P(m, 2500, Mark::Wrong) == 572);
        CHECK(is_error(m.revived(2000), r::Error::NonIncreasingTime));
    }
    {   // Table 5-30 thresholds, identical to the buff_v1 copy for every P.
        CHECK(r::mark_vulnerability_percent(999) == 0 && r::mark_vulnerability_percent(1000) == 15);
        CHECK(r::mark_vulnerability_percent(1199) == 15 && r::mark_vulnerability_percent(1200) == 20 && r::mark_vulnerability_percent(1500) == 20);
        for (I p = 0; p <= r::PROGRESS_MAX_TENTHS; ++p) {
            CHECK(r::mark_vulnerability_percent(p) == buff::radar_mark_vulnerability(p));
            CHECK(r::vulnerability_percent(p, true, true) == 2 * r::mark_vulnerability_percent(p));
            CHECK(r::vulnerability_percent(p, false, true) == r::mark_vulnerability_percent(p) && r::vulnerability_percent(p, true, false) == 0);
        }
    }
    {   // Double vulnerability: one chance per accumulated minute, at most two triggers, 30 s each.
        r::DoubleVulnerability d;
        CHECK(is_error(d.trigger(0), r::Error::NoChance));
        CHECK(ok(d.advance(30000, true)).charge_ms == 30000);
        CHECK(ok(d.advance(50000, false)).charge_ms == 30000);     // only time with a vulnerable enemy counts
        CHECK(ok(d.advance(79999, true)).chances == 0);
        CHECK(ok(d.advance(80000, true)).chances == 1 && d.snapshot().charge_ms == 0);
        const auto on = ok(d.trigger(80000));
        CHECK(on.chances == 0 && on.triggers == 1 && on.active_until_ms == 110000 && on.active(80000) && on.active(109999) && !on.active(110000));
        CHECK(ok(d.advance(140000, true)).chances == 1);            // the doubled period also accumulates
        CHECK(ok(d.advance(400000, true)).chances == 1 && d.snapshot().charge_ms == 0); // never more than two in total
        ok(d.trigger(400000));
        CHECK(d.snapshot().triggers == 2 && is_error(d.trigger(430000), r::Error::NoChance));
        CHECK(ok(d.advance(420000, true)).chances == 0 && d.snapshot().charge_ms == 0);
        CHECK(is_error(d.advance(419999, true), r::Error::NonIncreasingTime));
        r::DoubleVulnerability two; // both chances held, the second cannot start inside the first period
        ok(two.advance(130000, true));
        CHECK(two.snapshot().chances == 2 && two.snapshot().charge_ms == 0);
        ok(two.trigger(130000));
        CHECK(is_error(two.trigger(159999), r::Error::AlreadyActive));
        CHECK(ok(two.trigger(160000)).triggers == 2 && is_error(two.trigger(129999), r::Error::NonIncreasingTime));
    }
    {   // Jammer levels: the side that decoded further is immune to the other radar.
        r::Jammers j;
        CHECK(j.level(r::Team::Red) == 1 && j.level(r::Team::Blue) == 1);
        CHECK(j.marks_effective(r::Team::Red) && j.marks_effective(r::Team::Blue));
        CHECK(j.decoded(r::Team::Red) == 2);
        CHECK(j.marks_effective(r::Team::Red) && !j.marks_effective(r::Team::Blue)); // blue's marks on red are void
        CHECK(j.decoded(r::Team::Blue) == 2 && j.marks_effective(r::Team::Blue) && j.marks_effective(r::Team::Red));
        CHECK(j.decoded(r::Team::Blue) == 3 && j.decoded(r::Team::Blue) == 3);          // three levels only
        CHECK(!j.marks_effective(r::Team::Red) && j.marks_effective(r::Team::Blue));
    }
    {   // Randomised reports against an independent re-statement of table 5-29.
        std::uint64_t seed = 20270002;
        auto next = [&] { seed = seed * 6364136223846793005ull + 1442695040888963407ull; return seed >> 33; };
        for (int run = 0; run < 200; ++run) {
            r::MarkProgress m;
            I t = 0, p = 0, x = 0, silence = 0;
            int last = 0; // 0 none, 1 good, 2 bad
            auto wrong = [&] { x = last == 2 ? x - 8 : -8; last = 2; p = std::max<I>(0, p + x); };
            for (int i = 0; i < 300; ++i) {
                const I dt = I(next() % (next() % 8 ? 300 : 1800));
                t += dt;
                while (silence + 500 <= t) { silence += 500; wrong(); }
                const auto kind = next() % 10;
                if (kind < 8) {
                    const Mark mark = kind < 5 ? Mark::Accurate : kind < 7 ? Mark::SemiAccurate : Mark::Wrong;
                    if (mark == Mark::Wrong) wrong();
                    else { const I step = mark == Mark::Accurate ? 10 : 5; x = last == 1 ? x + step : step; last = 1; p = std::min<I>(1500, p + x); }
                    silence = t;
                    ok(m.report(t, mark));
                } else if (kind == 8) { x = 0; last = 0; ok(m.revived(t)); }
                else ok(m.advance(t));
                CHECK(m.snapshot().progress_tenths == p && m.snapshot().influence_tenths == x);
                CHECK(p >= 0 && p <= 1500 && m.snapshot().time_ms == t);
            }
        }
    }
    return finish("radar marking rules (§5.7.4, tables 5-29/5-30)");
}
