#include "match_rules.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
using namespace rmuc2027::match_v1;
int checks = 0;
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) { std::cerr << "FAIL match rule: " << label << '\n'; std::exit(1); }
}
template<class T> T value(const Result<T>& result) {
    check(std::holds_alternative<T>(result), "expected successful rule result");
    return std::get<T>(result);
}
template<class T> void error(const Result<T>& result, Error expected) {
    check(std::holds_alternative<Error>(result) && std::get<Error>(result) == expected, "expected explicit rule error");
}
ControlSnapshot controlled(unsigned mask) {
    auto s = initial_control();
    for (std::size_t i = 0; i < 4; ++i) {
        s.positions[i].controller = mask & (1u << i) ? Team::Blue : Team::Red;
        s.positions[i].degree = ControlDegree::Temporary;
    }
    return s;
}
MatchSnapshot fixture(std::int64_t time = MATCH_DURATION_MS) {
    return {time, {{{9000, 10, 100, 500}, {1000, 10, 100, 500}}}};
}
void result(const MatchSnapshot& s, Outcome outcome, Reason reason) {
    auto decision = value(evaluate(s));
    check(decision.outcome == outcome && decision.reason == reason, "expected decision and ordered reason");
}
int main() {
    check(MATCH_DURATION_MS == 420000, "seven-minute match duration");
    auto initial = value(control_rate(initial_control()));
    check(initial.points_per_second == std::array<std::int64_t,2>{0,0}, "initial split ownership earns no control");
    // Enumerate all 16 control-owner combinations. Temporary control counts.
    for (unsigned mask = 0; mask < 16; ++mask) {
        auto s = controlled(mask);
        auto rate = value(control_rate(s));
        std::array<std::int64_t,2> oracle{};
        for (unsigned team = 0; team < 2; ++team) {
            if (((mask & 1u) != 0) == (team != 0) && ((mask & 2u) != 0) == (team != 0)) oracle[team] += 1;
            if (((mask & 4u) != 0) == (team != 0) && ((mask & 8u) != 0) == (team != 0)) oracle[team] += 3;
        }
        check(rate.points_per_second == oracle, "both matching positions required; rates add independently");
        s.positions[0].degree = ControlDegree::Full;
        s.positions[1].degree = ControlDegree::Full;
        check(value(control_rate(s)).points_per_second == rate.points_per_second, "temporary and full outpost control both qualify");
    }
    auto bad = initial_control();
    bad.positions[3] = {Team::Red, ControlDegree::Full};
    error(control_rate(bad), Error::InvalidEnemyFullBaseControl);
    bad.positions[3].degree = ControlDegree::Temporary;
    check(value(control_rate(bad)).points_per_second[0] == 3, "enemy temporary base control qualifies");
    bad.positions[0].controller = static_cast<Team>(9);
    error(control_rate(bad), Error::InvalidTeam);
    bad = initial_control(); bad.positions[0].degree = static_cast<ControlDegree>(9);
    error(control_rate(bad), Error::InvalidControlDegree);

    ExternalControlLedger ledger;
    for (std::uint64_t i = 1; i <= 10; ++i) value(ledger.apply({i, controlled(0)}));
    check(ledger.totals() == std::array<std::int64_t,2>{40,0}, "ten externally resolved seconds controlling all positions earns 40");
    error(ledger.apply({10, controlled(0)}), Error::InvalidCreditSequence);
    error(ledger.apply({12, controlled(0)}), Error::InvalidCreditSequence);
    check(ledger.last_sequence() == 10 && ledger.totals()[0] == 40, "duplicate/gap credit rejection is atomic");
    error(ledger.apply({11, bad}), Error::InvalidControlDegree);
    check(ledger.last_sequence() == 10, "invalid control leaves ledger sequence unchanged");
    ledger.seal(); error(ledger.apply({11, controlled(0)}), Error::CreditAfterEnd);
    check(ledger.totals()[0] == 40, "sealed score cannot change");
    ExternalControlLedger split;
    for (std::uint64_t i = 1; i <= 10; ++i) value(split.apply({i, controlled(12)}));
    check(split.totals() == std::array<std::int64_t,2>{10,30}, "red outposts and blue bases accrue independently");

    auto s = fixture(419999); s.teams[1].control_score = 999;
    result(s, Outcome::Ongoing, Reason::NotEnded);
    s.elapsed_ms = 420000;
    result(s, Outcome::BlueWins, Reason::TimeoutControl); // Red base has much more HP.
    s.teams[1].base_hp = 0;
    result(s, Outcome::RedWins, Reason::BaseHp); // Base priority at the exact timeout.
    s.elapsed_ms = 1;
    result(s, Outcome::RedWins, Reason::BaseHp); // Early ending.
    s.teams[0].base_hp = 0;
    error(evaluate(s), Error::UnresolvedSimultaneousDestruction);
    s.simultaneous_destruction_resolved = true;
    result(s, Outcome::Draw, Reason::ResolvedBothBasesZero);
    s = fixture(); s.teams[0].attack_damage = 101;
    result(s, Outcome::RedWins, Reason::TimeoutAttackDamage);
    s.teams[0].attack_damage = 99;
    result(s, Outcome::BlueWins, Reason::TimeoutAttackDamage);
    s = fixture(); s.teams[1].surviving_robot_hp = 600;
    result(s, Outcome::BlueWins, Reason::TimeoutSurvivingRobotHp);
    s.teams[0].surviving_robot_hp = 601;
    result(s, Outcome::RedWins, Reason::TimeoutSurvivingRobotHp);
    s = fixture(); result(s, Outcome::Draw, Reason::TimeoutEqual); // Unequal intact base HP excluded.
    s.teams[0].control_score.reset(); error(evaluate(s), Error::MissingControlScore);
    s = fixture(); s.teams[0].attack_damage.reset(); error(evaluate(s), Error::MissingAttackDamage);
    s.teams[1].control_score = 11; result(s, Outcome::BlueWins, Reason::TimeoutControl); // Irrelevant unknown lower tier allowed.
    s = fixture(); s.teams[0].surviving_robot_hp.reset(); error(evaluate(s), Error::MissingSurvivingRobotHp);
    s.teams[0].attack_damage = 101; result(s, Outcome::RedWins, Reason::TimeoutAttackDamage);
    s = fixture(420001); error(evaluate(s), Error::InvalidElapsedTime);
    s.elapsed_ms = -1; error(evaluate(s), Error::InvalidElapsedTime);
    s = fixture(); s.teams[0].control_score = -1; error(evaluate(s), Error::NegativeValue);
    s = fixture(); s.referee_ruling = RefereeRuling::RoundForfeit;
    error(evaluate(s), Error::UnsupportedRefereeRuling);

    std::vector<RemainingHpFact> facts = {
        {1, Team::Red, EntityKind::Robot, 500, true, false},
        {2, Team::Blue, EntityKind::Robot, 600, true, false},
        {3, Team::Red, EntityKind::Base, 9000, true, false},
        {4, Team::Red, EntityKind::Outpost, 1000, true, false},
        {5, Team::Red, EntityKind::Robot, 100, false, false}, // Abnormally offline.
        {6, Team::Red, EntityKind::Robot, 100, true, true}, // Dismissed.
        {7, Team::Red, EntityKind::Robot, 0, true, false}
    };
    auto hp = value(surviving_robot_hp(facts));
    check(hp == std::array<std::int64_t,2>{500,600}, "only connected living non-dismissed robot HP counts");
    s = fixture(); s.teams[0].surviving_robot_hp = hp[0]; s.teams[1].surviving_robot_hp = hp[1];
    result(s, Outcome::BlueWins, Reason::TimeoutSurvivingRobotHp);
    facts.push_back(facts[0]); error(surviving_robot_hp(facts), Error::DuplicateEntity); facts.pop_back();
    facts[0].current_hp = -1; error(surviving_robot_hp(facts), Error::NegativeValue);
    facts[0].current_hp = std::numeric_limits<std::int64_t>::max();
    facts.push_back({8, Team::Red, EntityKind::Robot, 1, true, false});
    error(surviving_robot_hp(facts), Error::IntegerOverflow);

    MatchJudge judge;
    check(value(judge.update(fixture(1000))).outcome == Outcome::Ongoing, "judge ongoing before deadline");
    error(judge.update(fixture(999)), Error::TimeRegression);
    s = fixture(2000); s.teams[0].base_hp = -1;
    error(judge.update(s), Error::NegativeValue);
    check(judge.last_elapsed_ms() == 1000 && !judge.final_decision(), "failed update does not mutate judge");
    s = fixture(); s.teams[0].control_score = 11;
    check(value(judge.update(s)).outcome == Outcome::RedWins, "terminal decision is latched");
    s.teams[1].control_score = 999; error(judge.update(s), Error::AlreadyFinished);
    check(judge.final_decision()->outcome == Outcome::RedWins && judge.final_snapshot()->teams[1].control_score == 10,
          "late score changes cannot rewrite the result");
    std::cout << "PASS " << checks << " match-rule assertions; " << MODULE_VERSION << '\n';
}
