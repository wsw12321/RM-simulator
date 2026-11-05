#include "objective.hpp"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <variant>
namespace objective {
namespace {
template<class T> T get(cap::Result<T> r) {
    if(auto e=std::get_if<cap::Error>(&r))throw std::invalid_argument(std::string(cap::error_name(*e)));
    return std::get<T>(std::move(r));
}
bool valid(sandbox::Action a) {
    return a.dx>=-1000&&a.dx<=1000&&a.dy>=-1000&&a.dy<=1000&&a.aim_x>=0&&a.aim_x<=sandbox::WIDTH&&a.aim_y>=0&&a.aim_y<=sandbox::HEIGHT&&a.fire==0;
}
const char* boolean(bool b) { return b?"true":"false"; }
const char* degree(cap::ControlDegree d) { return d==cap::ControlDegree::Full?"full":"temporary"; }
int team(cap::Team t) { return t==cap::Team::Red?0:1; }
const char* outcome(match::Outcome o) {
    switch(o) {case match::Outcome::Ongoing:return "ongoing";case match::Outcome::RedWins:return "red-wins";case match::Outcome::BlueWins:return "blue-wins";case match::Outcome::Draw:return "draw";}
    return "invalid";
}
const char* reason(match::Reason r) {
    switch(r) {case match::Reason::NotEnded:return "not-ended";case match::Reason::BaseHp:return "base-hp";case match::Reason::TimeoutControl:return "timeout-control";case match::Reason::TimeoutAttackDamage:return "timeout-attack-damage";case match::Reason::TimeoutSurvivingRobotHp:return "timeout-surviving-robot-hp";case match::Reason::TimeoutEqual:return "timeout-equal";case match::Reason::ResolvedBothBasesZero:return "resolved-both-bases-zero";}
    return "invalid";
}
void config_json(std::ostream& o,const Config& c) {
    o<<"\"boundary_policy\":\""<<cap::policy_name(c.boundary)<<"\",\"credit_strategy\":\""<<(c.credits?CREDIT:"disabled")<<"\",\"lifecycle_strategy\":\""<<LIFECYCLE<<"\",\"clock_strategy\":\""<<CLOCK<<'"';
}
}
Engine::Engine(Config c):config_(c) {
    for(int z=0;z<4;z++) {
        auto owner=z%2?cap::Team::Blue:cap::Team::Red;
        auto kind=z<2?cap::PositionKind::Outpost:cap::PositionKind::Base;
        std::array<cap::ActivationDelay,2> detectors{
            get(cap::ActivationDelay::start(0,cap::QualifiedDetection::Absent,c.boundary)),
            get(cap::ActivationDelay::start(0,cap::QualifiedDetection::Absent,c.boundary))};
        zones_.push_back({std::move(detectors),get(cap::CaptureAttempt::start(0,get(cap::initial_control(kind,owner)),{false,false},c.boundary)),{}});
    }
}
bool Engine::detected(sandbox::Robot r,sandbox::Rect rect) {
    return r.hp>0&&r.x>=rect.x1&&r.x<=rect.x2&&r.y>=rect.y1&&r.y<=rect.y2;
}
match::ControlSnapshot Engine::control() const {
    auto result=match::initial_control();
    for(int z=0;z<4;z++) {
        const auto c=zones_[z].capture.snapshot().control;
        result.positions[z]={c.controller==cap::Team::Red?match::Team::Red:match::Team::Blue,
            c.degree==cap::ControlDegree::Full?match::ControlDegree::Full:match::ControlDegree::Temporary};
    }
    return result;
}
// Distinct, deliberately simple teaching routes. No combat or tactics claim.
sandbox::Action Engine::policy(int side) {
    auto r=movement_.state.robots[side];
    sandbox::Vec goal{};
    if(side==0) {
        if(r.x<6100&&r.y<5150)goal={r.x,5200};
        else if(r.x<6845)goal={6900,5200};
        else goal={6900,6100};
    } else {
        if(r.x>1655&&r.y>2455)goal={r.x,2400};
        else if(r.x>1655)goal={1600,2400};
        else goal={1600,6400};
    }
    return {std::clamp((goal.x-r.x)*1000/55,-1000,1000),std::clamp((goal.y-r.y)*1000/55,-1000,1000),goal.x,goal.y,0};
}
std::optional<std::string> Engine::advance_rules(std::int64_t end,const std::array<std::array<bool,2>,4>& physical) {
    // Whole-position atomic snapshots, never entity-order intermediate inputs.
    // Split internal grace expiries before the new sample; leave exact coincidences
    // in one apply call so the selected boundary policy really governs them.
    std::vector<std::int64_t> boundaries{end};
    for(const auto& z:zones_)for(const auto& d:z.detectors) {
        auto deadline=d.snapshot().deactivation_at_ms;
        if(deadline&&*deadline<end)boundaries.push_back(*deadline);
    }
    std::sort(boundaries.begin(),boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(),boundaries.end()),boundaries.end());
    for(auto t:boundaries)for(int zi=0;zi<4;zi++) {
        auto& z=zones_[zi];
        for(int side=0;side<2;side++) {
            auto& detector=z.detectors[side];
            std::optional<cap::QualifiedDetection> edge;
            auto target=physical[zi][side]?cap::QualifiedDetection::Present:cap::QualifiedDetection::Absent;
            if(t==end&&target!=detector.snapshot().detection)edge=target;
            auto result=detector.apply(t,edge);
            if(auto e=std::get_if<cap::Error>(&result))return std::string(IDS[zi])+" activation: "+std::string(cap::error_name(*e));
        }
        // After the first capture, detector tracks continue for visibility, but
        // no further capture/lifecycle claims are made. Control is experimentally latched.
        if(z.capture.snapshot().completed)continue;
        auto before=z.capture.snapshot();
        cap::LogicalActivation logical{z.detectors[0].snapshot().logically_active,z.detectors[1].snapshot().logically_active};
        std::optional<cap::LogicalActivation> input;
        if(logical!=before.active)input=logical;
        const bool challenger=before.control.controller==cap::Team::Red?before.active.blue:before.active.red;
        const bool progressing=challenger&&!(before.active.red&&before.active.blue);
        const auto deadline=before.time_ms+cap::CAPTURE_DURATION_MS-before.progress_ms;
        // If completion precedes an input, stop precisely there and route residual
        // time to the explicit latched strategy. At equality pass the input intact.
        if(progressing&&deadline<t) {
            auto result=z.capture.apply(deadline);
            if(auto e=std::get_if<cap::Error>(&result))return std::string(IDS[zi])+" capture: "+std::string(cap::error_name(*e));
            z.completed_at_ms=deadline;
        } else {
            auto result=z.capture.apply(t,input);
            if(auto e=std::get_if<cap::Error>(&result))return std::string(IDS[zi])+" capture: "+std::string(cap::error_name(*e));
            const auto& after=std::get<cap::CaptureAdvance>(result);
            if(after.state.completed)z.completed_at_ms=after.state.time_ms;
        }
    }
    return {};
}
std::optional<std::string> Engine::step_unchecked(const std::array<sandbox::Action,2>& actions) {
    if(movement_.state.terminal)return "Objective run already finished; reset to start again.";
    for(auto a:actions)if(!valid(a))return "Invalid objective action; movement-only mode requires fire=0.";
    const int tick=movement_.state.tick;
    if(!movement_.step(actions))return "Movement rejected.";
    std::array<std::array<bool,2>,4> physical{};
    for(int z=0;z<4;z++)for(int side=0;side<2;side++)physical[z][side]=detected(movement_.state.robots[side],ZONES[z]);
    auto t=time_ms(movement_.state.tick);
    if(auto error=advance_rules(t,physical))return error;
    if(config_.credits&&movement_.state.tick%60==0) {
        auto result=ledger_.apply({ledger_.last_sequence()+1,control()});
        if(auto e=std::get_if<match::Error>(&result))return std::string(match::error_name(*e));
    }
    // Actual 420 s timeout. No combat exists, hence an empty attack ledger (zero),
    // never inferred from HP. Base HP=1 is only a positive alive sentinel. The two
    // surviving HP values are engineering sandbox values, not official robot HP.
    if(config_.credits||t<match::MATCH_DURATION_MS) {
        auto totals=ledger_.totals();
        match::MatchSnapshot snapshot{t,{{{1,totals[0],0,movement_.state.robots[0].hp},{1,totals[1],0,movement_.state.robots[1].hp}}}};
        auto result=judge_.update(snapshot);
        if(auto e=std::get_if<match::Error>(&result))return std::string(match::error_name(*e));
        decision_=std::get<match::Decision>(result);
    }
    if(movement_.state.terminal)ledger_.seal();
    journal_.push_back({tick,actions,hash()});
    return {};
}
std::optional<std::string> Engine::advance(int count,int mode,sandbox::Action human) {
    if(count<1||count>600||mode<0||mode>2||!valid(human))return "Invalid objective request; count1..600, mode0..2 and fire=0 required.";
    if(movement_.state.terminal)return "Objective run already finished; reset to start again.";
    auto candidate=*this;
    for(int n=0;n<count&&!candidate.movement_.state.terminal;n++) {
        std::array<sandbox::Action,2> actions{};
        if(mode==1)actions={candidate.policy(0),candidate.policy(1)};
        else {actions[0]=human;if(mode==2)actions[1]=candidate.policy(1);}
        if(auto error=candidate.step_unchecked(actions))return error;
    }
    *this=std::move(candidate);return {};
}
std::optional<std::string> Engine::frame(int tick,const std::array<sandbox::Action,2>& actions) {
    if(tick!=movement_.state.tick)return "Replay tick is not sequential.";
    auto candidate=*this;
    if(auto error=candidate.step_unchecked(actions))return error;
    *this=std::move(candidate);return {};
}
std::uint32_t Engine::hash() const {
    std::uint32_t h=2166136261u;
    auto put=[&](std::int64_t v) {auto u=static_cast<std::uint64_t>(v);for(int i=0;i<8;i++){h^=(u>>(8*i))&255u;h*=16777619u;}};
    put(1);put(movement_.hash());put(static_cast<int>(config_.boundary));put(config_.credits);
    for(const auto& z:zones_) {
        for(const auto& d:z.detectors){auto s=d.snapshot();put(s.time_ms);put(static_cast<int>(s.detection));put(s.logically_active);put(s.deactivation_at_ms.value_or(-1));}
        auto c=z.capture.snapshot();put(c.time_ms);put(static_cast<int>(c.control.kind));put(team(c.control.original_owner));put(team(c.control.controller));put(static_cast<int>(c.control.degree));put(c.active.red);put(c.active.blue);put(c.progress_ms);put(c.completed);put(z.completed_at_ms.value_or(-1));
    }
    put(ledger_.totals()[0]);put(ledger_.totals()[1]);put(static_cast<std::int64_t>(ledger_.last_sequence()));put(static_cast<int>(decision_.outcome));put(static_cast<int>(decision_.reason));
    return h;
}
std::string Engine::json() const {
    auto base=movement_.json();base.pop_back();
    auto build_pos=base.find(SIM_BUILD_ID);base.replace(build_pos,std::string(SIM_BUILD_ID).size(),OBJECTIVE_BUILD_ID);
    auto hash_pos=base.find("\"hash\":")+7;auto hash_end=base.find(',',hash_pos);base.replace(hash_pos,hash_end-hash_pos,std::to_string(hash()));
    // The movement build is nested provenance; the objective build pins adapter+rules.
    std::ostringstream o;o<<base<<",\"movement_build\":\""<<SIM_BUILD_ID<<"\",\"profile\":\""<<PROFILE<<"\",\"official\":false,\"objective_hash\":"<<hash()<<",\"objective_build\":\""<<OBJECTIVE_BUILD_ID<<"\",\"objective\":{\"elapsed_ms\":"<<time_ms(movement_.state.tick)<<",\"qualified_ms\":"<<time_ms(movement_.state.tick)<<",\"world_time\":{\"numerator\":"<<movement_.state.tick<<",\"denominator\":60},\"duration_ms\":420000,";config_json(o,config_);
    o<<",\"zones\":[";
    for(int i=0;i<4;i++) {
        if(i)o<<',';
        const auto& z=zones_[i];auto c=z.capture.snapshot();auto r=ZONES[i];
        o<<"{\"id\":\""<<IDS[i]<<"\",\"label\":\""<<LABELS[i]<<"\",\"rect\":["<<r.x1<<','<<r.y1<<','<<r.x2<<','<<r.y2<<"],\"original_owner\":"<<team(c.control.original_owner)<<",\"controller\":"<<team(c.control.controller)<<",\"degree\":\""<<degree(c.control.degree)<<"\",\"detected\":[";
        for(int s=0;s<2;s++){if(s)o<<',';o<<boolean(z.detectors[s].snapshot().detection==cap::QualifiedDetection::Present);}o<<"],\"active\":[";
        for(int s=0;s<2;s++){if(s)o<<',';o<<boolean(z.detectors[s].snapshot().logically_active);}o<<"],\"deactivation_at_ms\":[";
        for(int s=0;s<2;s++){if(s)o<<',';auto d=z.detectors[s].snapshot().deactivation_at_ms;if(d)o<<*d;else o<<"null";}
        o<<"],\"progress_ms\":"<<c.progress_ms<<",\"completed\":"<<boolean(c.completed)<<",\"completed_at_ms\":";if(z.completed_at_ms)o<<*z.completed_at_ms;else o<<"null";o<<'}';
    }
    auto rate=std::get<match::ControlRate>(match::control_rate(control()));
    o<<"],\"rate\":["<<rate.points_per_second[0]<<','<<rate.points_per_second[1]<<"],\"score\":";
    if(config_.credits)o<<'['<<ledger_.totals()[0]<<','<<ledger_.totals()[1]<<']';else o<<"null";
    o<<",\"credits\":"<<ledger_.last_sequence()<<",\"result\":{\"outcome\":\""<<(!config_.credits&&movement_.state.terminal?"unscored-timeout":outcome(decision_.outcome))<<"\",\"reason\":\""<<(!config_.credits&&movement_.state.terminal?"credit-strategy-disabled":reason(decision_.reason))<<"\"}}}";
    return o.str();
}
std::string Engine::replay_json() const {
    std::ostringstream o;o<<"{\"schema\":1,\"profile\":\""<<PROFILE<<"\",\"engine\":\"objective-native-v1\",\"build\":\""<<OBJECTIVE_BUILD_ID<<"\",\"official\":false,\"config\":{";config_json(o,config_);o<<"},\"frames\":[";
    for(std::size_t i=0;i<journal_.size();i++){if(i)o<<',';const auto& f=journal_[i];o<<'['<<f.tick;for(auto a:f.actions)o<<','<<a.dx<<','<<a.dy<<','<<a.aim_x<<','<<a.aim_y<<','<<a.fire;o<<','<<f.hash<<']';}
    o<<"],\"final_hash\":"<<hash()<<'}';return o.str();
}
std::string meta_json() {
    std::ostringstream o;o<<"{\"profile\":\""<<PROFILE<<"\",\"official\":false,\"width\":12000,\"height\":7600,\"hz\":60,\"radius\":280,\"duration\":25200,\"duration_ms\":420000,\"walls\":[[5250,2800,6750,4800],[2600,1350,3800,1950],[8200,5650,9400,6250]],\"zones\":[";
    for(int i=0;i<4;i++){if(i)o<<',';auto r=ZONES[i];o<<"{\"id\":\""<<IDS[i]<<"\",\"label\":\""<<LABELS[i]<<"\",\"rect\":["<<r.x1<<','<<r.y1<<','<<r.x2<<','<<r.y2<<"],\"original_owner\":"<<i%2<<'}';}
    o<<"],\"transport\":\"C++ native objective engine / loopback Python bridge\",\"observation\":\"omniscient\",\"clock_strategy\":\""<<CLOCK<<"\",\"lifecycle_strategy\":\""<<LIFECYCLE<<"\",\"assumptions\":[\"EXPERIMENTAL movement-only teaching mode, not full RMUC or an official scorekeeper.\",\"Placeholder inclusive center-in-rectangle detection; one living, normally-connected, always-eligible robot per team. No interaction cards.\",\"60 Hz post-move occupancy is qualified once, with causal ceiling-millisecond timestamps (less than 1 ms sampling quantization).\",\"Verified 2 s deactivation grace is applied exactly once; verified 5 s capture progress follows atomic whole-position logical inputs.\",\"After first capture, Temporary control is experimentally latched for the rest of the run; no lifecycle adjudication is claimed.\",\"Credits are disabled by default. Opt-in experimental end-of-second credits sample control after same-time capture, including second420; this is not verified official cadence.\",\"Genuine420 s timeout; no combat means attack ledger0. Base HP1 is an alive sentinel; robot HP100 is an engineering placeholder.\",\"Autopilot demonstration is asymmetric: red targets blue outpost, blue targets red base.\"],\"unsupported\":[\"Firing, damage and destruction\",\"Temporary-control restoration, outpost rebuild and lifecycle\",\"Official geometry, card sensing, eligibility and multiple-robot aggregation\",\"Referee timing and contested-to-neither adjudication\"],\"capture_module\":\""<<cap::MODULE_VERSION<<"\",\"match_module\":\""<<match::MODULE_VERSION<<"\"}";return o.str();
}
}
