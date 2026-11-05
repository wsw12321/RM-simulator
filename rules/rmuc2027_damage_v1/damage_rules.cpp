#include "damage_rules.hpp"
#include <limits>
#include <utility>
namespace rmuc2027::damage_v1 {
namespace {
bool valid(Team t) { return t==Team::Red || t==Team::Blue; }
std::size_t index(Team t) { return t==Team::Red ? 0 : 1; }
bool add(std::int64_t& to,std::int64_t value) {
    if(value>std::numeric_limits<std::int64_t>::max()-to) return false;
    to+=value; return true;
}
}
std::string_view error_name(Error e) {
    switch(e) {
#define ERROR_NAME(x) case Error::x: return #x;
    ERROR_NAME(InvalidTeam) ERROR_NAME(InvalidEntityKind) ERROR_NAME(InvalidCause)
    ERROR_NAME(NegativeValue) ERROR_NAME(HpIncreased) ERROR_NAME(MissingAttackCredit)
    ERROR_NAME(ConflictingAttackCredit) ERROR_NAME(UnadjudicatedCause)
    ERROR_NAME(InvalidSequence) ERROR_NAME(TimeRegression) ERROR_NAME(IntegerOverflow)
    ERROR_NAME(LedgerSealed)
    ERROR_NAME(UnsupportedCauseForEntity)
#undef ERROR_NAME
    }
    return "UnknownError";
}
Result<Settlement> settle(const SettledDeduction& d) {
    if(!valid(d.target_team) || (d.attack_credit_team && !valid(*d.attack_credit_team))) return Error::InvalidTeam;
    if(d.target_kind!=EntityKind::Robot && d.target_kind!=EntityKind::Base && d.target_kind!=EntityKind::Outpost) return Error::InvalidEntityKind;
    if(d.time_ms<0 || d.hp_before<0 || d.hp_after<0) return Error::NegativeValue;
    if(d.hp_after>d.hp_before) return Error::HpIncreased;
    const auto loss=d.hp_before-d.hp_after;
    switch(d.cause) {
    case Cause::Projectile: case Cause::Dart: case Cause::AerialImpact:
        if(!d.attack_credit_team) return Error::MissingAttackCredit;
        return Settlement{loss,loss,d.attack_credit_team,false,false};
    case Cause::ViolationPenalty: {
        const auto opponent=d.target_team==Team::Red ? Team::Blue : Team::Red;
        if(d.attack_credit_team && *d.attack_credit_team!=opponent) return Error::ConflictingAttackCredit;
        return Settlement{loss,loss,opponent,false,true};
    }
    case Cause::ArmorCollision: case Cause::ModuleOffline:
        if(d.target_kind!=EntityKind::Robot) return Error::UnsupportedCauseForEntity;
        if(d.attack_credit_team) return Error::ConflictingAttackCredit;
        return Settlement{loss,0,std::nullopt,true,false};
    case Cause::Unadjudicated: return Error::UnadjudicatedCause;
    }
    return Error::InvalidCause;
}
Result<Settlement> Ledger::apply(const SettledDeduction& d) {
    if(sealed_) return Error::LedgerSealed;
    if(sequence_==std::numeric_limits<std::uint64_t>::max() || d.sequence!=sequence_+1) return Error::InvalidSequence;
    auto result=settle(d);
    if(auto error=std::get_if<Error>(&result)) return *error;
    if(d.time_ms<time_ms_) return Error::TimeRegression;
    const auto settlement=std::get<Settlement>(result);
    auto totals=totals_;
    if(!add(totals.actual_hp_loss[index(d.target_team)],settlement.actual_hp_loss)) return Error::IntegerOverflow;
    if(settlement.excluded && !add(totals.excluded_hp_loss[index(d.target_team)],settlement.actual_hp_loss)) return Error::IntegerOverflow;
    if(settlement.credited_team) {
        const auto recipient=index(*settlement.credited_team);
        if(!add(totals.attack_damage[recipient],settlement.attack_damage_credit)) return Error::IntegerOverflow;
        if(settlement.penalty && !add(totals.penalty_attack_damage[recipient],settlement.attack_damage_credit)) return Error::IntegerOverflow;
    }
    totals_=totals; sequence_=d.sequence; time_ms_=d.time_ms;
    return settlement;
}
Result<std::vector<Settlement>> Ledger::apply_batch(std::span<const SettledDeduction> deductions) {
    if(sealed_) return Error::LedgerSealed;
    Ledger candidate=*this;
    std::vector<Settlement> settlements;
    settlements.reserve(deductions.size());
    for(const auto& d:deductions) {
        auto result=candidate.apply(d);
        if(auto error=std::get_if<Error>(&result)) return *error;
        settlements.push_back(std::get<Settlement>(result));
    }
    *this=std::move(candidate);
    return settlements;
}
} // namespace rmuc2027::damage_v1
