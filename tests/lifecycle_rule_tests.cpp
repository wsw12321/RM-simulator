#include "../rules/rmuc2027_lifecycle_v1/lifecycle_rules.hpp"
#include "rule_check.hpp"
#include <cstdint>

namespace l = rmuc2027::lifecycle_v1;
using I = std::int64_t;

static l::RobotLife make(I hp = 200) { return std::get<l::RobotLife>(l::RobotLife::start(hp)); }
static l::Advance step(l::RobotLife& r, I t, l::Context c = {}) { return ok(r.advance(t, c), "advance"); }
static l::Context supply() { l::Context c; c.in_own_supply = true; return c; }
static l::Context card(bool in_supply = true) { l::Context c; c.in_own_supply = in_supply; c.weak_clearing_card = true; return c; }
// Facts at t are applied only after settling time to t (module contract).
static l::Loss hit(l::RobotLife& r, I t, I amount, l::LossKind k = l::LossKind::Combat, l::Context c = {}) {
    step(r, t, c);
    return ok(r.apply_loss(t, amount, k, c), "apply_loss");
}

int main() {
    // --- Logistics coefficient, table 5-19 (p94-95).
    CHECK(ok(l::logistics(10000)).numerator == 20000);
    CHECK(ok(l::logistics(0)).numerator == 10000);
    CHECK(ok(l::logistics(6000)).numerator == 16000);
    CHECK(is_error(l::logistics(-1), l::Error::InvalidBaseHp) && is_error(l::logistics(10001), l::Error::InvalidBaseHp));

    // --- Readout length, §5.3.2 p74 (worked example: remaining120 s, 2 instant -> 80).
    CHECK(ok(l::revival_readout(300000, 2, 10000)) == 80);
    CHECK(ok(l::revival_readout(0, 0, 10000)) == 10);
    CHECK(ok(l::revival_readout(420000, 0, 10000)) == 52);
    CHECK(ok(l::revival_readout(5000, 0, 10000)) == 11);   // 10.5 -> 11
    CHECK(ok(l::revival_readout(4999, 0, 10000)) == 10);   // 10.4999 -> 10
    CHECK(ok(l::revival_readout(300000, 2, 0)) == 160);    // /0.5
    CHECK(ok(l::revival_readout(300000, 2, 5000)) == 107); // 80/0.75 = 106.67
    CHECK(is_error(l::revival_readout(-1, 0, 10000), l::Error::TimeOutsideMatch));
    CHECK(is_error(l::revival_readout(0, -1, 10000), l::Error::NegativeValue));
    CHECK(l::readout_rate(false, 4000) == 1 && l::readout_rate(false, 3999) == 4 && l::readout_rate(true, 10000) == 4);

    // --- Regeneration §5.3.1 p73 with logistics and rounding.
    using R = l::RegenInput;
    CHECK(ok(l::regen_per_second(R{200, 0, true, true, false, 10000})) == 20);
    CHECK(ok(l::regen_per_second(R{200, 240000, true, true, false, 10000})) == 50);
    CHECK(ok(l::regen_per_second(R{200, 239999, true, true, false, 10000})) == 20);
    CHECK(ok(l::regen_per_second(R{200, 300000, false, true, false, 10000})) == 20); // in combat: 25% lost
    CHECK(ok(l::regen_per_second(R{200, 0, true, false, true, 10000})) == 10);      // outpost = 50%
    CHECK(ok(l::regen_per_second(R{200, 300000, true, false, true, 10000})) == 25);
    CHECK(ok(l::regen_per_second(R{200, 0, true, true, true, 10000})) == 20);       // max, not sum
    CHECK(ok(l::regen_per_second(R{200, 0, true, false, false, 10000})) == 0);
    CHECK(ok(l::regen_per_second(R{200, 0, true, true, false, 5000})) == 15);       // *0.75
    CHECK(ok(l::regen_per_second(R{225, 0, true, true, false, 10000})) == 23);      // 22.5 -> 23
    CHECK(ok(l::regen_per_second(R{225, 0, true, false, true, 10000})) == 11);      // 11.25 -> 11
    CHECK(ok(l::remote_heal_amount(200, 10000)) == 120 && ok(l::remote_heal_amount(200, 0)) == 60);
    CHECK(ok(l::remote_heal_amount(225, 10000)) == 135);

    // --- Out-of-combat (§2 p16): default at start; 6 s without firing or HP loss.
    {
        auto r = make();
        CHECK(r.snapshot().out_of_combat(0));
        step(r, 1000);
        ok(r.record_fire(1000));
        CHECK(!r.snapshot().out_of_combat(6999) && r.snapshot().out_of_combat(7000));
        const auto loss = hit(r, 2000, 50);
        CHECK(loss.actual == 50 && !loss.died && r.snapshot().hp == 150);
        CHECK(!r.snapshot().out_of_combat(7999) && r.snapshot().out_of_combat(8000));
        CHECK(hit(r, 3000, 0).actual == 0);
        CHECK(r.snapshot().last_hp_loss_ms == 2000); // zero loss is not "被扣血"
    }
    // --- Death, readout, readout-revival protection and weak clearing (§5.3.2 p74-75).
    {
        auto r = make();
        const auto kill = hit(r, 10000, 1000);
        CHECK(kill.died && kill.actual == 200);
        auto s = r.snapshot();
        CHECK(!s.alive() && s.hp == 0 && s.death_ms == 10000 && s.readout_required == 11 && s.deaths == 1);
        CHECK(!s.launcher_allowed() && !s.out_of_combat(30000));
        step(r, 10500);
        CHECK(is_error(r.record_fire(10500), l::Error::NotAlive));
        CHECK(is_error(r.apply_loss(10500, 5, l::LossKind::Other, {}), l::Error::NotAlive));
        CHECK(is_error(r.heal(10500, 5), l::Error::NotAlive));
        step(r, 15500);
        step(r, 20999);
        CHECK(!r.snapshot().alive() && r.snapshot().readout_progress == 10);
        auto a = step(r, 21000);
        s = a.state;
        CHECK(a.revived && s.alive() && s.hp == 20 && s.weak && s.revived_ms == 21000);
        CHECK(s.invincible(50999) && !s.invincible(51000) && !s.launcher_allowed());
        CHECK(s.out_of_combat(21000));
        // Invincibility blocks combat loss, not penalties.
        CHECK(hit(r, 22000, 15).actual == 0 && r.snapshot().hp == 20);
        CHECK(r.snapshot().last_hp_loss_ms == 10000);
        CHECK(ok(r.apply_loss(22000, 5, l::LossKind::Other, {})).actual == 5 && r.snapshot().hp == 15);
        // Weak robots receive no buff-point effects: no supply regeneration yet.
        CHECK(step(r, 24000, supply()).healed == 0);
        // A card at 25 s clears weak first; the same second then regenerates 10%.
        a = step(r, 25000, card());
        CHECK(!a.state.weak && a.state.launcher_allowed() && a.healed == 20 && a.state.hp == 35);
        // Readout invincibility had run 4 s (<10 s): it now ends at revival+10 s.
        CHECK(a.state.invincible(30999) && !a.state.invincible(31000));
    }
    {   // Card after more than 10 s of invincibility: it ends immediately.
        auto r = make();
        hit(r, 10000, 999);
        step(r, 21000);
        const auto a = step(r, 45000, card(false));
        CHECK(!a.state.weak && !a.state.invincible(45000));
        // A card during instant protection does not shorten the 3 s invincibility.
    }
    // --- Faster readout at own supply or when own base HP < 4000.
    {
        auto r = make();
        hit(r, 10000, 999);
        CHECK(!step(r, 12000, supply()).revived);
        CHECK(step(r, 13000, supply()).revived); // 4,8,12 >= 11
        auto low = make();
        l::Context c;
        c.base_hp = 3000;
        hit(low, 10000, 999, l::LossKind::Combat, c);
        CHECK(low.snapshot().readout_required == 17); // 11 / 0.65 = 16.9
        CHECK(!step(low, 14000, c).revived && step(low, 15000, c).revived); // 4..20
    }
    // --- Instant revival (§5.3.2 p75).
    {
        auto r = make();
        step(r, 500);
        CHECK(is_error(r.instant_revive(500), l::Error::NotDead));
        hit(r, 10000, 999);
        step(r, 12000);
        const auto s = ok(r.instant_revive(12000));
        CHECK(s.alive() && s.hp == 200 && s.weak && s.instant_revivals == 1 && s.last_revival == l::RevivalKind::Instant);
        CHECK(s.invincible(14999) && !s.invincible(15000));
        CHECK(s.power_boosted(15999) && !s.power_boosted(16000));
        CHECK(step(r, 14999, card(false)).state.weak == false); // card may clear weak early
        CHECK(r.snapshot().invincible(14999));                  // ...but not invincibility
        auto t = make();
        hit(t, 10000, 999);
        step(t, 11000);
        ok(t.instant_revive(11000));
        CHECK(step(t, 13999).state.weak && !step(t, 14000).state.weak);
        // Every instant revival lengthens later readouts by 20 (§5.3.2).
        hit(t, 60000, 999);
        CHECK(t.snapshot().readout_required == 36); // 10 + 6 + 20
    }
    // --- Remote HP exchange (§5.3.1 p73): out of combat, +60% after 6 s, void on death.
    {
        auto r = make();
        hit(r, 1000, 150);
        step(r, 5000);
        CHECK(is_error(r.request_remote_heal(5000), l::Error::NotOutOfCombat));
        step(r, 7000);
        ok(r.request_remote_heal(7000));
        step(r, 7100);
        CHECK(is_error(r.request_remote_heal(7100), l::Error::HealAlreadyPending));
        CHECK(step(r, 12999).healed == 0 && r.snapshot().hp == 50);
        const auto a = step(r, 13000);
        CHECK(a.healed == 120 && a.state.hp == 170 && a.state.pending_heal_due_ms == -1);
        step(r, 19000);
        ok(r.request_remote_heal(19000)); // still out of combat
        CHECK(step(r, 25000).healed == 30 && r.snapshot().hp == 200); // capped at max
        auto v = make();
        step(v, 1000);
        ok(v.request_remote_heal(1000));
        hit(v, 3000, 999);
        CHECK(v.snapshot().pending_heal_due_ms == -1);
        CHECK(step(v, 7000).healed == 0);
    }
    // --- Regeneration settles at whole seconds; late 25% requires out of combat.
    {
        auto r = make();
        hit(r, 1000, 150);
        CHECK(step(r, 1999, supply()).healed == 0);
        CHECK(step(r, 2000, supply()).healed == 20);
        CHECK(step(r, 4500, supply()).healed == 40); // seconds 3 and 4
        CHECK(step(r, 10000, supply()).state.hp == 200);
        auto late = make();
        hit(late, 250000, 190);
        CHECK(step(late, 251000, supply()).healed == 20); // in combat at 251 s: 10%
        CHECK(step(late, 255999, supply()).healed == 80); // seconds 252..255
        CHECK(step(late, 256000, supply()).healed == 50); // 6 s quiet: 25%
        CHECK(late.snapshot().hp == 160);
        auto outpost = make();
        hit(outpost, 1000, 100);
        l::Context o;
        o.in_full_own_outpost_buff = true;
        CHECK(step(outpost, 3000, o).healed == 20); // 10 per second, seconds 2 and 3
    }
    // --- Level-up max-HP changes (§5.5.2).
    {
        auto r = make();
        hit(r, 1000, 50);
        step(r, 2000);
        CHECK(ok(r.set_max_hp(2000, 225)).hp == 175);
        CHECK(ok(r.set_max_hp(2000, 100)).hp == 100);
        hit(r, 3000, 999);
        step(r, 4000);
        CHECK(ok(r.set_max_hp(4000, 400)).hp == 0);
        CHECK(step(r, 4000 + 1000 * 11).state.hp == 40); // 10% of the new max on revival
        step(r, 20000);
        CHECK(is_error(r.set_max_hp(20000, 0), l::Error::InvalidConfig));
    }
    // --- Time and argument errors are atomic.
    {
        auto r = make();
        step(r, 5000);
        const auto before = r.snapshot();
        CHECK(is_error(r.advance(4999, {}), l::Error::NonIncreasingTime));
        CHECK(is_error(r.advance(420001, {}), l::Error::TimeOutsideMatch));
        CHECK(is_error(r.apply_loss(5000, -1, l::LossKind::Combat, {}), l::Error::NegativeValue));
        CHECK(is_error(r.apply_loss(6000, 1, l::LossKind::Combat, {}), l::Error::UnsettledTime));
        CHECK(is_error(r.record_fire(6000), l::Error::UnsettledTime));
        l::Context bad;
        bad.base_hp = 10001;
        CHECK(is_error(r.advance(6000, bad), l::Error::InvalidBaseHp));
        CHECK(r.snapshot() == before);
        CHECK(is_error(l::RobotLife::start(0), l::Error::InvalidConfig));
        CHECK(step(r, 5000).state.time_ms == 5000); // same timestamp is allowed
    }
    // --- Randomised invariants over long sequences.
    {
        std::uint64_t seed = 0x9e3779b97f4a7c15ull;
        auto next = [&] { seed = seed * 6364136223846793005ull + 1442695040888963407ull; return seed >> 33; };
        for (int run = 0; run < 200; ++run) {
            auto r = make(150 + I(next() % 300));
            I t = 0;
            for (int i = 0; i < 400 && t < 419000; ++i) {
                t += I(next() % 1500);
                l::Context c;
                c.in_own_supply = next() % 4 == 0;
                c.weak_clearing_card = next() % 5 == 0;
                c.base_hp = I(next() % 10001);
                const auto before = r.snapshot();
                if (next() % 8) (void)r.advance(t, c); // mostly settle first; else expect UnsettledTime
                switch (next() % 6) {
                case 0: (void)r.apply_loss(t, I(next() % 120), next() % 2 ? l::LossKind::Combat : l::LossKind::Other, c); break;
                case 1: (void)r.record_fire(t); break;
                case 2: (void)r.request_remote_heal(t); break;
                case 3: (void)r.instant_revive(t); break;
                default: (void)r.advance(t, c); break;
                }
                const auto& s = r.snapshot();
                CHECK(s.hp >= 0 && s.hp <= s.max_hp);
                CHECK(s.alive() == (s.status == l::Status::Alive));
                CHECK(!s.alive() ? s.hp == 0 && !s.weak && s.pending_heal_due_ms == -1 : true);
                CHECK(s.time_ms >= before.time_ms);
                CHECK(s.deaths >= before.deaths && s.instant_revivals >= before.instant_revivals);
                CHECK(s.alive() || s.readout_progress < s.readout_required);
            }
        }
    }
    return finish("lifecycle rules (§2, §5.3, table 5-19)");
}
