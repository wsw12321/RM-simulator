#include "../rules/rmuc2027_damage_v1/damage_rules.hpp"
#include <cassert>
#include <iostream>
#include <limits>
namespace d=rmuc2027::damage_v1;
using T=d::Team;using K=d::EntityKind;using C=d::Cause;using E=d::Error;
d::SettledDeduction fact(std::uint64_t seq=1,C cause=C::Projectile,T victim=T::Blue,
                        std::int64_t before=100,std::int64_t after=80,
                        std::optional<T> recipient=T::Red) {
    return {seq,100,17,victim,K::Robot,cause,before,after,recipient};
}
int main() {
    // All qualifying causes and entity kinds consume ACTUAL settled deduction.
    for(auto cause:{C::Projectile,C::Dart,C::AerialImpact})for(auto kind:{K::Robot,K::Base,K::Outpost}) {
        auto f=fact(1,cause,T::Blue,7,0);f.target_kind=kind;
        const auto r=std::get<d::Settlement>(d::settle(f));
        assert(r.actual_hp_loss==7&&r.attack_damage_credit==7&&r.credited_team==T::Red&&!r.excluded&&!r.penalty);
        f.attack_credit_team.reset();assert(std::get<E>(d::settle(f))==E::MissingAttackCredit);
    }
    // Generic settled violation deductions credit the opponent for all target
    // kinds. The Robot5->1 /5->0 examples can represent yellow/red adjudication;
    // these arbitrary building facts do NOT extend warning floors/triggers to buildings.
    for(auto victim:{T::Red,T::Blue})for(auto kind:{K::Robot,K::Base,K::Outpost}) {
        auto f=fact(1,C::ViolationPenalty,victim,5,1,{});f.target_kind=kind;
        auto r=std::get<d::Settlement>(d::settle(f));
        assert(r.actual_hp_loss==4&&r.attack_damage_credit==4&&r.penalty&&!r.excluded&&r.credited_team!=victim);
        f.hp_after=0;r=std::get<d::Settlement>(d::settle(f));assert(r.attack_damage_credit==5);
        f.attack_credit_team=victim;assert(std::get<E>(d::settle(f))==E::ConflictingAttackCredit);
    }
    for(auto cause:{C::ArmorCollision,C::ModuleOffline}) {
        auto f=fact(1,cause,T::Red,100,98,{});auto r=std::get<d::Settlement>(d::settle(f));
        assert(r.excluded&&r.actual_hp_loss==2&&r.attack_damage_credit==0&&!r.credited_team);
        f.attack_credit_team=T::Blue;assert(std::get<E>(d::settle(f))==E::ConflictingAttackCredit);f.attack_credit_team.reset();
        for(auto kind:{K::Base,K::Outpost}) {f.target_kind=kind;assert(std::get<E>(d::settle(f))==E::UnsupportedCauseForEntity);}
    }
    d::Ledger ledger;
    std::array<d::SettledDeduction,4> batch{{fact(),fact(2,C::ViolationPenalty,T::Red,5,1,{}),
        fact(3,C::ArmorCollision,T::Blue,80,78,{}),fact(4,C::ModuleOffline,T::Red,100,97,{})}};
    assert(std::get<std::vector<d::Settlement>>(ledger.apply_batch(batch)).size()==4);
    assert((ledger.totals().attack_damage==std::array<std::int64_t,2>{20,4}));
    assert((ledger.totals().actual_hp_loss==std::array<std::int64_t,2>{7,22}));
    assert((ledger.totals().excluded_hp_loss==std::array<std::int64_t,2>{3,2}));
    assert((ledger.totals().penalty_attack_damage==std::array<std::int64_t,2>{0,4}));
    const auto before=ledger.totals();
    for(int mutation=0;mutation<10;++mutation) {
        auto f=fact(5);E error=E::InvalidSequence;
        if(mutation==0)f.sequence=4;
        if(mutation==1)f.sequence=6;
        if(mutation==2){f.time_ms=99;error=E::TimeRegression;}
        if(mutation==3){f.hp_before=-1;error=E::NegativeValue;}
        if(mutation==4){f.hp_after=101;error=E::HpIncreased;}
        if(mutation==5){f.target_team=static_cast<T>(99);error=E::InvalidTeam;}
        if(mutation==6){f.target_kind=static_cast<K>(99);error=E::InvalidEntityKind;}
        if(mutation==7){f.cause=static_cast<C>(99);error=E::InvalidCause;}
        if(mutation==8){f.cause=C::Unadjudicated;error=E::UnadjudicatedCause;}
        if(mutation==9){f.attack_credit_team.reset();error=E::MissingAttackCredit;}
        assert(std::get<E>(ledger.apply(f))==error&&ledger.totals()==before&&ledger.last_sequence()==4&&ledger.last_time_ms()==100);
    }
    std::array<d::SettledDeduction,2> bad{{fact(5),fact(7)}};
    assert(std::get<E>(ledger.apply_batch(bad))==E::InvalidSequence&&ledger.totals()==before&&ledger.last_sequence()==4);
    // Maximum integer loss is safe once; either direct or batched overflow is atomic.
    d::Ledger large;auto max=fact(1,C::Projectile,T::Blue,std::numeric_limits<std::int64_t>::max(),0);
    assert(std::holds_alternative<d::Settlement>(large.apply(max)));
    const auto huge=large.totals();assert(std::get<E>(large.apply(fact(2)))==E::IntegerOverflow&&large.totals()==huge&&large.last_sequence()==1);
    ledger.seal();assert(std::get<E>(ledger.apply(fact(5)))==E::LedgerSealed);
    assert(std::get<E>(ledger.apply_batch({}))==E::LedgerSealed&&ledger.totals()==before);
    std::cout<<"damage rule tests passed\n";
}
