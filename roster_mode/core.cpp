#include "core.hpp"
#include <algorithm>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
namespace roster {
namespace {
const char* boolean(bool b) {return b?"true":"false";}
std::uint32_t fnv(const std::string& s) {std::uint32_t h=2166136261u;for(unsigned char c:s){h^=c;h*=16777619u;}return h;}
int team(cap::Team t) {return t==cap::Team::Red?0:1;}
const char* degree(cap::ControlDegree d) {return d==cap::ControlDegree::Full?"full":"temporary";}
template<class T> T get(cap::Result<T> r) {if(auto e=std::get_if<cap::Error>(&r))throw std::invalid_argument(std::string(cap::error_name(*e)));return std::get<T>(std::move(r));}
bool point_wall(int x,int y,int radius) {
    for(auto w:sandbox::WALLS){I dx=x-std::clamp(x,w.x1,w.x2),dy=y-std::clamp(y,w.y1,w.y2);if(dx*dx+dy*dy<I(radius)*radius)return true;}return false;
}
// Conservative Q16 segment intersections. Coordinates and velocities are bounded
// before reaching here; every product is below 2^50, including relative motion.
int rect_hit(sandbox::Vec a,sandbox::Vec b,sandbox::Rect w) {
    I lo=0,hi=65536;
    for(int axis=0;axis<2;++axis){int s=axis?a.y:a.x,d=axis?b.y-a.y:b.x-a.x,mn=axis?w.y1:w.x1,mx=axis?w.y2:w.x2;
        if(!d){if(s<mn||s>mx)return -1;continue;}
        I q1=I(mn-s)*65536/d,q2=I(mx-s)*65536/d;if(q1>q2)std::swap(q1,q2);lo=std::max(lo,q1);hi=std::min(hi,q2);if(lo>hi)return -1;
    }return int(lo);
}
int circle_hit(sandbox::Vec a,sandbox::Vec b,sandbox::Vec c,int radius) {
    I dx=b.x-a.x,dy=b.y-a.y,len=dx*dx+dy*dy;
    auto inside=[&](int t){I x=I(a.x-c.x)+dx*t/65536,y=I(a.y-c.y)+dy*t/65536;return x*x+y*y<=I(radius)*radius;};
    if(inside(0))return 0;
    if(!len)return -1;
    I dot=I(c.x-a.x)*dx+I(c.y-a.y)*dy;int nearest=int(std::clamp(dot*65536/len,I{0},I{65536}));
    if(!inside(nearest))return -1;
    int lo=0,hi=nearest;while(lo+1<hi){int mid=(lo+hi)/2;if(inside(mid))hi=mid;else lo=mid;}return hi;
}
void profile_json(std::ostream& o,const Profile& p){
    o<<"{\"id\":\""<<p.id<<"\",\"max_hp\":"<<p.max_hp<<",\"ammo\":"<<p.ammo<<",\"radius_mm\":"<<p.radius_mm
     <<",\"speed_mm_tick\":"<<p.speed_mm_tick<<",\"projectile_speed_mm_tick\":"<<p.projectile_speed_mm_tick
     <<",\"projectile_damage\":"<<p.projectile_damage<<",\"cooldown_ticks\":"<<p.cooldown_ticks
     <<",\"sensor_range_mm\":"<<p.sensor_range_mm<<",\"heat_limit_tenths\":"<<p.heat_limit_tenths<<",\"cooling_per_second\":"<<p.cooling_per_second<<'}';
}
void roster_json(std::ostream& o,const std::vector<RosterEntry>& robots){o<<'[';for(std::size_t i=0;i<robots.size();++i){if(i)o<<',';const auto& r=robots[i];o<<"{\"id\":\""<<r.id<<"\",\"team\":"<<r.team<<",\"profile\":";profile_json(o,r.profile);o<<",\"spawn\":{\"x\":"<<r.spawn.x<<",\"y\":"<<r.spawn.y<<",\"yaw\":"<<r.spawn.yaw<<"},\"eligible\":"<<boolean(r.eligible)<<'}';}o<<']';}
void source_json(std::ostream& o,const Input& in){o<<"{\"id\":\""<<in.id<<"\",\"kind\":\""<<in.kind<<"\",\"sdk_version\":"<<in.sdk_version<<",\"controller_id\":";if(in.controller_id.empty())o<<"null";else o<<'"'<<in.controller_id<<'"';o<<'}';}
void frame_json(std::ostream& o,const Frame& f){o<<"{\"tick\":"<<f.tick<<",\"inputs\":[";for(std::size_t i=0;i<f.inputs.size();++i){if(i)o<<',';const auto& in=f.inputs[i];std::ostringstream s;source_json(s,in);auto text=s.str();text.pop_back();o<<text<<",\"action\":[";for(int j=0;j<5;++j){if(j)o<<',';o<<in.action[j];}o<<"],\"observation_hash\":"<<in.observation_hash<<'}';}o<<"],\"hash\":"<<f.hash<<'}';}
void frames_json(std::ostream& o,const std::vector<Frame>& frames){o<<'[';for(std::size_t i=0;i<frames.size();++i){if(i)o<<',';frame_json(o,frames[i]);}o<<']';}
policy::Action sdk_action(const Input& in){return {in.sdk_version,in.action[0],in.action[1],in.action[2],in.action[3],in.action[4]};}
sandbox::Action native_action(const policy::Action& a){return {int(std::get<I>(a.dx)),int(std::get<I>(a.dy)),int(std::get<I>(a.aim_x)),int(std::get<I>(a.aim_y)),int(std::get<I>(a.fire))};}
std::optional<std::string> valid_input(const Input& in){if(auto e=validate_identifier(in.id))return e;if(in.kind!="manual"&&in.kind!="policy")return "Input kind must be manual or policy.";if(in.kind=="manual"&&!in.controller_id.empty())return "Manual input cannot claim policy identity.";if(in.kind=="policy")if(auto e=validate_descriptor({in.sdk_version,in.controller_id}))return e;return policy::validate_action(sdk_action(in));}
}
Config::Config(){
    Profile standard;Profile scout{"SYNTHETIC-scout",80,90,240,65,300,8,9,5500,400,12};
    robots={{"blue-1",1,standard,{10400,2900,128},true},{"blue-2",1,scout,{10400,4700,128},true},{"red-1",0,standard,{1600,2900,0},true},{"red-2",0,scout,{1600,4700,0},true}};
}
std::string build_id(){return ROSTER_BUILD_ID;}
std::optional<std::string> validate_identifier(const std::string& id,std::size_t maximum){
    auto alnum=[](unsigned char c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9');};
    if(id.empty()||id.size()>maximum||!alnum(id[0]))return "Identifier must start alphanumeric and fit its ASCII length bound.";
    for(unsigned char c:id)if(!alnum(c)&&c!='_'&&c!='.'&&c!='-')return "Identifier contains unsupported characters.";
    return {};
}
std::optional<std::string> validate_descriptor(const policy::Descriptor& d){if(d.sdk_version!=1)return "Policy SDK must be integer1.";
    if(d.controller_id.empty() || !((d.controller_id[0]>='A'&&d.controller_id[0]<='Z') || (d.controller_id[0]>='a'&&d.controller_id[0]<='z')))return "Controller ID must start with an ASCII letter.";
    return validate_identifier(d.controller_id,64);}
std::optional<std::string> validate_config(const Config& c){
    if(c.boundary!=cap::BoundaryPolicy::StrictRejectUnadjudicated&&c.boundary!=cap::BoundaryPolicy::EngineeringTimerBeforeInputV1)return "Unsupported capture boundary policy.";
    if(c.heat_threshold!=heat::ThresholdPolicy::StrictRejectUnadjudicated&&c.heat_threshold!=heat::ThresholdPolicy::EngineeringProseStrictGreaterV1&&c.heat_threshold!=heat::ThresholdPolicy::EngineeringFlowchartGreaterEqualV1)return "Unsupported heat threshold policy.";
    if(c.robots.size()<2||c.robots.size()>8)return "Roster requires2..8 robots.";
    std::set<std::string> ids;std::array<int,2> counts{};
    for(const auto& r:c.robots){
        if(auto e=validate_identifier(r.id))return e;
        if(!ids.insert(r.id).second)return "Duplicate robot ID.";
        if(r.team<0||r.team>1)return "Team must be0 or1.";
        ++counts[std::size_t(r.team)];const auto& p=r.profile;
        if(auto e=validate_identifier(p.id))return e;
        if(p.id.rfind("SYNTHETIC-",0)!=0)return "Profile ID must start SYNTHETIC-.";
        const I values[]={p.max_hp,p.ammo,p.radius_mm,p.speed_mm_tick,p.projectile_speed_mm_tick,p.projectile_damage,p.cooldown_ticks,p.sensor_range_mm,p.heat_limit_tenths,p.cooling_per_second};
        const I lows[]={1,0,100,0,80,1,1,0,0,0},highs[]={10000,5000,500,100,1000,1000,120,20000,20000,1000};
        for(int j=0;j<10;++j)if(values[j]<lows[j]||values[j]>highs[j])return "SYNTHETIC profile integer outside safe bounds.";
        if(r.spawn.x<p.radius_mm||r.spawn.x>12000-p.radius_mm||r.spawn.y<p.radius_mm||r.spawn.y>7600-p.radius_mm||r.spawn.yaw<0||r.spawn.yaw>255)return "Invalid spawn bounds or yaw.";
        if(point_wall(int(r.spawn.x),int(r.spawn.y),int(p.radius_mm)))return "Spawn intersects a wall.";
    }
    if(counts[0]<1||counts[0]>4||counts[1]<1||counts[1]>4)return "Each team requires1..4 robots.";
    for(std::size_t i=0;i<c.robots.size();++i)for(std::size_t j=i+1;j<c.robots.size();++j){
        const auto& a=c.robots[i];const auto& b=c.robots[j];I dx=a.spawn.x-b.spawn.x,dy=a.spawn.y-b.spawn.y,rr=a.profile.radius_mm+b.profile.radius_mm;
        if(dx*dx+dy*dy<rr*rr)return "Spawn circles overlap.";
        if(a.profile.id==b.profile.id){std::ostringstream x,y;profile_json(x,a.profile);profile_json(y,b.profile);if(x.str()!=y.str())return "One profile ID cannot describe different values.";}
    }return {};
}
std::string config_json(const Config& c){std::ostringstream o;o<<"{\"boundary_policy\":\""<<cap::policy_name(c.boundary)<<"\",\"credit_strategy\":\""<<(c.credits?kCredit:"disabled")<<"\",\"attack_damage_strategy\":\""<<(c.damage_enabled?kDamage:kDamageMissing)<<"\",\"heat_strategy\":\""<<(c.heat_enabled?kHeat:"disabled")<<"\",\"heat_threshold_policy\":\""<<heat::policy_name(c.heat_threshold)<<"\",\"robots\":";auto robots=c.robots;std::sort(robots.begin(),robots.end(),[](const auto& a,const auto& b){return a.id<b.id;});roster_json(o,robots);o<<'}';return o.str();}
Engine::Engine(Config c){initialize(std::move(c));}
void Engine::reset(Config c){Engine candidate(std::move(c));*this=std::move(candidate);}
void Engine::initialize(Config c){
    if(auto e=validate_config(c))throw std::invalid_argument(*e);
    std::sort(c.robots.begin(),c.robots.end(),[](const auto& a,const auto& b){return a.id<b.id;});config_=std::move(c);
    state={};events.clear();journal.clear();frames.clear();zones_.clear();heat_.clear();expected_hp_.clear();blocked_fire_ticks_.assign(config_.robots.size(),0);
    ledger_={};judge_={};decision_={};unresolved_.reset();damage_={};damage_complete_=config_.damage_enabled;nominal_projectile_damage_={};
    for(const auto& r:config_.robots){
        state.robots.push_back({int(r.spawn.x),int(r.spawn.y),int(r.spawn.yaw),int(r.profile.max_hp),int(r.profile.ammo)});expected_hp_.push_back(int(r.profile.max_hp));
        heat::Config hc{r.profile.heat_limit_tenths,r.profile.cooling_per_second,config_.heat_threshold,heat::BoundaryPolicy::EngineeringCoolingBeforeShotV1,heat::DeathPolicy::EngineeringShotBeforeDeathResetV1};
        heat_.push_back(std::get<heat::Heat>(heat::Heat::start(hc)));
    }
    for(int z=0;z<4;++z){std::vector<cap::ActivationDelay> detectors;for(std::size_t i=0;i<config_.robots.size();++i)detectors.push_back(get(cap::ActivationDelay::start(0,cap::QualifiedDetection::Absent,config_.boundary)));
        auto owner=z%2?cap::Team::Blue:cap::Team::Red;auto kind=z<2?cap::PositionKind::Outpost:cap::PositionKind::Base;
        zones_.push_back({std::move(detectors),get(cap::CaptureAttempt::start(0,get(cap::initial_control(kind,owner)),{false,false},config_.boundary)),{}});
    }initial_hash_=hash();
}
std::size_t Engine::index(const std::string& id)const{const auto it=std::lower_bound(config_.robots.begin(),config_.robots.end(),id,[](const auto& r,const auto& key){return r.id<key;});if(it==config_.robots.end()||it->id!=id)throw std::out_of_range("Unknown robot ID.");return std::size_t(it-config_.robots.begin());}
match::ControlSnapshot Engine::control()const{auto r=match::initial_control();for(int z=0;z<4;++z){const auto c=zones_[z].capture.snapshot().control;r.positions[z]={c.controller==cap::Team::Red?match::Team::Red:match::Team::Blue,c.degree==cap::ControlDegree::Full?match::ControlDegree::Full:match::ControlDegree::Temporary};}return r;}
std::optional<std::string> Engine::advance_rules(I end,const std::vector<bool>& died){
    std::vector<I> boundaries{end};for(const auto& z:zones_)for(const auto& d:z.detectors){const auto deadline=d.snapshot().deactivation_at_ms;if(deadline&&*deadline<end)boundaries.push_back(*deadline);}
    std::sort(boundaries.begin(),boundaries.end());boundaries.erase(std::unique(boundaries.begin(),boundaries.end()),boundaries.end());
    for(I t:boundaries)for(int zi=0;zi<4;++zi){auto& z=zones_[zi];const auto rect=objective::ZONES[zi];
        for(std::size_t i=0;i<state.robots.size();++i){auto& detector=z.detectors[i];const auto& r=state.robots[i];const auto& entry=config_.robots[i];
            if(t==end&&died[i]){detector=get(cap::ActivationDelay::start(t,cap::QualifiedDetection::Absent,config_.boundary));continue;}
            const bool physical=entry.eligible&&r.hp>0&&r.x>=rect.x1&&r.x<=rect.x2&&r.y>=rect.y1&&r.y<=rect.y2;
            const auto target=physical?cap::QualifiedDetection::Present:cap::QualifiedDetection::Absent;std::optional<cap::QualifiedDetection> edge;
            if(t==end&&target!=detector.snapshot().detection)edge=target;
            const auto result=detector.apply(t,edge);if(auto e=std::get_if<cap::Error>(&result))return std::string(objective::IDS[zi])+" activation: "+std::string(cap::error_name(*e));
        }
        if(z.capture.snapshot().completed)continue;
        std::array<bool,2> active{};
        // Individual eligibility/death clearing is upstream of OR. Grace is never
        // applied to the resulting team input, so no double extension is possible.
        for(std::size_t i=0;i<state.robots.size();++i)if(z.detectors[i].snapshot().logically_active)active[std::size_t(config_.robots[i].team)]=true;
        auto before=z.capture.snapshot();cap::LogicalActivation logical{active[0],active[1]};std::optional<cap::LogicalActivation> input;if(logical!=before.active)input=logical;
        const bool challenger=before.control.controller==cap::Team::Red?before.active.blue:before.active.red;
        const bool progressing=challenger&&!(before.active.red&&before.active.blue);const I deadline=before.time_ms+cap::CAPTURE_DURATION_MS-before.progress_ms;
        if(progressing&&deadline<t){auto result=z.capture.apply(deadline);if(auto e=std::get_if<cap::Error>(&result))return std::string(cap::error_name(*e));z.completed_at_ms=deadline;}
        else {auto result=z.capture.apply(t,input);if(auto e=std::get_if<cap::Error>(&result))return std::string(objective::IDS[zi])+" capture: "+std::string(cap::error_name(*e));const auto& after=std::get<cap::CaptureAdvance>(result);if(after.state.completed)z.completed_at_ms=after.state.time_ms;}
    }return {};
}
std::optional<std::string> Engine::reconcile_damage(const std::vector<int>& before){
    if(!config_.damage_enabled)return {};
    if(before!=expected_hp_)return "Unreconciled HP baseline; batch unchanged.";
    std::vector<I> incoming(state.robots.size());std::vector<int> disabled(state.robots.size());
    for(const auto& e:events){if(e.tick!=state.tick-1||e.actor<0||std::size_t(e.actor)>=state.robots.size())return "Invalid native event attribution.";
        if(e.kind==2){if(e.target<0||std::size_t(e.target)>=state.robots.size()||config_.robots[e.actor].team==config_.robots[e.target].team||e.amount!=config_.robots[e.actor].profile.projectile_damage)return "Invalid opposing projectile deduction.";incoming[e.target]+=e.amount;}
        else if(e.kind==3){if(e.target!=e.actor||e.amount)return "Invalid disable event.";++disabled[e.target];}
        else if((e.kind!=1&&e.kind!=4)||e.target!=-1||e.amount)return "Unknown native event.";
    }
    std::vector<damage::SettledDeduction> deductions;
    for(std::size_t i=0;i<state.robots.size();++i){const auto hp=state.robots[i].hp;if(before[i]<0||before[i]>config_.robots[i].profile.max_hp||hp<0||hp>before[i]||I(before[i])-hp!=std::min<I>(before[i],incoming[i])||disabled[i]!=(before[i]>0&&hp==0?1:0))return "Projectile HP settlement failed reconciliation.";
        const auto side=config_.robots[i].team;nominal_projectile_damage_[std::size_t(1-side)]+=incoming[i];
        if(hp<before[i])deductions.push_back({damage_.last_sequence()+deductions.size()+1,time_ms(state.tick),std::uint32_t(i),side?damage::Team::Blue:damage::Team::Red,damage::EntityKind::Robot,damage::Cause::Projectile,before[i],hp,side?damage::Team::Red:damage::Team::Blue});
        expected_hp_[i]=hp;
    }
    const auto result=damage_.apply_batch(deductions);if(auto e=std::get_if<damage::Error>(&result))return std::string(damage::error_name(*e));return {};
}
std::optional<std::array<I,2>> Engine::remaining_robot_hp()const{std::vector<match::RemainingHpFact> facts;for(std::size_t i=0;i<state.robots.size();++i){const auto hp=state.robots[i].hp;if(hp<0||hp>config_.robots[i].profile.max_hp)return {};facts.push_back({std::uint32_t(i),config_.robots[i].team?match::Team::Blue:match::Team::Red,match::EntityKind::Robot,hp,true,false});}const auto r=match::surviving_robot_hp(facts);if(auto hp=std::get_if<std::array<I,2>>(&r))return *hp;return {};}
std::optional<std::string> Engine::step_unchecked(const std::vector<sandbox::Action>& actions){
    if(state.terminal)return "Roster run terminal; reset to continue.";
    const auto n=state.robots.size();const auto old=state.robots;auto moved=old;auto effective=actions;const I t=time_ms(state.tick+1);events.clear();
    if(config_.heat_enabled)for(std::size_t i=0;i<n;++i){auto cooled=heat_[i];const auto result=cooled.apply({t,0,false});if(auto e=std::get_if<heat::Error>(&result))return std::string(heat::error_name(*e));if(cooled.snapshot().firing_locked()&&effective[i].fire){effective[i].fire=0;++blocked_fire_ticks_[i];}}
    for(std::size_t i=0;i<n;++i){auto& r=moved[i];if(r.hp<=0)continue;const auto& p=config_.robots[i].profile;const auto a=effective[i];if(r.cooldown>0)--r.cooldown;
        const auto len=sandbox::isqrt(std::uint64_t(I(a.dx)*a.dx+I(a.dy)*a.dy));const I div=I(std::max(std::uint64_t{1000},len));const int vx=int(I(a.dx)*p.speed_mm_tick/div),vy=int(I(a.dy)*p.speed_mm_tick/div),radius=int(p.radius_mm);
        const int nx=std::clamp(r.x+vx,radius,12000-radius);if(!point_wall(nx,r.y,radius))r.x=nx;const int ny=std::clamp(r.y+vy,radius,7600-radius);if(!point_wall(r.x,ny,radius))r.y=ny;
        const int desired=sandbox::aim_direction({r.x,r.y},{a.aim_x,a.aim_y});const int diff=(desired-r.yaw+384)%256-128;r.yaw=(r.yaw+std::clamp(diff,-4,4)+256)%256;
    }
    // Iterative closure: rejecting a mover can obstruct another robot's accepted
    // path. All conflicts in a pass are marked before any reversion.
    for(std::size_t pass=0;pass<n;++pass){std::vector<bool> reject(n);bool conflict=false;
        for(std::size_t i=0;i<n;++i)for(std::size_t j=i+1;j<n;++j){const bool mi=moved[i].x!=old[i].x||moved[i].y!=old[i].y,mj=moved[j].x!=old[j].x||moved[j].y!=old[j].y;if(!mi&&!mj)continue;
            sandbox::Vec a{old[i].x-old[j].x,old[i].y-old[j].y},b{moved[i].x-moved[j].x,moved[i].y-moved[j].y};
            const I radius=config_.robots[i].profile.radius_mm+config_.robots[j].profile.radius_mm;
            const bool separating=I(a.x)*(b.x-a.x)+I(a.y)*(b.y-a.y)>=0 && I(a.x)*a.x+I(a.y)*a.y>=radius*radius;
            if(!separating && circle_hit(a,b,{0,0},int(radius))>=0){reject[i]=mi;reject[j]=mj;conflict=true;}}
        if(!conflict)break;
        for(std::size_t i=0;i<n;++i)if(reject[i]){moved[i].x=old[i].x;moved[i].y=old[i].y;}
    }
    state.robots=std::move(moved);
    std::vector<I> shots(n),incoming(n);std::vector<int> hp_before;for(const auto& r:old)hp_before.push_back(r.hp);
    for(std::size_t i=0;i<n;++i){auto& r=state.robots[i];const auto& p=config_.robots[i].profile;
        if(r.hp>0&&effective[i].fire&&r.cooldown==0&&r.ammo>0){if(state.bullets.size()>=2048)return "Projectile capacity exceeded; batch unchanged.";
            const auto d=sandbox::direction(r.yaw);const int offset=int(p.radius_mm)+40;Bullet b{state.next_id++,int(i),r.x+d.x*offset/1000,r.y+d.y*offset/1000,int(d.x*p.projectile_speed_mm_tick/1000),int(d.y*p.projectile_speed_mm_tick/1000),180,int(p.projectile_damage)};
            --r.ammo;++r.shots;++shots[i];r.cooldown=int(p.cooldown_ticks);events.push_back({state.tick,1,int(i),-1,0});
            if(sandbox::line_blocked({r.x,r.y},{b.x,b.y}))events.push_back({state.tick,4,int(i),-1,0});else state.bullets.push_back(b);
        }
    }
    std::vector<Bullet> surviving;
    for(auto b:state.bullets){sandbox::Vec a{b.x,b.y},end{b.x+b.vx,b.y+b.vy};int first=65537,target=-1;
        for(auto wall:sandbox::WALLS){const int hit=rect_hit(a,end,wall);if(hit>=0&&hit<first){first=hit;target=-1;}}
        for(std::size_t j=0;j<n;++j)if(config_.robots[j].team!=config_.robots[std::size_t(b.owner)].team&&state.robots[j].hp>0){const auto& r=state.robots[j];int hit=circle_hit(a,end,{r.x,r.y},int(config_.robots[j].profile.radius_mm)+30);if(hit>=0&&hit<first){first=hit;target=int(j);}}
        if(first<=65536){if(target>=0){incoming[std::size_t(target)]+=b.damage;++state.robots[std::size_t(b.owner)].hits;events.push_back({state.tick,2,b.owner,target,b.damage});}else events.push_back({state.tick,4,b.owner,-1,0});continue;}
        b.x=end.x;b.y=end.y;--b.life;if(b.life>0&&b.x>=0&&b.x<=12000&&b.y>=0&&b.y<=7600)surviving.push_back(b);
    }
    state.bullets=std::move(surviving);std::vector<bool> died(n);
    for(std::size_t i=0;i<n;++i){auto& r=state.robots[i];r.hp=int(std::max<I>(0,I(r.hp)-incoming[i]));died[i]=old[i].hp>0&&r.hp==0;if(died[i])events.push_back({state.tick,3,int(i),int(i),0});
        if(config_.heat_enabled){const auto result=heat_[i].apply({t,shots[i],died[i]});if(auto e=std::get_if<heat::Error>(&result))return "Heat: "+std::string(heat::error_name(*e))+"; batch unchanged.";}
    }
    ++state.tick;state.terminal=state.tick==kDurationTicks;
    if(auto e=reconcile_damage(hp_before))return e;
    if(auto e=advance_rules(t,died))return e;
    if(config_.credits&&state.tick%60==0){const auto result=ledger_.apply({ledger_.last_sequence()+1,control()});if(auto e=std::get_if<match::Error>(&result))return std::string(match::error_name(*e));}
    const auto totals=ledger_.totals();const auto hp=remaining_robot_hp();const auto attack=damage_.totals().attack_damage;match::MatchSnapshot snapshot;snapshot.elapsed_ms=t;
    for(int side=0;side<2;++side)snapshot.teams[side]={1,config_.credits?std::optional<I>{totals[side]}:std::nullopt,damage_complete_?std::optional<I>{attack[side]}:std::nullopt,hp?std::optional<I>{(*hp)[side]}:std::nullopt};
    const auto result=judge_.update(snapshot);if(auto e=std::get_if<match::Error>(&result)){if(t==match::MATCH_DURATION_MS&&(*e==match::Error::MissingAttackDamage||*e==match::Error::MissingControlScore||*e==match::Error::MissingSurvivingRobotHp))unresolved_=*e;else return std::string(match::error_name(*e));}else decision_=std::get<match::Decision>(result);
    if(state.terminal){ledger_.seal();damage_.seal();}return {};
}
policy::Observation Engine::observation(const std::string& id)const{
    const auto i=index(id);const auto& r=state.robots[i];const auto& entry=config_.robots[i];const I range=entry.profile.sensor_range_mm;
    const auto visible=[&](int x,int y){const I dx=I(x)-r.x,dy=I(y)-r.y;return dx*dx+dy*dy<=range*range&&!sandbox::line_blocked({r.x,r.y},{x,y});};
    policy::Arena arena;for(const auto& w:sandbox::WALLS)arena.walls.push_back({w.x1,w.y1,w.x2,w.y2});
    std::vector<policy::Contact> contacts;for(std::size_t j=0;j<state.robots.size();++j)if(j!=i){const auto& other=state.robots[j];if(visible(other.x,other.y))contacts.push_back({config_.robots[j].id,config_.robots[j].team,other.x,other.y,other.yaw});}
    std::vector<policy::Projectile> projectiles;for(const auto& b:state.bullets)if(visible(b.x,b.y))projectiles.push_back({b.x,b.y,b.vx,b.vy});
    std::sort(projectiles.begin(),projectiles.end(),[](const auto& a,const auto& b){return std::tie(a.x,a.y,a.vx,a.vy)<std::tie(b.x,b.y,b.vx,b.vy);});
    const auto& q=heat_[i].snapshot();const auto& hc=heat_[i].config();policy::SelfHeat own{config_.heat_enabled,q.heat_tenths,hc.limit_tenths,hc.cooling_per_second,q.overheat_locked,q.permanent_latched,q.firing_locked()};
    std::array<policy::Zone,4> zones;for(int z=0;z<4;++z){const auto b=objective::ZONES[z];const auto c=zones_[z].capture.snapshot().control;zones[z]={objective::IDS[z],{b.x1,b.y1,b.x2,b.y2},team(c.original_owner),team(c.controller),degree(c.degree)};}
    const auto rate=std::get<match::ControlRate>(match::control_rate(control()));
    return {1,1,60,state.tick,id,entry.team,range,{r.x,r.y,r.yaw,r.hp,r.ammo,r.cooldown},std::move(arena),config_.robots,std::move(contacts),std::move(projectiles),own,std::move(zones),ledger_.totals(),rate.points_per_second,time_ms(state.tick),420000,config_.credits};
}
std::string observation_json(const policy::Observation& obs){
    std::ostringstream o;const auto& r=obs.self;
    o<<"{\"sdk_version\":1,\"observation_version\":1,\"profile\":\""<<policy::kObservationProfile<<"\",\"hz\":60,\"tick\":"<<obs.tick<<",\"self_id\":\""<<obs.self_id<<"\",\"self_team\":"<<obs.self_team<<",\"sensor_range_mm\":"<<obs.sensor_range_mm
     <<",\"self\":{\"x\":"<<r.x<<",\"y\":"<<r.y<<",\"yaw\":"<<r.yaw<<",\"hp\":"<<r.hp<<",\"ammo\":"<<r.ammo<<",\"cooldown\":"<<r.cooldown<<"},\"arena\":{\"width\":"<<obs.arena.width<<",\"height\":"<<obs.arena.height<<",\"walls\":[";
    for(std::size_t i=0;i<obs.arena.walls.size();++i){if(i)o<<',';const auto w=obs.arena.walls[i];o<<"{\"x1\":"<<w.x1<<",\"y1\":"<<w.y1<<",\"x2\":"<<w.x2<<",\"y2\":"<<w.y2<<'}';}o<<"]},\"roster\":";roster_json(o,obs.roster);o<<",\"contacts\":[";
    for(std::size_t i=0;i<obs.contacts.size();++i){if(i)o<<',';const auto& c=obs.contacts[i];o<<"{\"id\":\""<<c.id<<"\",\"team\":"<<c.team<<",\"x\":"<<c.x<<",\"y\":"<<c.y<<",\"yaw\":"<<c.yaw<<'}';}
    o<<"],\"projectiles\":[";for(std::size_t i=0;i<obs.projectiles.size();++i){if(i)o<<',';const auto b=obs.projectiles[i];o<<"{\"x\":"<<b.x<<",\"y\":"<<b.y<<",\"vx\":"<<b.vx<<",\"vy\":"<<b.vy<<'}';}
    const auto q=obs.self_heat;o<<"],\"self_heat\":{\"enabled\":"<<boolean(q.enabled)<<",\"heat_tenths\":"<<q.heat_tenths<<",\"limit_tenths\":"<<q.limit_tenths<<",\"cooling_per_second\":"<<q.cooling_per_second<<",\"overheat_locked\":"<<boolean(q.overheat_locked)<<",\"permanent_latched\":"<<boolean(q.permanent_latched)<<",\"firing_locked\":"<<boolean(q.firing_locked)<<"},\"zones\":[";
    for(std::size_t i=0;i<obs.zones.size();++i){if(i)o<<',';const auto& z=obs.zones[i];o<<"{\"id\":\""<<z.id<<"\",\"rect\":{\"x1\":"<<z.rect.x1<<",\"y1\":"<<z.rect.y1<<",\"x2\":"<<z.rect.x2<<",\"y2\":"<<z.rect.y2<<"},\"original_owner\":"<<z.original_owner<<",\"controller\":"<<z.controller<<",\"degree\":\""<<z.degree<<"\"}";}
    o<<"],\"score\":";if(obs.credits_enabled)o<<'['<<obs.score[0]<<','<<obs.score[1]<<']';else o<<"null";
    o<<",\"rate\":["<<obs.rate[0]<<','<<obs.rate[1]<<"],\"qualified_ms\":"<<obs.qualified_ms<<",\"duration_ms\":"<<obs.duration_ms<<",\"credits_enabled\":"<<boolean(obs.credits_enabled)<<'}';return o.str();
}
std::uint32_t observation_hash(const policy::Observation& o){return fnv(observation_json(o));}
std::string policy_metadata_json(const policy::Descriptor& d){if(auto e=validate_descriptor(d))throw std::invalid_argument(*e);std::ostringstream o;o<<"{\"sdk_version\":"<<d.sdk_version<<",\"controller_id\":\""<<d.controller_id<<"\",\"execution\":\"trusted-local-native\",\"observation\":\""<<policy::kObservationProfile<<"\",\"state_mode\":\"stateless\",\"decision_hz\":60}";return o.str();}
std::optional<std::string> Engine::advance(I count,const std::vector<Request>& request_inputs,const std::vector<Controller>& controllers){
    if(count<1||count>kMaxBatch)return "Step count outside [1,600].";
    if(state.terminal)return "Roster run terminal; reset to continue.";
    if(request_inputs.size()!=config_.robots.size())return "Exactly one request per robot ID is required.";
    auto requests=request_inputs;std::sort(requests.begin(),requests.end(),[](const auto& a,const auto& b){return a.id<b.id;});std::vector<const Controller*> selected(requests.size());std::set<std::string> used;
    for(std::size_t i=0;i<requests.size();++i){const auto& r=requests[i];if(r.id!=config_.robots[i].id)return "Request IDs differ from canonical roster membership.";
        if(auto e=policy::validate_action(r.action))return e;
        if(r.kind=="policy"){for(const auto& c:controllers)if(c.id==r.id){if(selected[i])return "Duplicate controller binding.";selected[i]=&c;}
            if(!selected[i]||!selected[i]->decide)return "Missing controller callback for robot ID.";
            if(auto e=validate_descriptor(selected[i]->descriptor))return e;
            used.insert(r.id);
        }else if(r.kind!="manual")return "Input kind must be manual or policy.";
    }
    if(used.size()!=controllers.size())return "Unexpected or duplicate controller binding.";
    try{Engine candidate=*this;candidate.frames.clear();
        for(I tick=0;tick<count&&!candidate.state.terminal;++tick){
            std::vector<policy::Observation> observations;observations.reserve(requests.size());for(const auto& r:requests)observations.push_back(candidate.observation(r.id));
            std::vector<policy::Action> actions;actions.reserve(requests.size());for(std::size_t i=0;i<requests.size();++i)actions.push_back(selected[i]?selected[i]->decide(observations[i]):requests[i].action);
            for(const auto& a:actions)if(auto e=policy::validate_action(a))return e;
            Frame frame;frame.tick=candidate.state.tick;std::vector<sandbox::Action> native;native.reserve(actions.size());
            for(std::size_t i=0;i<requests.size();++i){const auto& a=actions[i];frame.inputs.push_back({requests[i].id,requests[i].kind,1,selected[i]?selected[i]->descriptor.controller_id:"",{std::get<I>(a.dx),std::get<I>(a.dy),std::get<I>(a.aim_x),std::get<I>(a.aim_y),std::get<I>(a.fire)},observation_hash(observations[i])});native.push_back(native_action(a));}
            if(auto e=candidate.step_unchecked(native))return e;
            frame.hash=candidate.hash();candidate.journal.push_back(frame);candidate.frames.push_back(std::move(frame));
        }*this=std::move(candidate);return {};
    }catch(...){return "Policy exception; entire batch unchanged.";}
}
std::optional<std::string> Engine::replay_frame(const Frame& f){return replay_frames({f});}
std::optional<std::string> Engine::replay_frames(const std::vector<Frame>& expected){
    if(expected.empty()||expected.size()>std::size_t(kMaxBatch))return "Replay batch count outside [1,600].";
    try{Engine candidate=*this;candidate.frames.clear();for(auto f:expected){
        if(f.tick!=candidate.state.tick||candidate.state.terminal)return "Replay tick is not next live tick; batch unchanged.";
        if(f.inputs.size()!=config_.robots.size())return "Replay input membership differs from roster.";
        std::sort(f.inputs.begin(),f.inputs.end(),[](const auto& a,const auto& b){return a.id<b.id;});std::vector<sandbox::Action> actions;
        for(std::size_t i=0;i<f.inputs.size();++i){const auto& input=f.inputs[i];if(input.id!=config_.robots[i].id)return "Replay robot IDs differ from roster.";if(auto e=valid_input(input))return e;if(input.observation_hash!=observation_hash(candidate.observation(input.id)))return "Replay observation hash mismatch; batch unchanged.";actions.push_back(native_action(sdk_action(input)));}
        if(auto e=candidate.step_unchecked(actions))return e;
        if(f.hash!=candidate.hash())return "Replay hidden-state hash mismatch; batch unchanged.";
        candidate.journal.push_back(f);candidate.frames.push_back(std::move(f));
    }*this=std::move(candidate);return {};}catch(...){return "Replay exception; entire batch unchanged.";}
}
std::uint32_t Engine::hash()const{
    std::uint32_t h=2166136261u;auto put=[&](I v){auto u=std::uint64_t(v);for(int i=0;i<8;++i){h^=(u>>(i*8))&255u;h*=16777619u;}};
    put(1);put(fnv(build_id()));put(fnv(kEngine));put(fnv(kReplaySchema));put(fnv(policy::kObservationProfile));put(fnv(config_json(config_)));put(state.tick);put(state.next_id);put(state.terminal);put(state.robots.size());
    for(const auto& r:state.robots){put(r.x);put(r.y);put(r.yaw);put(r.hp);put(r.ammo);put(r.cooldown);put(r.shots);put(r.hits);}
    put(state.bullets.size());for(const auto& b:state.bullets){put(b.id);put(b.owner);put(b.x);put(b.y);put(b.vx);put(b.vy);put(b.life);put(b.damage);}
    for(std::size_t i=0;i<heat_.size();++i){const auto q=heat_[i].snapshot();put(q.time_ms);put(q.heat_tenths);put(q.overheat_locked);put(q.permanent_latched);put(q.detected_shots);put(q.last_event_ms);put(blocked_fire_ticks_[i]);}
    for(const auto& z:zones_){for(const auto& d:z.detectors){const auto s=d.snapshot();put(s.time_ms);put(int(s.detection));put(s.logically_active);put(s.deactivation_at_ms.value_or(-1));}const auto c=z.capture.snapshot();put(c.time_ms);put(int(c.control.kind));put(team(c.control.original_owner));put(team(c.control.controller));put(int(c.control.degree));put(c.active.red);put(c.active.blue);put(c.progress_ms);put(c.completed);put(z.completed_at_ms.value_or(-1));}
    put(ledger_.totals()[0]);put(ledger_.totals()[1]);put(ledger_.last_sequence());put(judge_.last_elapsed_ms());
    const auto& totals=damage_.totals();for(const auto* values:{&totals.attack_damage,&totals.actual_hp_loss,&totals.excluded_hp_loss,&totals.penalty_attack_damage,&nominal_projectile_damage_})for(I value:*values)put(value);
    put(damage_complete_);put(damage_.last_sequence());put(damage_.last_time_ms());put(damage_.sealed());for(int hp:expected_hp_)put(hp);
    put(int(decision_.outcome));put(int(decision_.reason));put(unresolved_?int(*unresolved_):-1);return h;
}
std::string Engine::result_json() const {
    if(unresolved_==match::Error::MissingAttackDamage) return "{\"outcome\":\"unresolved-timeout-tie\",\"reason\":\"missing-adjudicated-attack-damage\"}";
    if(unresolved_==match::Error::MissingControlScore) return "{\"outcome\":\"unscored-timeout\",\"reason\":\"credit-strategy-disabled\"}";
    if(unresolved_==match::Error::MissingSurvivingRobotHp) return "{\"outcome\":\"unresolved-timeout-tie\",\"reason\":\"missing-adjudicated-surviving-robot-hp\"}";
    const char* outcome="ongoing";
    switch(decision_.outcome) {
    case match::Outcome::RedWins:outcome="red-wins";break;
    case match::Outcome::BlueWins:outcome="blue-wins";break;
    case match::Outcome::Draw:outcome="draw";break;
    case match::Outcome::Ongoing:break;
    }
    const char* reason="not-ended";
    switch(decision_.reason) {
    case match::Reason::TimeoutControl:reason="timeout-control";break;
    case match::Reason::TimeoutAttackDamage:reason="timeout-attack-damage";break;
    case match::Reason::TimeoutSurvivingRobotHp:reason="timeout-surviving-robot-hp";break;
    case match::Reason::TimeoutEqual:reason="timeout-equal";break;
    default:break;
    }
    return std::string("{\"outcome\":\"")+outcome+"\",\"reason\":\""+reason+"\"}";
}
std::string Engine::damage_admin_json() const {
    std::ostringstream o;
    const auto array=[&](const auto& a) {o<<'['<<a[0]<<','<<a[1]<<']';};
    o<<"{\"schema\":1,\"module\":\""<<damage::MODULE_VERSION<<"\",\"strategy\":\""
     <<(!config_.damage_enabled ? kDamageMissing : kDamage)
     <<"\",\"complete\":"<<boolean(damage_complete_)<<",\"reason\":";
    if(damage_complete_) o<<"null";
    else o<<'"'<<(!config_.damage_enabled ? kDamageMissing : "unadjudicated-native-damage")<<'"';
    const auto& totals=damage_.totals();
    o<<",\"attack_damage\":";if(damage_complete_)array(totals.attack_damage);else o<<"null";
    o<<",\"remaining_robot_hp\":";const auto hp=remaining_robot_hp();if(hp)array(*hp);else o<<"null";
    o<<",\"actual_hp_loss\":";array(totals.actual_hp_loss);
    o<<",\"excluded_hp_loss\":";array(totals.excluded_hp_loss);
    o<<",\"penalty_attack_damage\":";array(totals.penalty_attack_damage);
    o<<",\"nominal_projectile_damage\":";array(nominal_projectile_damage_);
    o<<",\"last_sequence\":"<<damage_.last_sequence()<<'}';
    return o.str();
}
std::string Engine::heat_admin_json()const{std::ostringstream o;o<<"{\"schema\":1,\"module\":\""<<heat::MODULE_VERSION<<"\",\"strategy\":\""<<(config_.heat_enabled?kHeat:"disabled")<<"\",\"threshold_policy\":\""<<heat::policy_name(config_.heat_threshold)<<"\",\"boundary_policy\":\""<<kHeatBoundary<<"\",\"robots\":[";for(std::size_t i=0;i<heat_.size();++i){if(i)o<<',';const auto q=heat_[i].snapshot();o<<"{\"id\":\""<<config_.robots[i].id<<"\",\"heat_tenths\":"<<q.heat_tenths<<",\"overheat_locked\":"<<boolean(q.overheat_locked)<<",\"permanent_latched\":"<<boolean(q.permanent_latched)<<",\"firing_locked\":"<<boolean(q.firing_locked())<<",\"detected_shots\":"<<q.detected_shots<<",\"blocked_fire_ticks\":"<<blocked_fire_ticks_[i]<<'}';}o<<"]}";return o.str();}
std::string Engine::json(const std::string& observer,bool include_frames)const{
    const auto obs=observation(observer);std::ostringstream o;o<<"{\"build\":\""<<build_id()<<"\",\"schema\":1,\"profile\":\""<<kProfile<<"\",\"observation_profile\":\""<<policy::kObservationProfile<<"\",\"observation_version\":1,\"official\":false,\"observer\":\""<<observer<<"\",\"tick\":"<<state.tick<<",\"terminal\":"<<state.terminal<<",\"hash\":"<<hash()<<",\"config\":"<<config_json(config_)<<",\"result\":"<<result_json()<<",\"damage_admin\":"<<damage_admin_json()<<",\"heat_admin\":"<<heat_admin_json()<<",\"observation\":"<<observation_json(obs)<<",\"last_sources\":";
    if(journal.empty())o<<"null";else{o<<'[';for(std::size_t i=0;i<journal.back().inputs.size();++i){if(i)o<<',';source_json(o,journal.back().inputs[i]);}o<<']';}o<<",\"frames\":";if(include_frames)frames_json(o,frames);else o<<"[]";o<<'}';return o.str();
}
std::string Engine::replay_json()const{std::ostringstream o;o<<"{\"schema\":\""<<kReplaySchema<<"\",\"profile\":\""<<kProfile<<"\",\"build\":\""<<build_id()<<"\",\"engine\":\""<<kEngine<<"\",\"observation_profile\":\""<<policy::kObservationProfile<<"\",\"observation_version\":1,\"official\":false,\"config\":"<<config_json(config_)<<",\"initial_hash\":"<<initial_hash_<<",\"frames\":";frames_json(o,journal);o<<",\"final_hash\":"<<hash()<<'}';return o.str();}
std::string meta_json(){std::ostringstream o;o<<"{\"schema\":1,\"profile\":\""<<kProfile<<"\",\"build\":\""<<build_id()<<"\",\"engine\":\""<<kEngine<<"\",\"observation_profile\":\""<<policy::kObservationProfile<<"\",\"observation_version\":1,\"official\":false,\"hz\":60,\"duration\":25200,\"duration_ms\":420000,\"max_batch\":600,\"default_config\":"<<config_json({})<<",\"capture_module\":\""<<cap::MODULE_VERSION<<"\",\"match_module\":\""<<match::MODULE_VERSION<<"\",\"damage_module\":\""<<damage::MODULE_VERSION<<"\",\"heat_module\":\""<<heat::MODULE_VERSION<<"\"}";return o.str();}
} // namespace roster
