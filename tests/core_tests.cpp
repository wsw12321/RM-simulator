#include "sim.hpp"
#include <cstdlib>
#include <iostream>
using namespace sandbox;
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
std::array<Action,2> neutral(){return {{{0,0,10000,3800,0},{0,0,1000,3800,0}}};}
int main(){
 Engine a,b;check(a.hash()==b.hash(),"identical reset state");check(isqrt(0)==0&&isqrt(2)==1&&isqrt(144)==12,"integer square root");
 auto input=neutral();auto hash=a.hash();input[0].dx=1001;check(!a.step(input)&&a.hash()==hash&&a.journal.empty(),"invalid action is atomic");
 input=neutral();input[0].dx=1000;input[0].dy=1000;check(a.step(input),"valid frame accepted");auto r=a.state.robots[0];check((r.x-1600)*(r.x-1600)+(r.y-3800)*(r.y-3800)<=SPEED*SPEED,"diagonal speed limited");
 a.reset();input=neutral();input[0].dx=-1000;for(int i=0;i<300;i++)a.step(input);check(a.state.robots[0].x==RADIUS,"arena boundary");
 a.reset();a.state.robots[0].x=4900;input[0].dx=1000;for(int i=0;i<50;i++)a.step(input);check(a.state.robots[0].x<=5250-RADIUS,"static wall collision");
 a.reset();a.state.robots[0].x=2000;a.state.robots[1].x=2600;input=neutral();input[0].dx=1000;input[1].dx=-1000;a.step(input);check(a.state.robots[1].x-a.state.robots[0].x>=2*RADIUS,"symmetric robot contact");
 a.reset();input=neutral();input[0].aim_x=0;a.step(input);check(a.state.robots[0].yaw==252,"bounded gimbal slew");
 a.reset();a.state.robots[1].x=3200;input=neutral();input[0].fire=1;for(int i=0;i<15;i++)a.step(input);check(a.state.robots[1].hp<MAX_HP&&a.state.robots[0].shots==2,"projectile hit and cooldown");
 a.reset();input=neutral();input[0].fire=1;for(int i=0;i<150;i++)a.step(input);check(a.state.robots[1].hp==MAX_HP,"projectiles blocked by cover");
 a.reset();a.state.robots[0].x=2000;a.state.robots[1].x=3000;a.state.robots[0].hp=10;a.state.robots[1].hp=10;a.state.bullets={{1,0,2600,3800,600,0,10},{2,1,2400,3800,-600,0,10}};input=neutral();a.step(input);check(a.state.robots[0].hp==0&&a.state.robots[1].hp==0&&a.state.terminal,"simultaneous damage and swept hits");
 hash=a.hash();check(!a.step(input)&&a.hash()==hash,"terminal state frozen");
 a.reset();a.state.robots[0].ammo=0;input=neutral();input[0].fire=1;a.step(input);check(a.state.robots[0].shots==0,"empty ammo rejects firing");
 a.reset();b.reset();for(int i=0;i<3000&&!a.state.terminal;i++){auto before=a.state.robots;std::array<Action,2> actions{scripted_policy({a.state.tick,0,before}),scripted_policy({a.state.tick,1,before})};check(a.step(actions),"policy action accepted");check(b.step(actions)&&a.hash()==b.hash(),"replay per-tick hash identical");for(auto rr:a.state.robots)check(rr.hp>=0&&rr.hp<=MAX_HP&&rr.ammo>=0&&rr.ammo<=AMMO&&rr.x>=RADIUS&&rr.x<=WIDTH-RADIUS&&rr.y>=RADIUS&&rr.y<=HEIGHT-RADIUS,"state invariants");}
 a.reset();b.reset();for(int i=0;i<3000;i++){auto actions=neutral();actions[0].dx=(i/120)%2?1000:-1000;actions[0].dy=(i/240)%2?1000:-1000;check(a.step(actions)&&b.step(actions),"long trajectory accepted");check(a.hash()==b.hash(),"long trajectory replay hash");}
 std::cout<<"PASS "<<checks<<" checks; determinism fixture ticks="<<a.state.tick<<", terminal="<<a.state.terminal<<", hash="<<a.hash()<<'\n';
}
