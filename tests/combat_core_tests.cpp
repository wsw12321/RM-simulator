#include "../combat_mode/core.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {
namespace p=combat::policy;
using I=p::Integer;
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().self)>);
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().enemy)>);
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().arena)>);
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().projectiles)>);
static_assert(!std::is_reference_v<decltype(std::declval<p::Observation>().self)>);
static_assert(!std::is_pointer_v<decltype(std::declval<p::Observation>().self)>);
const p::Descriptor test_descriptor{1,"combat-test-v1"};
p::Action neutral(I x=1600,I y=3800) { return {I{1},I{0},I{0},x,y,I{0}}; }
std::array<combat::Request,2> manuals(p::Action first=neutral(),p::Action second=neutral(10400,3800)) {
    return {{{"manual",first},{"manual",second}}};
}
std::array<combat::Request,2> policies() { return {{{"policy",{}},{"policy",{}}}}; }
std::array<combat::Controller,2> bindings(p::DecisionFunction a=p::decide,p::DecisionFunction b=p::decide) {
    return {{{test_descriptor,a},{test_descriptor,b}}};
}
p::Action output;
std::vector<I> calls,ticks,xs;
I fail_tick=-1;
int fail_kind=0;
p::Action fixed(const p::Observation& o) { calls.push_back(o.self_index); return output; }
p::Action recording(const p::Observation& o) {
    calls.push_back(o.self_index); ticks.push_back(o.tick); xs.push_back(o.self.x);
    if (o.tick==fail_tick && o.self_index==0) {
        if (fail_kind==1) throw std::runtime_error("private-exception-text");
        if (fail_kind==2) throw 11;
        return {I{1},I{1001},I{0},I{1600},I{3800},I{0}};
    }
    return {I{1},I{o.self_index ? -1000 : 1000},I{0},I{6000},I{1000},I{0}};
}
void clear() { calls.clear();ticks.clear();xs.clear();fail_tick=-1;fail_kind=0; }
}
int main() {
    // The immutable, standalone snapshot owns copies; lifetime is engine-independent.
    {
        const auto snapshot=[] { sandbox::Engine e; return combat::make_observation(e,0); }();
        assert(snapshot.self.x==1600 && snapshot.arena.walls.size()==3);
        assert(!snapshot.enemy && snapshot.projectiles.empty());
        sandbox::Engine e;
        const auto saved=combat::observation_json(combat::make_observation(e,0));
        e.state.robots[0].x=999;
        assert(combat::observation_json(snapshot)==saved);
    }
    // Hidden-field metamorphisms cannot change any observable bytes or decisions.
    {
        sandbox::Engine base;
        base.state.bullets={{8,0,2200,3800,260,0,33},{9,1,2400,3900,-200,12,55},
                            {10,1,10000,7000,200,2,44}};
        const auto original=combat::make_observation(base,0);
        const auto encoded=combat::observation_json(original);
        auto changed=base;
        auto& enemy=changed.state.robots[1];
        enemy.x=11000; enemy.y=7000; enemy.yaw=73; enemy.hp=0; enemy.ammo=1; enemy.cooldown=7; enemy.shots=42; enemy.hits=9;
        changed.state.robots[0].shots=88; changed.state.robots[0].hits=77;
        changed.state.next_id=907; changed.state.terminal=1;
        changed.events={{92,2,1,0,10}}; changed.journal={{42,{},1}};
        std::reverse(changed.state.bullets.begin(),changed.state.bullets.end());
        for (auto& b:changed.state.bullets) { b.id+=100; b.owner=1-b.owner; b.life=1; }
        changed.state.bullets.push_back({99,0,11000,1000,99,-34,12});
        const auto filtered=combat::make_observation(changed,0);
        assert(combat::observation_json(filtered)==encoded);
        assert(combat::observation_hash(filtered)==combat::observation_hash(original));
        auto a=p::decide(original), b=p::decide(filtered);
        assert(a.dx==b.dx && a.dy==b.dy && a.aim_x==b.aim_x && a.aim_y==b.aim_y && a.fire==b.fire);
        // Even visible opponent HP does not control enemy presence.
        base.state.robots[1]={4000,3800,128};
        const auto visible=combat::observation_json(combat::make_observation(base,0));
        base.state.robots[1].hp=0; base.state.robots[1].ammo=0; base.state.robots[1].shots=99;
        assert(combat::observation_json(combat::make_observation(base,0))==visible);
    }
    // Inclusive range boundary and original conservative line-of-sight behavior.
    {
        sandbox::Engine e;
        e.state.robots[0]={1000,1000,0}; e.state.robots[1]={7000,1000,128};
        assert(combat::make_observation(e,0).enemy);
        e.state.robots[1].x=7001; assert(!combat::make_observation(e,0).enemy);
        e.state.robots[0]={4500,3800,0}; e.state.robots[1]={7500,3800,128};
        assert(!combat::make_observation(e,0).enemy);
        e.state.robots[0].y=2300; e.state.robots[1].y=2300;
        assert(combat::make_observation(e,0).enemy);
        e.state.robots[0].y=2800; e.state.robots[1].y=2800;
        assert(!combat::make_observation(e,0).enemy); // tangency is blocked
    }
    // Both callbacks see the same pre-step tick; no first-agent movement commits early.
    {
        clear(); combat::Engine e;
        assert(!e.advance(8,policies(),bindings(recording,recording)));
        assert(calls.size()==16 && e.native.state.tick==8);
        for (I t=0;t<8;++t) {
            assert(calls[std::size_t(t*2)]==0 && calls[std::size_t(t*2+1)]==1);
            assert(ticks[std::size_t(t*2)]==t && ticks[std::size_t(t*2+1)]==t);
            assert(xs[std::size_t(t*2)]==1600+t*55 && xs[std::size_t(t*2+1)]==10400-t*55);
            assert(e.journal[std::size_t(t)].tick==t);
        }
    }
    // Manual and policy output actions use exactly the same validator and range/type errors.
    {
        const std::array<p::Action,13> bad{{
            {I{0},I{0},I{0},I{0},I{0},I{0}}, {true,I{0},I{0},I{0},I{0},I{0}},
            {1.0,I{0},I{0},I{0},I{0},I{0}}, {I{1},true,I{0},I{0},I{0},I{0}},
            {I{1},I{0},0.0,I{0},I{0},I{0}}, {I{1},I{1001},I{0},I{0},I{0},I{0}},
            {I{1},I{0},I{-1001},I{0},I{0},I{0}}, {I{1},I{0},I{0},I{12001},I{0},I{0}},
            {I{1},I{0},I{0},I{0},I{-1},I{0}}, {I{1},I{0},I{0},I{0},I{0},true},
            {I{1},I{0},I{0},I{0},I{0},I{2}},
            {I{1},std::numeric_limits<double>::infinity(),I{0},I{0},I{0},I{0}},
            {I{1},I{0},std::numeric_limits<I>::min(),I{0},I{0},I{0}}}};
        for (const auto& action:bad) {
            combat::Engine manual,policy; output=action; clear();
            const auto before=manual.replay_json();
            const auto expected=p::validate_action(action); assert(expected);
            assert(manual.advance(1,manuals(action))==expected);
            assert(policy.advance(1,policies(),bindings(fixed,fixed))==expected);
            assert(calls.size()==2); // even bad output from robot0 did not skip collecting robot1
            assert(manual.replay_json()==before && policy.replay_json()==before);
        }
        for (I dx:{I{-1000},I{-1},I{0},I{1},I{1000}}) {
            combat::Engine manual,policy; output={I{1},dx,-dx,I{6000},I{1000},I{0}};
            assert(!manual.advance(5,manuals(output,output)));
            assert(!policy.advance(5,policies(),bindings(fixed,fixed)));
            assert(manual.native.hash()==policy.native.hash());
            for (std::size_t i=0;i<5;++i) {
                assert(manual.frames[i].inputs[0].action==policy.frames[i].inputs[0].action);
                assert(manual.frames[i].observation_hashes==policy.frames[i].observation_hashes);
            }
        }
    }
    // Late bad output / standard or nonstandard exceptions roll back the entire simulation/log batch.
    {
        for (int failure=0;failure<3;++failure) {
            clear();combat::Engine e; assert(!e.advance(2,manuals()));
            const auto before=e.replay_json(),state=e.json(0,true),native=e.native.json();
            fail_tick=e.native.state.tick+3;fail_kind=failure;
            const auto error=e.advance(8,policies(),bindings(recording,recording));
            assert(error && error->find("private-exception")==std::string::npos);
            assert(e.replay_json()==before && e.json(0,true)==state && e.native.json()==native);
        }
    }
    // Two fully filtered controllers seek contact around cover, hit, and end a real combat run.
    {
        combat::Engine e;
        assert(!combat::make_observation(e.native,0).enemy);
        while (!e.native.state.terminal) assert(!e.advance(120,policies(),bindings()));
        assert(e.native.state.tick<600);
        assert(e.native.state.robots[0].shots>0 && e.native.state.robots[1].shots>0);
        assert(e.native.state.robots[0].hits>0 && e.native.state.robots[1].hits>0);
        assert(e.native.state.robots[0].hp==0 || e.native.state.robots[1].hp==0);
        // Reconstruct resolved inputs only, with all historical policy callbacks absent.
        combat::Engine replay;
        for (std::size_t start=0;start<e.journal.size();start+=120) {
            const auto end=std::min(e.journal.size(),start+120);
            assert(!replay.replay_frames({e.journal.begin()+std::ptrdiff_t(start),e.journal.begin()+std::ptrdiff_t(end)}));
        }
        assert(replay.replay_json()==e.replay_json());
        const auto final=e.replay_json(); assert(e.advance(1,policies(),bindings())); assert(e.replay_json()==final);
        // Replay tampering must reject without committing even earlier valid frames in a batch.
        for (int mutation=0;mutation<4;++mutation) {
            combat::Engine rejected; auto batch=std::vector<combat::Frame>(e.journal.begin(),e.journal.begin()+10);
            if (mutation==0) ++batch.back().hash;
            if (mutation==1) ++batch.back().observation_hashes[1];
            if (mutation==2) batch.back().inputs[0].action[0]=1001;
            if (mutation==3) batch.back().tick=99;
            const auto before=rejected.replay_json();
            assert(rejected.replay_frames(batch)); assert(rejected.replay_json()==before);
        }
        std::cout << "policy-v-policy terminal tick=" << e.native.state.tick << " hash=" << e.native.hash() << '\n';
    }
    // Independent manual-v-policy dispatch, including the opposite controlled side.
    for (int policy_robot=0;policy_robot<2;++policy_robot) {
        combat::Engine e; auto requests=manuals(); requests[policy_robot].kind="policy";
        while (!e.native.state.terminal) assert(!e.advance(120,requests,bindings()));
        assert(e.native.state.tick<1200);
        assert(e.native.state.robots[1-policy_robot].hp==0);
        assert(e.native.state.robots[policy_robot].ammo<120);
    }
    std::cout << "filtered combat core tests passed\n";
}
