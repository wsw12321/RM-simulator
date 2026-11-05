// Independent adversarial fixtures. Administrative injection is test-only.
#include "../roster_mode/core.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <type_traits>

namespace roster {
struct ReviewTestProbe {
    static auto& zones(Engine& e) { return e.zones_; }
    static const auto& zones(const Engine& e) { return e.zones_; }
    static auto& heat(Engine& e) { return e.heat_; }
    static const auto& heat(const Engine& e) { return e.heat_; }
    static const auto& damage(const Engine& e) { return e.damage_.totals(); }
    static auto nominal(const Engine& e) { return e.nominal_projectile_damage_; }
    static auto blocked(const Engine& e) { return e.blocked_fire_ticks_; }
    static auto complete(const Engine& e) { return e.damage_complete_; }
    static auto sequence(const Engine& e) { return e.damage_.last_sequence(); }
    static auto decision(const Engine& e) { return e.decision_; }
    static auto unresolved(const Engine& e) { return e.unresolved_; }
    static auto ledger_sequence(const Engine& e) { return e.ledger_.last_sequence(); }
    static void expected_hp(Engine& e) {
        e.expected_hp_.clear();
        for(const auto& r:e.state.robots)e.expected_hp_.push_back(r.hp);
    }
    static std::optional<std::string> rules(Engine& e,I t,std::vector<bool> died={}) {
        if(died.empty())died.assign(e.state.robots.size(),false);
        auto candidate=e;
        auto error=candidate.advance_rules(t,died);
        if(!error)e=std::move(candidate);
        return error;
    }
};
}
namespace {
namespace r=roster;
namespace p=r::policy;
namespace cap=r::cap;
using Probe=r::ReviewTestProbe;
using I=p::Integer;
int assertions=0;
void check(bool value,const std::string& message) {
    ++assertions;
    if(!value)throw std::runtime_error(message);
}
void ok(const std::optional<std::string>& error,const std::string& label) {
    check(!error,label+(error?": "+*error:""));
}
p::Action action(I dx=0,I dy=0,I x=6000,I y=3800,I fire=0) {
    return {I{1},dx,dy,x,y,fire};
}
std::vector<r::Request> manual(const r::Engine& e) {
    std::vector<r::Request> result;
    for(const auto& robot:e.config().robots)result.push_back({robot.id,"manual",action()});
    return result;
}
void set_action(std::vector<r::Request>& requests,const std::string& id,p::Action a) {
    for(auto& request:requests)if(request.id==id){request.action=std::move(a);return;}
    throw std::runtime_error("test requested nonexistent ID");
}
std::string bytes(const r::Engine& e,const std::string& id="red-1") {
    return r::observation_json(e.observation(id));
}
r::Robot& robot(r::Engine& e,const std::string& id) {return e.state.robots[e.index(id)];}
void incoming(r::Engine& e,const std::string& owner,const std::string& target,int amount,int count=1) {
    const auto& victim=robot(e,target);
    for(int i=0;i<count;++i)e.state.bullets.push_back({e.state.next_id++,int(e.index(owner)),
        victim.x,victim.y,0,0,100,amount});
}
#define ABSENT_MEMBER(member) \
 template<class T,class=void>struct has_##member:std::false_type{}; \
 template<class T>struct has_##member<T,std::void_t<decltype(std::declval<T>().member)>>:std::true_type{};
ABSENT_MEMBER(hp) ABSENT_MEMBER(heat) ABSENT_MEMBER(shots) ABSENT_MEMBER(hits)
ABSENT_MEMBER(owner) ABSENT_MEMBER(id) ABSENT_MEMBER(life) ABSENT_MEMBER(damage)
ABSENT_MEMBER(events) ABSENT_MEMBER(hash) ABSENT_MEMBER(terminal) ABSENT_MEMBER(progress_ms)
ABSENT_MEMBER(detectors) ABSENT_MEMBER(active) ABSENT_MEMBER(completed_at_ms)
#undef ABSENT_MEMBER
static_assert(!has_hp<p::Contact>::value && !has_heat<p::Contact>::value);
static_assert(!has_shots<p::Self>::value && !has_hits<p::Self>::value);
static_assert(!has_owner<p::Projectile>::value && !has_id<p::Projectile>::value);
static_assert(!has_life<p::Projectile>::value && !has_damage<p::Projectile>::value);
static_assert(!has_events<p::Observation>::value && !has_hash<p::Observation>::value);
static_assert(!has_terminal<p::Observation>::value && !has_progress_ms<p::Zone>::value);
static_assert(!has_detectors<p::Zone>::value && !has_active<p::Zone>::value);
static_assert(!has_completed_at_ms<p::Zone>::value);
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().contacts)>);
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().self)>);
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().roster)>);
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().self_heat)>);

