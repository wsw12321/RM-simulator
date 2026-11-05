#include "policy_sdk.hpp"
#include <array>
#include <limits>

namespace roster::policy {
namespace {
Integer distance_squared(Point a, Point b) {
    const auto dx=a.x-b.x, dy=a.y-b.y; return dx*dx+dy*dy;
}
Integer absolute(Integer n) { return n<0 ? -n : n; }
// Closed segment vs inflated public wall, using the filtered SDK's conservative Q16
// convention. This uses authored geometry only, never simulator state or helpers.
bool blocked(Point a, Point b, const Arena& arena, Integer radius) {
    const auto margin=radius+60;
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
Point next_waypoint(Point start, Point goal, const Arena& arena, Integer radius) {
    // Small deterministic visibility graph. Extra corner clearance also absorbs
    // integer per-axis movement rounding; replan from each fresh snapshot.
    std::vector<Point> nodes{start,goal};
    const auto offset=radius+220;
    for (const auto& wall : arena.walls) {
        for (auto x : {wall.x1-offset,wall.x2+offset})
            for (auto y : {wall.y1-offset,wall.y2+offset})
                if (x>=radius && x<=arena.width-radius && y>=radius && y<=arena.height-radius)
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
            if (visited[target] || blocked(nodes[std::size_t(current)],nodes[target],arena,radius)) continue;
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
Descriptor descriptor() { return {kSdkVersion,"synthetic-roster-capture-v1"}; }
Action decide(const Observation& o) {
    const Point here{o.self.x,o.self.y};
    Action action{kSdkVersion,Integer{0},Integer{0},here.x,here.y,Integer{0}};
    if(o.self.hp<=0) return action;
    std::size_t rank=0;
    Integer radius=280;
    for(const auto& entry:o.roster) {
        if(entry.id==o.self_id) radius=entry.profile.radius_mm;
        if(entry.team==o.self_team && entry.id<o.self_id) ++rank;
    }
    const std::size_t goal_index = o.self_team==0 ? (rank%2==0?3:1) : (rank%2==0?0:2);
    const auto& zone=o.zones[goal_index];
    const bool captured=zone.controller==o.self_team;
    Point goal{(zone.rect.x1+zone.rect.x2)/2,(zone.rect.y1+zone.rect.y2)/2};
    // Distinct public lanes; extra robots stagger their static target slightly.
    goal.y += Integer(rank/2)*300;
    if(captured) goal={6000,rank%2==0?1500:6200};
    bool moving=distance_squared(here,goal)>Integer{80}*80;
    action.aim_x=goal.x;action.aim_y=goal.y;
    const Contact* nearest=nullptr;
    for(const auto& c:o.contacts) if(c.team!=o.self_team && (!nearest ||
        distance_squared(here,{c.x,c.y})<distance_squared(here,{nearest->x,nearest->y}))) nearest=&c;
    if(nearest && (captured || o.tick>=1800)) {
        Point target{nearest->x,nearest->y};
        action.aim_x=target.x;action.aim_y=target.y;
        const auto heading=aim_direction(here,target);
        const auto delta=(heading-o.self.yaw+384)%256-128;
        action.fire=Integer{!o.self_heat.firing_locked && o.self.ammo>0 && o.self.cooldown<=1 && absolute(delta)<=4 ? 1:0};
        if(o.self.ammo>0 && o.tick%900<600 && distance_squared(here,target)<=Integer{3600}*3600) moving=false;
    }
    if(moving) {
        auto navigation=o.arena;
        for(const auto& c:o.contacts) {
            Integer other_radius=280;
            for(const auto& entry:o.roster) if(entry.id==c.id) other_radius=entry.profile.radius_mm;
            navigation.walls.push_back({c.x-other_radius,c.y-other_radius,c.x+other_radius,c.y+other_radius});
        }
        const auto next=next_waypoint(here,goal,navigation,radius);
        const auto dx=next.x-here.x,dy=next.y-here.y;
        const auto scale=std::max({Integer{1000},absolute(dx),absolute(dy)});
        action.dx=dx*1000/scale;action.dy=dy*1000/scale;
    }
    return action;
}
} // namespace roster::policy
