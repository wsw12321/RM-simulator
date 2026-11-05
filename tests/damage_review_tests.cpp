// Independent adversarial review. Direct state/probe setup is test-only;
// synthetic fixtures are not official referee adjudication or runtime APIs.
#include "../tactical_mode/core.hpp"
#include "../rules/rmuc2027_damage_v1/damage_rules.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace tactical {
struct DamageReviewTestProbe {
    static const auto& damage(const Engine& e) { return e.damage_; }
    static auto& damage(Engine& e) { return e.damage_; }
    static bool complete(const Engine& e) { return e.damage_complete_; }
    static void incomplete(Engine& e) { e.damage_complete_ = false; }
    static auto nominal(const Engine& e) { return e.nominal_projectile_damage_; }
    static void sync_hp(Engine& e) {
        e.expected_hp_ = {e.native.state.robots[0].hp, e.native.state.robots[1].hp};
    }
    static void reconcile(Engine& e, std::array<int,2> hp, int tick) { e.reconcile_damage(hp,tick); }
    static auto remaining(const Engine& e) { return e.remaining_robot_hp(); }
    static auto decision(const Engine& e) { return e.decision_; }
    static auto unresolved(const Engine& e) { return e.unresolved_; }
    static void red_credit(Engine& e) {
        auto c=match::initial_control();
        c.positions[1]={match::Team::Red,match::ControlDegree::Temporary};
        const auto result=e.ledger_.apply({1,c});
        if(std::holds_alternative<match::Error>(result)) throw std::runtime_error("test control setup");
    }
};
}
namespace {
namespace d=rmuc2027::damage_v1;
namespace m=rmuc2027::match_v1;
namespace t=tactical;
namespace p=tactical::policy;
using Probe=t::DamageReviewTestProbe;
int checks=0;
void check(bool yes,const std::string& message) { ++checks; if(!yes)throw std::runtime_error(message); }
template<class T> T get(d::Result<T> result) {
    if(auto e=std::get_if<d::Error>(&result))throw std::runtime_error(std::string(d::error_name(*e)));
    return std::get<T>(std::move(result));
}
template<class T> void reject(const d::Result<T>& result,d::Error expected,const std::string& message) {
    check(std::holds_alternative<d::Error>(result) && std::get<d::Error>(result)==expected,message);
}
auto idle() {
    const p::Action a{t::I{1},t::I{0},t::I{0},t::I{6000},t::I{3800},t::I{0}};
    return std::array<t::Request,2>{{{"manual",a},{"manual",a}}};
}
void advance(t::Engine& engine,int ticks) {
    const auto error=engine.advance(ticks,idle());
    check(!error,error.value_or("advance"));
}
d::SettledDeduction fact(std::uint64_t sequence=1,std::int64_t when=17) {
    return {sequence,when,77,d::Team::Blue,d::EntityKind::Robot,d::Cause::Projectile,7,0,d::Team::Red};
}
void formal_causes_and_actual_loss() {
    for(auto kind:{d::EntityKind::Robot,d::EntityKind::Base,d::EntityKind::Outpost}) {
        for(auto cause:{d::Cause::Projectile,d::Cause::Dart,d::Cause::AerialImpact}) {
            auto f=fact();f.target_kind=kind;f.cause=cause;
            auto settled=get(d::settle(f));
            check(settled.actual_hp_loss==7 && settled.attack_damage_credit==7,"actual HP loss not nominal projectile power");
            check(settled.credited_team==d::Team::Red && !settled.excluded && !settled.penalty,"externally settled attack attribution");
            f.attack_credit_team.reset();
            reject(d::settle(f),d::Error::MissingAttackCredit,"unknown/friendly-fire score attribution must not be inferred");
        }
    }
    for(auto team:{d::Team::Red,d::Team::Blue}) {
        auto f=fact();f.target_team=team;f.cause=d::Cause::ViolationPenalty;f.attack_credit_team.reset();
        const auto settled=get(d::settle(f));
        const auto opponent=team==d::Team::Red?d::Team::Blue:d::Team::Red;
        check(settled.credited_team==opponent && settled.penalty && settled.attack_damage_credit==7,"penalty not credited to opponent");
        f.attack_credit_team=team;
        reject(d::settle(f),d::Error::ConflictingAttackCredit,"penalty credit override accepted");
        f.attack_credit_team=opponent;get(d::settle(f));
    }
    // The ledger consumes settled penalty outcomes; it must not reapply a
    // generic zero clamp or invent a yellow-warning floor for red cards.
    auto warning=fact();warning.cause=d::Cause::ViolationPenalty;
    warning.hp_before=5;warning.hp_after=1;warning.attack_credit_team.reset();
    check(get(d::settle(warning)).attack_damage_credit==4,"settled yellow warning floor ignored");
    warning.hp_after=0;
    check(get(d::settle(warning)).attack_damage_credit==5,"settled red-card deduction given yellow floor");
    for(auto cause:{d::Cause::ArmorCollision,d::Cause::ModuleOffline}) {
        auto f=fact();f.cause=cause;f.attack_credit_team.reset();
        const auto settled=get(d::settle(f));
        check(settled.excluded && settled.actual_hp_loss==7 && !settled.credited_team && !settled.attack_damage_credit,"nonattack credited");
        f.attack_credit_team=d::Team::Red;
        reject(d::settle(f),d::Error::ConflictingAttackCredit,"excluded cause with attack credit accepted");
    }
    for(auto kind:{d::EntityKind::Base,d::EntityKind::Outpost}) {
        for(auto cause:{d::Cause::ArmorCollision,d::Cause::ModuleOffline}) {
            auto f=fact();f.target_kind=kind;f.cause=cause;f.attack_credit_team.reset();
            check(std::holds_alternative<d::Error>(d::settle(f)),"robot-only nonattack cause generalized to building");
        }
    }
    for(auto kind:{d::EntityKind::Robot,d::EntityKind::Base,d::EntityKind::Outpost}) {
        auto f=fact();f.target_kind=kind;f.cause=d::Cause::ViolationPenalty;f.attack_credit_team.reset();
        check(get(d::settle(f)).credited_team==d::Team::Red,"penalty must count for robot/base/outpost");
    }
    auto f=fact();f.cause=d::Cause::Unadjudicated;
    reject(d::settle(f),d::Error::UnadjudicatedCause,"unknown damage classified as attack");
    f=fact();f.hp_before=f.hp_after=0;
    check(get(d::settle(f)).attack_damage_credit==0,"zero actual loss fabricated damage");
    for(int mutation=0;mutation<8;++mutation) {
        f=fact();d::Error expected=d::Error::NegativeValue;
        switch(mutation) {
        case 0:f.hp_before=-1;break;
        case 1:f.hp_after=-1;break;
        case 2:f.time_ms=-1;break;
        case 3:f.hp_after=8;expected=d::Error::HpIncreased;break;
        case 4:f.target_team=static_cast<d::Team>(9);expected=d::Error::InvalidTeam;break;
        case 5:f.attack_credit_team=static_cast<d::Team>(9);expected=d::Error::InvalidTeam;break;
        case 6:f.target_kind=static_cast<d::EntityKind>(9);expected=d::Error::InvalidEntityKind;break;
        case 7:f.cause=static_cast<d::Cause>(99);expected=d::Error::InvalidCause;break;
        }
        reject(d::settle(f),expected,"bad settled deduction accepted");
    }
}
void ledger_atomicity_and_overflow() {
    d::Ledger ledger;
    auto attack=fact();auto collision=fact(2,17);collision.cause=d::Cause::ArmorCollision;collision.attack_credit_team.reset();
    auto penalty=fact(3,18);penalty.target_team=d::Team::Red;penalty.cause=d::Cause::ViolationPenalty;penalty.attack_credit_team.reset();
    const std::array initial{attack,collision,penalty};
    check(get(ledger.apply_batch(initial)).size()==3,"batch settlement count");
    check(ledger.totals().attack_damage==std::array<std::int64_t,2>{7,7},"attack totals");
    check(ledger.totals().actual_hp_loss==std::array<std::int64_t,2>{7,14},"victim loss totals");
    check(ledger.totals().excluded_hp_loss==std::array<std::int64_t,2>{0,7},"excluded totals");
    check(ledger.totals().penalty_attack_damage==std::array<std::int64_t,2>{0,7},"penalty totals");
    const auto before=ledger.totals();
    auto first=fact(4,19),bad=fact(5,18);
    reject(ledger.apply_batch(std::array{first,bad}),d::Error::TimeRegression,"late batch regression accepted");
    check(ledger.totals()==before && ledger.last_sequence()==3 && ledger.last_time_ms()==18,"late failure committed prefix");
    reject(ledger.apply(fact(3,19)),d::Error::InvalidSequence,"duplicate sequence accepted");
    reject(ledger.apply(fact(5,19)),d::Error::InvalidSequence,"sequence gap accepted");
    get(ledger.apply(fact(4,18))); // Explicit same-time deductions may be externally ordered.
    ledger.seal();
    const auto sealed=ledger.totals();
    reject(ledger.apply(fact(5,20)),d::Error::LedgerSealed,"post-seal deduction accepted");
    reject(ledger.apply_batch(std::array<d::SettledDeduction,0>{}),d::Error::LedgerSealed,"sealed empty batch accepted");
    check(ledger.totals()==sealed && ledger.sealed(),"sealed mutation");
    d::Ledger overflow;auto huge=fact();huge.hp_before=std::numeric_limits<std::int64_t>::max();
    get(overflow.apply(huge));const auto maximum=overflow.totals();
    auto extra=fact(2);extra.hp_before=1;
    reject(overflow.apply(extra),d::Error::IntegerOverflow,"aggregate overflow accepted");
    check(overflow.totals()==maximum && overflow.last_sequence()==1,"overflow partially committed");
}
void overkill_mutual_ko_and_replay() {
    t::Engine seeded;
    seeded.native.state.robots[0]={1000,1000,0,7};
    seeded.native.state.robots[1]={11000,1000,128,13};
    Probe::sync_hp(seeded);
    seeded.native.state.bullets={{1,1,1000,1000,0,0,10},{2,1,1000,1000,0,0,10},
        {3,0,11000,1000,0,0,10},{4,0,11000,1000,0,0,10},{5,0,11000,1000,0,0,10}};
    auto original=seeded,permuted=seeded;
    std::reverse(permuted.native.state.bullets.begin(),permuted.native.state.bullets.end());
    advance(seeded,1);advance(permuted,1);
    check(Probe::complete(seeded),"reconciled overkill marked incomplete");
    const auto totals=Probe::damage(seeded).totals();
    check(totals.attack_damage==std::array<std::int64_t,2>{13,7},"nominal overkill leaked into ledger");
    check(totals.actual_hp_loss==std::array<std::int64_t,2>{7,13},"actual HP deductions not reconciled");
    check(Probe::nominal(seeded)==std::array<std::int64_t,2>{30,20},"nominal diagnostic should stay distinct");
    check(Probe::damage(seeded).last_sequence()==2,"must settle once per victim/tick, not allocate overkill to individual hits");
    check(seeded.hash()==permuted.hash(),"bullet ordering changed aggregate damage attribution");
    check(seeded.native.state.robots[0].hp==0 && seeded.native.state.robots[1].hp==0 && !seeded.native.state.terminal,"mutual KO ended match early");
    check(Probe::remaining(seeded)==std::array<std::int64_t,2>{0,0},"dead HP counted as surviving");
    auto replay=original;
    check(!replay.replay_frames(seeded.journal),"synthetically seeded replay failed");
    check(replay.hash()==seeded.hash() && Probe::damage(replay).totals()==totals,"damage state omitted from replay");
    advance(seeded,10);
    check(Probe::damage(seeded).totals()==totals && Probe::damage(seeded).last_sequence()==2,"dead target credited twice");
    auto invalid=original;auto frames=seeded.journal;frames.back().hash^=1;
    const auto before=invalid.json(0,true);
    check(invalid.replay_frames(frames).has_value(),"late replay mismatch accepted");
    check(invalid.json(0,true)==before,"late replay mismatch committed private ledger prefix");
    auto changed=original;
    auto f=fact();get(Probe::damage(changed).apply(f));
    check(changed.hash()!=original.hash(),"ledger totals absent from canonical hash");
    check(t::observation_json(changed.observation(0))==t::observation_json(original.observation(0)),"damage ledger leaked into policy observation");
    changed=original;Probe::incomplete(changed);
    check(changed.hash()!=original.hash(),"ledger completeness absent from canonical hash");
}
p::Action failing_after_damage(const p::Observation& observation) {
    if(observation.combat.tick==3 && observation.combat.self_index==1)
        throw std::runtime_error("private late callback detail");
    return {t::I{1},t::I{0},t::I{0},t::I{6000},t::I{3800},t::I{0}};
}
void damage_then_late_callback_rollback() {
    t::Engine e;e.native.state.bullets={{1,0,e.native.state.robots[1].x,e.native.state.robots[1].y,0,0,10}};
    const auto before=e.json(0,true),replay=e.replay_json();
    const std::array<t::Request,2> requests{{{"policy",{}},{"policy",{}}}};
    const std::array<t::Controller,2> controllers{{
        {{1,"damage-review-v1"},failing_after_damage},{{1,"damage-review-v1"},failing_after_damage}}};
    const auto error=e.advance(8,requests,controllers);
    check(error.has_value(),"late callback failure accepted");
    check(error->find("private late callback detail")==std::string::npos,"private callback diagnostic exposed");
    check(e.json(0,true)==before && e.replay_json()==replay,"damage committed before later callback failure");
    check(Probe::damage(e).last_sequence()==0 && Probe::complete(e),"damage accounting mutated on rejected batch");
    advance(e,1);
    check(Probe::damage(e).totals().attack_damage[0]==10,"valid retry did not settle damage once");
}
void adversarial_reconciliation() {
    for(int mutation=0;mutation<8;++mutation) {
        t::Engine e;e.native.state.tick=1;e.native.state.robots[0].hp=90;
        e.native.events={{0,2,1,0,10}};
        switch(mutation) {
        case 0:e.native.events.clear();break;
        case 1:e.native.events[0].actor=0;break;
        case 2:e.native.events[0].target=2;break;
        case 3:e.native.events[0].amount=11;break;
        case 4:e.native.events[0].tick=1;break;
        case 5:e.native.events.push_back({0,2,1,0,10});break;
        case 6:e.native.state.robots[0].hp=101;break;
        case 7:e.native.events.push_back({0,99,1,0,0});break;
        }
        Probe::reconcile(e,{100,100},0);
        check(!Probe::complete(e),"unsupported/inconsistent deduction accepted as complete");
        check(Probe::damage(e).last_sequence()==0,"inconsistent tick partially credited");
    }
    t::Engine e;e.native.state.robots[0].hp=99; // Unexplained between-tick mutation.
    advance(e,1);
    check(!Probe::complete(e),"unrecorded HP loss silently became trusted baseline");
    advance(e,1);check(!Probe::complete(e),"incomplete ledger healed itself");
}
void timeout_boundaries_and_priority() {
    t::Engine e;
    for(int i=0;i<41;++i)advance(e,600);
    advance(e,599);
    check(e.native.state.tick==25199 && !e.native.state.terminal,"timeout before420 seconds");
    check(e.json(0).find("\"outcome\":\"ongoing\"")!=std::string::npos,"pretimeout tie judged early");
    e.native.state.bullets={{1,0,e.native.state.robots[1].x,e.native.state.robots[1].y,0,0,10}};
    advance(e,1);
    check(Probe::damage(e).totals().attack_damage==std::array<std::int64_t,2>{10,0},"final tick damage not included");
    check(Probe::decision(e).outcome==m::Outcome::RedWins && Probe::decision(e).reason==m::Reason::TimeoutAttackDamage,"final tick damage not used after tied control");
    check(Probe::damage(e).sealed(),"timeout did not seal damage ledger");
    const auto after=e.json(0,true);check(e.advance(1,idle()).has_value(),"after-timeout tick allowed");
    check(e.json(0,true)==after,"post-timeout request changed ledger");
    for(int scenario=0;scenario<5;++scenario) {
        t::Config config;if(scenario==1)config.damage_strategy=t::DamageStrategy::MissingAdjudicated;
        if(scenario==4)config.credits=false;
        t::Engine synthetic(config);synthetic.native.state.tick=25199;
        if(scenario==2 || scenario==3)Probe::incomplete(synthetic);
        if(scenario==3)Probe::red_credit(synthetic);
        advance(synthetic,1);
        if(scenario==0)check(Probe::decision(synthetic).outcome==m::Outcome::Draw && Probe::decision(synthetic).reason==m::Reason::TimeoutEqual,"zero genuine ledger should draw");
        if(scenario==1 || scenario==2)check(Probe::unresolved(synthetic)==m::Error::MissingAttackDamage,"unknown attack ledger should remain unresolved when needed");
        if(scenario==3)check(Probe::decision(synthetic).outcome==m::Outcome::RedWins && Probe::decision(synthetic).reason==m::Reason::TimeoutControl,"missing damage blocked higher-priority control winner");
        if(scenario==4)check(Probe::unresolved(synthetic)==m::Error::MissingControlScore,"disabled scoring fabricated result");
    }
}
void externally_adjudicated_surviving_hp() {
    // The equal100-HP, no-heal, two-opponent model cannot naturally reach unequal
    // HP after tied attack totals. This fixture tests only the standalone rule
    // with externally settled facts; it is not a native-playthrough claim.
    const std::array<m::RemainingHpFact,6> facts{{
        {1,m::Team::Red,m::EntityKind::Robot,40,true,false},
        {2,m::Team::Blue,m::EntityKind::Robot,30,true,false},
        {3,m::Team::Blue,m::EntityKind::Robot,100,false,false},
        {4,m::Team::Blue,m::EntityKind::Robot,100,true,true},
        {5,m::Team::Blue,m::EntityKind::Base,900,true,false},
        {6,m::Team::Blue,m::EntityKind::Outpost,300,true,false}}};
    const auto hp=m::surviving_robot_hp(facts);
    check(std::holds_alternative<std::array<std::int64_t,2>>(hp),"surviving facts rejected");
    check(std::get<std::array<std::int64_t,2>>(hp)==std::array<std::int64_t,2>{40,30},"survivor definition counted offline/dismissed/buildings");
    m::MatchSnapshot snapshot{420000,{{{1,0,17,40},{1,0,17,30}}}};
    auto result=m::evaluate(snapshot);
    check(std::holds_alternative<m::Decision>(result) && std::get<m::Decision>(result).reason==m::Reason::TimeoutSurvivingRobotHp && std::get<m::Decision>(result).outcome==m::Outcome::RedWins,"surviving HP tiebreak");
    snapshot.teams[0].surviving_robot_hp.reset();
    check(std::get<m::Error>(m::evaluate(snapshot))==m::Error::MissingSurvivingRobotHp,"missing survival adjudication silently assumed");
    snapshot.teams[0].attack_damage=18;
    check(std::get<m::Decision>(m::evaluate(snapshot)).reason==m::Reason::TimeoutAttackDamage,"missing survival blocked attack priority");
}
}
int main() {
    try {
        formal_causes_and_actual_loss();ledger_atomicity_and_overflow();
        overkill_mutual_ko_and_replay();damage_then_late_callback_rollback();adversarial_reconciliation();
        timeout_boundaries_and_priority();externally_adjudicated_surviving_hp();
        std::cout<<"independent damage review passed: "<<checks<<" checks\n";
    } catch(const std::exception& e) { std::cerr<<"independent damage review FAILED: "<<e.what()<<'\n';return 1; }
}