template<class T>T get(cap::Result<T> result) {
    if(auto error=std::get_if<cap::Error>(&result))throw std::runtime_error(std::string(cap::error_name(*error)));
    return std::get<T>(std::move(result));
}

void privacy() {
    r::Engine original;
    robot(original,"red-1")={1000,1000,0,100,120};
    robot(original,"red-2")={2000,1000,0,80,90};
    robot(original,"blue-1")={4000,1000,128,100,120};
    robot(original,"blue-2")={11000,7000,128,80,90};
    original.state.bullets={{1,int(original.index("red-1")),2000,1000,20,0,180,10},
                            {2,int(original.index("blue-1")),3000,1000,-20,0,180,10}};
    const auto expected=bytes(original);
    const auto retained=original.observation("red-1");
    std::mt19937 rng(17891);
    for(int iteration=0;iteration<160;++iteration) {
        auto changed=original;
        for(const auto& id:{"red-2","blue-1","blue-2"}) {
            auto& body=robot(changed,id);
            body.hp=int(rng()%80);body.ammo=int(rng()%90);body.cooldown=int(rng()%9);
            body.shots=int(rng()%80);body.hits=int(rng()%80);
            r::heat::Config cfg;cfg.threshold=r::heat::ThresholdPolicy::EngineeringFlowchartGreaterEqualV1;
            auto value=std::get<r::heat::Heat>(r::heat::Heat::start(cfg));
            (void)value.apply({1,I(rng()%18),false});
            Probe::heat(changed)[changed.index(id)]=value;
        }
        robot(changed,"blue-2").x=10000+int(rng()%1800);
        robot(changed,"blue-2").y=6500+int(rng()%700);
        auto& own=robot(changed,"red-1");own.hits=int(rng()%120);own.shots=int(rng()%120);
        for(auto& bullet:changed.state.bullets) {
            bullet.id=int(rng());bullet.owner=int(rng()%4);bullet.life=1+int(rng()%180);bullet.damage=1+int(rng()%1000);
        }
        std::reverse(changed.state.bullets.begin(),changed.state.bullets.end());
        changed.state.bullets.push_back({5,0,11000,7000,260,0,80,10});
        changed.state.next_id=int(rng());changed.state.terminal=int(rng()%2);
        changed.events.push_back({0,2,0,1,10});
        for(auto& zone:Probe::zones(changed)) {
            const auto control=zone.capture.snapshot().control;
            const bool red=control.controller==cap::Team::Red;
            zone.capture=get(cap::CaptureAttempt::start(0,control,{!red,red},changed.config().boundary));
            (void)zone.capture.apply(1+rng()%4000);
            zone.completed_at_ms=10000+rng()%1000;
        }
        check(bytes(changed)==expected,"hidden current data or projectile owner/order leaks into fixed visible observation");
    }
    check(r::observation_json(retained)==expected,"retained owning snapshot changed");
    auto dead=original;
    robot(dead,"blue-1").hp=0;robot(dead,"red-2").hp=0;
    check(bytes(dead)==expected,"visible contact presence leaked death/HP");
    const auto contacts=original.observation("red-1").contacts;
    check(contacts.size()==2 && contacts[0].id=="blue-1" && contacts[1].id=="red-2","contact order is not stable ID order");
}

