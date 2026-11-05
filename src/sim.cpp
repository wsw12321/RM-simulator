#include "sim.hpp"
#include <algorithm>
#include <cstdlib>
#include <sstream>
namespace sandbox {
    static constexpr std::array<Vec,256> DIR {
        {
#include "directions.inc"
        }
    };
    std::uint64_t isqrt(std::uint64_t n) {
        std::uint64_t x=0,bit=std::uint64_t {1}
        <<62;
        while(bit>n)bit>>=2;
        while(bit) {
            if(n>=x+bit) {
                n-=x+bit;
                x=(x>>1)+bit;
            }
            else x>>=1;
            bit>>=2;
        }
        return x;
    }
    Vec direction(int yaw) {
        return DIR[static_cast<unsigned>(yaw)&255];
    }
    int aim_direction(Vec from,Vec target) {
        int best=0;
        std::int64_t score=INT64_MIN;
        for(int i=0; i<256; i++) {
            auto d=DIR[i];
            auto s=std::int64_t(target.x-from.x)*d.x+std::int64_t(target.y-from.y)*d.y;
            auto cross=std::llabs(std::int64_t(target.x-from.x)*d.y-std::int64_t(target.y-from.y)*d.x);
            auto prior=DIR[best];
            auto oldcross=std::llabs(std::int64_t(target.x-from.x)*prior.y-std::int64_t(target.y-from.y)*prior.x);
            if(s>score||(s==score&&cross<oldcross)) {
                score=s;
                best=i;
            }
        }
        return best;
    }
    static bool point_wall(int x,int y,int radius) {
        for(auto w:WALLS) {
            int px=std::clamp(x,w.x1,w.x2),py=std::clamp(y,w.y1,w.y2);
            auto dx=std::int64_t(x-px),dy=std::int64_t(y-py);
            if(dx*dx+dy*dy<std::int64_t(radius)*radius)return true;
        }
        return false;
    }
    // Conservative swept segment/AABB test with Q16 intersection time. An early hit is
    // at most one Q16 fraction of a segment away. Tie order is cover before robot.
    static int rect_hit(Vec a,Vec b,Rect w) {
        std::int64_t lo=0,hi=65536;
        for(int axis=0; axis<2; axis++) {
            int start=axis?a.y:a.x,delta=axis?b.y-a.y:b.x-a.x,mn=axis?w.y1:w.x1,mx=axis?w.y2:w.x2;
            if(delta==0) {
                if(start<mn||start>mx)return -1;
                continue;
            }
            std::int64_t q1=std::int64_t(mn-start)*65536/delta,q2=std::int64_t(mx-start)*65536/delta;
            if(q1>q2)std::swap(q1,q2);
            lo=std::max(lo,q1);
            hi=std::min(hi,q2);
            if(lo>hi)return -1;
        }
        return static_cast<int>(lo);
    }
    bool line_blocked(Vec a,Vec b) {
        for(auto w:WALLS)if(rect_hit(a,b,w)>=0)return true;
        return false;
    }
    // Integer swept circle intersection. Bounded arena coordinates keep all products
    // within int64. Find nearest sample then bisect entry on the approaching half.
    static int circle_hit(Vec a,Vec b,Vec c,int radius) {
        std::int64_t dx=b.x-a.x,dy=b.y-a.y,len=dx*dx+dy*dy;
        auto inside=[&](int t) {
            auto x=std::int64_t(a.x-c.x)+dx*t/65536,y=std::int64_t(a.y-c.y)+dy*t/65536;
            return x*x+y*y<=std::int64_t(radius)*radius;
        };
        if(inside(0))return 0;
        if(!len)return -1;
        auto dot=std::int64_t(c.x-a.x)*dx+std::int64_t(c.y-a.y)*dy;
        int closest=static_cast<int>(std::clamp(dot*65536/len,std::int64_t {0},std::int64_t {65536}));
        if(!inside(closest))return -1;
        int lo=0,hi=closest;
        while(lo+1<hi) {
            int mid=(lo+hi)/2;
            if(inside(mid))hi=mid;
            else lo=mid;
        }
        return hi;
    }
    void Engine::reset() {
        state= {};
        state.next_id=1;
        state.robots[0]= {1600,3800,0};
        state.robots[1]= {10400,3800,128};
        journal.clear();
        events.clear();
    }
    bool Engine::step(const std::array<Action,2>& in) {
        if(state.terminal)return false;
        for(auto a:in)if(a.dx < -1000||a.dx>1000||a.dy < -1000||a.dy>1000||a.aim_x<0||a.aim_x>WIDTH||a.aim_y<0||a.aim_y>HEIGHT||(a.fire!=0&&a.fire!=1))return false;
        events.clear();
        auto old=state.robots;
        auto moved=old;
        // Inputs observed from a common tick boundary. Axis sliding is a sandbox model.
        for(int i=0; i<2; i++) {
            auto& r=moved[i];
            if(r.hp<=0)continue;
            auto a=in[i];
            if(r.cooldown>0)--r.cooldown;
            auto len=isqrt(std::uint64_t(std::int64_t(a.dx)*a.dx+std::int64_t(a.dy)*a.dy));
            auto divisor=std::max(std::uint64_t {1000},len);
            int vx=static_cast<int>(std::int64_t(a.dx)*SPEED/static_cast<std::int64_t>(divisor)),vy=static_cast<int>(std::int64_t(a.dy)*SPEED/static_cast<std::int64_t>(divisor));
            int nx=std::clamp(r.x+vx,RADIUS,WIDTH-RADIUS);
            if(!point_wall(nx,r.y,RADIUS))r.x=nx;
            int ny=std::clamp(r.y+vy,RADIUS,HEIGHT-RADIUS);
            if(!point_wall(r.x,ny,RADIUS))r.y=ny;
            int desired=aim_direction( {r.x,r.y}, {a.aim_x,a.aim_y});
            int diff=(desired-r.yaw+384)%256-128;
            r.yaw=(r.yaw+std::clamp(diff,-4,4)+256)%256;
        }
        auto dx=std::int64_t(moved[0].x-moved[1].x),dy=std::int64_t(moved[0].y-moved[1].y);
        if(dx*dx+dy*dy<std::int64_t(2*RADIUS)*(2*RADIUS)) {
            for(int i=0; i<2; i++) {
                moved[i].x=old[i].x;
                moved[i].y=old[i].y;
            }
        }
        state.robots=moved;
        // Both living robots may fire before damage commits, allowing simultaneous KO.
        for(int i=0; i<2; i++) {
            auto& r=state.robots[i];
            if(r.hp>0&&in[i].fire&&r.cooldown==0&&r.ammo>0&&state.bullets.size()<128) {
                auto d=direction(r.yaw);
                int offset=RADIUS+40;
                Bullet b {
                    state.next_id++,i,r.x+d.x*offset/1000,r.y+d.y*offset/1000,d.x*PROJECTILE_SPEED/1000,d.y*PROJECTILE_SPEED/1000,180
                };
                --r.ammo;
                ++r.shots;
                r.cooldown=COOLDOWN;
                events.push_back( {state.tick,1,i,-1,0});
                if(line_blocked( {r.x,r.y}, {b.x,b.y})) {
                    events.push_back( {state.tick,4,i,-1,0});
                }
                else state.bullets.push_back(b);
            }
        }
        std::array<int,2> damage {};
        std::vector<Bullet> alive;
        for(auto b:state.bullets) {
            Vec a {b.x,b.y},end {b.x+b.vx,b.y+b.vy};
            int first=65537,target=-1;
            for(auto w:WALLS) {
                int hit=rect_hit(a,end,w);
                if(hit>=0&&hit<first) {
                    first=hit;
                    target=-1;
                }
            }
            for(int j=0; j<2; j++)if(j!=b.owner&&state.robots[j].hp>0) {
                auto r=state.robots[j];
                int hit=circle_hit(a,end, {r.x,r.y},RADIUS+30);
                if(hit>=0&&hit<first) {
                    first=hit;
                    target=j;
                }
            }
            if(first<=65536) {
                if(target>=0) {
                    damage[target]+=DAMAGE;
                    ++state.robots[b.owner].hits;
                    events.push_back( {state.tick,2,b.owner,target,DAMAGE});
                }
                else events.push_back( {state.tick,4,b.owner,-1,0});
                continue;
            }
            b.x=end.x;
            b.y=end.y;
            --b.life;
            if(b.life>0&&b.x>=0&&b.x<=WIDTH&&b.y>=0&&b.y<=HEIGHT)alive.push_back(b);
        }
        state.bullets=std::move(alive);
        for(int i=0; i<2; i++) {
            auto& r=state.robots[i];
            int hp=r.hp;
            r.hp=std::max(0,r.hp-damage[i]);
            if(hp>0&&r.hp==0)events.push_back( {state.tick,3,i,i,0});
        }
        ++state.tick;
        if((options.stop_on_disable&&(state.robots[0].hp==0||state.robots[1].hp==0))||state.tick>=options.duration_ticks)state.terminal=1;
        if(options.record_journal)journal.push_back( {state.tick-1,in,hash()});
        return true;
    }
    std::uint32_t Engine::hash()const {
        std::uint32_t h=2166136261u;
        auto put=[&](int v) {
            auto u=static_cast<std::uint32_t>(v);
            for(int i=0; i<4; i++) {
                h^=(u>>(i*8))&255u;
                h*=16777619u;
            }
        };
        put(ABI_VERSION);
        put(state.tick);
        put(state.next_id);
        put(state.terminal);
        for(auto r:state.robots) {
            put(r.x);
            put(r.y);
            put(r.yaw);
            put(r.hp);
            put(r.ammo);
            put(r.cooldown);
            put(r.shots);
            put(r.hits);
        }
        put(static_cast<int>(state.bullets.size()));
        for(auto b:state.bullets) {
            put(b.id);
            put(b.owner);
            put(b.x);
            put(b.y);
            put(b.vx);
            put(b.vy);
            put(b.life);
        }
        return h;
    }
    std::string Engine::json()const {
        std::ostringstream o;
        o<<"{\"build\":\""<<SIM_BUILD_ID<<"\",\"tick\":"<<state.tick<<",\"hash\":"<<hash()<<",\"terminal\":"<<state.terminal<<",\"robots\":[";
        for(int i=0; i<2; i++) {
            if(i)o<<',';
            auto r=state.robots[i];
            o<<"{\"x\":"<<r.x<<",\"y\":"<<r.y<<",\"yaw\":"<<r.yaw<<",\"hp\":"<<r.hp<<",\"ammo\":"<<r.ammo<<",\"cooldown\":"<<r.cooldown<<",\"shots\":"<<r.shots<<",\"hits\":"<<r.hits<<'}';
        }
        o<<"],\"bullets\":[";
        for(std::size_t i=0; i<state.bullets.size(); i++) {
            if(i)o<<',';
            auto b=state.bullets[i];
            o<<"["<<b.x<<','<<b.y<<','<<b.owner<<"]";
        }
        o<<"],\"events\":[";
        for(std::size_t i=0; i<events.size(); i++) {
            if(i)o<<',';
            auto e=events[i];
            o<<'['<<e.tick<<','<<e.kind<<','<<e.actor<<','<<e.target<<','<<e.amount<<']';
        }
        o<<"]}";
        return o.str();
    }
    std::string Engine::replay_json()const {
        std::ostringstream o;
        o<<"{\"build\":\""<<SIM_BUILD_ID<<"\",\"schema\":1,\"profile\":\"sandbox-v1\",\"engine\":\"integer-mm-v1\",\"observation\":\"omniscient\",\"official\":false,\"frames\":[";
        for(std::size_t i=0; i<journal.size(); i++) {
            if(i)o<<',';
            auto f=journal[i];
            o<<'['<<f.tick;
            for(auto a:f.actions)o<<','<<a.dx<<','<<a.dy<<','<<a.aim_x<<','<<a.aim_y<<','<<a.fire;
            o<<','<<f.hash<<']';
        }
        o<<"],\"final_hash\":"<<hash()<<'}';
        return o.str();
    }
}
