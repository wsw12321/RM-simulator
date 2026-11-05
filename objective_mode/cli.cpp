#include "objective.hpp"
#include <iostream>
#include <sstream>
namespace {
bool done(std::istringstream& in) {std::string extra;return !(in>>extra);}
void error(const std::string& s) {std::cout<<"{\"error\":\""<<s<<"\"}"<<std::endl;}
}
int main() {
    objective::Engine engine;
    std::string line;
    while(std::getline(std::cin,line)) {
        std::istringstream in(line);std::string command;in>>command;
        if(command=="RESET") {
            int boundary,credits;
            if(!(in>>boundary>>credits)||!done(in)||boundary<0||boundary>1||credits<0||credits>1) {error("Invalid objective configuration.");continue;}
            engine=objective::Engine({static_cast<objective::cap::BoundaryPolicy>(boundary),credits==1});
            std::cout<<engine.json()<<std::endl;
        } else if((command=="STATE"||command=="EXPORT"||command=="META")&&done(in)) {
            std::cout<<(command=="STATE"?engine.json():command=="EXPORT"?engine.replay_json():objective::meta_json())<<std::endl;
        } else if(command=="ADVANCE") {
            int count,mode;sandbox::Action a{};
            if(!(in>>count>>mode>>a.dx>>a.dy>>a.aim_x>>a.aim_y>>a.fire)||!done(in)){error("Invalid objective request.");continue;}
            if(auto e=engine.advance(count,mode,a))error(*e);else std::cout<<engine.json()<<std::endl;
        } else if(command=="FRAME") {
            int tick;std::array<sandbox::Action,2> actions{};bool ok=bool(in>>tick);
            for(auto& a:actions)ok=ok&&bool(in>>a.dx>>a.dy>>a.aim_x>>a.aim_y>>a.fire);
            if(!ok||!done(in)){error("Invalid objective frame.");continue;}
            if(auto e=engine.frame(tick,actions))error(*e);else std::cout<<engine.json()<<std::endl;
        } else error("Unknown objective command or trailing data.");
    }
}