void stable_identity_and_transaction() {
    r::Config config;
    std::reverse(config.robots.begin(),config.robots.end());
    r::Engine changed(config),reference;
    check(changed.replay_json()==reference.replay_json(),"roster order changed identity/hash");
    auto inputs=manual(reference);
    for(auto& input:inputs)input.action=action(input.id.rfind("red",0)==0?1000:-1000,0);
    auto reversed=inputs;std::reverse(reversed.begin(),reversed.end());
    ok(reference.advance(9,inputs),"canonical input advance");
    ok(changed.advance(9,reversed),"reversed input advance");
    check(changed.replay_json()==reference.replay_json(),"input order changed per-ID actions");
    auto saved=reference.replay_json();
    for(const auto bad:{I{-1},I{0},I{601},std::numeric_limits<I>::max()}) {
        check(reference.advance(bad,inputs).has_value(),"bad batch accepted");
        check(reference.replay_json()==saved,"bad batch changed journal");
    }
    auto duplicate=inputs;duplicate[0].id=duplicate[1].id;
    check(reference.advance(1,duplicate).has_value(),"duplicate ID accepted");
    auto missing=inputs;missing.pop_back();
    check(reference.advance(1,missing).has_value(),"missing ID accepted");
    check(reference.replay_json()==saved,"bad membership changed journal");
    auto frames=reference.journal;
    for(auto& frame:frames)std::reverse(frame.inputs.begin(),frame.inputs.end());
    r::Engine replay;
    ok(replay.replay_frames(frames),"unordered frame input replay");
    check(replay.replay_json()==saved,"replay did not canonicalize stable IDs");
    frames.back().hash^=1;
    r::Engine failed;const auto empty=failed.replay_json();
    check(failed.replay_frames(frames).has_value(),"late corrupted replay accepted");
    check(failed.replay_json()==empty,"late replay divergence committed prefix");
    for(auto invalid:{r::Config{},r::Config{}}) {
        invalid.robots.clear();
        bool rejected=false;
        try{reference.reset(invalid);}catch(const std::exception&){rejected=true;}
        check(rejected&&reference.replay_json()==saved,"zero roster reset did not reject atomically");
    }
}

std::vector<std::pair<std::string,std::string>> observed;
int callback_failure_tick=-1;
bool callback_invalid=false;
p::Action callback(const p::Observation& obs) {
    observed.emplace_back(obs.self_id,r::observation_json(obs));
    if(obs.self_id=="red-2" && obs.tick==callback_failure_tick)throw std::runtime_error("private-review-exception");
    auto result=action(obs.self_team==0?1000:-1000,0);
    if(callback_invalid&&obs.self_id=="red-2")result.fire=true;
    return result;
}
void frozen_callbacks() {
    r::Engine engine,reference;
    std::vector<r::Request> requests;
    std::vector<r::Controller> controllers;
    for(const auto& entry:engine.config().robots) {
        requests.push_back({entry.id,"policy",{}});
        controllers.push_back({entry.id,{1,"independent-roster-review-v1"},callback});
    }
    std::reverse(controllers.begin(),controllers.end());
    std::reverse(requests.begin(),requests.end());
    observed.clear();
    ok(engine.advance(7,requests,controllers),"all-ID callback step");
    check(observed.size()==28,"callback count incorrect");
    for(int tick=0;tick<7;++tick) {
        for(std::size_t index=0;index<4;++index) {
            const auto id=reference.config().robots[index].id;
            check(observed[4*tick+index].first==id,"callback ID order not canonical");
            check(observed[4*tick+index].second==bytes(reference,id),"callback did not receive frozen pre-step state");
        }
        auto inputs=manual(reference);
        for(auto& input:inputs)input.action=action(reference.config().robots[reference.index(input.id)].team==0?1000:-1000);
        ok(reference.advance(1,inputs),"manual frozen snapshot reference");
    }
    check(engine.hash()==reference.hash(),"manual and callback physics differ");
    const auto before=engine.replay_json(),state=engine.json("red-1",true);
    callback_failure_tick=11;
    auto error=engine.advance(10,requests,controllers);
    check(error.has_value()&&error->find("private-review-exception")==std::string::npos,"callback failure missing or private diagnostic leaked");
    check(engine.replay_json()==before&&engine.json("red-1",true)==state,"late callback failure committed partial state");
    callback_failure_tick=-1;callback_invalid=true;
    check(engine.advance(2,requests,controllers).has_value(),"boolean callback action accepted");
    check(engine.replay_json()==before,"invalid callback action committed");
    callback_invalid=false;
    r::Engine replay;
    ok(replay.replay_frames(engine.journal),"callback-free replay");
    check(replay.replay_json()==engine.replay_json(),"callback-free reconstruction differs");
}

