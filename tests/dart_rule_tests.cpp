#include "../rules/rmuc2027_buff_v1/buff_rules.hpp"
#include "../rules/rmuc2027_dart_v1/dart_rules.hpp"
#include "rule_check.hpp"

namespace d = rmuc2027::dart_v1;
namespace buff = rmuc2027::buff_v1;
using I = std::int64_t;
using d::EventKind;
using d::Target;

static int count(const std::vector<d::Event>& ev, EventKind k) { int n = 0; for (const auto& x : ev) n += x.kind == k; return n; }
// Opens the gate at t and returns the millisecond it is fully open.
static I open(d::DartStation& s, I t, Target target) {
    ok(s.select_target(t, target), "select");
    ok(s.open_gate(t), "open");
    ok(s.advance(t + d::GATE_OPENING_MS), "opening");
    return t + d::GATE_OPENING_MS;
}
static d::HitEffect strike(d::DartStation& s, I t, d::Struck where, bool still = true) {
    ok(s.launch(t), "launch");
    return ok(s.hit(t + 1, where, still), "hit").effect;
}

int main() {
    {   // Two gate chances, 30 s and 4 min after the start; unused chances accumulate (p110).
        d::DartStation s;
        CHECK(s.snapshot().chances == 0 && s.snapshot().darts_left == 4 && s.snapshot().gate == d::Gate::Closed);
        CHECK(is_error(s.open_gate(29999), d::Error::NoChance));
        const auto a = ok(s.advance(30000));
        CHECK(a.events.size() == 1 && a.events[0].kind == EventKind::ChanceGranted && a.events[0].time_ms == 30000);
        CHECK(ok(s.advance(240000)).events.size() == 1 && s.snapshot().chances == 2);
        CHECK(ok(s.advance(420000)).events.empty() && is_error(s.advance(1), d::Error::NonIncreasingTime));
    }
    {   // Gate timeline: 7 s opening, 30 s launch window, closing, 15 s cool-down; 40 s detection window.
        d::DartStation s;
        CHECK(is_error(s.launch(30000), d::Error::GateNotOpen));
        ok(s.select_target(30000, Target::BaseFixed));
        const auto o = ok(s.open_gate(30000));
        CHECK(count(o.events, EventKind::GateOpening) == 1 && s.snapshot().gate == d::Gate::Opening && s.snapshot().chances == 0);
        CHECK(is_error(s.open_gate(30001), d::Error::GateNotClosed) && is_error(s.launch(36999), d::Error::GateNotOpen));
        const auto full = ok(s.advance(37000));
        CHECK(count(full.events, EventKind::GateOpen) == 1 && s.snapshot().gate == d::Gate::Open);
        CHECK(s.snapshot().detect_from_ms == 37000 && s.snapshot().detect_until_ms == 77000 && s.snapshot().gate_until_ms == 67000);
        ok(s.launch(66999));
        CHECK(is_error(s.launch(67000), d::Error::GateNotOpen)); // launch window is over
        CHECK(s.snapshot().gate == d::Gate::Opening || s.snapshot().gate == d::Gate::Open); // the failed call changed nothing
        const auto closing = ok(s.advance(67000));
        CHECK(count(closing.events, EventKind::GateClosing) == 1 && s.snapshot().gate == d::Gate::Closing);
        // The dart launched at the last moment is still inside the detection window.
        CHECK(ok(s.hit(70000, d::Struck::BaseModule)).effect.valid);
        const auto closed = ok(s.advance(74000));
        CHECK(count(closed.events, EventKind::GateClosed) == 1 && s.snapshot().gate == d::Gate::Closed && s.snapshot().cooldown_until_ms == 89000);
        ok(s.advance(240000));
        ok(s.open_gate(240000));
        ok(s.advance(300000));
        CHECK(s.snapshot().openings == 2 && s.snapshot().gate == d::Gate::Closed && is_error(s.open_gate(300000), d::Error::NoChance));
        // With both chances saved up, the second opening must wait for the cool-down.
        d::DartStation b;
        ok(b.advance(240000));
        ok(b.open_gate(240000));
        ok(b.advance(284000)); // 7 + 30 + 7 s
        CHECK(b.snapshot().gate == d::Gate::Closed && b.snapshot().cooldown_until_ms == 299000);
        CHECK(is_error(b.open_gate(298999), d::Error::GateCoolingDown));
        CHECK(count(ok(b.open_gate(299000)).events, EventKind::GateOpening) == 1);
    }
    {   // Four darts per round; a hit needs a launched dart.
        d::DartStation s;
        I t = open(s, 30000, Target::Outpost);
        CHECK(is_error(s.hit(t, d::Struck::OutpostModule), d::Error::NoDartInFlight) && is_error(s.missed(t), d::Error::NoDartInFlight));
        for (int i = 0; i < 4; ++i) ok(s.launch(t + i));
        CHECK(is_error(s.launch(t + 10), d::Error::NoDarts) && s.snapshot().darts_left == 0 && s.snapshot().launched == 4);
        ok(s.missed(t + 100));
        for (int i = 0; i < 3; ++i) ok(s.hit(t + 200 + 3000 * i, d::Struck::OutpostModule));
        CHECK(s.snapshot().resolved == 4 && s.snapshot().hits == 3 && is_error(s.hit(t + 20000, d::Struck::OutpostModule), d::Error::NoDartInFlight));
        CHECK(is_error(s.hit(t + 20000, static_cast<d::Struck>(4)), d::Error::InvalidChoice));
    }
    {   // Outpost / fixed hits: occlusion 10, 5, 3, 2 s by hit order; stacking; 2 s blackout; point disabled 30 s.
        d::DartStation s;
        const I t = open(s, 30000, Target::Outpost);
        auto e1 = strike(s, t, d::Struck::OutpostModule);
        CHECK(e1.valid && e1.hit_number == 1 && e1.occlusion_ms == 10000 && e1.experience_tenths == 0 && !e1.buffs_voided && e1.buff_suspend_ms == 0);
        CHECK(s.snapshot().occlusion_until_ms == t + 1 + 10000 && s.snapshot().outpost_point_disabled_until_ms == t + 1 + 30000);
        CHECK(s.snapshot().base_point_disabled_until_ms == -1 && s.snapshot().blackout_until_ms == t + 2001);
        ok(s.launch(t + 500));
        const auto blocked = ok(s.hit(t + 2000, d::Struck::OutpostModule)).effect; // detection closed for 2 s
        CHECK(!blocked.valid && blocked.miss == d::Miss::HitBlackout && s.snapshot().hits == 1);
        auto e2 = strike(s, t + 2001, d::Struck::OutpostModule);
        CHECK(e2.valid && e2.hit_number == 2 && e2.occlusion_ms == 5000);
        CHECK(s.snapshot().occlusion_until_ms == t + 1 + 15000); // consecutive hits add up
        CHECK(s.snapshot().outpost_point_disabled_until_ms == t + 2002 + 30000); // refreshed
        CHECK(ok(buff::raw_damage(buff::Target::OutpostDartModule, buff::Source::Dart)) == 1000);
        // Hits on the structure that was not selected do not count.
        d::DartStation w;
        const I tw = open(w, 30000, Target::Outpost);
        const auto wrong = strike(w, tw, d::Struck::BaseModule);
        CHECK(!wrong.valid && wrong.miss == d::Miss::NotSelectedTarget && w.snapshot().hits == 0);
        // Outside the 40 s window nothing is detected.
        d::DartStation o;
        const I to = open(o, 30000, Target::BaseFixed);
        ok(o.launch(to + 29999));
        const auto late = ok(o.hit(to + 40000, d::Struck::BaseModule)).effect;
        CHECK(!late.valid && late.miss == d::Miss::OutsideDetectionWindow);
        d::DartStation never;
        CHECK(never.snapshot().detect_from_ms == -1);
    }
    {   // Fixed base target: 200 experience per hit; base armour deploys when all four hit.
        d::DartStation s;
        I t = open(s, 30000, Target::BaseFixed);
        const I occ[4] = {10000, 5000, 3000, 2000};
        for (int i = 0; i < 2; ++i) {
            const auto e = strike(s, t + 3000 * i, d::Struck::BaseModule);
            CHECK(e.valid && e.hit_number == i + 1 && e.occlusion_ms == occ[i] && e.experience_tenths == 2000 && !e.base_armor_deployed && e.max_hp_loss_percent == 0);
        }
        ok(s.advance(240000));
        t = open(s, 240000, Target::BaseFixed);
        const auto e3 = strike(s, t, d::Struck::BaseModule);
        CHECK(e3.occlusion_ms == 3000 && !e3.base_armor_deployed && !s.snapshot().base_armor_deployed);
        const auto e4 = strike(s, t + 3000, d::Struck::BaseModule);
        CHECK(e4.hit_number == 4 && e4.occlusion_ms == 2000 && e4.base_armor_deployed && s.snapshot().base_armor_deployed);
        CHECK(s.snapshot().base_point_disabled_until_ms == t + 3001 + 30000 && s.snapshot().outpost_point_disabled_until_ms == -1);
        CHECK(ok(buff::raw_damage(buff::Target::BaseDartModule, buff::Source::Dart, buff::DartTarget::Fixed)) == 200);
    }
    {   // Random fixed target: 600 experience, opponent terrain/energy buffs suspended for the occlusion time.
        d::DartStation s;
        const I t = open(s, 30000, Target::BaseRandomFixed);
        const auto moving = strike(s, t, d::Struck::BaseModule, false); // module still moving: not valid
        CHECK(!moving.valid && moving.miss == d::Miss::ModuleMoving);
        const auto e1 = strike(s, t + 100, d::Struck::BaseModule);
        CHECK(e1.valid && e1.hit_number == 1 && e1.experience_tenths == 6000 && e1.buff_suspend_ms == 10000 && !e1.buffs_voided);
        CHECK(s.snapshot().buff_suspended_until_ms == t + 101 + 10000);
        const auto e2 = strike(s, t + 3000, d::Struck::BaseModule);
        CHECK(e2.buff_suspend_ms == 5000 && s.snapshot().buff_suspended_until_ms == t + 101 + 15000 && s.snapshot().fixed_base_hits == 2);
        CHECK(ok(buff::raw_damage(buff::Target::BaseDartModule, buff::Source::Dart, buff::DartTarget::RandomFixed)) == 300);
        // A fixed target does not care whether the module moves.
        d::DartStation f;
        const I tf = open(f, 30000, Target::BaseFixed);
        CHECK(strike(f, tf, d::Struck::BaseModule, false).valid);
    }
    for (const Target target : {Target::BaseRandomMoving, Target::BaseTerminalMoving}) {
        // Moving targets: 10 s occlusion, 2500 experience, buffs voided, 20% / 50% max HP, armour deploys at once.
        d::DartStation s;
        const I t = open(s, 30000, target);
        const auto e = strike(s, t, d::Struck::BaseModule);
        CHECK(e.valid && e.occlusion_ms == 10000 && e.experience_tenths == 25000 && e.buffs_voided && e.base_armor_deployed);
        CHECK(e.max_hp_loss_percent == (target == Target::BaseRandomMoving ? 20 : 50) && e.buff_suspend_ms == 0);
        const auto e2 = strike(s, t + 3000, d::Struck::BaseModule);
        CHECK(e2.valid && e2.occlusion_ms == 10000 && !e2.base_armor_deployed && s.snapshot().base_armor_deployed); // deployed only once
        CHECK(s.snapshot().occlusion_until_ms == t + 1 + 20000 && s.snapshot().fixed_base_hits == 0);
        const auto raw = ok(buff::raw_damage(buff::Target::BaseDartModule, buff::Source::Dart,
                                             target == Target::BaseRandomMoving ? buff::DartTarget::RandomMoving : buff::DartTarget::TerminalMoving));
        CHECK(raw == (target == Target::BaseRandomMoving ? 800 : 1250));
    }
    {   // HP removed by a moving-target hit: percent of the max HP, rounded half-up.
        CHECK(ok(d::max_hp_loss(400, 20)) == 80 && ok(d::max_hp_loss(175, 20)) == 35 && ok(d::max_hp_loss(600, 50)) == 300);
        CHECK(ok(d::max_hp_loss(175, 50)) == 88 && ok(d::max_hp_loss(225, 50)) == 113 && ok(d::max_hp_loss(0, 50)) == 0);
        CHECK(is_error(d::max_hp_loss(-1, 20), d::Error::NegativeValue) && is_error(d::max_hp_loss(100, 101), d::Error::InvalidChoice));
    }
    {   // Target selection: free before opening; one outpost -> base switch after an outpost hit.
        d::DartStation s;
        CHECK(s.snapshot().target == Target::Outpost);
        ok(s.select_target(0, Target::BaseTerminalMoving));
        ok(s.select_target(10, Target::Outpost));
        CHECK(is_error(s.select_target(10, static_cast<Target>(9)), d::Error::InvalidChoice));
        const I t = open(s, 30000, Target::Outpost);
        CHECK(is_error(s.select_target(t, Target::BaseFixed), d::Error::SelectionLocked)); // no hit yet
        CHECK(strike(s, t, d::Struck::OutpostModule).valid);
        CHECK(is_error(s.select_target(t + 10, Target::Outpost), d::Error::SelectionLocked));
        ok(s.select_target(t + 10, Target::BaseFixed));
        CHECK(s.snapshot().target == Target::BaseFixed && is_error(s.select_target(t + 20, Target::BaseRandomFixed), d::Error::SelectionLocked));
        const auto e = strike(s, t + 3000, d::Struck::BaseModule);
        CHECK(e.valid && e.hit_number == 2 && e.occlusion_ms == 5000 && e.experience_tenths == 2000);
        ok(s.advance(t + 44000));
        CHECK(s.snapshot().gate == d::Gate::Closed);
        ok(s.select_target(t + 44000, Target::BaseRandomMoving)); // free again once closed
        // A base selection can never be switched during an opening.
        d::DartStation b;
        const I tb = open(b, 30000, Target::BaseFixed);
        strike(b, tb, d::Struck::BaseModule);
        CHECK(is_error(b.select_target(tb + 10, Target::BaseRandomFixed), d::Error::SelectionLocked));
        CHECK(d::target_name(Target::BaseTerminalMoving) == "base-terminal-moving" && d::is_moving(Target::BaseRandomMoving) && !d::is_moving(Target::BaseRandomFixed));
    }
    {   // Randomised command streams: structural invariants.
        std::uint64_t seed = 20270003;
        auto next = [&] { seed = seed * 6364136223846793005ull + 1442695040888963407ull; return seed >> 33; };
        long valid_hits = 0, deployed = 0;
        for (int run = 0; run < 400; ++run) {
            d::DartStation s;
            I t = 0, occlusion_sum = 0;
            for (int i = 0; i < 250 && t < 420000; ++i) {
                t += I(next() % 4000);
                const auto before = s.snapshot();
                const auto op = next() % 8;
                bool failed = false;
                if (op == 0) failed = !std::holds_alternative<d::Step>(s.select_target(t, static_cast<Target>(next() % 5)));
                else if (op == 1) failed = !std::holds_alternative<d::Step>(s.open_gate(t));
                else if (op < 4) failed = !std::holds_alternative<d::Step>(s.launch(t));
                else if (op < 7) {
                    const auto r = s.hit(t, next() % 2 ? d::Struck::BaseModule : d::Struck::OutpostModule, next() % 4 != 0);
                    if (const auto* h = std::get_if<d::HitStep>(&r)) {
                        if (h->effect.valid) {
                            ++valid_hits;
                            occlusion_sum += h->effect.occlusion_ms;
                            CHECK(h->effect.hit_number == before.hits + 1 && t >= before.blackout_until_ms);
                            CHECK(t >= h->state.detect_from_ms && t < h->state.detect_until_ms);
                            CHECK(d::is_base(h->effect.target) == (h->effect.experience_tenths > 0));
                            CHECK(h->effect.buffs_voided == d::is_moving(h->effect.target));
                            if (h->effect.base_armor_deployed) { ++deployed; CHECK(!before.base_armor_deployed); }
                        } else CHECK(h->effect.miss != d::Miss::None && h->state.hits == before.hits);
                    } else failed = true;
                } else ok(s.advance(t));
                const auto& x = s.snapshot();
                if (failed) CHECK(x == before);
                else CHECK(x.time_ms == t);
                CHECK(x.darts_left >= 0 && x.darts_left + x.launched == 4 && x.resolved <= x.launched && x.hits <= x.resolved);
                CHECK(x.chances >= 0 && x.chances + x.openings <= 2 && x.openings <= 2);
                CHECK((x.gate == d::Gate::Closed) == (x.gate_until_ms == -1) && (x.gate == d::Gate::Closed || x.gate_until_ms > x.time_ms));
                CHECK(x.base_armor_deployed || x.fixed_base_hits < 4);
                // Stacked occlusion never exceeds the sum of the awarded durations after the last hit.
                CHECK(x.occlusion_until_ms <= t + occlusion_sum);
            }
        }
        CHECK(valid_hits > 200 && deployed > 20);
    }
    return finish("dart system rules (§5.7.3, table 5-2 dart column)");
}
