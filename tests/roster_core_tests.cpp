#include "../roster_mode/core.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace roster {
struct TestProbe {
    static auto thermal(const Engine& e,std::size_t i){return e.heat_[i].snapshot();}
    static auto damage(const Engine& e){return e.damage_.totals();}
    static auto capture(const Engine& e,int z){return e.zones_[z].capture.snapshot();}
};
}
namespace {
using namespace roster;
int calls_a=0,calls_b=0;
policy::Action first_policy(const policy::Observation& o){
    ++calls_a;
    assert(o.self_id=="red-1" && o.tick==0 && o.self.x==1600);
    return {I{1},I{1000},I{0},I{12000},I{2900},I{1}};
}
policy::Action second_policy(const policy::Observation& o){
    ++calls_b;
    assert(o.self_id=="red-2" && o.tick==0 && o.self.x==1600);
    const auto contact=std::find_if(o.contacts.begin(),o.contacts.end(),[](const auto& c){return c.id=="red-1";});
    assert(contact!=o.contacts.end() && contact->x==1600);
    return {I{1},I{-1000},I{0},I{0},I{4700},I{0}};
}
policy::Action throw_later(const policy::Observation& o){
    if(o.tick==3)throw std::runtime_error("late fault");
    return {I{1},I{1000},I{0},I{12000},I{2900},I{1}};
}
std::vector<Request> requests(const Engine& e){std::vector<Request> r;for(const auto& entry:e.config().robots)r.push_back({entry.id,"manual",{}});return r;}
void must_fail_reset(Engine& e,Config c){const auto before=e.replay_json();bool failed=false;try{e.reset(c);}catch(...){failed=true;}assert(failed&&before==e.replay_json());}
void identity_and_validation(){
    Engine e;assert(e.state.robots.size()==4);assert(e.config().robots[0].id=="blue-1");
    auto c=e.config();std::reverse(c.robots.begin(),c.robots.end());Engine reordered(c);assert(e.hash()==reordered.hash());
    auto r=requests(e);r[2].action.dx=I{1000};r[3].action.dy=I{1000};auto reverse=r;std::reverse(reverse.begin(),reverse.end());
    assert(!e.advance(1,r));assert(!reordered.advance(1,reverse));assert(e.hash()==reordered.hash());assert(e.replay_json()==reordered.replay_json());
    for(int variant=0;variant<8;++variant){auto bad=r;if(variant==0)bad.pop_back();if(variant==1)bad[1].id=bad[0].id;if(variant==2)bad[0].id="absent";if(variant==3)bad[0].action.dx=1.0;if(variant==4)bad[0].action.fire=true;if(variant==5)bad[0].action.sdk_version=I{2};if(variant==6)bad[0].action.aim_x=I{12001};if(variant==7)bad[0].kind="unknown";const auto before=e.replay_json();assert(e.advance(2,bad));assert(e.replay_json()==before);}
    c=e.config();c.robots[0].profile.max_hp=std::numeric_limits<I>::max();must_fail_reset(e,c);
    c=e.config();c.robots[0].spawn.x=6000;c.robots[0].spawn.y=3800;must_fail_reset(e,c);
    c=e.config();c.robots[1].spawn=c.robots[0].spawn;must_fail_reset(e,c);
    c=e.config();c.robots[0].profile.id="official-infantry";must_fail_reset(e,c);
    c=e.config();c.robots[0].profile.max_hp+=1;must_fail_reset(e,c); // conflicting reused profile ID
    c=e.config();c.robots[0].id="bad\"id";must_fail_reset(e,c);
    c=e.config();c.robots[0].team=2;must_fail_reset(e,c);
    assert(validate_descriptor({1,"9invalid"}));assert(!validate_descriptor({1,"valid-1"}));
}
void controllers_and_rollback(){
    Engine e;auto r=requests(e);r[2].kind="policy";r[3].kind="policy";std::reverse(r.begin(),r.end());
    assert(!e.advance(1,r,{{"red-2",{1,"second"},second_policy},{"red-1",{1,"first"},first_policy}}));
    assert(calls_a==1&&calls_b==1);assert(e.state.robots[e.index("red-1")].x==1655);assert(e.state.robots[e.index("red-2")].x==1535);
    assert(e.frames[0].inputs[2].controller_id=="first");assert(e.frames[0].inputs[3].controller_id=="second");
    Engine bad;r=requests(bad);r[2].kind="policy";const auto before=bad.replay_json();
    assert(bad.advance(5,r,{{"red-1",{1,"throw-later"},throw_later}}));assert(before==bad.replay_json());
    assert(bad.advance(1,r,{{"blue-1",{1,"wrong-id"},throw_later}}));assert(before==bad.replay_json());
    assert(bad.advance(1,r,{{"red-1",{1,"one"},throw_later},{"red-1",{1,"two"},throw_later}}));assert(before==bad.replay_json());
    Engine replay;auto f=e.frames[0];std::reverse(f.inputs.begin(),f.inputs.end());assert(!replay.replay_frame(f));assert(replay.hash()==e.hash());assert(calls_a==1&&calls_b==1);
    Engine fresh;f=e.frames[0];f.inputs[2].observation_hash^=1;assert(fresh.replay_frame(f));assert(fresh.state.tick==0);f=e.frames[0];f.hash^=1;assert(fresh.replay_frame(f));assert(fresh.state.tick==0);
}
void configurable_ownership_and_heat(){
    Config c;c.heat_enabled=true;c.robots[0].spawn={8000,900,128};c.robots[1].spawn={8000,2200,128};c.robots[2].spawn={1000,900,0};c.robots[3].spawn={1000,2200,0};
    Engine e(c);auto r=requests(e);const auto red=e.index("red-1"),blue=e.index("blue-1");
    r[red].action={I{1},I{0},I{0},I{12000},I{900},I{1}};
    assert(!e.advance(1,r));assert(e.state.robots[red].ammo==119);assert(e.state.robots[red].shots==1);assert(TestProbe::thermal(e,red).detected_shots==1);assert(TestProbe::thermal(e,blue).detected_shots==0);assert(e.state.robots[blue].hp==100);
    assert(!e.advance(40,r));assert(e.state.robots[red].shots==5);assert(e.state.robots[blue].hp<100); // settled opposing loss after distant travel
    const auto before=e.observation("red-1");e.state.robots[blue].hp=7;e.state.robots[blue].ammo=1;e.state.robots[blue].cooldown=99;
    assert(observation_json(before)==observation_json(e.observation("red-1")));
    // Own stats differ by the selected profile; other private stats stay absent.
    const auto scout=e.observation("red-2");assert(scout.self.hp==80&&scout.self.ammo==90&&scout.sensor_range_mm==5500);
}
void full_example_and_replay(){
    Engine e;auto r=requests(e);std::vector<Controller> bindings;for(auto& request:r){request.kind="policy";bindings.push_back({request.id,policy::descriptor(),policy::decide});}
    for(int batch=0;batch<42;++batch)assert(!e.advance(600,r,bindings));
    assert(e.state.tick==25200&&e.state.terminal==1);assert(e.journal.size()==25200);
    int shots=0,captures=0;for(const auto& robot:e.state.robots)shots+=robot.shots;for(int z=0;z<4;++z)captures+=TestProbe::capture(e,z).completed;
    assert(shots>0);assert(captures>=2);
    Engine replay;for(std::size_t start=0;start<e.journal.size();start+=600){std::vector<Frame> batch(e.journal.begin()+start,e.journal.begin()+start+600);assert(!replay.replay_frames(batch));}assert(replay.hash()==e.hash());assert(replay.replay_json()==e.replay_json());
    assert(e.advance(1,r,bindings));assert(e.json("red-1").find("\"outcome\":\"ongoing\"")==std::string::npos);
    std::cout<<"Default example: "<<captures<<" captures, "<<shots<<" emitted shots, full420s replay verified.\n";
}
}
int main(){identity_and_validation();controllers_and_rollback();configurable_ownership_and_heat();full_example_and_replay();std::cout<<"roster core tests passed\n";}