void teammate_capture_continuity() {
    r::Engine ordinary,partial_death;
    for(auto* e:{&ordinary,&partial_death}) {
        robot(*e,"red-1").x=6500;robot(*e,"red-1").y=5700;
        robot(*e,"red-2").x=7300;robot(*e,"red-2").y=6500;
        ok(Probe::rules(*e,1),"two teammate capture acquire");
    }
    robot(ordinary,"red-1").x=5000;
    robot(partial_death,"red-1").hp=0;
    std::vector<bool> died(4,false);died[partial_death.index("red-1")]=true;
    ok(Probe::rules(ordinary,3001),"one teammate leaves");
    ok(Probe::rules(partial_death,3001,died),"one teammate dies");
    check(Probe::zones(partial_death)[1].detectors[partial_death.index("red-2")].snapshot().logically_active,
          "teammate death reset surviving member");
    check(!Probe::zones(partial_death)[1].detectors[partial_death.index("red-1")].snapshot().logically_active,
          "dead member retained activation/grace");
    for(auto* e:{&ordinary,&partial_death}) {
        ok(Probe::rules(*e,5002),"survivor capture completes");
        check(Probe::zones(*e)[1].completed_at_ms==5001,"partial leave/death lost team capture continuity");
    }
    r::Engine grace;
    robot(grace,"red-1").x=6500;robot(grace,"red-1").y=5700;
    robot(grace,"red-2").x=7300;robot(grace,"red-2").y=6500;
    ok(Probe::rules(grace,1),"member-grace acquire");
    robot(grace,"red-1").x=5000;
    ok(Probe::rules(grace,1001),"first member leaves");
    robot(grace,"red-2").hp=0;
    died.assign(4,false);died[grace.index("red-2")]=true;
    ok(Probe::rules(grace,2001,died),"last present member dies");
    check(Probe::zones(grace)[1].capture.snapshot().active.red,"one member's death erased living teammate's independent grace");
    ok(Probe::rules(grace,3001),"per-member grace expires");
    check(!Probe::zones(grace)[1].capture.snapshot().active.red,"aggregate added a second grace period");

    for(auto boundary:{cap::BoundaryPolicy::StrictRejectUnadjudicated,cap::BoundaryPolicy::EngineeringTimerBeforeInputV1}) {
        r::Config config;config.boundary=boundary;r::Engine e(config);
        robot(e,"red-1").x=6500;robot(e,"red-1").y=5700;
        ok(Probe::rules(e,1),"exact boundary acquire");
        robot(e,"red-1").hp=0;died.assign(4,false);died[e.index("red-1")]=true;
        const auto before=e.hash();
        auto error=Probe::rules(e,5001,died);
        if(boundary==cap::BoundaryPolicy::StrictRejectUnadjudicated) {
            check(error&&error->find("UnadjudicatedCoincidence")!=std::string::npos,"strict same-time death/completion accepted");
            check(e.hash()==before,"strict capture boundary committed partial detector state");
        } else {
            ok(error,"engineering exact death/completion");
            check(Probe::zones(e)[1].completed_at_ms==5001,"engineering exact timer-before-death completion lost");
        }
    }
}

void simultaneous_damage_and_heat() {
    r::Config config;config.heat_enabled=false;
    r::Engine e(config);
    robot(e,"red-1").hp=7;robot(e,"red-2").hp=6;
    robot(e,"blue-1").hp=9;robot(e,"blue-2").hp=8;
    Probe::expected_hp(e);
    incoming(e,"blue-1","red-1",10,2);
    incoming(e,"blue-2","red-2",8,2);
    incoming(e,"red-1","blue-1",10,2);
    incoming(e,"red-2","blue-2",8,2);
    auto shuffled=e;std::reverse(shuffled.state.bullets.begin(),shuffled.state.bullets.end());
    ok(e.advance(1,manual(e)),"simultaneous all-ID overkill");
    ok(shuffled.advance(1,manual(shuffled)),"reversed projectile order");
    for(const auto& body:e.state.robots)check(body.hp==0,"simultaneous damage missed victim");
    const auto totals=Probe::damage(e);
    check(totals.attack_damage==std::array<I,2>{17,13},"team actual-loss credit confused roster indices or counted overkill");
    check(totals.actual_hp_loss==std::array<I,2>{13,17},"victim team attribution wrong");
    check(Probe::sequence(e)==4,"victim damage not aggregated once per robot");
    check(Probe::nominal(e)==std::array<I,2>{36,36},"nominal per-team projectile total wrong");
    check(e.hash()==shuffled.hash(),"same-tick hit order affected settled state");
    check(!e.state.terminal,"zero living roster ended clock early");
    ok(e.advance(10,manual(e)),"all-disabled clock continuation");
    check(e.state.tick==11,"disabled roster clock stopped");

    r::Engine ally;
    const auto ally_hp=robot(ally,"red-2").hp;
    incoming(ally,"red-1","red-2",1000);
    ok(ally.advance(1,manual(ally)),"friendly projectile pass-through");
    check(robot(ally,"red-2").hp==ally_hp&&Probe::damage(ally).attack_damage==std::array<I,2>{0,0},
          "friendly pass-through caused damage or credit");

    r::Engine heat;
    auto requests=manual(heat);set_action(requests,"red-1",action(0,0,12000,2900,1));
    ok(heat.advance(41,requests),"own heat burst");
    check(robot(heat,"red-1").shots==5,"expected own emitted-shot count");
    check(Probe::heat(heat)[heat.index("red-1")].snapshot().overheat_locked,"own thermal gate did not latch");
    for(const auto& entry:heat.config().robots)if(entry.id!="red-1")
        check(Probe::heat(heat)[heat.index(entry.id)].snapshot().heat_tenths==0,"heat leaked between robot IDs");
    const auto ammo=robot(heat,"red-1").ammo;
    ok(heat.advance(20,requests),"own thermal lock pause");
    check(robot(heat,"red-1").ammo==ammo,"blocked fire consumed ammo");
    check(Probe::blocked(heat)[heat.index("red-1")]>=20,"blocked own fire not counted");
    for(const auto& frame:heat.journal)
        for(const auto& input:frame.inputs)if(input.id=="red-1")check(input.action[4]==1,"replay lost requested blocked fire");
}

