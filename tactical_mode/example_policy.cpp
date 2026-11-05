#include "policy_sdk.hpp"
#include <array>
#include <limits>

namespace tactical::policy {
namespace {
Integer distance_squared(Point a, Point b) {
    const auto dx=a.x-b.x, dy=a.y-b.y; return dx*dx+dy*dy;
}
Integer absolute(Integer n) { return n<0 ? -n : n; }
// Closed segment vs inflated public wall, using the filtered SDK's conservative Q16
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
    // Small deterministic visibility graph. Extra corner clearance also absorbs
    // integer per-axis movement rounding; replan from each fresh snapshot.
    std::vector<Point> nodes{start,goal};
    const auto offset=arena.radius+220;
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
Descriptor descriptor() { return {kSdkVersion,"public-capture-combat-v1"}; }
Action decide(const Observation& observation) {
    const auto& o=observation.combat;
    const Point here{o.self.x,o.self.y};
    Action action{kSdkVersion,Integer{0},Integer{0},here.x,here.y,Integer{0}};
    if (o.self.hp<=0) return action;

    // This asymmetry demonstrates two distinct public capture broadcasts before
    // combat. It is a placeholder-arena teaching policy, not official RMUC tactics.
    const auto target_index=std::size_t(o.self_index==0 ? 1 : 2);
    const auto& objective=observation.zones[target_index];
    const bool own_capture=objective.controller==o.self_index;
    const bool both_captured=observation.zones[1].controller==0 &&
                             observation.zones[2].controller==1;
    const bool combat_phase=own_capture && (both_captured || o.tick>=1800);
    const auto& rect=objective.rect;
    Point goal{(rect.x1+rect.x2)/2,(rect.y1+rect.y2)/2};
    bool should_move=distance_squared(here,goal)>Integer{60}*60;

    if (own_capture) {
        // Both teams use the same rendezvous. Alternating public waypoints and
        // bounded firing pauses avoid waiting forever on a visible disabled body:
        // this SDK never reveals whether that opponent is alive or disabled.
        constexpr std::array<Point,2> rendezvous{{{6000,6200},{6000,1500}}};
        goal=rendezvous[std::size_t((o.tick/900)%2)];
        should_move=distance_squared(here,goal)>Integer{90}*90;
    }
    action.aim_x=goal.x; action.aim_y=goal.y;
    if (combat_phase && o.enemy) {
        const Point target{o.enemy->x,o.enemy->y};
        action.aim_x=target.x; action.aim_y=target.y;
        const auto heading=aim_direction(here,target);
        const auto delta=(heading-o.self.yaw+384)%256-128;
        action.fire=Integer{!observation.self_heat.firing_locked && o.self.ammo>0 && o.self.cooldown<=1 && absolute(delta)<=4 ? 1 : 0};
        // Hold a stable firing line for part of each fixed interval, then resume
        // the public route even if enemy pose remains present. No hit/HP inference.
        if (o.self.ammo>0 && o.tick%900<600 &&
            distance_squared(here,target)<=Integer{3600}*3600) should_move=false;
    }
    if (should_move) {
        auto navigation=o.arena;
        if (o.enemy) {
            // A currently observed body is a movement obstacle regardless of
            // hidden health. This also keeps the initial capture routes apart.
            const auto r=o.arena.radius;
            navigation.walls.push_back({o.enemy->x-r,o.enemy->y-r,
                                        o.enemy->x+r,o.enemy->y+r});
        }
        const auto next=next_waypoint(here,goal,navigation);
        const auto dx=next.x-here.x, dy=next.y-here.y;
        const auto scale=std::max({Integer{1000},absolute(dx),absolute(dy)});
        // Preserve the planned segment's slope. Independent axis clipping can
        // cut a corner and strand a robot inside the inflated planning obstacle.
        action.dx=dx*1000/scale;
        action.dy=dy*1000/scale;
    }
    return action;
}
} // namespace tactical::policy
