#include "match_rules.hpp"
#include <limits>
#include <set>

namespace rmuc2027::match_v1 {
namespace {
bool valid(Team t) { return t == Team::Red || t == Team::Blue; }
bool valid(ControlDegree d) { return d == ControlDegree::Temporary || d == ControlDegree::Full; }
std::size_t index(Team t) { return t == Team::Red ? 0 : 1; }
bool negative(const std::optional<std::int64_t>& v) { return v && *v < 0; }
Outcome greater(std::int64_t red, std::int64_t blue) {
    return red > blue ? Outcome::RedWins : red < blue ? Outcome::BlueWins : Outcome::Draw;
}
}
std::string_view error_name(Error e) {
    switch (e) {
#define ERROR_NAME(name) case Error::name: return #name
        ERROR_NAME(InvalidTeam); ERROR_NAME(InvalidControlDegree);
        ERROR_NAME(InvalidEnemyFullBaseControl); ERROR_NAME(NegativeValue);
        ERROR_NAME(InvalidElapsedTime); ERROR_NAME(TimeRegression);
        ERROR_NAME(AlreadyFinished); ERROR_NAME(MissingControlScore);
        ERROR_NAME(MissingAttackDamage); ERROR_NAME(MissingSurvivingRobotHp);
        ERROR_NAME(UnresolvedSimultaneousDestruction); ERROR_NAME(UnsupportedRefereeRuling);
        ERROR_NAME(DuplicateEntity); ERROR_NAME(InvalidEntityKind);
        ERROR_NAME(IntegerOverflow); ERROR_NAME(InvalidCreditSequence); ERROR_NAME(CreditAfterEnd);
#undef ERROR_NAME
    }
    return "UnknownError";
}
ControlSnapshot initial_control() {
    return {std::array{PositionControl{Team::Red, ControlDegree::Full},
                       PositionControl{Team::Blue, ControlDegree::Full},
                       PositionControl{Team::Red, ControlDegree::Full},
                       PositionControl{Team::Blue, ControlDegree::Full}}};
}
Result<ControlRate> control_rate(const ControlSnapshot& s) {
    for (const auto& p : s.positions) {
        if (!valid(p.controller)) return Error::InvalidTeam;
        if (!valid(p.degree)) return Error::InvalidControlDegree;
    }
    if ((s.positions[2].degree == ControlDegree::Full && s.positions[2].controller != Team::Red) ||
        (s.positions[3].degree == ControlDegree::Full && s.positions[3].controller != Team::Blue)) {
        return Error::InvalidEnemyFullBaseControl;
    }
    ControlRate rate;
    if (s.positions[0].controller == s.positions[1].controller)
        rate.points_per_second[index(s.positions[0].controller)] += 1;
    if (s.positions[2].controller == s.positions[3].controller)
        rate.points_per_second[index(s.positions[2].controller)] += 3;
    return rate;
}
Result<ControlRate> ExternalControlLedger::apply(const ExternalOneSecondCredit& credit) {
    if (sealed_) return Error::CreditAfterEnd;
    if (sequence_ == std::numeric_limits<std::uint64_t>::max() || credit.sequence != sequence_ + 1)
        return Error::InvalidCreditSequence;
    auto computed = control_rate(credit.adjudicated_control);
    if (auto error = std::get_if<Error>(&computed)) return *error;
    auto rate = std::get<ControlRate>(computed);
    for (std::size_t i = 0; i < totals_.size(); ++i)
        if (totals_[i] > std::numeric_limits<std::int64_t>::max() - rate.points_per_second[i])
            return Error::IntegerOverflow;
    for (std::size_t i = 0; i < totals_.size(); ++i) totals_[i] += rate.points_per_second[i];
    sequence_ = credit.sequence;
    return rate;
}
Result<std::array<std::int64_t, 2>> surviving_robot_hp(std::span<const RemainingHpFact> facts) {
    std::array<std::int64_t, 2> totals{};
    std::set<std::uint32_t> ids;
    for (const auto& f : facts) {
        if (!valid(f.team)) return Error::InvalidTeam;
        if (f.current_hp < 0) return Error::NegativeValue;
        if (!ids.insert(f.entity_id).second) return Error::DuplicateEntity;
        if (f.kind != EntityKind::Robot && f.kind != EntityKind::Base && f.kind != EntityKind::Outpost)
            return Error::InvalidEntityKind;
        if (f.kind != EntityKind::Robot || !f.main_controller_normally_connected || f.dismissed || f.current_hp == 0)
            continue;
        auto& total = totals[index(f.team)];
        if (total > std::numeric_limits<std::int64_t>::max() - f.current_hp) return Error::IntegerOverflow;
        total += f.current_hp;
    }
    return totals;
}
Result<Decision> evaluate(const MatchSnapshot& s) {
    if (s.elapsed_ms < 0 || s.elapsed_ms > MATCH_DURATION_MS) return Error::InvalidElapsedTime;
    if (s.referee_ruling != RefereeRuling::None) return Error::UnsupportedRefereeRuling;
    for (const auto& t : s.teams)
        if (t.base_hp < 0 || negative(t.control_score) || negative(t.attack_damage) || negative(t.surviving_robot_hp))
            return Error::NegativeValue;
    const auto& red = s.teams[0];
    const auto& blue = s.teams[1];
    if (red.base_hp == 0 || blue.base_hp == 0) {
        if (red.base_hp == 0 && blue.base_hp == 0) {
            if (!s.simultaneous_destruction_resolved) return Error::UnresolvedSimultaneousDestruction;
            return Decision{Outcome::Draw, Reason::ResolvedBothBasesZero, "victory.conditions.1+5"};
        }
        return Decision{greater(red.base_hp, blue.base_hp), Reason::BaseHp, "victory.conditions.1"};
    }
    if (s.elapsed_ms < MATCH_DURATION_MS)
        return Decision{Outcome::Ongoing, Reason::NotEnded, "PHASE-01/PHASE-04"};
    if (!red.control_score || !blue.control_score) return Error::MissingControlScore;
    if (*red.control_score != *blue.control_score)
        return Decision{greater(*red.control_score, *blue.control_score), Reason::TimeoutControl, "victory.conditions.2"};
    if (!red.attack_damage || !blue.attack_damage) return Error::MissingAttackDamage;
    if (*red.attack_damage != *blue.attack_damage)
        return Decision{greater(*red.attack_damage, *blue.attack_damage), Reason::TimeoutAttackDamage, "victory.conditions.3"};
    if (!red.surviving_robot_hp || !blue.surviving_robot_hp) return Error::MissingSurvivingRobotHp;
    if (*red.surviving_robot_hp != *blue.surviving_robot_hp)
        return Decision{greater(*red.surviving_robot_hp, *blue.surviving_robot_hp), Reason::TimeoutSurvivingRobotHp, "victory.conditions.4"};
    return Decision{Outcome::Draw, Reason::TimeoutEqual, "victory.conditions.5"};
}
Result<Decision> MatchJudge::update(const MatchSnapshot& snapshot) {
    if (final_) return Error::AlreadyFinished;
    if (snapshot.elapsed_ms < last_elapsed_ms_) return Error::TimeRegression;
    auto result = evaluate(snapshot);
    if (std::holds_alternative<Error>(result)) return result;
    const auto decision = std::get<Decision>(result);
    last_elapsed_ms_ = snapshot.elapsed_ms;
    if (decision.outcome != Outcome::Ongoing) {
        final_ = decision;
        final_snapshot_ = snapshot;
    }
    return decision;
}
}
