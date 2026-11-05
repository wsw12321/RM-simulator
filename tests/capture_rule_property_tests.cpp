#include "capture_rules.hpp"
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
using namespace rmuc2027::capture_v1;
int checks=0;
void ck(bool x, const char* what) { ++checks; if(!x) throw std::runtime_error(what); }
template<class T> T ok(Result<T> v) { if(auto e=std::get_if<Error>(&v)) throw std::runtime_error(std::string(error_name(*e))); return std::get<T>(v); }
template<class T> void err(Result<T> v, Error e) { ck(std::holds_alternative<Error>(v)&&std::get<Error>(v)==e,"wrong error"); }
int main(){
 // Fixed seed; tests partition equivalence, not a separate rules oracle.
 std::mt19937_64 rng(412);
 for(int run=0;run<1000;++run) {
  Team owner=(run%2)?Team::Red:Team::Blue;
  auto c=ok(initial_control(PositionKind::Outpost,owner));
  LogicalActivation initial{bool(rng()%2),bool(rng()%2)};
  auto one=ok(CaptureAttempt::start(0,c,initial));
  auto split=one;
  for(int event=0;event<100&&!one.snapshot().completed;++event) {
   const auto begin=one.snapshot().time_ms;
   const auto dt=std::int64_t(1+rng()%500);
   const auto end=begin+dt;
   // No-input interval must be exactly equivalent, including event traces.
   auto got=ok(one.apply(end));
   for(auto t=begin+1;t<end&&!split.snapshot().completed;t+=1+rng()%51) ok(split.apply(t));
   if(!split.snapshot().completed) ok(split.apply(end));
   ck(one.snapshot()==split.snapshot(),"capture partition state");
   ck(one.trace()==split.trace(),"capture partition trace");
   if(one.snapshot().completed) {ck(got.unconsumed_ms==end-one.snapshot().time_ms,"residual"); break;}
   LogicalActivation next{bool(rng()%2),bool(rng()%2)};
   auto a=one.apply(end+1,next); auto b=split.apply(end+1,next);
   ck(a.index()==b.index(),"event outcome");
   if(auto e=std::get_if<Error>(&a)) ck(*e==std::get<Error>(b),"event error");
   ck(one.snapshot()==split.snapshot(),"capture event state");
   ck(one.trace()==split.trace(),"capture event trace");
  }
 }
 for(int run=0;run<1000;++run) {
  auto one=ok(ActivationDelay::start(0,QualifiedDetection(run%2)));
  auto split=one;
  for(int event=0;event<100;++event) {
   const auto begin=one.snapshot().time_ms;
   const auto end=begin+1+rng()%3000;
   ok(one.apply(end));
   for(auto t=begin+1;t<static_cast<std::int64_t>(end);t+=1+rng()%101) ok(split.apply(t));
   ok(split.apply(end));
   ck(one.snapshot()==split.snapshot(),"delay partition state");
   ck(one.trace()==split.trace(),"delay partition trace");
   const auto next=one.snapshot().detection==QualifiedDetection::Present ? QualifiedDetection::Absent : QualifiedDetection::Present;
   auto a=one.apply(end+1,next),b=split.apply(end+1,next);
   ck(a.index()==b.index(),"delay event outcome");
   if(auto e=std::get_if<Error>(&a)) ck(*e==std::get<Error>(b),"delay event error");
   ck(one.snapshot()==split.snapshot(),"delay event state");
   ck(one.trace()==split.trace(),"delay event trace");
  }
 }
 const auto m=std::numeric_limits<std::int64_t>::max();
 auto a=ok(ActivationDelay::start(m-2001,QualifiedDetection::Present));
 ok(a.apply(m-2000,QualifiedDetection::Absent)); ok(a.apply(m));
 ck(!a.snapshot().logically_active,"max grace");
 auto b=ok(ActivationDelay::start(m-2000,QualifiedDetection::Present));
 auto before=b.snapshot(); auto trace=b.trace(); err(b.apply(m-1999,QualifiedDetection::Absent),Error::IntegerOverflow);
 ck(b.snapshot()==before&&b.trace()==trace,"overflow atomic");
 auto c=ok(CaptureAttempt::start(m-6000,ok(initial_control(PositionKind::Base,Team::Red)),{false,true}));
 auto out=ok(c.apply(m)); ck(out.state.time_ms==m-1000&&out.unconsumed_ms==1000,"max capture");
 std::cout<<checks<<" capture property checks passed\n";
}
