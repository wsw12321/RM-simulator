#include "sim.hpp"
#include <iostream>
#include <sstream>
int main() {
    sandbox::Engine engine;
    std::string line;
    while(std::getline(std::cin,line)) {
        std::istringstream in(line);
        std::string command;
        in>>command;
        if(command=="RESET") {
            engine.reset();
            std::cout<<engine.json()<<std::endl;
        }
        else if(command=="STATE")std::cout<<engine.json()<<std::endl;
        else if(command=="EXPORT")std::cout<<engine.replay_json()<<std::endl;
        else if(command=="ADVANCE") {
            int count,mode;
            sandbox::Action human {};
            if(!(in>>count>>mode>>human.dx>>human.dy>>human.aim_x>>human.aim_y>>human.fire)||count<1||count>12||mode<0||mode>2) {
                std::cout<<"{\"error\":\"invalid request\"}"<<std::endl;
                continue;
            }
            std::vector<sandbox::Event> all;
            bool ok=true;
            for(int k=0; k<count&&!engine.state.terminal; k++) {
                auto robots=engine.state.robots;
                std::array<sandbox::Action,2> actions {
                    sandbox::scripted_policy( {engine.state.tick,0,robots}),sandbox::scripted_policy( {engine.state.tick,1,robots})
                };
                if(mode==0)actions[0]=human;
                else if(mode==2) {
                    actions[0].dx=human.dx;
                    actions[0].dy=human.dy;
                }
                if(!engine.step(actions)) {
                    ok=false;
                    break;
                }
                all.insert(all.end(),engine.events.begin(),engine.events.end());
            }
            engine.events=std::move(all);
            std::cout<<(ok?engine.json():"{\"error\":\"invalid action\"}")<<std::endl;
        }
        else if(command=="FRAME") {
            int tick;
            std::array<sandbox::Action,2> a {};
            bool ok=bool(in>>tick);
            for(auto& action:a)ok=ok&&bool(in>>action.dx>>action.dy>>action.aim_x>>action.aim_y>>action.fire);
            ok=ok&&tick==engine.state.tick&&engine.step(a);
            std::cout<<(ok?engine.json():"{\"error\":\"replay frame rejected\"}")<<std::endl;
        }
        else std::cout<<"{\"error\":\"unknown command\"}"<<std::endl;
    }
}
