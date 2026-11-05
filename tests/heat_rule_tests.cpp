#include "../rules/rmuc2027_heat_v1/heat_rules.hpp"
#include <cassert>
#include <iostream>
#include <limits>
namespace h=rmuc2027::heat_v1;
h::Heat make(h::Config c={}) {return std::get<h::Heat>(h::Heat::start(c));}
h::Snapshot run(h::Heat& q,h::Event e) {auto r=q.apply(e);if(auto error=std::get_if<h::Error>(&r)) std::cerr<<e.time_ms<<":"<<e.detected_shots<<":"<<h::error_name(*error)<<"\n";assert(std::holds_alternative<h::Snapshot>(r));return std::get<h::Snapshot>(r);}
int main() {
    {auto q=make();assert(run(q,{0,1}).heat_tenths==100);assert(run(q,{99}).heat_tenths==100);
     assert(run(q,{100}).heat_tenths==88);assert(run(q,{200}).heat_tenths==76);
     assert(run(q,{1000}).heat_tenths==0);assert(!q.snapshot().firing_locked());}
    {auto q=make();assert(run(q,{1,4}).heat_tenths==400);assert(!q.snapshot().firing_locked());
     assert(run(q,{2,1}).overheat_locked);run(q,{1000});assert(q.snapshot().heat_tenths==380&&q.snapshot().firing_locked());
     run(q,{4100});assert(q.snapshot().firing_locked());run(q,{4200});assert(!q.snapshot().firing_locked());}
    for(auto threshold:{h::ThresholdPolicy::EngineeringProseStrictGreaterV1,h::ThresholdPolicy::EngineeringFlowchartGreaterEqualV1}) {
        h::Config c;c.threshold=threshold;c.cooling_per_second=0;auto q=make(c);
        run(q,{1,14});assert(q.snapshot().overheat_locked);
        assert(q.snapshot().permanent_latched==(threshold==h::ThresholdPolicy::EngineeringFlowchartGreaterEqualV1));
        run(q,{2,1});assert(q.snapshot().permanent_latched);run(q,{3,0,true});
        assert(q.snapshot().heat_tenths==0&&!q.snapshot().overheat_locked&&q.snapshot().permanent_latched&&q.snapshot().firing_locked());
    }
    {h::Config c;c.cooling_per_second=0;auto q=make(c);run(q,{1,13});auto before=q.snapshot();
     assert(std::get<h::Error>(q.apply({2,2}))==h::Error::UnadjudicatedThresholdEquality);assert(q.snapshot()==before);
     assert(std::get<h::Error>(q.apply({100,1}))==h::Error::UnadjudicatedShotCoolingCoincidence);assert(q.snapshot()==before);
     assert(std::get<h::Error>(q.apply({2,1,true}))==h::Error::UnadjudicatedShotDeathCoincidence);assert(q.snapshot()==before);}
    for(auto order:{h::BoundaryPolicy::EngineeringCoolingBeforeShotV1,h::BoundaryPolicy::EngineeringShotBeforeCoolingV1}) {
        h::Config c;c.boundary=order;auto q=make(c);run(q,{1,4});run(q,{800});run(q,{900,1});
        assert(q.snapshot().heat_tenths==392);
        assert(q.snapshot().overheat_locked==(order==h::BoundaryPolicy::EngineeringShotBeforeCoolingV1));
    }
    {auto q=make();run(q,{1,2});const auto before=q.snapshot();
     for(auto event:{h::Event{-1},h::Event{1},h::Event{0},h::Event{2,-1},h::Event{2,std::numeric_limits<std::int64_t>::max()}}) {
       assert(std::holds_alternative<h::Error>(q.apply(event)));assert(q.snapshot()==before);}
     run(q,{std::numeric_limits<std::int64_t>::max()});assert(q.snapshot().heat_tenths==0);}
    {h::Config c;c.limit_tenths=std::numeric_limits<std::int64_t>::max();assert(std::holds_alternative<h::Error>(h::Heat::start(c)));
     c={};c.cooling_per_second=-1;assert(std::holds_alternative<h::Error>(h::Heat::start(c)));
     c={};c.threshold=static_cast<h::ThresholdPolicy>(99);assert(std::holds_alternative<h::Error>(h::Heat::start(c)));}
    // Batching never erases intermediate threshold consequences.
    for(auto policy:{h::ThresholdPolicy::EngineeringProseStrictGreaterV1,h::ThresholdPolicy::EngineeringFlowchartGreaterEqualV1})
      for(int n=1;n<=100;++n) {
        h::Config c;c.cooling_per_second=0;c.threshold=policy;c.boundary=h::BoundaryPolicy::EngineeringCoolingBeforeShotV1;auto batch=make(c),single=make(c);
        run(batch,{1,n});for(int i=0;i<n;++i)run(single,{i+1,1});
        assert(batch.snapshot().heat_tenths==single.snapshot().heat_tenths);
        assert(batch.snapshot().overheat_locked==single.snapshot().overheat_locked);
        assert(batch.snapshot().permanent_latched==single.snapshot().permanent_latched);
      }
    std::cout<<"heat rule tests passed\n";
}