void late_atomic_rules_and_heat() {
    r::Config config;
    for(auto& entry:config.robots) {
        if(entry.id=="red-1")entry.spawn={6500,5700,0};
        if(entry.id=="blue-1")entry.spawn={7300,6500,128};
    }
    r::Engine contested(config);
    ok(contested.advance(10,manual(contested)),"establish actual contested occupancy");
    incoming(contested,"blue-1","red-1",10,10);
    incoming(contested,"red-1","blue-1",10,10);
    const auto state=contested.json("red-1",true),replay=contested.replay_json();
    const auto error=contested.advance(1,manual(contested));
    check(error&&error->find("UnadjudicatedContestExitToNone")!=std::string::npos,
          "contested-to-none simultaneous death invented a rule");
    check(contested.json("red-1",true)==state&&contested.replay_json()==replay,
          "late capture ambiguity committed same-tick combat/damage/heat");
    check(Probe::sequence(contested)==0&&robot(contested,"red-1").hp==100,
          "capture rollback retained settled damage/death");

    r::Engine heat;
    ok(heat.advance(4,manual(heat)),"thermal rollback baseline");
    robot(heat,"red-1").cooldown=2;
    r::heat::Config strict_config;
    auto strict=std::get<r::heat::Heat>(r::heat::Heat::start(strict_config));
    (void)strict.apply({r::Engine::time_ms(4),0,false});
    Probe::heat(heat)[heat.index("red-1")]=strict;
    incoming(heat,"red-1","blue-1",10);
    const auto before=heat.json("red-1",true),before_replay=heat.replay_json();
    auto requests=manual(heat);set_action(requests,"red-1",action(1000,0,12000,2900,1));
    const auto heat_error=heat.advance(2,requests);
    check(heat_error&&heat_error->find("UnadjudicatedShotCoolingCoincidence")!=std::string::npos,
          "strict same-time cooling/shot accepted");
    check(heat.json("red-1",true)==before&&heat.replay_json()==before_replay,
          "late heat error committed prior candidate damage or motion");
    check(Probe::sequence(heat)==0&&robot(heat,"blue-1").hp==100,
          "late thermal rollback retained victim settlement");
}

