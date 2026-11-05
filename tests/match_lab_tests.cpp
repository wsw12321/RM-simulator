// Integrated rules lab: determinism plus per-tick rule invariants that tie the
// engine's behaviour back to the individual source-linked modules.
#include "../match_lab/engine.hpp"
#include "rule_check.hpp"
#include <map>
#include <set>

using namespace match_lab;
namespace g = rmuc2027::growth_v1;

static Config config(int variant) {
    Config c;
    if (variant == 1) {
        c.teams[0].chassis = {g::Chassis::PowerFirst, g::Chassis::PowerFirst};
        c.teams[0].launcher = {g::Launcher::BurstFirst, g::Launcher::BurstFirst};
        c.teams[0].gold_reserve = 100;
        c.teams[1].sentry_auto = false;
        c.teams[1].max_assembly = 1;
        c.teams[1].gold_reserve = 300;
    } else if (variant == 2) {
        c.teams[0].chassis = {g::Chassis::HpFirst, g::Chassis::PowerFirst};
        c.teams[0].launcher = {g::Launcher::CoolingFirst, g::Launcher::BurstFirst};
        c.teams[1] = c.teams[0];
    } else if (variant == 3) {
        c.teams[0].max_assembly = 0;
        c.teams[1].max_assembly = 2;
        c.credits = false;
    }
    return c;
}

int main() {
    // --- Validation.
    {
        Config c;
        c.duration_ticks = 0;
        CHECK(validate(c).has_value());
        c = {};
        c.teams[0].max_assembly = 4;
        CHECK(validate(c).has_value());
        c = {};
        c.teams[1].chassis[0] = static_cast<g::Chassis>(9);
        CHECK(validate(c).has_value());
        CHECK(!validate(Config{}).has_value());
    }
    std::set<std::string> seen_kinds;
    for (int variant = 0; variant < 4; ++variant) {
        // --- Determinism: two independent runs give byte-identical output.
        Engine a(config(variant)), b(config(variant));
        a.run();
        b.run();
        CHECK(a.hash() == b.hash());
        CHECK(a.json(false) == b.json(false));
        // --- Per-tick invariants.
        Engine e(config(variant));
        std::vector<bool> alive(e.robots().size(), true);
        std::array<I, 2> base_before{10000, 10000};
        std::array<I, 2> control_recomputed{};
        for (bool more = true; more;) {
            more = e.step(); // the terminal tick is checked too
            const I t = time_ms(e.tick());
            for (std::size_t i = 0; i < e.robots().size(); ++i) {
                const auto& r = e.robots()[i];
                const auto& s = r.life.snapshot();
                const auto& team = e.teams()[std::size_t(r.team)];
                CHECK(s.hp >= 0 && s.hp <= s.max_hp);
                CHECK(s.alive() == (s.hp > 0));
                if (r.heat) CHECK(r.heat->snapshot().heat_tenths >= 0);
                CHECK(r.allowance.available(rmuc2027::economy_v1::Ammo::Mm17) >= 0);
                if (r.cls == g::RobotClass::Infantry) {
                    const I cap = std::get<I>(g::team_level_cap(team.highest_assembly));
                    CHECK(r.level == std::get<I>(g::level_for_experience_tenths(r.exp_tenths, cap)));
                    CHECK(s.max_hp == std::get<g::Performance>(g::performance(r.loadout, r.level)).max_hp);
                }
                // Readout length at the moment of death follows §5.3.2 + table 5-19.
                if (alive[i] && !s.alive()) {
                    const I base_hp = e.structures()[std::size_t(2 + r.team)].hp;
                    CHECK(s.readout_required == std::get<I>(rmuc2027::lifecycle_v1::revival_readout(s.death_ms, s.instant_revivals, base_hp)));
                }
                alive[i] = s.alive();
            }
            for (int k = 0; k < 2; ++k) {
                const auto& w = e.teams()[std::size_t(k)].wallet.snapshot();
                CHECK(w.gold >= 0 && w.gold == w.total_income - w.total_spent);
                CHECK(w.purchased_17mm <= 1000 && w.purchased_42mm <= 100);
                // §5.6.1: while the own outpost position is fully own-controlled the base takes no damage.
                const I hp = e.structures()[std::size_t(2 + k)].hp;
                if (e.positions()[std::size_t(k)].full_by(k ? rmuc2027::position_v1::Team::Blue : rmuc2027::position_v1::Team::Red))
                    CHECK(hp == base_before[std::size_t(k)]);
                CHECK(hp <= base_before[std::size_t(k)]);
                base_before[std::size_t(k)] = hp;
            }
            for (int z = 0; z < 4; ++z) {
                const auto& p = e.positions()[std::size_t(z)].snapshot();
                const auto& st = e.structures()[std::size_t(z)];
                CHECK(st.hp >= 0 && st.hp <= st.max_hp);
                if (z >= 2) CHECK(p.degree == rmuc2027::position_v1::Degree::Temporary || p.controller == p.original_owner);
                if (z < 2 && st.alive) CHECK((st.team == 1) == (p.outpost_owner == rmuc2027::position_v1::Team::Blue));
                if (z < 2 && p.degree == rmuc2027::position_v1::Degree::Full) CHECK(st.alive);
            }
            // Recompute control credits from position states (§5.6.2: 1/s both outposts, 3/s both bases).
            if (config(variant).credits && t % 1000 == 0) {
                for (int k = 0; k < 2; ++k) {
                    const auto team = k ? rmuc2027::position_v1::Team::Blue : rmuc2027::position_v1::Team::Red;
                    const auto& ps = e.positions();
                    control_recomputed[std::size_t(k)] += (ps[0].snapshot().controller == team && ps[1].snapshot().controller == team ? 1 : 0) +
                                                          (ps[2].snapshot().controller == team && ps[3].snapshot().controller == team ? 3 : 0);
                }
            }
        }
        if (config(variant).credits) {
            CHECK(control_recomputed[0] == e.control_ledger().totals()[0]);
            CHECK(control_recomputed[1] == e.control_ledger().totals()[1]);
        }
        // Attack damage ledger equals what the robots of each team dealt.
        std::array<I, 2> dealt{};
        for (const auto& r : e.robots()) dealt[std::size_t(r.team)] += r.damage_dealt;
        CHECK(dealt[0] == e.damage_ledger().totals().attack_damage[0] && dealt[1] == e.damage_ledger().totals().attack_damage[1]);
        CHECK(e.terminal());
        CHECK(e.decision().has_value() || !e.unresolved().empty() || e.tick() == DURATION_TICKS);
        if (!config(variant).credits) CHECK(!e.decision() || e.decision()->reason == rmuc2027::match_v1::Reason::BaseHp);
        for (const auto& ev : e.events()) seen_kinds.insert(ev.kind);
        CHECK(e.hash() == a.hash());
    }
    // The four scenarios together exercise every integrated mechanism.
    for (const char* kind : {"death", "revived", "level-up", "purchase", "assembly", "temporary-control", "full-control",
                             "outpost-destroyed", "outpost-rebuilt", "remote-heal", "sentry-supply", "posture",
                             "42mm-unlocked", "42mm-installed", "42mm-confirmed", "heavy-stage", "match-end"}) {
        if (!seen_kinds.count(kind)) std::cerr << "missing event kind " << kind << "\n";
        CHECK(seen_kinds.count(kind) == 1);
    }
    return finish("integrated rules lab (determinism + per-tick invariants)");
}
