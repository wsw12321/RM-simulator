// Independent source-boundary and integration review. These intentionally injected
// permanent/ambiguity fixtures are not naturally reachable gameplay claims.
#include "../rules/rmuc2027_heat_v1/heat_rules.hpp"
#include "../tactical_mode/core.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <string>
#include <variant>

namespace h=rmuc2027::heat_v1;
namespace t=tactical;
using I=t::I;
namespace tactical {
struct HeatReviewTestProbe {
    static auto& heat(Engine& e,int side) { return e.heat_.at(side); }
    static auto blocked(const Engine& e) { return e.blocked_fire_ticks_; }
    static auto damage(const Engine& e) { return e.damage_.totals(); }
    static auto damage_sequence(const Engine& e) { return e.damage_.last_sequence(); }
};
}
using Probe=t::HeatReviewTestProbe;
h::Heat start(h::Config config={}) {
    auto result=h::Heat::start(config);
    assert(std::holds_alternative<h::Heat>(result));
    return std::get<h::Heat>(result);
}
h::Snapshot accept(h::Heat& heat,h::Event event) {
    auto result=heat.apply(event);
    assert(std::holds_alternative<h::Snapshot>(result));
    return std::get<h::Snapshot>(result);
}
void rejects(h::Heat& heat,h::Event event,h::Error expected) {
    const auto before=heat.snapshot();
    auto result=heat.apply(event);
    assert(std::holds_alternative<h::Error>(result));
    assert(std::get<h::Error>(result)==expected);
    assert(heat.snapshot()==before);
}
auto action(I fire=0,I dx=0,I dy=0) { return t::policy::Action{I{1},dx,dy,I{6000},I{3800},fire}; }
auto manual(I fire=0,I dx=0,I dy=0) {
    return std::array<t::Request,2>{{{"manual",action(fire,dx,dy)},{"manual",action()}}};
}
void ticks(t::Engine& e,int count,I fire=0) {
    while(count) { int n=std::min(count,600); assert(!e.advance(n,manual(fire))); count-=n; }
}
void incoming(t::Engine& e,int victim,int count) {
    const auto robot=e.native.state.robots[victim];
    for(int i=0;i<count;++i)
        e.native.state.bullets.push_back({e.native.state.next_id++,1-victim,robot.x,robot.y,0,0,2});
}