void profile_bounds_and_motion() {
    r::Config config;
    for(auto& entry:config.robots) {
        entry.profile.id="SYNTHETIC-maximum";
        auto& p=entry.profile;p.max_hp=10000;p.ammo=5000;p.radius_mm=500;p.speed_mm_tick=100;
        p.projectile_speed_mm_tick=1000;p.projectile_damage=1000;p.cooldown_ticks=1;
        p.sensor_range_mm=20000;p.heat_limit_tenths=20000;p.cooling_per_second=1000;
    }
    r::Engine maximum(config);
    auto inputs=manual(maximum);
    for(auto& input:inputs)input.action=action(0,0,6000,3800,1);
    ok(maximum.advance(600,inputs),"maximum profile arithmetic");
    for(const auto& body:maximum.state.robots)check(body.hp>=0&&body.ammo>=0,"maximum profile arithmetic underflow");
    auto crowded_config=config;
    crowded_config.heat_enabled=false;
    const auto prototype=crowded_config.robots.front();
    crowded_config.robots.clear();
    for(int team=0;team<2;++team)for(int i=0;i<4;++i) {
        auto entry=prototype;
        entry.id=std::string(team?"blue-":"red-")+std::to_string(i+1);
        entry.team=team;
        entry.spawn={team?11000:1000,I(600+i*2100),team?128:0};
        entry.profile.radius_mm=100;
        entry.profile.projectile_speed_mm_tick=80;
        crowded_config.robots.push_back(entry);
    }
    r::Engine crowded(crowded_config);
    auto crowded_inputs=manual(crowded);
    for(auto& input:crowded_inputs)input.action=action(0,0,6000,3800,1);
    ok(crowded.advance(600,crowded_inputs),"eight robot maximum-ammo rapid-fire bounds");
    check(crowded.state.bullets.size()<2048,"admissible eight-robot settings reached projectile capacity");
    for(auto& entry:config.robots)entry.profile.radius_mm=100;
    r::Engine moving(config);
    // Chain: A follows B; B's proposed pose hits stationary C. Reverting B
    // must also block A rather than let A overlap B's old position.
    robot(moving,"red-1")={1000,1000,0,10000,5000};
    robot(moving,"red-2")={1250,1000,0,10000,5000};
    robot(moving,"blue-1")={1500,1000,128,10000,5000};
    robot(moving,"blue-2")={11000,7000,128,10000,5000};
    inputs=manual(moving);set_action(inputs,"red-1",action(1000,0));set_action(inputs,"red-2",action(1000,0));
    ok(moving.advance(1,inputs),"multi-body translation closure");
    check(robot(moving,"red-1").x==1000&&robot(moving,"red-2").x==1250,"collision reversion did not close over third robot");
    r::Engine tangent(config);
    robot(tangent,"red-1")={1000,1000,0,10000,5000};
    robot(tangent,"red-2")={1200,1000,0,10000,5000};
    robot(tangent,"blue-1")={4000,1000,128,10000,5000};
    robot(tangent,"blue-2")={11000,7000,128,10000,5000};
    auto separating=manual(tangent);
    set_action(separating,"red-1",action(-1000,0));
    set_action(separating,"red-2",action(1000,0));
    ok(tangent.advance(1,separating),"initial tangent separating motion");
    check(robot(tangent,"red-1").x==900&&robot(tangent,"red-2").x==1300,
          "valid touching robots cannot separate");
    for(std::size_t i=0;i<moving.state.robots.size();++i)
        for(std::size_t j=i+1;j<moving.state.robots.size();++j) {
            const auto dx=I(moving.state.robots[i].x)-moving.state.robots[j].x;
            const auto dy=I(moving.state.robots[i].y)-moving.state.robots[j].y;
            check(dx*dx+dy*dy>=40000,"post-reversion robot overlap");
        }
}
void timeout_team_aggregation() {
    r::Config config;
    for(auto& entry:config.robots)if(entry.team==0) {
        entry.profile.id="SYNTHETIC-red-"+entry.id;
        entry.profile.max_hp+=1;
    }
    r::Engine e(config);
    while(!e.state.terminal)ok(e.advance(r::kMaxBatch,manual(e)),"full roster timeout");
    check(e.state.tick==25200&&Probe::ledger_sequence(e)==420,"roster timeout clock/credit count incorrect");
    const auto decision=Probe::decision(e);
    check(decision.outcome==r::match::Outcome::RedWins&&decision.reason==r::match::Reason::TimeoutSurvivingRobotHp,
          "timeout did not aggregate remaining HP by stable team membership");
    const auto saved=e.replay_json();
    check(e.advance(1,manual(e)).has_value()&&e.replay_json()==saved,"terminal request committed");
    r::Engine replay(config);
    for(std::size_t at=0;at<e.journal.size();at+=r::kMaxBatch) {
        const auto end=std::min(e.journal.size(),at+std::size_t(r::kMaxBatch));
        ok(replay.replay_frames({e.journal.begin()+at,e.journal.begin()+end}),"full roster resolved-action replay");
    }
    check(replay.replay_json()==saved,"full 420s roster replay differs");
}
}
int main() {
    try {
        privacy();stable_identity_and_transaction();frozen_callbacks();
        teammate_capture_continuity();simultaneous_damage_and_heat();late_atomic_rules_and_heat();profile_bounds_and_motion();timeout_team_aggregation();
        std::cout<<"PASS independent roster review: "<<assertions<<" assertions; 2v2 privacy, ID permutations, frozen callbacks, rollback/replay, member capture/death/grace, simultaneous overkill, heat isolation, bounds and collision closure.\n";
    } catch(const std::exception& e) {
        std::cerr<<"FAIL independent roster review: "<<e.what()<<'\n';return 1;
    }
}
