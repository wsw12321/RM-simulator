#include <array>
#include <optional>
#include <string>
#include <vector>
#include "objective.hpp"
#include <iostream>
#include <stdexcept>
void require(bool v,const char* m){if(!v)throw std::runtime_error(m);}
using Physical=std::array<std::array<bool,2>,4>;
// Artificial already-qualified timeline fixtures, NOT physical/official detector tests.
namespace objective {
struct AdapterTestProbe {
    static std::optional<std::string> atomic(Engine& e,long t,Physical p) {
        auto c=e;auto r=c.advance_rules(t,p);if(!r)e=std::move(c);return r;
    }
};
}
std::optional<std::string> atomic(objective::Engine& e,long t,Physical p) {
    return objective::AdapterTestProbe::atomic(e,t,p);
}
int main(){using namespace objective; int assertions=0;
 auto check=[&](bool x,const char* m){assertions++;require(x,m);};
 for(auto policy:{cap::BoundaryPolicy::StrictRejectUnadjudicated,cap::BoundaryPolicy::EngineeringTimerBeforeInputV1}){
  Engine e({policy,false});Physical p{};p[0][1]=true;check(!atomic(e,1,p),"enter");p[0][1]=false;check(!atomic(e,3001,p),"leave");auto before=e.json();auto err=atomic(e,5002,p);
  if(policy==cap::BoundaryPolicy::StrictRejectUnadjudicated){check(err&&err->find("UnadjudicatedCoincidence")!=std::string::npos,"strict expiry before sampled horizon cannot evade ambiguity");check(before==e.json(),"atomic strict error");}
  else{check(!err,"timer first succeeds");check(e.zones()[0].completed_at_ms==5001,"exact capture timer");check(!e.zones()[0].detectors[1].snapshot().logically_active,"no stale activation");}
 }
 {Engine e;Physical p{};p[0][1]=true;check(!atomic(e,1,p),"enter2");p[0][1]=false;check(!atomic(e,2001,p),"leave2");check(!atomic(e,4001,p),"expiry2");check(e.zones()[0].capture.snapshot().progress_ms==4000,"single grace contributes exactly2s");check(!atomic(e,5001,p),"decay");check(e.zones()[0].capture.snapshot().progress_ms==3000,"no double grace");}
 for(auto policy:{cap::BoundaryPolicy::StrictRejectUnadjudicated,cap::BoundaryPolicy::EngineeringTimerBeforeInputV1}){
  Engine e({policy,false});Physical p{};p[0]={true,true};check(!atomic(e,17,p),"contest");p[0]={false,false};check(!atomic(e,1017,p),"leave both");auto before=e.json();auto err=atomic(e,3034,p);check(err&&err->find("UnadjudicatedContestExitToNone")!=std::string::npos,"both leave ambiguity remains rejected");check(e.json()==before,"contest rollback");
 }
 {Engine e;Physical p{};p[0][1]=true;check(!atomic(e,17,p),"offframe enter");p[0][0]=true;check(!atomic(e,1000,p),"contest at1000");p[0][0]=false;check(!atomic(e,1034,p),"defender leaves");check(!atomic(e,3034,p),"contest grace ends");check(e.zones()[0].capture.snapshot().progress_ms==983,"pause retains983ms");check(!atomic(e,7050,p),"before off-frame deadline");check(e.zones()[0].capture.snapshot().progress_ms==4999,"one ms left");p[0][0]=true;check(!atomic(e,7067,p),"input after capture routes latch");check(e.zones()[0].completed_at_ms==7051,"capture at7051 not7067");check(e.zones()[0].detectors[0].snapshot().logically_active,"current detector state continues after capture");check(e.zones()[0].capture.snapshot().time_ms==7051,"completed capture freezes at endpoint");}
 {Engine batch({cap::BoundaryPolicy::StrictRejectUnadjudicated,true}),single({cap::BoundaryPolicy::StrictRejectUnadjudicated,true});check(!batch.advance(600,1,{}),"batch600");for(int i=0;i<600;i++)check(!single.advance(1,1,{}),"single tick");check(batch.json()==single.json(),"batch partition state");check(batch.replay_json()==single.replay_json(),"batch partition replay");while(!batch.world().terminal)check(!batch.advance(600,1,{}),"full run");check(batch.ledger().last_sequence()==420,"includes final credit420");check(batch.world().tick==25200,"exact world time");check(batch.world().robots[0].hp==100&&batch.world().robots[1].hp==100&&batch.world().bullets.empty(),"movement-only HP and bullets");}
 std::cout<<"objective white-box boundary checks passed: "<<assertions<<"\n";
}
