#include "core.hpp"
#include "policy_sdk.hpp"
#include <algorithm>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

#ifndef LAYERED_BUILD_ID
#define LAYERED_BUILD_ID "unversioned-development-build"
#endif
namespace layered {
namespace {
using W=__int128_t;
constexpr I COORD=100000;
W cross(Point a,Point b,Point c) {return W(b.x-a.x)*(c.y-a.y)-W(b.y-a.y)*(c.x-a.x);}
W dot(Point a,Point b,Point c) {return W(b.x-a.x)*(c.x-a.x)+W(b.y-a.y)*(c.y-a.y);}
W length2(Point a,Point b) {return W(b.x-a.x)*(b.x-a.x)+W(b.y-a.y)*(b.y-a.y);}
bool same(Point a,Point b){return a.x==b.x&&a.y==b.y;}
bool on_segment(Point p,Point a,Point b){if(same(a,b))return same(p,a);return cross(a,b,p)==0&&dot(a,b,p)>=0&&dot(a,b,p)<=length2(a,b);}
bool inside(const Polygon& p,Point c,I r=0,int skip=-1) {
    for(size_t i=0;i<p.size();++i)if(int(i)!=skip){Point a=p[i],b=p[(i+1)%p.size()];W v=cross(a,b,c);if(v<0||v*v<W(r)*r*length2(a,b))return false;}
    return true;
}
bool near_segment(Point p,Point a,Point b,I r) {
    W t=dot(a,b,p),l=length2(a,b);
    if(t<=0)return length2(p,a)<=W(r)*r;
    if(t>=l)return length2(p,b)<=W(r)*r;
    W c=cross(a,b,p);return c*c<=W(r)*r*l;
}
bool intersects_segments(Point a,Point b,Point c,Point d) {
    W ab_c=cross(a,b,c),ab_d=cross(a,b,d),cd_a=cross(c,d,a),cd_b=cross(c,d,b);
    if(ab_c==0&&on_segment(c,a,b))return true;
    if(ab_d==0&&on_segment(d,a,b))return true;
    if(cd_a==0&&on_segment(a,c,d))return true;
    if(cd_b==0&&on_segment(b,c,d))return true;
    return ((ab_c<0&&ab_d>0)||(ab_c>0&&ab_d<0))&&((cd_a<0&&cd_b>0)||(cd_a>0&&cd_b<0));
}
bool capsule(const Polygon& p,Point a,Point b,I r) {
    if(inside(p,a)||inside(p,b))return true;
    for(size_t i=0;i<p.size();++i){Point c=p[i],d=p[(i+1)%p.size()];if(intersects_segments(a,b,c,d)||near_segment(a,c,d,r)||near_segment(b,c,d,r)||near_segment(c,a,b,r)||near_segment(d,a,b,r))return true;}return false;
}
I height(const Surface& s,Point p){return s.z*Q*Q+s.dx*p.x+s.dy*p.y;}
bool slope_ok(const Surface&s,const Config&c){return s.dx*s.dx+s.dy*s.dy<=c.max_slope*c.max_slope;}
int edge(const Polygon& p,Point a,Point b){for(size_t i=0;i<p.size();++i)if(on_segment(a,p[i],p[(i+1)%p.size()])&&on_segment(b,p[i],p[(i+1)%p.size()]))return int(i);return -1;}
// Conservative two-piece support: both non-seam half-plane sets and portal endpoint corridor.
bool pair_contains(const Map&m,const Portal&p,Point c,I r){
    const auto&a=m.surfaces[p.from];const auto&b=m.surfaces[p.to];
    if(!inside(a.polygon,c,r,p.edge_from)||!inside(b.polygon,c,r,p.edge_to))return false;
    W t=dot(p.a,p.b,c),l=length2(p.a,p.b),rr=W(r)*r*l;
    return t>=0&&t<=l&&t*t>=rr&&(l-t)*(l-t)>=rr;
}
struct Support {bool ok=false;std::vector<int> surfaces;std::string reason="support",id;};
Support support(const Map&m,const Config&c,int current,int target,Point a,Point b) {
    I r=c.radius*Q;const auto&s=m.surfaces[current];
    if(!inside(m.boundary,a,r)||!inside(m.boundary,b,r))return {false,{},"boundary","boundary"};
    if(!s.traversable)return {false,{},"support",s.id};
    if(!slope_ok(s,c))return {false,{},"slope",s.id};
    if(current==target&&inside(s.polygon,a,r)&&inside(s.polygon,b,r))return {true,{current},"",""};
    for(const auto&p:m.portals){
        int other=p.from==current?p.to:p.to==current?p.from:-1;
        if(other<0||(target!=current&&target!=other))continue;
        // Footprint support is undirected, but a center transfer is not.
        if(target!=current&&!(p.from==current||p.bidirectional))continue;
        const auto&o=m.surfaces[other];
        if(!o.traversable||!slope_ok(o,c))continue;
        if(pair_contains(m,p,a,r)&&pair_contains(m,p,b,r))return {true,{current,other},"",""};
    }
    return {false,{},"support",s.id};
}
struct Envelope {I low=0,high=0;std::optional<I> ceiling;};
Envelope envelope(const Map&m,const Config&c,const Support& s,Point a,Point b){
    Envelope e; e.low=std::numeric_limits<I>::max();e.high=std::numeric_limits<I>::min();
    for(int i:s.surfaces){const auto&v=m.surfaces[i];I d=(std::abs(v.dx)+std::abs(v.dy))*c.radius*Q;
        e.low=std::min(e.low,std::min(height(v,a),height(v,b))-d);
        e.high=std::max(e.high,std::max(height(v,a),height(v,b))+d+c.height*Q*Q);
        if(v.has_ceiling)e.ceiling=e.ceiling?std::min(*e.ceiling,v.ceiling*Q*Q):v.ceiling*Q*Q;
    }return e;
}
std::vector<Contact> blocked(const Map&m,const Config&c,const Support&s,Point a,Point b){
    if(!s.ok)return {{s.reason,s.id}};
    auto e=envelope(m,c,s,a,b);std::vector<Contact> out;
    if(e.ceiling&&e.high>=*e.ceiling){
        // Attribute each violated declaration to its author, not the selected support.
        // Authored surface order is stable even when portal traversal is reversed.
        auto participating=s.surfaces;std::sort(participating.begin(),participating.end());
        for(int index:participating){const auto&surface=m.surfaces[index];
            if(surface.has_ceiling&&e.high>=surface.ceiling*Q*Q)
                out.push_back({"ceiling",surface.id});
        }
    }
    for(const auto&v:m.solids)if(e.low<v.high*Q*Q&&e.high>=v.low*Q*Q&&capsule(v.polygon,a,b,c.radius*Q))out.push_back({"collision",v.id});
    return out;
}
bool scoped(const Volume&v,int s){return std::find(v.scopes.begin(),v.scopes.end(),s)!=v.scopes.end();}
std::vector<bool> trigger_hits(const Map&m,const Config&c,int current,int target,const Support&s,Point a,Point b){
    std::vector<bool> hits(m.triggers.size(),false);if(!s.ok)return hits;auto e=envelope(m,c,s,a,b);
    for(size_t i=0;i<m.triggers.size();++i){const auto&v=m.triggers[i];hits[i]=(scoped(v,current)||scoped(v,target))&&e.low<=v.high*Q*Q&&e.high>=v.low*Q*Q&&capsule(v.polygon,a,b,c.radius*Q);}return hits;
}
bool ident(const std::string&s){if(s.empty()||s.size()>64||!((s[0]>='a'&&s[0]<='z')||(s[0]>='A'&&s[0]<='Z')))return false;for(char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-'))return false;return true;}
bool digest(const std::string&s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
struct Reader{
    std::istream&in;std::set<std::string> ids;
    I integer(I lo,I hi){I v;if(!(in>>v)||v<lo||v>hi)throw std::runtime_error("Native profile integer missing or out of bounds.");return v;}
    bool boolean(){return integer(0,1)==1;}
    std::string id(bool unique=true){std::string v;if(!(in>>v)||!ident(v)||(unique&&!ids.insert(v).second))throw std::runtime_error("Invalid or duplicate native identifier.");return v;}
    std::string hash(){std::string v;if(!(in>>v)||!digest(v))throw std::runtime_error("Invalid native identity digest.");return v;}
    Point point(){return {integer(-COORD,COORD)*Q,integer(-COORD,COORD)*Q};}
    Polygon polygon(){Polygon p;int n=int(integer(3,16));for(int i=0;i<n;++i)p.push_back(point());for(int i=0;i<n;++i)for(int j=0;j<n;++j)if(j!=i&&j!=(i+1)%n&&cross(p[i],p[(i+1)%n],p[j])<=0)throw std::runtime_error("Native polygon must be strictly convex CCW.");return p;}
};
void add_json_string(std::ostream&o,const std::string&s){o<<'"'<<s<<'"';} // all native emitted identifiers are validated ASCII
void fnv_bytes(uint64_t&h,const std::string&s){for(unsigned char c:s){h^=c;h*=1099511628211ULL;}h^=0xff;h*=1099511628211ULL;}
void fnv_i(uint64_t&h,I n){uint64_t v=static_cast<uint64_t>(n);for(int i=0;i<8;++i){h^=uint8_t(v>>(i*8));h*=1099511628211ULL;}}
}
std::string build_id(){return LAYERED_BUILD_ID;}
std::optional<std::string> Engine::load(std::istream&in){
    try{
        Engine e;Reader r{in,{}};e.map.map_hash=r.hash();e.map.config_hash=r.hash();
        e.config.radius=r.integer(1,1000);e.config.height=r.integer(1,3000);e.config.speed=r.integer(1,50);e.config.max_slope=r.integer(0,1024);e.config.max_ticks=r.integer(1,36000);e.config.start=r.point();e.config.surface=int(r.integer(0,31));
        e.map.boundary=r.polygon();int ns=int(r.integer(1,32));
        for(int i=0;i<ns;++i){Surface s;s.id=r.id();s.layer=r.id(false);s.z=r.integer(-COORD,COORD);s.dx=r.integer(-1024,1024);s.dy=r.integer(-1024,1024);if(std::abs(s.dx)+std::abs(s.dy)>1024)throw std::runtime_error("Native slope L1 bound exceeded.");s.has_ceiling=r.boolean();s.ceiling=r.integer(-COORD,COORD);s.traversable=r.boolean();s.polygon=r.polygon();for(auto p:s.polygon){I z=height(s,p);if(!inside(e.map.boundary,p)||z<-COORD*Q*Q||z>COORD*Q*Q||(s.has_ceiling&&z>=s.ceiling*Q*Q))throw std::runtime_error("Native support outside boundary or invalid height/ceiling.");}e.map.surfaces.push_back(s);}
        if(e.config.surface>=ns)throw std::runtime_error("Native start surface out of range.");
        auto volumes=[&](std::vector<Volume>&out,int limit,bool triggers){int n=int(r.integer(0,limit));for(int i=0;i<n;++i){Volume v;v.id=r.id();v.low=r.integer(-COORD,COORD);v.high=r.integer(-COORD,COORD);if(v.low>=v.high)throw std::runtime_error("Native volume must have positive height.");v.polygon=r.polygon();if(triggers){int k=int(r.integer(1,32));std::set<int> used;for(int j=0;j<k;++j){int s=int(r.integer(0,ns-1));if(!used.insert(s).second)throw std::runtime_error("Duplicate native trigger scope.");v.scopes.push_back(s);}}out.push_back(v);}};
        volumes(e.map.solids,64,false);volumes(e.map.triggers,32,true);
        int np=int(r.integer(0,64));for(int i=0;i<np;++i){Portal p;p.id=r.id();p.from=int(r.integer(0,ns-1));p.to=int(r.integer(0,ns-1));p.a=r.point();p.b=r.point();p.bidirectional=r.boolean();r.integer(0,0);if(p.from==p.to||same(p.a,p.b))throw std::runtime_error("Native portal must connect distinct surfaces.");const auto&a=e.map.surfaces[p.from];const auto&b=e.map.surfaces[p.to];p.edge_from=edge(a.polygon,p.a,p.b);p.edge_to=edge(b.polygon,p.a,p.b);if(p.edge_from<0||p.edge_to<0||height(a,p.a)!=height(b,p.a)||height(a,p.b)!=height(b,p.b))throw std::runtime_error("Native portal must share exact edges and continuous heights.");Point aa=a.polygon[p.edge_from],ab=a.polygon[(p.edge_from+1)%a.polygon.size()],ba=b.polygon[p.edge_to],bb=b.polygon[(p.edge_to+1)%b.polygon.size()];if(W(ab.x-aa.x)*(bb.x-ba.x)+W(ab.y-aa.y)*(bb.y-ba.y)>=0)throw std::runtime_error("Native portal surfaces must occupy opposite sides.");e.map.portals.push_back(p);}
        std::string extra;if(in>>extra)throw std::runtime_error("Trailing native map tokens.");
        e.position=e.config.start;e.surface=e.config.surface;
        if(!inside(e.map.surfaces[e.surface].polygon,e.position))throw std::runtime_error("Native start center is outside selected support.");
        auto s=support(e.map,e.config,e.surface,e.surface,e.position,e.position);auto c=blocked(e.map,e.config,s,e.position,e.position);if(!c.empty())throw std::runtime_error("Native start footprint is infeasible: "+c[0].kind+" / "+c[0].id+".");
        e.active=trigger_hits(e.map,e.config,e.surface,e.surface,s,e.position,e.position);for(size_t i=0;i<e.active.size();++i)if(e.active[i])e.events.push_back({0,0,"trigger_enter",e.map.triggers[i].id,"",""});
        e.ready=true;*this=std::move(e);return std::nullopt;
    }catch(const std::exception&err){return err.what();}
}
void Engine::step(I dx,I dy){
    events.clear();contacts.clear();I vx=dx*config.speed*Q/1000,vy=dy*config.speed*Q/1000;
    I n=std::max<I>(1,(std::max(std::abs(vx),std::abs(vy))+Q-1)/Q);Point origin=position;
    for(I k=1;k<=n;++k){Point next{origin.x+vx*k/n,origin.y+vy*k/n};int target=surface;const Portal*transfer=nullptr;
        if(!inside(map.surfaces[surface].polygon,next)){
            for(const auto&p:map.portals){int other=p.from==surface?p.to:(p.bidirectional&&p.to==surface?p.from:-1);if(other>=0&&inside(map.surfaces[other].polygon,next)&&intersects_segments(position,next,p.a,p.b)){target=other;transfer=&p;break;}}
            if(!transfer){contacts={{"support",map.surfaces[surface].id}};break;}
        }
        auto s=support(map,config,surface,target,position,next);auto c=blocked(map,config,s,position,next);if(!c.empty()){contacts=std::move(c);break;}
        auto swept=trigger_hits(map,config,surface,target,s,position,next);
        auto endpoint_support=support(map,config,target,target,next,next);
        auto hits=trigger_hits(map,config,target,target,endpoint_support,next,next);
        if(transfer)events.push_back({tick+1,int(k-1),"transition",transfer->id,map.surfaces[surface].id,map.surfaces[target].id});
        for(size_t i=0;i<active.size();++i)if(active[i]&&!hits[i])events.push_back({tick+1,int(k-1),"trigger_exit",map.triggers[i].id,"",""});
        for(size_t i=0;i<active.size();++i)if(!active[i]&&hits[i])events.push_back({tick+1,int(k-1),"trigger_enter",map.triggers[i].id,"",""});
        for(size_t i=0;i<active.size();++i)if(!active[i]&&!hits[i]&&swept[i]){events.push_back({tick+1,int(k-1),"trigger_enter",map.triggers[i].id,"",""});events.push_back({tick+1,int(k-1),"trigger_exit",map.triggers[i].id,"",""});}
        active=std::move(hits);position=next;surface=target;
    }++tick;
}
std::optional<std::string> Engine::advance(I count,I dx,I dy,std::optional<I> frame_tick){
    if(!ready)return "Native map is not loaded.";
    if(count<1||count>600)return "Native action/count outside bounds.";
    if(auto error=policy::validate_action({policy::kSdkVersion,dx,dy}))return error;
    if(frame_tick&&(count!=1||*frame_tick!=tick))return "Native replay tick is not sequential.";
    if(count>config.max_ticks-tick)return "Native duration exhausted; no judgement is fabricated.";
    Engine candidate=*this;candidate.frames.clear();candidate.batch_events.clear();for(I i=0;i<count;++i){I t=candidate.tick;candidate.step(dx,dy);if(candidate.batch_events.size()+candidate.events.size()>8192)return "Native batch event bound exceeded; reduce count. Existing run unchanged.";candidate.batch_events.insert(candidate.batch_events.end(),candidate.events.begin(),candidate.events.end());candidate.frames.push_back({t,dx,dy,candidate.hash()});}*this=std::move(candidate);return std::nullopt;
}
std::string Engine::hash()const{
    uint64_t h=14695981039346656037ULL;fnv_bytes(h,"layered-state-fnv64-v1");fnv_bytes(h,build_id());fnv_bytes(h,map.map_hash);fnv_bytes(h,map.config_hash);fnv_i(h,tick);fnv_i(h,position.x);fnv_i(h,position.y);fnv_i(h,surface);if(ready)fnv_i(h,height(map.surfaces[surface],position));
    fnv_i(h,active.size());for(bool b:active)fnv_i(h,b);fnv_i(h,contacts.size());for(const auto&c:contacts){fnv_bytes(h,c.kind);fnv_bytes(h,c.id);}fnv_i(h,events.size());for(const auto&e:events){fnv_i(h,e.tick);fnv_i(h,e.substep);fnv_bytes(h,e.type);fnv_bytes(h,e.id);fnv_bytes(h,e.from);fnv_bytes(h,e.to);}std::ostringstream o;o<<std::hex<<std::setfill('0')<<std::setw(16)<<h;return o.str();
}
std::string Engine::json()const{
    if(!ready)return "{\"error\":\"Native map is not loaded.\"}";
    const auto&s=map.surfaces[surface];auto sup=support(map,config,surface,surface,position,position);auto env=envelope(map,config,sup,position,position);I z=height(s,position);
    std::ostringstream o;o<<std::setprecision(15)<<"{\"schema\":\"rmuc-layered-state.v1\",\"profile\":\"layered-native-v1\",\"official\":false,\"build\":\""<<build_id()<<"\",\"map_hash\":\""<<map.map_hash<<"\",\"config_hash\":\""<<map.config_hash<<"\",\"tick\":"<<tick<<",\"status\":\""<<(tick>=config.max_ticks?"limit_reached":"active")<<"\",\"robot\":{\"x_q1024\":"<<position.x<<",\"y_q1024\":"<<position.y<<",\"z_q1048576\":"<<z<<",\"x_mm\":"<<double(position.x)/Q<<",\"y_mm\":"<<double(position.y)/Q<<",\"z_mm\":"<<double(z)/(Q*Q)<<",\"surface_id\":\""<<s.id<<"\",\"layer_id\":\""<<s.layer<<"\",\"clearance_mm\":";
    if(env.ceiling)o<<double(*env.ceiling-(env.high-config.height*Q*Q))/(Q*Q);else o<<"null";o<<",\"support_min_mm\":"<<double(env.low)/(Q*Q)<<",\"support_max_mm\":"<<double(env.high-config.height*Q*Q)/(Q*Q)<<",\"envelope_min_mm\":"<<double(env.low)/(Q*Q)<<",\"envelope_max_mm\":"<<double(env.high)/(Q*Q)<<",\"ceiling_z_mm\":";if(env.ceiling)o<<double(*env.ceiling)/(Q*Q);else o<<"null";o<<"},\"contacts\":[";for(size_t i=0;i<contacts.size();++i){if(i)o<<',';o<<"{\"kind\":\""<<contacts[i].kind<<"\",\"id\":\""<<contacts[i].id<<"\"}";}o<<"],\"active_trigger_ids\":[";bool comma=false;for(size_t i=0;i<active.size();++i)if(active[i]){if(comma)o<<',';add_json_string(o,map.triggers[i].id);comma=true;}o<<"],\"events\":[";
    for(size_t i=0;i<events.size();++i){const auto&e=events[i];if(i)o<<',';o<<"{\"tick\":"<<e.tick<<",\"substep\":"<<e.substep<<",\"type\":\""<<e.type<<"\",\"id\":\""<<e.id<<'"';if(e.type=="transition")o<<",\"from_surface\":\""<<e.from<<"\",\"to_surface\":\""<<e.to<<'"';o<<'}';}o<<"],\"hash\":\""<<hash()<<"\"}";return o.str();
}
std::string Engine::result_json()const{std::ostringstream o;o<<"{\"state\":"<<json()<<",\"frames\":[";for(size_t i=0;i<frames.size();++i){if(i)o<<',';const auto&f=frames[i];o<<'['<<f.tick<<','<<f.dx<<','<<f.dy<<",\""<<f.hash<<"\"]";}o<<"],\"batch_events\":[";for(size_t i=0;i<batch_events.size();++i){const auto&e=batch_events[i];if(i)o<<',';o<<"{\"tick\":"<<e.tick<<",\"substep\":"<<e.substep<<",\"type\":\""<<e.type<<"\",\"id\":\""<<e.id<<'"';if(e.type=="transition")o<<",\"from_surface\":\""<<e.from<<"\",\"to_surface\":\""<<e.to<<'"';o<<'}';}o<<"]}";return o.str();}
}
