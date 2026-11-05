#include "../tactical_mode/core.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
namespace tactical {
struct TestProbe {
    static void hidden_progress(Engine& e) {
        auto& z=e.zones_[0];
        z.detectors[1]=std::get<cap::ActivationDelay>(cap::ActivationDelay::start(0,cap::QualifiedDetection::Present,e.config_.boundary));
        z.capture=std::get<cap::CaptureAttempt>(cap::CaptureAttempt::start(0,
            std::get<cap::PositionControl>(cap::initial_control(cap::PositionKind::Outpost,cap::Team::Red)),{false,true},e.config_.boundary));
        assert(!std::holds_alternative<cap::Error>(z.capture.apply(1700)));
        z.completed_at_ms=999; // Deliberately hostile diagnostic metadata, never broadcast.
    }
    static const auto& zones(const Engine& e) {return e.zones_;}
    static std::uint64_t credits(const Engine& e) {return e.ledger_.last_sequence();}
};
}
namespace {
namespace p=tactical::policy;
using I=p::Integer;
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().combat)>);
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().zones)>);
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().score)>);
static_assert(!std::is_reference_v<decltype(std::declval<p::Observation>().combat)>);
static_assert(!std::is_pointer_v<decltype(std::declval<p::Observation>().combat)>);
p::Action neutral(I x=1600,I y=3800) {return {I{1},I{0},I{0},x,y,I{0}};}
std::array<tactical::Request,2> manuals(p::Action a=neutral(),p::Action b=neutral(10400,3800)) {return {{{"manual",a},{"manual",b}}};}
std::array<tactical::Request,2> policies() {return {{{"policy",{}},{"policy",{}}}};}
std::array<tactical::Controller,2> bindings(p::DecisionFunction a=p::decide,p::DecisionFunction b=p::decide) {return {{{p::descriptor(),a},{p::descriptor(),b}}};}
p::Action fixed_output;
std::vector<I> calls,ticks,xs;
std::vector<std::array<I,2>> scores,rates;
std::vector<std::string> first_controls;
I fail_tick=-1;int failure=0;
p::Action fixed(const p::Observation& o) {calls.push_back(o.combat.self_index);return fixed_output;}
p::Action recording(const p::Observation& o) {
    calls.push_back(o.combat.self_index);ticks.push_back(o.combat.tick);xs.push_back(o.combat.self.x);
    scores.push_back(o.score);rates.push_back(o.rate);
    first_controls.push_back(o.zones[0].degree+std::to_string(o.zones[0].controller));
    if(o.combat.tick==fail_tick && o.combat.self_index==0) {
        if(failure==1) throw std::runtime_error("PRIVATE-DIAGNOSTIC-SECRET");
        if(failure==2) throw 42;
        return {I{1},I{1001},I{0},I{0},I{0},I{0}};
    }
    return {I{1},I{o.combat.self_index ? -1000 : 1000},I{0},I{6000},I{1000},I{0}};
}
void clear(){calls.clear();ticks.clear();xs.clear();scores.clear();rates.clear();first_controls.clear();fail_tick=-1;failure=0;}
bool same(const p::Action& a,const p::Action& b) {return a.sdk_version==b.sdk_version&&a.dx==b.dx&&a.dy==b.dy&&a.aim_x==b.aim_x&&a.aim_y==b.aim_y&&a.fire==b.fire;}
}
int main() {
    // Isolated owning snapshots outlive the engine and preserve the unchanged sensor.
    {
        const auto snapshot=[] {tactical::Engine e;return e.observation(0);}();
        assert(snapshot.combat.self.x==1600 && !snapshot.combat.enemy);
        assert(snapshot.zones.size()==4 && snapshot.zones[1].controller==1);
        assert((snapshot.credits_enabled && snapshot.rate==std::array<I,2>{}));
        tactical::Engine e;const auto saved=tactical::observation_json(snapshot);
        e.native.state.robots[0].x=500;
        assert(tactical::observation_json(snapshot)==saved);
        assert(tactical::Engine::time_ms(1)==17 && tactical::Engine::time_ms(2)==34 && tactical::Engine::time_ms(3)==50);
        assert(e.native.options.duration_ticks==25200 && !e.native.options.stop_on_disable && !e.native.options.record_journal);
    }
    // Hidden enemy/activation/progress changes leave all allowed observation bytes
    // and decisions invariant, CONDITIONAL ON unchanged public control and score.
    {
        tactical::Engine a,b;
        a.native.state.bullets={{8,0,2200,3800,260,0,33},{9,1,2400,3900,-200,12,55},{10,1,10000,7000,200,2,44}};
        b=a;auto& foe=b.native.state.robots[1];
        foe.x=11000;foe.y=7000;foe.yaw=73;foe.hp=0;foe.ammo=1;foe.cooldown=7;foe.shots=42;foe.hits=9;
        b.native.state.robots[0].shots=88;b.native.state.robots[0].hits=77;b.native.state.next_id=907;b.native.state.terminal=1;
        b.native.events={{92,2,1,0,10}};b.native.journal={{42,{},1}};
        std::reverse(b.native.state.bullets.begin(),b.native.state.bullets.end());
        for(auto& bullet:b.native.state.bullets){bullet.id+=100;bullet.owner=1-bullet.owner;bullet.life=1;}
        b.native.state.bullets.push_back({99,0,11000,1000,99,-34,12});
        tactical::TestProbe::hidden_progress(b);
        const auto ao=a.observation(0),bo=b.observation(0);
        assert(a.hash()!=b.hash());
        assert(tactical::observation_json(ao)==tactical::observation_json(bo));
        assert(tactical::observation_hash(ao)==tactical::observation_hash(bo));
        assert(same(p::decide(ao),p::decide(bo)));
        const auto text=tactical::observation_json(bo);
        for(const auto* forbidden:{"progress_ms","detected","deactivation_at","completed_at","hash","events","hits","shots","terminal"})
            assert(text.find(forbidden)==std::string::npos);
        // A visible disabled enemy is still a pose, not an HP-dependent presence leak.
        a.native.state.robots[1]={4000,3800,128};
        auto visible=tactical::observation_json(a.observation(0));
        a.native.state.robots[1].hp=0;a.native.state.robots[1].ammo=0;
        assert(tactical::observation_json(a.observation(0))==visible);
    }
    // Common pre-step snapshots include identical public broadcast state.
    {
        clear();tactical::Engine e;assert(!e.advance(8,policies(),bindings(recording,recording)));
        assert(calls.size()==16);
        for(I t=0;t<8;++t) {
            auto i=std::size_t(t*2);assert(calls[i]==0&&calls[i+1]==1&&ticks[i]==t&&ticks[i+1]==t);
            assert(xs[i]==1600+t*55&&xs[i+1]==10400-t*55);
            assert(scores[i]==scores[i+1]&&rates[i]==rates[i+1]&&first_controls[i]==first_controls[i+1]);
        }
    }
    // Shared validator, same resolved actions, manual/policy parity, atomic failures.
    {
        const std::array<p::Action,12> bad{{
            {I{0},I{0},I{0},I{0},I{0},I{0}},{true,I{0},I{0},I{0},I{0},I{0}},
            {1.0,I{0},I{0},I{0},I{0},I{0}},{I{1},true,I{0},I{0},I{0},I{0}},
            {I{1},I{0},0.0,I{0},I{0},I{0}},{I{1},I{1001},I{0},I{0},I{0},I{0}},
            {I{1},I{0},I{-1001},I{0},I{0},I{0}},{I{1},I{0},I{0},I{12001},I{0},I{0}},
            {I{1},I{0},I{0},I{0},I{-1},I{0}},{I{1},I{0},I{0},I{0},I{0},true},
            {I{1},I{0},I{0},I{0},I{0},I{2}},{I{1},std::numeric_limits<double>::infinity(),I{0},I{0},I{0},I{0}}}};
        for(const auto& action:bad) {
            tactical::Engine manual,policy;fixed_output=action;clear();const auto before=manual.replay_json();
            auto error=p::validate_action(action);assert(error);
            assert(manual.advance(1,manuals(action))==error);
            assert(policy.advance(1,policies(),bindings(fixed,fixed))==error);
            assert(calls.size()==2&&manual.replay_json()==before&&policy.replay_json()==before);
        }
        for(I dx:{I{-1000},I{0},I{1000}}) {
            tactical::Engine m,p;fixed_output={I{1},dx,-dx,I{6000},I{1000},I{0}};
            assert(!m.advance(5,manuals(fixed_output,fixed_output)));assert(!p.advance(5,policies(),bindings(fixed,fixed)));
            assert(m.hash()==p.hash());
            for(std::size_t i=0;i<5;++i) assert(m.frames[i].inputs[0].action==p.frames[i].inputs[0].action&&m.frames[i].observation_hashes==p.frames[i].observation_hashes);
        }
        for(int failure_kind=0;failure_kind<3;++failure_kind) {
            clear();tactical::Engine e;assert(!e.advance(2,manuals()));
            auto before=e.replay_json(),state=e.json(0,true);fail_tick=5;failure=failure_kind;
            const auto error=e.advance(8,policies(),bindings(recording,recording));
            assert(error&&error->find("PRIVATE-DIAGNOSTIC")==std::string::npos);
            assert(e.replay_json()==before&&e.json(0,true)==state);
        }
    }
    // Disabled robots retain bodies but cannot act; no early KO or respawn.
    {
        tactical::Engine e;e.native.state.robots[0].hp=0;const auto before=e.native.state.robots[0];
        assert(!e.advance(60,manuals({I{1},I{1000},I{1000},I{5000},I{5000},I{1}})));
        const auto after=e.native.state.robots[0];assert(after.x==before.x&&after.y==before.y&&after.yaw==before.yaw&&after.ammo==before.ammo&&after.shots==0&&after.hp==0);
        assert(!e.native.state.terminal&&e.native.state.tick==60);
    }
    // SAME run: filtered policy captures, earns control, fights, remains alive as a
    // simulation after KO, reaches actual420s, and replays without any callbacks.
    {
        tactical::Engine e;int first_damage=-1,first_capture=-1,disable=-1;
        while(!e.native.state.terminal) {
            const auto error=e.advance(600,policies(),bindings());if(error)std::cerr<<*error<<'\n';assert(!error);
            const auto obs=e.observation(0);
            if(first_capture<0&&(obs.zones[1].controller==0||obs.zones[2].controller==1))first_capture=e.native.state.tick;
            if(first_damage<0&&(e.native.state.robots[0].hp<100||e.native.state.robots[1].hp<100))first_damage=e.native.state.tick;
            if(disable<0&&(e.native.state.robots[0].hp==0||e.native.state.robots[1].hp==0))disable=e.native.state.tick;
        }
        const auto obs=e.observation(0);
        assert(e.native.state.tick==25200&&e.journal.size()==25200&&tactical::TestProbe::credits(e)==420);
        assert(first_capture>0&&first_damage>0&&disable>0&&disable<25200);
        assert(obs.zones[1].controller==0&&obs.zones[2].controller==1&&obs.score[0]>0&&obs.score[1]>0);
        assert(e.native.state.robots[0].shots>0&&e.native.state.robots[1].shots>0);
        assert(e.json(0).find("timeout-control")!=std::string::npos);
        tactical::Engine replay;
        for(std::size_t start=0;start<e.journal.size();start+=600) {
            const auto end=std::min(e.journal.size(),start+600);
            assert(!replay.replay_frames({e.journal.begin()+std::ptrdiff_t(start),e.journal.begin()+std::ptrdiff_t(end)}));
        }
        assert(replay.replay_json()==e.replay_json());assert(replay.json(0)==e.json(0));
        const auto final=e.replay_json();assert(e.advance(1,policies(),bindings()));assert(e.replay_json()==final);
        for(int mutation=0;mutation<5;++mutation) {
            tactical::Engine rejected;auto batch=std::vector<tactical::Frame>(e.journal.begin(),e.journal.begin()+10);
            if(mutation==0)++batch.back().hash;
            if(mutation==1)++batch.back().observation_hashes[1];
            if(mutation==2)batch.back().inputs[0].action[0]=1001;
            if(mutation==3)batch.back().tick=99;
            if(mutation==4)batch.back().inputs[0].controller_id="bad id";
            auto before=rejected.replay_json();assert(rejected.replay_frames(batch));assert(rejected.replay_json()==before);
        }
        std::cout<<"tactical fixture: capture_by="<<first_capture<<" damage_by="<<first_damage<<" disable_by="<<disable<<" timeout="<<e.native.state.tick<<" score="<<obs.score[0]<<','<<obs.score[1]<<" hash="<<e.hash()<<'\n';
    }
    // Explicit missing strategy preserves conservative legacy unresolved behavior.
    // Default experimental complete idle ledger separately permits a real draw.
    for(bool credits:{true,false}) {
        tactical::Engine e({tactical::cap::BoundaryPolicy::EngineeringTimerBeforeInputV1,credits,tactical::DamageStrategy::MissingAdjudicated});
        while(!e.native.state.terminal)assert(!e.advance(600,manuals()));
        assert(e.native.state.tick==25200);
        const auto text=e.json(0);
        assert(text.find(credits ? "unresolved-timeout-tie" : "unscored-timeout")!=std::string::npos);
        assert(text.find(credits ? "missing-adjudicated-attack-damage" : "credit-strategy-disabled")!=std::string::npos);
        if(!credits)assert(text.find("\"score\":null")!=std::string::npos);
    }
    { tactical::Engine e; while(!e.native.state.terminal)assert(!e.advance(600,manuals()));
      assert(e.json(0).find("timeout-equal")!=std::string::npos); }
    std::cout<<"tactical core tests passed\n";
}
