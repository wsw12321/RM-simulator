#include "../rules/rmuc2027_buff_v1/buff_rules.hpp"
#include "../rules/rmuc2027_energy_v1/energy_rules.hpp"
#include "rule_check.hpp"
#include <map>

namespace e = rmuc2027::energy_v1;
namespace buff = rmuc2027::buff_v1;
using I = std::int64_t;
using e::EventKind;

static e::EnergyMechanism make(std::uint64_t seed = 7, e::ChancePolicy p = e::ChancePolicy::EngineeringConsumeOnTriggerV1) {
    return std::get<e::EnergyMechanism>(e::EnergyMechanism::start(seed, p));
}
static int count(const e::Step& s, EventKind k) { int n = 0; for (const auto& x : s.events) n += x.kind == k; return n; }
static int bits(std::uint8_t m) { int n = 0; for (int i = 0; i < 5; ++i) n += (m >> i) & 1; return n; }
static int lit_arm(const e::EnergyMechanism& m, int skip = -1) {
    for (int a = 0; a < 5; ++a) if (a != skip && ((m.snapshot().lit_mask >> a) & 1)) return a;
    return -1;
}
static int unlit_arm(const e::EnergyMechanism& m) {
    for (int a = 0; a < 5; ++a) if (!((m.snapshot().lit_mask >> a) & 1) && a != m.snapshot().first_hit_arm) return a;
    return -1;
}
// Completes a small activation with one hit every `gap` ms; returns the activation time.
static I activate_small(e::EnergyMechanism& m, I t, I gap = 500) {
    ok(m.trigger(t, e::Operator::InfantryOperator), "small trigger");
    for (int i = 0; i < 5; ++i) { t += gap; ok(m.hit(t, lit_arm(m), 5), "small hit"); }
    return t;
}
// Completes a big activation; `both` hits the second module of every group.
static I activate_big(e::EnergyMechanism& m, I t, I ring, bool both) {
    ok(m.trigger(t, e::Operator::Sentry), "big trigger");
    for (int g = 0; g < 5; ++g) {
        t += 300;
        const int first = lit_arm(m);
        ok(m.hit(t, first, ring), "big first hit");
        if (both) { t += 200; ok(m.hit(t, lit_arm(m), ring), "big second hit"); }
        else { t += e::SECOND_HIT_WINDOW_MS; ok(m.advance(t), "big group close"); }
    }
    return t;
}

