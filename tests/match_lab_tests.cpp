// Integrated rules lab: determinism plus per-tick rule invariants that tie the
// engine's behaviour back to the individual source-linked modules.
#include "../match_lab/engine.hpp"
#include "rule_check.hpp"
#include <map>
#include <set>

using namespace match_lab;
namespace g = rmuc2027::growth_v1;
namespace d = rmuc2027::dart_v1;
constexpr int VARIANTS = 7;

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
    } else if (variant == 4) {
        // Strong combined systems against weak ones: moving dart target, precise energy, sharp radar.
        c.seed = 1;
        c.teams[0].energy_accuracy = 95;
        c.teams[0].dart_target = d::Target::BaseRandomMoving;
        c.teams[0].dart_accuracy = 90;
        c.teams[0].radar_accuracy = 90;
        c.teams[1].energy_accuracy = 40;
        c.teams[1].dart_target = d::Target::BaseRandomFixed;
        c.teams[1].dart_accuracy = 90;
        c.teams[1].radar_accuracy = 30;
    } else if (variant == 5) {
        // Every combined system switched off: the pre-existing mechanisms alone.
        for (auto& t : c.teams) { t.energy_accuracy = 0; t.dart_target.reset(); t.dart_accuracy = 0; t.radar_accuracy = 0; }
    } else if (variant == 6) {
        c.seed = 77;
        c.teams[0].dart_target = d::Target::BaseTerminalMoving;
        c.teams[0].dart_accuracy = 100;
        c.teams[0].energy_accuracy = 100;
        c.teams[1].dart_target = d::Target::BaseFixed;
        c.teams[1].dart_accuracy = 100;
        c.teams[1].energy_accuracy = 100; // holds the small buff when the first moving-target dart lands
        c.teams[1].radar_accuracy = 100;
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
        c = {};
        c.teams[0].energy_accuracy = 101;
        CHECK(validate(c).has_value());
        c = {};
        c.teams[1].radar_accuracy = -1;
        CHECK(validate(c).has_value());
        c = {};
        c.teams[1].dart_target = static_cast<d::Target>(9);
        CHECK(validate(c).has_value());
        c = {};
        c.seed = 1'000'000'000;
        CHECK(validate(c).has_value());
        CHECK(!validate(Config{}).has_value());
        for (int variant = 0; variant < VARIANTS; ++variant) CHECK(!validate(config(variant)).has_value());
    }
    {   // The seed changes the synthetic rolls and nothing else about the contract.
        Config a, b;
        b.seed = a.seed + 1;
        a.duration_ticks = b.duration_ticks = 3600;
        Engine x(a), y(a), z(b);
        x.run(); y.run(); z.run();
        CHECK(x.hash() == y.hash() && x.hash() != z.hash());
    }
    std::set<std::string> seen_kinds;
    I small_total = 0, big_total = 0, dart_hits = 0;
    for (int variant = 0; variant < VARIANTS; ++variant) {
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
        std::array<I, 2> dart_before{};
        std::array<I, 2> control_recomputed{};
        const Config used = config(variant);
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
                // Radar marking (§5.7.4): P stays in [0,150]; the vulnerability is exactly
                // table 5-30, doubled only while the opposing radar's 30 s period runs.
                const I progress = r.mark.snapshot().progress_tenths;
                const auto& marker = e.teams()[std::size_t(1 - r.team)];
                CHECK(progress >= 0 && progress <= rmuc2027::radar_v1::PROGRESS_MAX_TENTHS);
                CHECK(e.radar_vulnerability(r, t) == rmuc2027::buff_v1::radar_mark_vulnerability(progress) * (marker.radar_double.snapshot().active(t) ? 2 : 1));
                if (used.teams[std::size_t(1 - r.team)].radar_accuracy == 0) CHECK(progress == 0);
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
                // §5.6.1: while the own outpost position is fully own-controlled the base takes no
                // projectile damage. Darts ignore defence (table 5-25), so only what the opposing
                // dart system dealt in this tick may be missing.
                const I hp = e.structures()[std::size_t(2 + k)].hp;
                const I dart_now = e.teams()[std::size_t(1 - k)].dart_damage;
                if (e.positions()[std::size_t(k)].full_by(k ? rmuc2027::position_v1::Team::Blue : rmuc2027::position_v1::Team::Red))
                    CHECK(base_before[std::size_t(k)] - hp <= dart_now - dart_before[std::size_t(k)]);
                CHECK(hp <= base_before[std::size_t(k)] && dart_now >= dart_before[std::size_t(k)]);
                base_before[std::size_t(k)] = hp;
                dart_before[std::size_t(k)] = dart_now;
                // Energy mechanism (§5.6.3): the effect equals the tables for the held buff.
                const auto& team = e.teams()[std::size_t(k)];
                const auto& en = team.energy.snapshot();
                const auto effect = e.energy_effect(k, t);
                if (!en.buff_active() || t < team.energy_suspended_until_ms) CHECK(effect.attack_percent == 100 && effect.defense_percent == 0 && effect.cooling_tenths == 10);
                else if (en.phase == rmuc2027::energy_v1::Phase::Small) CHECK(effect.attack_percent == 100 && effect.defense_percent == 25 && effect.cooling_tenths == 10);
                else CHECK(effect.attack_percent >= 150 && effect.defense_percent >= 25 && en.hits >= 5 && en.hits <= 10);
                CHECK(en.time_ms == t && en.small_activations <= 2 && en.big_activations <= 3);
                CHECK(team.energy_bonus_tenths <= en.small_activations * rmuc2027::energy_v1::SMALL_BONUS_CAP_TENTHS);
                if (used.teams[std::size_t(k)].energy_accuracy == 0) CHECK(en.attempts == 0);
                // Dart station (§5.7.3) and radar command.
                const auto& da = team.dart.snapshot();
                CHECK(da.launched <= 4 && da.hits <= da.resolved && da.resolved <= da.launched && da.openings <= 2);
                CHECK(da.resolved + I(team.darts_in_flight.size()) == da.launched);
                if (!used.teams[std::size_t(k)].dart_target) CHECK(da.openings == 0 && team.dart_damage == 0);
                CHECK(team.radar_double.snapshot().triggers <= 2);
                CHECK(e.teams()[std::size_t(1 - k)].base_armor_deployed == da.base_armor_deployed);
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
        // Attack damage ledger equals what the robots and the dart system of each team dealt.
        std::array<I, 2> dealt{e.teams()[0].dart_damage, e.teams()[1].dart_damage};
        for (const auto& r : e.robots()) dealt[std::size_t(r.team)] += r.damage_dealt;
        // No traffic deadlock: nobody spends more than a fifth of the match pushing against an obstacle.
        for (const auto& r : e.robots()) {
            if (r.blocked_ticks * 5 >= e.tick()) std::cerr << "variant " << variant << ' ' << r.id << " blocked " << r.blocked_ticks << " of " << e.tick() << " ticks\n";
            CHECK(r.blocked_ticks * 5 < e.tick());
        }
        for (int k = 0; k < 2; ++k) {
            small_total += e.teams()[std::size_t(k)].energy.snapshot().small_activations;
            big_total += e.teams()[std::size_t(k)].energy.snapshot().big_activations;
            dart_hits += e.teams()[std::size_t(k)].dart.snapshot().hits;
        }
        if (variant == 5)
            for (const auto& ev : e.events())
                CHECK(ev.kind.rfind("dart-", 0) != 0 && ev.kind.rfind("radar-", 0) != 0 && ev.kind != "energy-trigger" && ev.kind != "energy-activated" && ev.kind != "base-armor");
        I last = 0;
        for (const auto& ev : e.events()) { CHECK(ev.time_ms >= last); last = ev.time_ms; }
        CHECK(dealt[0] == e.damage_ledger().totals().attack_damage[0] && dealt[1] == e.damage_ledger().totals().attack_damage[1]);
        CHECK(e.terminal());
        CHECK(e.decision().has_value() || !e.unresolved().empty() || e.tick() == DURATION_TICKS);
        if (!config(variant).credits) CHECK(!e.decision() || e.decision()->reason == rmuc2027::match_v1::Reason::BaseHp);
        for (const auto& ev : e.events()) seen_kinds.insert(ev.kind);
        CHECK(e.hash() == a.hash());
    }
    // The scenarios together exercise every integrated mechanism.
    CHECK(small_total >= 4 && big_total >= 4 && dart_hits >= 8);
    for (const char* kind : {"death", "revived", "level-up", "purchase", "assembly", "temporary-control", "full-control",
                             "outpost-destroyed", "outpost-rebuilt", "remote-heal", "sentry-supply", "posture",
                             "42mm-unlocked", "42mm-installed", "42mm-confirmed", "heavy-stage", "match-end",
                             "energy-chance", "energy-trigger", "energy-activated", "energy-failed", "energy-ended", "energy-voided",
                             "energy-suspended", "dart-gate", "dart-launch", "dart-hit", "dart-miss", "base-armor",
                             "radar-marked", "radar-double"}) {
        if (!seen_kinds.count(kind)) std::cerr << "missing event kind " << kind << "\n";
        CHECK(seen_kinds.count(kind) == 1);
    }
    return finish("integrated rules lab (determinism + per-tick invariants)");
}
