#include "sim.hpp"
#include <algorithm>
namespace sandbox {
    // Edit this C++ function, rebuild, then restart the local server to try your policy.
    // Policy gets read-only observation and returns intent; no health/state setters.
    Action scripted_policy(const Observation& obs) {
        auto self=obs.robots[obs.self],enemy=obs.robots[1-obs.self];
        Action a {0,0,enemy.x,enemy.y,0};
        if(self.hp<=0||enemy.hp<=0)return a;
        Vec goal {enemy.x,enemy.y};
        bool blocked=line_blocked( {self.x,self.y},goal);
        if(blocked) {
            goal.y=2100;
            goal.x=self.x<6000?4500:7500;
            if(std::abs(self.y-goal.y)<400)goal.x=enemy.x;
        }
        int dx=goal.x-self.x,dy=goal.y-self.y;
        if(blocked||std::int64_t(dx)*dx+std::int64_t(dy)*dy>std::int64_t(4000)*4000) {
            a.dx=std::clamp(dx,-1000,1000);
            a.dy=std::clamp(dy,-1000,1000);
        }
        else {
            int strafe=(obs.tick/180)%2?1:-1;
            a.dx=std::clamp(-dy*strafe,-650,650);
            a.dy=std::clamp(dx*strafe,-650,650);
        }
        int desired=aim_direction( {self.x,self.y}, {enemy.x,enemy.y}),delta=(desired-self.yaw+384)%256-128;
        a.fire=!blocked&&std::abs(delta)<=4;
        return a;
    }
}