int main() {
    // Four verified detections at distinct sub-100ms timestamps give exact Q0.
    // A subsequent detected round must still count even if a hardware gate failed.
    {
        auto heat=start();
        for(int i=1;i<=4;++i) {
            auto state=accept(heat,{i,1,false});
            assert(state.heat_tenths==100*i && !state.firing_locked());
        }
        auto state=accept(heat,{5,1,false});
        assert(state.heat_tenths==500 && state.overheat_locked && !state.permanent_latched);
        state=accept(heat,{900,0,false});
        assert(state.heat_tenths==392 && state.overheat_locked);
        state=accept(heat,{4100,0,false});
        assert(state.heat_tenths==8 && state.overheat_locked);
        state=accept(heat,{4200,0,false});
        assert(state.heat_tenths==0 && !state.firing_locked() && state.detected_shots==5);
    }
    // Reaching Q0 while already locked does not undo the zero-only latch.
    {
        h::Config config; config.cooling_per_second=10;
        auto heat=start(config); accept(heat,{1,5,false});
        auto state=accept(heat,{1000,0,false});
        assert(state.heat_tenths==400 && state.overheat_locked);
    }
    // Q2 equality is ambiguous, with no partial clock/shot/lock commit by default.
    {
        auto heat=start(); accept(heat,{1,13,false});
        rejects(heat,{2,1,false},h::Error::UnadjudicatedThresholdEquality);
        rejects(heat,{2,2,false},h::Error::UnadjudicatedThresholdEquality);
        // Cooling changes the residue: the next two distinct detections pass
        // through1388 then1488, not the unresolved exact1400 boundary.
        accept(heat,{100,0,false});
        auto state=accept(heat,{101,2,false});
        assert(state.heat_tenths==1488 && state.permanent_latched && state.firing_locked());
        state=accept(heat,{100000,0,false});
        assert(state.heat_tenths==0 && !state.overheat_locked && state.permanent_latched && state.firing_locked());
        state=accept(heat,{100001,0,true});
        assert(state.heat_tenths==0 && state.permanent_latched && state.firing_locked());
    }
    // Both explicitly nonofficial equality policies are deterministic; prose-
    // greater uses a temporary-lock fallback at equality, not a verified rule.
    for(auto policy:{h::ThresholdPolicy::EngineeringProseStrictGreaterV1,
                     h::ThresholdPolicy::EngineeringFlowchartGreaterEqualV1}) {
        h::Config config; config.threshold=policy;
        auto heat=start(config); auto state=accept(heat,{1,14,false});
        const bool permanent=policy==h::ThresholdPolicy::EngineeringFlowchartGreaterEqualV1;
        assert(state.heat_tenths==1400 && state.firing_locked());
        assert(state.permanent_latched==permanent);
        state=accept(heat,{100000,0,false});
        assert(state.heat_tenths==0 && state.firing_locked()==permanent);
    }
    // A detected-shot batch must retain the same threshold/latch transitions as
    // individually detected shots, even when its final heat is already permanent.
    for(auto policy:{h::ThresholdPolicy::EngineeringProseStrictGreaterV1,
                     h::ThresholdPolicy::EngineeringFlowchartGreaterEqualV1}) {
        for(int count=1;count<=25;++count) {
            h::Config config; config.threshold=policy;
            auto batch=start(config),single=start(config);
            auto batched=accept(batch,{1,count,false});
            for(int i=1;i<=count;++i) accept(single,{i,1,false});
            const auto sequential=single.snapshot();
            assert(batched.heat_tenths==sequential.heat_tenths);
            assert(batched.overheat_locked==sequential.overheat_locked);
            assert(batched.permanent_latched==sequential.permanent_latched);
            assert(batched.detected_shots==sequential.detected_shots);
        }
    }
    // Same final heat, different lock history: this ambiguity is not cosmetic.
    {
        auto strict=start(); accept(strict,{1,4,false}); accept(strict,{800,0,false});
        assert(strict.snapshot().heat_tenths==304);
        rejects(strict,{900,1,false},h::Error::UnadjudicatedShotCoolingCoincidence);
        for(auto policy:{h::BoundaryPolicy::EngineeringCoolingBeforeShotV1,
                         h::BoundaryPolicy::EngineeringShotBeforeCoolingV1}) {
            h::Config config; config.boundary=policy;
            auto heat=start(config); accept(heat,{1,4,false}); accept(heat,{800,0,false});
            auto state=accept(heat,{900,1,false});
            assert(state.heat_tenths==392);
            assert(state.overheat_locked==(policy==h::BoundaryPolicy::EngineeringShotBeforeCoolingV1));
        }
        accept(strict,{900,0,false});
        rejects(strict,{900,1,false},h::Error::NonIncreasingTime);
    }
    // Death resets heat immediately, independently of cooling and permanent latch.
    {
        auto heat=start(); accept(heat,{1,5,false});
        rejects(heat,{2,1,true},h::Error::UnadjudicatedShotDeathCoincidence);
        auto state=accept(heat,{2,0,true});
        assert(state.heat_tenths==0 && !state.overheat_locked && !state.permanent_latched);
        h::Config config; config.death=h::DeathPolicy::EngineeringShotBeforeDeathResetV1;
        config.threshold=h::ThresholdPolicy::EngineeringFlowchartGreaterEqualV1;
        auto ordered=start(config); state=accept(ordered,{1,15,true});
        assert(state.heat_tenths==0 && state.detected_shots==15 && state.permanent_latched && state.firing_locked());
    }
    // Validate numeric boundaries and arithmetic overflow without partial writes.
    {
        const auto max=std::numeric_limits<std::int64_t>::max();
        for(auto config:{h::Config{-1,12},h::Config{400,-1},h::Config{max,12}})
            assert(std::holds_alternative<h::Error>(h::Heat::start(config)));
        auto heat=start();
        rejects(heat,{-1,0,false},h::Error::NegativeValue);
        rejects(heat,{0,-1,false},h::Error::NegativeValue);
        rejects(heat,{0,max,false},h::Error::IntegerOverflow);
        accept(heat,{0,1,false});
        rejects(heat,{0,1,false},h::Error::NonIncreasingTime);
        auto state=accept(heat,{max,0,false});
        assert(state.heat_tenths==0);
        h::Config huge; huge.limit_tenths=max-1000; huge.cooling_per_second=0;
        auto overflow=start(huge); accept(overflow,{0,max/100,false});
        rejects(overflow,{1,1,false},h::Error::IntegerOverflow);
        h::Config invalid; invalid.threshold=static_cast<h::ThresholdPolicy>(255);
        assert(std::holds_alternative<h::Error>(h::Heat::start(invalid)));
        invalid={}; invalid.boundary=static_cast<h::BoundaryPolicy>(255);
        assert(std::holds_alternative<h::Error>(h::Heat::start(invalid)));
        invalid={}; invalid.death=static_cast<h::DeathPolicy>(255);
        assert(std::holds_alternative<h::Error>(h::Heat::start(invalid)));
    }
    // Fixed cooling arithmetic for every cooling-priority infantry level. The
    // choice of level1 in this HP100 sandbox remains an explicit heat-only fixture.
    for(auto rate:{12,14,16,18,20,22,24,26,28,30}) {
        h::Config config; config.limit_tenths=4000; config.cooling_per_second=rate;
        auto heat=start(config); accept(heat,{1,20,false});
        auto state=accept(heat,{1000,0,false});
        assert(state.heat_tenths==2000-rate*10);
    }
    // Ordinary action-only use: attempts during cooldown are not detections;
    // temporary gate rejects emission but preserves original fire input in replay.
    {
        t::Engine engine; ticks(engine,41,1);
        assert(engine.native.state.robots[0].shots==5 && engine.native.state.robots[0].ammo==115);
        assert(Probe::heat(engine,0).snapshot().heat_tenths==428);
        assert(Probe::heat(engine,0).snapshot().overheat_locked);
        const auto damage=Probe::damage(engine); const auto ammo=engine.native.state.robots[0].ammo;
        ticks(engine,20,1);
        assert(engine.native.state.robots[0].ammo==ammo && engine.native.state.robots[0].shots==5);
        assert(Probe::heat(engine,0).snapshot().heat_tenths==380);
        assert(Probe::heat(engine,0).snapshot().overheat_locked);
        assert(Probe::damage(engine)==damage && engine.native.state.robots[0].hp==100);
        assert(Probe::blocked(engine)[0]>=20);
        for(const auto& frame:engine.journal) assert(frame.inputs[0].action[4]==1);
        auto replay=t::Engine{}; assert(!replay.replay_frames(engine.journal));
        assert(replay.replay_json()==engine.replay_json());
        ticks(engine,191);
        assert(engine.native.state.tick==252 && Probe::heat(engine,0).snapshot().heat_tenths==0);
        assert(!Probe::heat(engine,0).snapshot().firing_locked());
        ticks(engine,1,1); assert(engine.native.state.robots[0].shots==6);
    }
    // Hidden opponent heat/lock changes must alter authoritative state identity,
    // never the other observer's observation or private damage totals.
    {
        t::Engine plain,changed;
        h::Config config; config.threshold=h::ThresholdPolicy::EngineeringFlowchartGreaterEqualV1;
        Probe::heat(changed,1)=start(config);
        accept(Probe::heat(changed,1),{0,15,false});
        assert(plain.hash()!=changed.hash());
        assert(t::observation_json(plain.observation(0))==t::observation_json(changed.observation(0)));
        assert(t::observation_json(plain.observation(1))!=t::observation_json(changed.observation(1)));
        assert(Probe::damage(plain)==Probe::damage(changed));
    }
    // Heat clearing cannot revive a dead robot, refill ammo, or clear a surviving
    // native cooldown. These are independently owned firing prerequisites.
    {
        t::Engine dead; ticks(dead,41,1); incoming(dead,0,10); ticks(dead,1,1);
        assert(dead.native.state.robots[0].hp==0 && Probe::heat(dead,0).snapshot().heat_tenths==0);
        const auto ammo=dead.native.state.robots[0].ammo; ticks(dead,120,1);
        assert(dead.native.state.robots[0].ammo==ammo && dead.native.state.robots[0].shots==5);
        t::Engine empty; empty.native.state.robots[0].ammo=0; ticks(empty,30,1);
        assert(empty.native.state.robots[0].shots==0 && Probe::heat(empty,0).snapshot().heat_tenths==0);
        t::Engine cooldown; ticks(cooldown,1,1); ticks(cooldown,1,1);
        assert(cooldown.native.state.robots[0].shots==1 && cooldown.native.state.robots[0].cooldown==9);
    }
    // Deliberate private strict-boundary injection makes the second candidate
    // tick reject after movement/emission. The first candidate tick already
    // settled projectile HP loss into the damage ledger, so this checks whole-
    // batch rollback, not merely early rejection before any damage accounting.
    {
        t::Engine engine; ticks(engine,4);
        engine.native.state.robots[0].cooldown=2;
        auto strict=start(); accept(strict,{t::Engine::time_ms(4),0,false});
        Probe::heat(engine,0)=strict;
        incoming(engine,1,1);
        const auto state=engine.json(0,true),native=engine.native.json(),replay=engine.replay_json();
        const auto hash=engine.hash(); const auto damage=Probe::damage(engine);
        const auto sequence=Probe::damage_sequence(engine); const auto blocked=Probe::blocked(engine);
        const auto heat=Probe::heat(engine,0).snapshot();
        auto error=engine.advance(2,manual(1,1000));
        assert(error && error->find("UnadjudicatedShotCoolingCoincidence")!=std::string::npos);
        assert(engine.json(0,true)==state && engine.native.json()==native && engine.replay_json()==replay);
        assert(engine.hash()==hash && Probe::damage(engine)==damage && Probe::damage_sequence(engine)==sequence);
        assert(Probe::blocked(engine)==blocked && Probe::heat(engine,0).snapshot()==heat);
    }
    std::cout << "independent 17mm heat review tests passed\n";
}