int main() {
    {   // Chances: 0 s and 90 s (small), 180 s / 255 s / 330 s (big); they accumulate (p97).
        auto m = make();
        CHECK(m.snapshot().small_chances == 1 && m.snapshot().big_chances == 0 && m.snapshot().state == e::State::Inactive);
        CHECK(ok(m.advance(89999)).events.empty());
        const auto s = ok(m.advance(90000));
        CHECK(s.events.size() == 1 && s.events[0].kind == EventKind::ChanceGranted && s.events[0].time_ms == 90000 && s.events[0].value == 2);
        CHECK(m.snapshot().small_chances == 2);
        const auto all = ok(m.advance(420000));
        CHECK(count(all, EventKind::ChanceGranted) == 3 && m.snapshot().big_chances == 3);
        CHECK(m.snapshot().small_chances == 0 && m.snapshot().big_phase); // small chances end with the small phase
        CHECK(all.events[0].time_ms == 180000 && all.events[1].time_ms == 255000 && all.events[2].time_ms == 330000);
        CHECK(e::phase_at(179999) == e::Phase::Small && e::phase_at(180000) == e::Phase::Big);
        CHECK(is_error(m.advance(419999), e::Error::NonIncreasingTime));
    }
    {   // Small mechanism: one lit module at a time, five hits activate, 45 s buff.
        auto m = make(11);
        const auto t0 = ok(m.trigger(1000, e::Operator::InfantryOperator));
        CHECK(count(t0, EventKind::Triggered) == 1 && count(t0, EventKind::GroupLit) == 1);
        CHECK(m.snapshot().state == e::State::Activating && m.snapshot().small_chances == 0 && bits(m.snapshot().lit_mask) == 1);
        CHECK(is_error(m.trigger(1000, e::Operator::InfantryOperator), e::Error::NotInactive));
        std::uint8_t seen = 0;
        I t = 1000;
        for (int i = 0; i < 5; ++i) {
            const int arm = lit_arm(m);
            CHECK(!((seen >> arm) & 1)); // never an arm already activated in this attempt
            seen |= std::uint8_t(1u << arm);
            t += 2499;
            const auto h = ok(m.hit(t, arm, 1));
            CHECK(count(h, EventKind::ArmHit) == 1 && count(h, EventKind::FailedTimeout) == 0);
            if (i < 4) CHECK(m.snapshot().group == i + 1 && bits(m.snapshot().lit_mask) == 1 && m.snapshot().deadline_ms == t + 2500);
            else CHECK(count(h, EventKind::Activated) == 1 && h.events.back().value == 5);
        }
        CHECK(seen == 31 && m.snapshot().state == e::State::Activated && m.snapshot().buff_active());
        CHECK(m.snapshot().buff_until_ms == t + 45000 && m.snapshot().small_activations == 1);
        CHECK(e::SMALL_BUFF_MS == buff::SMALL_ENERGY_BUFF.duration_ms);
        CHECK(ok(m.advance(t + 44999)).events.empty() && m.snapshot().buff_active());
        const auto end = ok(m.advance(t + 45000));
        CHECK(end.events.size() == 1 && end.events[0].kind == EventKind::BuffEnded && end.events[0].time_ms == t + 45000);
        CHECK(m.snapshot().state == e::State::Inactive && !m.snapshot().buff_active());
        CHECK(is_error(m.trigger(t + 45000, e::Operator::InfantryOperator), e::Error::NoChance));
    }
    {   // Failure by timeout: restarts the attempt at exactly +2.5 s (timer before input).
        auto m = make(3);
        ok(m.trigger(0, e::Operator::InfantryOperator));
        ok(m.hit(1000, lit_arm(m), 5));
        ok(m.hit(2000, lit_arm(m), 5));
        CHECK(m.snapshot().group == 2 && m.snapshot().deadline_ms == 4500);
        CHECK(ok(m.advance(4499)).events.empty());
        const auto f = ok(m.advance(4500));
        CHECK(f.events.size() == 2 && f.events[0].kind == EventKind::FailedTimeout && f.events[0].time_ms == 4500 && f.events[0].value == 2);
        CHECK(f.events[1].kind == EventKind::GroupLit && m.snapshot().group == 0 && m.snapshot().done_mask == 0 && m.snapshot().failures == 1);
        CHECK(m.snapshot().state == e::State::Activating && m.snapshot().deadline_ms == 7000);
        // A long silence fails every 2.5 s until the 20 s window ends.
        const auto rest = ok(m.advance(60000));
        CHECK(count(rest, EventKind::FailedTimeout) == 6 && count(rest, EventKind::WindowExpired) == 1);
        CHECK(rest.events.back().kind == EventKind::WindowExpired && rest.events.back().time_ms == 20000);
        CHECK(m.snapshot().state == e::State::Inactive && m.snapshot().lit_mask == 0);
    }
    {   // Failure by hitting a module that is not lit, including an already activated arm.
        auto m = make(5);
        ok(m.trigger(0, e::Operator::InfantryOperator));
        const int first = lit_arm(m);
        ok(m.hit(100, first, 5));
        const auto f = ok(m.hit(200, first, 5)); // already activated arm is not the lit one
        CHECK(count(f, EventKind::FailedWrongModule) == 1 && m.snapshot().group == 0 && m.snapshot().hits == 0);
        const auto g = ok(m.hit(300, unlit_arm(m), 5));
        CHECK(count(g, EventKind::FailedWrongModule) == 1 && m.snapshot().failures == 2);
        CHECK(is_error(m.hit(300, 5, 5), e::Error::InvalidChoice) && is_error(m.hit(300, -1, 5), e::Error::InvalidChoice));
        CHECK(is_error(m.hit(300, 0, 0), e::Error::InvalidChoice) && is_error(m.hit(300, 0, 11), e::Error::InvalidChoice));
        CHECK(is_error(m.hit(299, 0, 5), e::Error::NonIncreasingTime));
    }
    {   // 20 s window and the two chance policies.
        auto a = make(1), b = make(1, e::ChancePolicy::EngineeringConsumeOnActivationV1);
        for (auto* m : {&a, &b}) {
            ok(m->trigger(5000, e::Operator::Sentry));
            const auto x = ok(m->advance(25000));
            CHECK(x.events.back().kind == EventKind::WindowExpired && x.events.back().time_ms == 25000);
            CHECK(m->snapshot().state == e::State::Inactive && m->snapshot().attempts == 1);
        }
        CHECK(a.snapshot().small_chances == 0 && b.snapshot().small_chances == 1);
        CHECK(is_error(a.trigger(25000, e::Operator::Sentry), e::Error::NoChance));
        const I t = activate_small(b, 25000);
        CHECK(b.snapshot().small_chances == 0 && b.snapshot().state == e::State::Activated && t == 27500);
        // A hit at exactly the window end is too late.
        auto c = make(9);
        ok(c.trigger(0, e::Operator::InfantryOperator));
        I tc = 0;
        for (int i = 0; i < 4; ++i) { tc += 2400; ok(c.hit(tc, lit_arm(c), 5)); }
        const auto late = ok(c.hit(20000, 0, 5));
        CHECK(count(late, EventKind::WindowExpired) == 1 && count(late, EventKind::ArmHit) == 0 && c.snapshot().state == e::State::Inactive);
        CHECK(is_error(e::EnergyMechanism::start(1, static_cast<e::ChancePolicy>(9)), e::Error::InvalidChoice));
        CHECK(is_error(a.trigger(30000, static_cast<e::Operator>(4)), e::Error::InvalidChoice));
    }
    {   // Big mechanism: two lit modules, 1 s for the second, 5..10 lit arms, tables 5-22/5-23.
        auto m = make(21);
        CHECK(is_error(m.trigger(179999, e::Operator::InfantryOperator), e::Error::NoChance) == false); // small chance, small phase
        ok(m.advance(199999)); // that small attempt was aborted at 180 s
        ok(m.trigger(200000, e::Operator::InfantryOperator));
        CHECK(m.snapshot().phase == e::Phase::Big && bits(m.snapshot().lit_mask) == 2 && m.snapshot().big_chances == 0);
        const int first = lit_arm(m);
        const auto h1 = ok(m.hit(200500, first, 8));
        CHECK(count(h1, EventKind::ArmHit) == 1 && m.snapshot().first_hit_arm == first && bits(m.snapshot().lit_mask) == 1);
        CHECK(m.snapshot().deadline_ms == 201500 && m.snapshot().group == 0);
        CHECK(ok(m.hit(200600, first, 8)).events.empty()); // repeated hit on the same module is ignored
        const auto h2 = ok(m.hit(200700, lit_arm(m), 10));
        CHECK(count(h2, EventKind::ArmHit) == 1 && count(h2, EventKind::GroupLit) == 1 && m.snapshot().group == 1);
        CHECK(m.snapshot().hits == 2 && m.snapshot().ring_sum == 18 && bits(m.snapshot().lit_mask) == 2);
        // Second group: only the first module; the group closes exactly 1 s later.
        ok(m.hit(201000, lit_arm(m), 6));
        CHECK(ok(m.advance(201999)).events.empty());
        const auto close = ok(m.advance(202000));
        CHECK(close.events.size() == 1 && close.events[0].kind == EventKind::GroupLit && close.events[0].time_ms == 202000 && m.snapshot().group == 2);
        CHECK(m.snapshot().hits == 3 && m.snapshot().ring_sum == 24);
    }
    for (int both = 0; both < 2; ++both) {   // Lit arms 5 and 10 with their durations.
        auto m = make(33 + std::uint64_t(both));
        const I t = activate_big(m, 180000, 9, both);
        const auto& s = m.snapshot();
        CHECK(s.state == e::State::Activated && s.phase == e::Phase::Big && s.big_activations == 1);
        CHECK(s.hits == (both ? 10 : 5) && s.ring_sum == 9 * s.hits);
        CHECK(s.buff_until_ms == t + (both ? 60000 : 30000));
        const auto table = ok(buff::big_energy_buff(s.ring_sum, s.hits, s.hits));
        CHECK(table.duration_ms == s.buff_until_ms - t && table.attack_percent == 200 && table.cooling_multiplier_tenths == 30);
    }
    for (I arms = 5; arms <= 10; ++arms) {   // Table 5-23 equals the buff_v1 copy.
        CHECK(ok(e::big_buff_duration_ms(arms)) == ok(buff::big_energy_buff(arms * 5, arms, arms)).duration_ms);
    }
    CHECK(is_error(e::big_buff_duration_ms(4), e::Error::InvalidChoice) && is_error(e::big_buff_duration_ms(11), e::Error::InvalidChoice));
    {   // Table 5-24: valid rings shrink after each successful big activation.
        CHECK(e::lowest_valid_ring(0) == 1 && e::lowest_valid_ring(1) == 4 && e::lowest_valid_ring(2) == 7 && e::lowest_valid_ring(3) == 7);
        auto m = make(77);
        I t = activate_big(m, 180000, 2, false);       // rings 1..10 valid on the first activation
        CHECK(m.snapshot().ring_sum == 10);
        ok(m.advance(255000));
        ok(m.trigger(255000, e::Operator::Sentry));
        CHECK(ok(m.hit(255100, lit_arm(m), 3)).events.empty());   // ring 3 no longer detected
        CHECK(ok(m.hit(255150, unlit_arm(m), 3)).events.empty()); // nor does it fail the attempt
        CHECK(count(ok(m.hit(255200, lit_arm(m), 4)), EventKind::ArmHit) == 1);
        ok(m.advance(275200)); // let the attempt lapse
        CHECK(m.snapshot().state == e::State::Inactive && m.snapshot().big_activations == 1);
        ok(m.advance(330000));
        t = activate_big(m, 330000, 7, true);
        CHECK(m.snapshot().big_activations == 2 && m.snapshot().ring_sum == 70);
        ok(m.advance(t + 60000));
        CHECK(m.snapshot().state == e::State::Inactive && m.snapshot().big_chances == 0);
        auto n = make(78, e::ChancePolicy::EngineeringConsumeOnActivationV1);
        activate_big(n, 180000, 10, true);
        ok(n.advance(260000));
        activate_big(n, 260000, 10, true);
        ok(n.advance(330000));
        ok(n.trigger(330000, e::Operator::Sentry));
        CHECK(ok(n.hit(330100, lit_arm(n), 6)).events.empty());
        CHECK(count(ok(n.hit(330200, lit_arm(n), 7)), EventKind::ArmHit) == 1);
    }
    {   // Figure 5-17: ring 10 inside Ø30, ring 1 out to Ø300, nothing beyond.
        CHECK(ok(e::ring_at_radius_mm(0)) == 10 && ok(e::ring_at_radius_mm(14)) == 10 && ok(e::ring_at_radius_mm(15)) == 9);
        CHECK(ok(e::ring_at_radius_mm(134)) == 2 && ok(e::ring_at_radius_mm(135)) == 1 && ok(e::ring_at_radius_mm(149)) == 1);
        CHECK(ok(e::ring_at_radius_mm(150)) == 0 && ok(e::ring_at_radius_mm(4000)) == 0);
        CHECK(is_error(e::ring_at_radius_mm(-1), e::Error::NegativeValue));
        for (I r = 0; r < 150; ++r) CHECK(ok(e::ring_at_radius_mm(r)) >= 1 && ok(e::ring_at_radius_mm(r)) <= 10);
    }
    {   // The fifth group's second-hit window may outlast the 20 s window.
        auto m = make(101);
        ok(m.trigger(180000, e::Operator::Sentry));
        I t = 185000;
        CHECK(count(ok(m.advance(t)), EventKind::FailedTimeout) == 2); // 5 s wasted first
        for (int g = 0; g < 4; ++g) { t += 2499; ok(m.hit(t, lit_arm(m), 5)); t += 1000; ok(m.advance(t)); }
        CHECK(t == 198996 && m.snapshot().group == 4);
        ok(m.hit(199900, lit_arm(m), 5)); // first module of the fifth group at 19.9 s
        const auto x = ok(m.advance(200900));
        CHECK(count(x, EventKind::WindowExpired) == 0 && count(x, EventKind::Activated) == 1 && x.events.back().time_ms == 200900);
        CHECK(m.snapshot().hits == 5 && m.snapshot().buff_until_ms == 230900);
        // A wrong module in that window after the 20 s leaves no time for a new attempt.
        auto w = make(102);
        ok(w.trigger(180000, e::Operator::Sentry));
        t = 185000;
        ok(w.advance(t));
        for (int g = 0; g < 4; ++g) { t += 2499; ok(w.hit(t, lit_arm(w), 5)); t += 1000; ok(w.advance(t)); }
        ok(w.hit(199900, lit_arm(w), 5));
        const auto y = ok(w.hit(200100, unlit_arm(w), 5));
        CHECK(count(y, EventKind::FailedWrongModule) == 1 && count(y, EventKind::WindowExpired) == 1 && w.snapshot().state == e::State::Inactive);
    }
    {   // Phase change: a small attempt is aborted, a running small buff completes.
        auto m = make(13);
        ok(m.advance(170000));
        ok(m.trigger(170000, e::Operator::InfantryOperator));
        const auto x = ok(m.advance(180000));
        CHECK(count(x, EventKind::SmallAbortedByBigPhase) == 1 && count(x, EventKind::ChanceGranted) == 1);
        CHECK(m.snapshot().state == e::State::Inactive && m.snapshot().small_chances == 0 && m.snapshot().big_chances == 1);
        auto b = make(14);
        ok(b.advance(160000));
        const I t = activate_small(b, 160000);
        ok(b.advance(190000));
        CHECK(b.snapshot().buff_active() && b.snapshot().phase == e::Phase::Small && b.snapshot().big_chances == 1);
        CHECK(is_error(b.trigger(190000, e::Operator::Sentry), e::Error::NotInactive));
        const auto end = ok(b.trigger(t + 45000, e::Operator::Sentry)); // buff end precedes the trigger
        CHECK(end.events[0].kind == EventKind::BuffEnded && b.snapshot().phase == e::Phase::Big && b.snapshot().state == e::State::Activating);
    }
    {   // Small-buff extra experience: 100% extra, 1200 per buff period for the team.
        auto m = make(2);
        CHECK(ok(m.small_bonus_tenths(0, 500)) == 0);
        const I t = activate_small(m, 0);
        CHECK(is_error(m.small_bonus_tenths(t + 1, 10), e::Error::UnsettledTime));
        CHECK(is_error(m.small_bonus_tenths(t, -1), e::Error::NegativeValue));
        CHECK(ok(m.small_bonus_tenths(t, 5000)) == 5000 && ok(m.small_bonus_tenths(t, 6000)) == 6000);
        CHECK(ok(m.small_bonus_tenths(t, 5000)) == 1000 && ok(m.small_bonus_tenths(t, 5000)) == 0);
        ok(m.advance(90000));
        const I t2 = activate_small(m, 90000);
        CHECK(m.snapshot().small_bonus_left_tenths == e::SMALL_BONUS_CAP_TENTHS && ok(m.small_bonus_tenths(t2, 20000)) == 12000);
        auto b = make(4);
        const I tb = activate_big(b, 180000, 5, false);
        CHECK(ok(b.small_bonus_tenths(tb, 1000)) == 0); // only the small buff
        CHECK(e::BIG_ACTIVATION_EXPERIENCE_TENTHS == 7500);
    }
    {   // A dart can void the held buff.
        auto m = make(6);
        CHECK(is_error(m.void_buff(0), e::Error::NoBuff));
        const I t = activate_small(m, 0);
        const auto v = ok(m.void_buff(t + 1000));
        CHECK(v.events.size() == 1 && v.events[0].kind == EventKind::BuffVoided && m.snapshot().state == e::State::Inactive);
        CHECK(ok(m.small_bonus_tenths(t + 1000, 100)) == 0 && ok(m.advance(t + 50000)).events.empty());
    }
    {   // Hits while not activating change nothing; identical seeds select identical modules.
        auto m = make(8);
        CHECK(ok(m.hit(100, 2, 5)).events.empty() && m.snapshot().hits == 0);
        auto a = make(55), b = make(55), c = make(56);
        bool differ = false;
        std::map<int, int> small_seen, big_seen;
        for (int round = 0; round < 400; ++round) {
            // The first small and the first big selection of 400 differently seeded mechanisms.
            auto x = make(1000 + std::uint64_t(round), e::ChancePolicy::EngineeringConsumeOnActivationV1);
            ok(x.trigger(0, e::Operator::Sentry));
            ++small_seen[lit_arm(x)];
            ok(x.advance(180000));
            ok(x.trigger(180000, e::Operator::Sentry));
            ++big_seen[x.snapshot().lit_mask];
        }
        ok(a.trigger(0, e::Operator::Sentry)); ok(b.trigger(0, e::Operator::Sentry)); ok(c.trigger(0, e::Operator::Sentry));
        for (I t = 2500; t < 20000; t += 2500) {
            ok(a.advance(t)); ok(b.advance(t)); ok(c.advance(t));
            CHECK(a.snapshot() == b.snapshot());
            differ = differ || a.snapshot().lit_mask != c.snapshot().lit_mask;
        }
        CHECK(differ && small_seen.size() == 5 && big_seen.size() == 10);
        for (const auto& [arm, n] : small_seen) CHECK(arm >= 0 && n > 40);       // roughly uniform over 5 arms
        for (const auto& [mask, n] : big_seen) CHECK(bits(std::uint8_t(mask)) == 2 && n > 15); // and over 10 pairs
    }
    {   // Randomised sequences: structural invariants after every operation.
        std::uint64_t seed = 20270001;
        auto next = [&] { seed = seed * 6364136223846793005ull + 1442695040888963407ull; return seed >> 33; };
        std::map<EventKind, long> kinds;
        I small_total = 0, big_total = 0;
        for (int run = 0; run < 300; ++run) {
            auto m = make(next(), next() % 2 ? e::ChancePolicy::EngineeringConsumeOnTriggerV1 : e::ChancePolicy::EngineeringConsumeOnActivationV1);
            I t = 0, activations = 0;
            for (int i = 0; i < 400 && t < 420000; ++i) {
                t += I(next() % 1500);
                const auto before = m.snapshot();
                // Settle first so that the hit can be aimed at what is lit now.
                std::vector<e::Event> events = ok(m.advance(t)).events;
                const auto settled = m.snapshot();
                e::Result<e::Step> r = e::Step{settled, {}};
                const auto op = next() % 10;
                if (op < 2) r = m.trigger(t, next() % 2 ? e::Operator::Sentry : e::Operator::InfantryOperator);
                else if (op < 8) {
                    // Mostly aim at a lit module so that activations do happen.
                    const int arm = next() % 4 && lit_arm(m) >= 0 ? lit_arm(m) : int(next() % 5);
                    r = m.hit(t, arm, I(next() % 10) + 1);
                }
                const auto& s = m.snapshot();
                if (const auto* step = std::get_if<e::Step>(&r)) events.insert(events.end(), step->events.begin(), step->events.end());
                else CHECK(s == settled); // an error changes nothing
                I last = before.time_ms;
                for (const auto& ev : events) {
                    CHECK(ev.time_ms >= last && ev.time_ms <= t);
                    last = ev.time_ms;
                    ++kinds[ev.kind];
                    if (ev.kind == EventKind::Activated) { ++activations; CHECK(ev.value >= 5 && ev.value <= 10); }
                }
                CHECK(s.time_ms == t);
                CHECK(s.small_chances >= 0 && s.small_chances <= 2 && s.big_chances >= 0 && s.big_chances <= 3);
                CHECK(s.small_activations + s.big_activations == activations && s.failures >= before.failures);
                if (s.state == e::State::Activating) {
                    CHECK(s.group >= 0 && s.group < 5 && s.deadline_ms > t);
                    if (s.phase == e::Phase::Small) CHECK(bits(s.lit_mask) == 1 && !(s.lit_mask & s.done_mask) && bits(s.done_mask) == s.group && s.hits == s.group && t < e::BIG_PHASE_FROM_MS);
                    else CHECK(bits(s.lit_mask) == (s.first_hit_arm < 0 ? 2 : 1) && s.hits >= s.group && s.hits <= 2 * s.group + 1 && s.ring_sum >= s.hits * e::lowest_valid_ring(s.big_activations) && s.ring_sum <= 10 * s.hits);
                    CHECK(t < s.activating_since_ms + e::ACTIVATING_WINDOW_MS || (s.group == 4 && s.first_hit_arm >= 0));
                } else {
                    CHECK(s.lit_mask == 0 && s.first_hit_arm == -1);
                    if (s.state == e::State::Activated) {
                        CHECK(s.buff_until_ms > t);
                        if (s.phase == e::Phase::Big) CHECK(s.hits >= 5 && s.hits <= 10 && std::holds_alternative<buff::EnergyBuff>(buff::big_energy_buff(s.ring_sum, s.hits, s.hits)));
                        else CHECK(s.small_bonus_left_tenths >= 0 && s.small_bonus_left_tenths <= e::SMALL_BONUS_CAP_TENTHS);
                    } else CHECK(s.buff_until_ms == -1 && s.small_bonus_left_tenths == 0);
                }
                if (s.big_phase) CHECK(s.small_chances == 0);
            }
            small_total += m.snapshot().small_activations;
            big_total += m.snapshot().big_activations;
        }
        // The random driver really reaches every transition, not only the easy ones.
        CHECK(small_total > 20 && big_total > 20 && kinds.size() == 10);
        for (auto k : {EventKind::ChanceGranted, EventKind::Triggered, EventKind::GroupLit, EventKind::ArmHit, EventKind::FailedTimeout,
                       EventKind::FailedWrongModule, EventKind::WindowExpired, EventKind::SmallAbortedByBigPhase, EventKind::Activated, EventKind::BuffEnded})
            CHECK(kinds[k] > 0);
    }
    return finish("energy mechanism rules (§5.6.3, tables 5-23/5-24)");
}
