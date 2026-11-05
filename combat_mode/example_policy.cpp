#include "policy_sdk.hpp"
#include <array>
#include <limits>

namespace combat::policy {
namespace {
Integer distance_squared(Point a, Point b) {
    const auto dx=a.x-b.x, dy=a.y-b.y; return dx*dx+dy*dy;
}
Integer absolute(Integer n) { return n<0 ? -n : n; }
// Closed segment vs inflated public wall, using the sandbox's conservative Q16
// convention. This uses authored geometry only, never simulator state or helpers.
bool blocked(Point a, Point b, const Arena& arena) {
    const auto margin=arena.radius+60;
    for (const auto& wall : arena.walls) {
        Integer lo=0,hi=65536;
        bool misses=false;
        for (int axis=0; axis<2; ++axis) {
            const auto start=axis?a.y:a.x, delta=axis?b.y-a.y:b.x-a.x;
            const auto low=(axis?wall.y1:wall.x1)-margin, high=(axis?wall.y2:wall.x2)+margin;
            if (!delta) { if (start<low || start>high) { misses=true; break; } continue; }
            auto q1=(low-start)*65536/delta, q2=(high-start)*65536/delta;
            if (q1>q2) std::swap(q1,q2);
            lo=std::max(lo,q1); hi=std::min(hi,q2);
            if (lo>hi) { misses=true; break; }
        }
        if (!misses) return true;
    }
    return false;
}
Point next_waypoint(Point start, Point goal, const Arena& arena) {
    // Small deterministic visibility graph. The extra 40 mm puts nodes outside
    // the inflated rectangles; movement is replanned from each fresh snapshot.
    std::vector<Point> nodes{start,goal};
    const auto offset=arena.radius+100;
    for (const auto& wall : arena.walls) {
        for (auto x : {wall.x1-offset,wall.x2+offset})
            for (auto y : {wall.y1-offset,wall.y2+offset})
                if (x>=arena.radius && x<=arena.width-arena.radius && y>=arena.radius && y<=arena.height-arena.radius)
                    nodes.push_back({x,y});
    }
    const auto inf=std::numeric_limits<Integer>::max()/4;
    std::vector<Integer> costs(nodes.size(),inf);
    std::vector<int> previous(nodes.size(),-1);
    std::vector<bool> visited(nodes.size(),false);
    costs[0]=0;
    for (std::size_t iteration=0; iteration<nodes.size(); ++iteration) {
        int current=-1;
        for (std::size_t i=0; i<nodes.size(); ++i)
            if (!visited[i] && (current<0 || costs[i]<costs[std::size_t(current)])) current=int(i);
        if (current<0 || costs[std::size_t(current)]==inf) break;
        if (current==1) break;
        visited[std::size_t(current)]=true;
        for (std::size_t target=0; target<nodes.size(); ++target) {
            if (visited[target] || blocked(nodes[std::size_t(current)],nodes[target],arena)) continue;
            const auto weight=absolute(nodes[std::size_t(current)].x-nodes[target].x)+absolute(nodes[std::size_t(current)].y-nodes[target].y);
            const auto candidate=costs[std::size_t(current)]+weight;
            if (candidate<costs[target]) { costs[target]=candidate; previous[target]=current; }
        }
    }
    if (previous[1]<0) return start;
    int first=1;
    while (previous[std::size_t(first)]>0) first=previous[std::size_t(first)];
    return nodes[std::size_t(first)];
}
}
Descriptor descriptor() { return {kSdkVersion,"filtered-cover-v1"}; }
Action decide(const Observation& o) {
    const Point here{o.self.x,o.self.y};
    Action action{kSdkVersion,Integer{0},Integer{0},o.self.x,o.self.y,Integer{0}};
    if (o.self.hp<=0) return action;
    // This route is deliberately a small synthetic-arena example, not RMUC tactics.
    // Tick and self index select patrol goals when the opponent is not observed.
    constexpr std::array<Point,4> patrol{{{4400,2200},{7600,2200},{7600,5400},{4400,5400}}};
    auto goal=patrol[std::size_t((o.tick/180)%4)];
    if (o.self_index==1) goal.x=o.arena.width-goal.x;
    bool should_move=true;
    if (o.enemy) {
        const Point target{o.enemy->x,o.enemy->y};
        action.aim_x=target.x; action.aim_y=target.y;
        const int heading=aim_direction(here,target);
        const auto delta=(heading-o.self.yaw+384)%256-128;
        // A ready shot uses the actual turret heading, visible pose and own ammo.
        action.fire=Integer{o.self.ammo>0 && o.self.cooldown<=1 && absolute(delta)<=4 ? 1 : 0};
        const auto distance=distance_squared(here,target);
        if (o.self.hp<=30 && distance<Integer{2400}*2400) {
            // Low-health retreat is based only on own HP and a currently visible foe.
            goal={std::clamp(here.x+(here.x-target.x),o.arena.radius,o.arena.width-o.arena.radius),
                  std::clamp(here.y+(here.y-target.y),o.arena.radius,o.arena.height-o.arena.radius)};
        } else if (o.self.ammo>0 && distance>Integer{3900}*3900) goal=target;
        else should_move=false;
    } else {
        // Looking toward the planned search route is not a hidden enemy aim.
        action.aim_x=goal.x; action.aim_y=goal.y;
    }
    if (should_move) {
        const auto next=next_waypoint(here,goal,o.arena);
        action.dx=std::clamp(next.x-here.x,Integer{-1000},Integer{1000});
        action.dy=std::clamp(next.y-here.y,Integer{-1000},Integer{1000});
    }
    return action;
}
} // namespace combat::policy
