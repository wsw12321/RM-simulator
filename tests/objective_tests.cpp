#include "objective.hpp"
#include <cassert>
#include <iostream>
using objective::Engine;
using objective::cap::BoundaryPolicy;
static int checks=0;
#define CHECK(x) do {++checks;assert(x);} while(false)
static sandbox::Action idle{};
static Engine entered(BoundaryPolicy policy=BoundaryPolicy::StrictRejectUnadjudicated) {
    Engine e({policy,true});
    while(!e.zones()[1].detectors[0].snapshot().logically_active)CHECK(!e.advance(1,1,idle));
    CHECK(e.world().tick==123);CHECK(Engine::time_ms(e.world().tick)==2050);
    CHECK(e.zones()[1].capture.snapshot().progress_ms==0);
    return e;
}
int main() {
    CHECK(Engine::time_ms(0)==0&&Engine::time_ms(1)==17&&Engine::time_ms(2)==34&&Engine::time_ms(3)==50);
    CHECK(Engine::detected({6100,5300,0},objective::ZONES[1]));
    CHECK(!Engine::detected({6099,5300,0},objective::ZONES[1]));
    auto dead=sandbox::Robot{6100,5300,0};dead.hp=0;CHECK(!Engine::detected(dead,objective::ZONES[1]));
    Engine e({BoundaryPolicy::StrictRejectUnadjudicated,true});
    auto before=e.json();CHECK(e.advance(1,0,{0,0,0,0,1}));CHECK(e.json()==before);
    CHECK(e.advance(601,0,idle));CHECK(e.advance(1,3,idle));CHECK(e.frame(1,{}));CHECK(e.json()==before);
    Engine batched=e,singles=e;
    CHECK(!batched.advance(12,1,idle));for(int i=0;i<12;i++)CHECK(!singles.advance(1,1,idle));CHECK(batched.hash()==singles.hash());CHECK(batched.replay_json()==singles.replay_json());
    auto progress=entered();
    CHECK(!progress.advance(299,0,idle));CHECK(progress.zones()[1].capture.snapshot().progress_ms==4984);
    CHECK(!progress.advance(1,0,idle));CHECK(progress.zones()[1].capture.snapshot().completed);CHECK(progress.zones()[1].completed_at_ms==7050);
    // Leave immediately: the exactly-once 2s grace contributes capture progress,
    // then absence decays it. No double grace and no raw occupancy-as-control.
    auto loss=entered();CHECK(!loss.advance(1,0,{0,-1000,0,0,0}));
    auto lost=Engine::time_ms(loss.world().tick);CHECK(lost==2067);
    CHECK(loss.zones()[1].detectors[0].snapshot().detection==objective::cap::QualifiedDetection::Absent);
    CHECK(loss.zones()[1].detectors[0].snapshot().deactivation_at_ms==lost+2000);
    CHECK(!loss.advance(119,0,idle));CHECK(loss.zones()[1].detectors[0].snapshot().logically_active);
    CHECK(!loss.advance(1,0,idle));CHECK(!loss.zones()[1].detectors[0].snapshot().logically_active);
    CHECK(loss.zones()[1].capture.snapshot().progress_ms==2017);
    CHECK(!loss.advance(121,0,idle));CHECK(loss.zones()[1].capture.snapshot().progress_ms==0);
    // Strict expiry/reacquisition is not silently timer-first; complete batch rolls back.
    auto reacquire=entered();CHECK(!reacquire.advance(1,0,{0,-1000,0,0,0}));CHECK(!reacquire.advance(119,0,idle));
    before=reacquire.json();auto replay_before=reacquire.replay_json();
    CHECK(reacquire.advance(1,0,{0,1000,0,0,0}));CHECK(reacquire.json()==before&&reacquire.replay_json()==replay_before);
    auto timer=entered(BoundaryPolicy::EngineeringTimerBeforeInputV1);CHECK(!timer.advance(1,0,{0,-1000,0,0,0}));CHECK(!timer.advance(119,0,idle));CHECK(!timer.advance(1,0,{0,1000,0,0,0}));CHECK(timer.zones()[1].detectors[0].snapshot().logically_active);
    // Leave after exactly3s, so grace expiry coincides with capture threshold.
    auto boundary=entered();CHECK(!boundary.advance(179,0,idle));CHECK(!boundary.advance(1,0,{0,-1000,0,0,0}));CHECK(!boundary.advance(118,0,idle));
    before=boundary.json();replay_before=boundary.replay_json();CHECK(boundary.advance(2,0,idle));CHECK(boundary.json()==before&&boundary.replay_json()==replay_before);
    auto ordered=entered(BoundaryPolicy::EngineeringTimerBeforeInputV1);CHECK(!ordered.advance(179,0,idle));CHECK(!ordered.advance(1,0,{0,-1000,0,0,0}));CHECK(!ordered.advance(120,0,idle));CHECK(ordered.zones()[1].capture.snapshot().completed);CHECK(!ordered.zones()[1].detectors[0].snapshot().logically_active);
    // Current detector telemetry evolves after the frozen first-capture snapshot.
    CHECK(!ordered.advance(300,0,idle));CHECK(ordered.zones()[1].capture.snapshot().control.degree==objective::cap::ControlDegree::Temporary);CHECK(ordered.zones()[1].completed_at_ms==7050);
    CHECK(!e.advance(600,1,idle));CHECK(!e.advance(600,1,idle));CHECK(e.ledger().totals()[0]==13&&e.ledger().totals()[1]==33);
    CHECK(e.zones()[1].capture.snapshot().completed&&e.zones()[2].capture.snapshot().completed);
    while(!e.world().terminal)CHECK(!e.advance(600,1,idle));
    CHECK(e.world().tick==25200&&e.ledger().last_sequence()==420);CHECK(e.ledger().totals()[0]==413&&e.ledger().totals()[1]==1233);
    CHECK(e.json().find("blue-wins")!=std::string::npos);before=e.json();CHECK(e.advance(1,1,idle));CHECK(e.json()==before);
    Engine disabled;while(!disabled.world().terminal)CHECK(!disabled.advance(600,0,idle));CHECK(disabled.ledger().last_sequence()==0);CHECK(disabled.json().find("unscored-timeout")!=std::string::npos);
    Engine reset;CHECK(reset.world().tick==0&&reset.ledger().last_sequence()==0&&!reset.zones()[1].capture.snapshot().completed);
    std::cout<<"objective native checks passed: "<<checks<<"\n";
}
