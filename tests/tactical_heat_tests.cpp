#include "../tactical_mode/core.hpp"
#include <cassert>
#include <iostream>
#include <type_traits>
namespace tactical {
struct HeatTestProbe {
    static auto& heat(Engine& e,int side) {return e.heat_[side];}
    static auto blocked(const Engine& e,int side) {return e.blocked_fire_ticks_[side];}
    static auto damage(const Engine& e) {return e.damage_.totals();}
};
}
namespace t=tactical;
namespace h=t::heat;
using I=t::I;
auto requests(int fire=1) {return std::array<t::Request,2>{{{"manual",{I{1},I{0},I{0},I{1000},I{1000},I{fire}}},{"manual",{I{1},I{0},I{0},I{11000},I{1000},I{0}}}}};}
void step(t::Engine& e,int ticks,int fire=1){assert(!e.advance(ticks,requests(fire)));}
int main() {
    static_assert(std::is_const_v<decltype(std::declval<t::policy::Observation>().self_heat)>);
    static_assert(t::policy::kSdkVersion==1&&t::policy::kObservationVersion==2);
    {t::Engine e;step(e,41);const auto& q=t::HeatTestProbe::heat(e,0).snapshot();
     assert(q.detected_shots==5&&q.heat_tenths==428&&q.overheat_locked);
     assert(e.native.state.robots[0].ammo==115&&e.native.state.robots[0].hp==100);
     const auto d=t::HeatTestProbe::damage(e);assert((d.attack_damage==std::array<std::int64_t,2>{}));
     step(e,19);assert(e.native.state.robots[0].shots==5&&t::HeatTestProbe::blocked(e,0)==19);
     for(const auto& f:e.journal)assert(f.inputs[0].action[4]==1);
     step(e,192);assert(t::HeatTestProbe::heat(e,0).snapshot().heat_tenths==100);
     assert(e.native.state.robots[0].shots==6); //100ms cooling unlocks before same-tick native gate.
     t::Engine replay;assert(!replay.replay_frames(e.journal));assert(replay.replay_json()==e.replay_json());}
    // Pure heat does not move any damage totals or HP; no enemy heat leaks.
    {t::Engine a,b;auto r=t::HeatTestProbe::heat(b,1).apply({1,5});assert(std::holds_alternative<h::Snapshot>(r));
     assert(a.hash()!=b.hash());assert(t::observation_json(a.observation(0))==t::observation_json(b.observation(0)));
     assert(t::observation_json(a.observation(1))!=t::observation_json(b.observation(1)));
     const auto text=t::observation_json(b.observation(1));for(auto word:{"detected_shots","blocked_fire_ticks","heat_admin","damage_admin","video_locked"})assert(text.find(word)==std::string::npos);
     assert(t::HeatTestProbe::damage(a)==t::HeatTestProbe::damage(b));}
    // Native cooldown/no ammo creates no detected shot or thermal accumulation.
    {t::Engine e;e.native.state.robots[0].ammo=0;step(e,60);assert(t::HeatTestProbe::heat(e,0).snapshot().detected_shots==0);assert(e.observation(0).self_heat.heat_tenths==0);}
    {t::Config c;c.heat_strategy=t::HeatStrategy::Disabled;t::Engine e(c);step(e,60);
     assert(e.native.state.robots[0].shots==6&&!e.observation(0).self_heat.enabled);
     assert(e.observation(0).self_heat.heat_tenths==0&&t::HeatTestProbe::blocked(e,0)==0);}
    // The native firing gate prevents an already locked fixture from reachingQ2.
    {t::Engine e;h::Config c;c.limit_tenths=0;c.cooling_per_second=0;c.boundary=h::BoundaryPolicy::EngineeringCoolingBeforeShotV1;
     auto injected=std::get<h::Heat>(h::Heat::start(c));assert(std::holds_alternative<h::Snapshot>(injected.apply({0,9})));
     // This fixture is already temp-locked. Cannot emit atQ2 in normal gated play.
     t::HeatTestProbe::heat(e,0)=injected;const auto before=e.native.state.robots[0].shots;step(e,60);assert(e.native.state.robots[0].shots==before);}
    // Externally injected permanent heat fact remains after cooling/death reset.
    {t::Config c;c.heat_threshold=h::ThresholdPolicy::EngineeringFlowchartGreaterEqualV1;t::Engine e(c);
     auto& q=t::HeatTestProbe::heat(e,0);assert(std::holds_alternative<h::Snapshot>(q.apply({0,15})));
     const auto hp=e.native.state.robots[0].hp;e.native.state.bullets.push_back({99,1,1600,3800,0,0,2});
     step(e,1);assert(e.native.state.robots[0].hp==hp-10);
     // q reference was invalidated by transactional assignment; read fresh.
     assert(t::HeatTestProbe::heat(e,0).snapshot().permanent_latched);
     e.native.state.bullets.clear();for(int i=0;i<9;++i)e.native.state.bullets.push_back({100+i,1,1600,3800,0,0,2});
     step(e,1);const auto state=t::HeatTestProbe::heat(e,0).snapshot();assert(state.heat_tenths==0&&!state.overheat_locked&&state.permanent_latched);
     assert(e.native.state.robots[0].hp==0);step(e,60);assert(e.native.state.robots[0].shots==0);}
    std::cout<<"tactical heat tests passed\n";
}
