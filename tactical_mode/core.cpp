#include "core.hpp"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <utility>
namespace tactical {
namespace {
std::uint32_t fnv(const std::string& data) {
    std::uint32_t hash = 2166136261u;
    for (unsigned char c : data) { hash ^= c; hash *= 16777619u; }
    return hash;
}
policy::Action sdk_action(const Input& in) {
    return {in.sdk_version,in.action[0],in.action[1],in.action[2],in.action[3],in.action[4]};
}
sandbox::Action native_action(const policy::Action& a) {
    return {int(std::get<I>(a.dx)), int(std::get<I>(a.dy)), int(std::get<I>(a.aim_x)),
            int(std::get<I>(a.aim_y)), int(std::get<I>(a.fire))};
}
Input resolved(const Request& request, const policy::Action& a, const Controller& c) {
    return {request.kind, std::get<I>(a.sdk_version), request.kind == "policy" ? c.descriptor.controller_id : "",
        {std::get<I>(a.dx), std::get<I>(a.dy), std::get<I>(a.aim_x), std::get<I>(a.aim_y), std::get<I>(a.fire)}};
}
void source_json(std::ostream& out, const Input& in) {
    out << "{\"kind\":\"" << in.kind << "\",\"sdk_version\":" << in.sdk_version << ",\"controller_id\":";
    if (in.controller_id.empty()) out << "null";
    else out << '"' << in.controller_id << '"';
    out << '}';
}
void input_json(std::ostream& out, const Input& in) {
    auto source = std::ostringstream{};
    source_json(source,in);
    auto text = source.str(); text.pop_back();
    out << text << ",\"action\":[";
    for (std::size_t i=0; i<in.action.size(); ++i) { if (i) out << ','; out << in.action[i]; }
    out << "]}";
}
void frame_json(std::ostream& out, const Frame& frame) {
    out << "{\"tick\":" << frame.tick << ",\"inputs\":[";
    input_json(out,frame.inputs[0]); out << ','; input_json(out,frame.inputs[1]);
    out << "],\"observation_hashes\":[" << frame.observation_hashes[0] << ',' << frame.observation_hashes[1]
        << "],\"hash\":" << frame.hash << '}';
}
void frames_json(std::ostream& out, const std::vector<Frame>& frames) {
    out << '[';
    for (std::size_t i=0; i<frames.size(); ++i) { if (i) out << ','; frame_json(out,frames[i]); }
    out << ']';
}
std::optional<std::string> valid_input(const Input& in) {
    if (in.kind != "manual" && in.kind != "policy") return "Input kind must be manual or policy.";
    if (in.kind == "manual" && !in.controller_id.empty()) return "Manual input cannot claim a controller ID.";
    if (in.kind == "policy") {
        if (auto e = combat::validate_descriptor({in.sdk_version,in.controller_id})) return e;
    }
    return policy::validate_action(sdk_action(in));
}
template<class T> T get(cap::Result<T> result) {
    if (auto e=std::get_if<cap::Error>(&result)) throw std::invalid_argument(std::string(cap::error_name(*e)));
    return std::get<T>(std::move(result));
}
int team(cap::Team t) { return t==cap::Team::Red ? 0 : 1; }
const char* degree(cap::ControlDegree d) { return d==cap::ControlDegree::Full ? "full" : "temporary"; }
const char* boolean(bool b) { return b ? "true" : "false"; }
bool detected(sandbox::Robot r, sandbox::Rect b) {
    return r.hp>0 && r.x>=b.x1 && r.x<=b.x2 && r.y>=b.y1 && r.y<=b.y2;
}
} // namespace
std::string build_id() { return TACTICAL_BUILD_ID; }
std::string config_json(const Config& c) {
    std::ostringstream o;
    o << "{\"preset\":\"" << (c.credits && c.boundary==cap::BoundaryPolicy::EngineeringTimerBeforeInputV1 && c.damage_strategy==DamageStrategy::ExperimentalReconciledProjectileHpLossV1 && c.heat_strategy==HeatStrategy::ExperimentalFixedLevel1CoolingPriority17mmV1 && c.heat_threshold==heat::ThresholdPolicy::StrictRejectUnadjudicated ? kPreset : "EXPERIMENTAL-custom-v3")
      << "\",\"boundary_policy\":\"" << cap::policy_name(c.boundary)
      << "\",\"credit_strategy\":\"" << (c.credits ? kCredit : "disabled")
      << "\",\"geometry_strategy\":\"" << kGeometry << "\",\"eligibility_strategy\":\"" << kEligibility
      << "\",\"death_strategy\":\"" << kDeath << "\",\"lifecycle_strategy\":\"" << kLifecycle
      << "\",\"clock_strategy\":\"" << kClock << "\",\"phase_order\":\"" << kPhase
      << "\",\"observation_strategy\":\"" << policy::kObservationProfile
      << "\",\"duration_ticks\":" << kDurationTicks << ",\"hz\":60,\"stop_on_disable\":false,\"respawn\":false,\"base_hp_sentinel\":1,\"attack_damage_strategy\":\""
      << (c.damage_strategy==DamageStrategy::MissingAdjudicated ? kDamageMissing : kDamageExperimental)
      << "\",\"remaining_hp_strategy\":\"" << kRemainingHp
      << "\",\"heat_strategy\":\"" << (c.heat_strategy==HeatStrategy::Disabled ? "disabled" : kHeatExperimental)
      << "\",\"heat_threshold_policy\":\"" << heat::policy_name(c.heat_threshold)
      << "\",\"heat_boundary_policy\":\"" << kHeatBoundary
      << "\",\"heat_death_policy\":\"" << kHeatDeath << "\"}";
    return o.str();
}
Engine::Engine(Config c) { reset(c); }
void Engine::reset(Config c) {
    if (c.boundary!=cap::BoundaryPolicy::StrictRejectUnadjudicated && c.boundary!=cap::BoundaryPolicy::EngineeringTimerBeforeInputV1)
        throw std::invalid_argument("Unsupported tactical boundary policy.");
    if(c.damage_strategy!=DamageStrategy::MissingAdjudicated && c.damage_strategy!=DamageStrategy::ExperimentalReconciledProjectileHpLossV1)
        throw std::invalid_argument("Unsupported tactical attack damage strategy.");
    if(c.heat_strategy!=HeatStrategy::Disabled && c.heat_strategy!=HeatStrategy::ExperimentalFixedLevel1CoolingPriority17mmV1)
        throw std::invalid_argument("Unsupported tactical heat strategy.");
    heat::Config heat_config;
    heat_config.threshold=c.heat_threshold;
    heat_config.boundary=heat::BoundaryPolicy::EngineeringCoolingBeforeShotV1;
    heat_config.death=heat::DeathPolicy::EngineeringShotBeforeDeathResetV1;
    auto initial_heat=heat::Heat::start(heat_config);
    if(auto error=std::get_if<heat::Error>(&initial_heat))
        throw std::invalid_argument(std::string(heat::error_name(*error)));
    config_=c; native=sandbox::Engine{{kDurationTicks,false,false}};
    heat_.assign(2,std::get<heat::Heat>(initial_heat)); blocked_fire_ticks_={};
    zones_.clear(); ledger_={}; judge_={}; decision_={}; unresolved_.reset(); journal.clear(); frames.clear();
    damage_={}; damage_complete_=c.damage_strategy==DamageStrategy::ExperimentalReconciledProjectileHpLossV1;
    nominal_projectile_damage_={}; expected_hp_={sandbox::MAX_HP,sandbox::MAX_HP};
    for(int z=0;z<4;++z) {
        auto owner=z%2 ? cap::Team::Blue : cap::Team::Red;
        auto kind=z<2 ? cap::PositionKind::Outpost : cap::PositionKind::Base;
        std::array<cap::ActivationDelay,2> detectors{
            get(cap::ActivationDelay::start(0,cap::QualifiedDetection::Absent,c.boundary)),
            get(cap::ActivationDelay::start(0,cap::QualifiedDetection::Absent,c.boundary))};
        zones_.push_back({std::move(detectors),get(cap::CaptureAttempt::start(0,get(cap::initial_control(kind,owner)),{false,false},c.boundary)),{}});
    }
    initial_hash_=hash();
}
match::ControlSnapshot Engine::control() const {
    auto result=match::initial_control();
    for(int z=0;z<4;++z) {
        const auto c=zones_[z].capture.snapshot().control;
        result.positions[z]={c.controller==cap::Team::Red ? match::Team::Red : match::Team::Blue,
            c.degree==cap::ControlDegree::Full ? match::ControlDegree::Full : match::ControlDegree::Temporary};
    }
    return result;
}
std::optional<std::string> Engine::advance_rules(std::int64_t end,const std::array<std::array<bool,2>,4>& physical, const std::array<bool,2>& died) {
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
            // Death is not ordinary detector loss. This explicit experimental
            // adapter discards any grace immediately at the post-combat timestamp.
            // Capture still receives the atomic input at that exact timestamp, so
            // strict versus engineering deadline handling remains in the module.
            if (t==end && died[side]) {
                detector=get(cap::ActivationDelay::start(t,cap::QualifiedDetection::Absent,config_.boundary));
                continue;
            }
            auto result=detector.apply(t,edge);
            if(auto e=std::get_if<cap::Error>(&result))return std::string(objective::IDS[zi])+" activation: "+std::string(cap::error_name(*e));
        }
        // After the first capture, detector tracks continue internally, but
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
            if(auto e=std::get_if<cap::Error>(&result))return std::string(objective::IDS[zi])+" capture: "+std::string(cap::error_name(*e));
            z.completed_at_ms=deadline;
        } else {
            auto result=z.capture.apply(t,input);
            if(auto e=std::get_if<cap::Error>(&result))return std::string(objective::IDS[zi])+" capture: "+std::string(cap::error_name(*e));
            const auto& after=std::get<cap::CaptureAdvance>(result);
            if(after.state.completed)z.completed_at_ms=after.state.time_ms;
        }
    }
    return {};
}
void Engine::reconcile_damage(const std::array<int,2>& hp_before,int prior_tick) {
    const std::array<int,2> hp_after{native.state.robots[0].hp,native.state.robots[1].hp};
    const bool baseline_matches=hp_before==expected_hp_;
    expected_hp_=hp_after;
    if(config_.damage_strategy==DamageStrategy::MissingAdjudicated || !damage_complete_) return;
    // This adapter is explicitly experimental, not a referee classifier. The
    // unchanged sandbox has only opposing projectile HP deductions. Verify that
    // closed-world assumption every tick; never call gross HP change "attack".
    bool valid=baseline_matches && prior_tick>=0 && prior_tick<kDurationTicks && native.state.tick==prior_tick+1;
    std::array<std::int64_t,2> incoming{};
    std::array<int,2> disabled{};
    for(const auto& event:native.events) {
        if(event.tick!=prior_tick || event.actor<0 || event.actor>1) {valid=false;continue;}
        switch(event.kind) {
        case 1: case 4:
            if(event.target!=-1 || event.amount!=0) valid=false;
            break;
        case 2:
            if(event.target<0 || event.target>1 || event.actor==event.target || event.amount!=sandbox::DAMAGE) {valid=false;break;}
            incoming[event.target]+=event.amount;
            break;
        case 3:
            if(event.target!=event.actor || event.amount!=0) {valid=false;break;}
            ++disabled[event.target];
            break;
        default: valid=false;
        }
    }
    for(int side=0;side<2;++side) {
        if(hp_before[side]<0 || hp_before[side]>sandbox::MAX_HP || hp_after[side]<0 || hp_after[side]>hp_before[side]) valid=false;
        if(incoming[side]>0 && hp_before[side]<=0) valid=false;
        if(std::int64_t(hp_before[side])-hp_after[side]!=std::min<std::int64_t>(hp_before[side],incoming[side])) valid=false;
        if(disabled[side]!=(hp_before[side]>0 && hp_after[side]==0 ? 1 : 0)) valid=false;
    }
    if(!valid) {damage_complete_=false;return;}
    std::vector<damage::SettledDeduction> deductions;
    for(int side=0;side<2;++side) if(hp_after[side]<hp_before[side]) {
        // Aggregate all simultaneous hits on this one victim BEFORE HP clamp.
        // Every source is the same opponent. No per-hit overkill allocation or
        // source ordering is asserted. Both victim totals commit atomically.
        deductions.push_back({damage_.last_sequence()+deductions.size()+1,time_ms(native.state.tick),
            static_cast<std::uint32_t>(side),side ? damage::Team::Blue : damage::Team::Red,
            damage::EntityKind::Robot,damage::Cause::Projectile,hp_before[side],hp_after[side],
            side ? damage::Team::Red : damage::Team::Blue});
    }
    if(std::holds_alternative<damage::Error>(damage_.apply_batch(deductions))) {damage_complete_=false;return;}
    nominal_projectile_damage_[0]+=incoming[1];
    nominal_projectile_damage_[1]+=incoming[0];
}
std::optional<std::array<std::int64_t,2>> Engine::remaining_robot_hp() const {
    std::array<match::RemainingHpFact,2> facts;
    for(int side=0;side<2;++side) {
        const auto hp=native.state.robots[side].hp;
        if(hp<0 || hp>sandbox::MAX_HP) return {};
        facts[side]={static_cast<std::uint32_t>(side),side ? match::Team::Blue : match::Team::Red,
            match::EntityKind::Robot,hp,true,false};
    }
    const auto result=match::surviving_robot_hp(facts);
    if(const auto* totals=std::get_if<std::array<std::int64_t,2>>(&result)) return *totals;
    return {};
}
std::optional<std::string> Engine::step_unchecked(const std::array<sandbox::Action,2>& actions) {
    if (native.state.terminal) return "Tactical run is terminal; reset to start another run.";
    const std::array<bool,2> alive{native.state.robots[0].hp>0,native.state.robots[1].hp>0};
    const std::array<int,2> hp_before{native.state.robots[0].hp,native.state.robots[1].hp};
    const auto prior_tick=native.state.tick;
    const auto t=time_ms(prior_tick+1);
    auto effective=actions;
    const bool heat_enabled=config_.heat_strategy!=HeatStrategy::Disabled;
    if(heat_enabled) for(int side=0;side<2;++side) {
        auto cooled=heat_[side];
        const auto preview=cooled.apply({t,0,false});
        if(const auto* error=std::get_if<heat::Error>(&preview))
            return "Heat: "+std::string(heat::error_name(*error))+"; batch unchanged.";
        // A named experimental cooling-first phase. Decisions and replay retain
        // the requested action; only the native effective fire bit is gated.
        if(cooled.snapshot().firing_locked() && actions[side].fire) {
            effective[side].fire=0; ++blocked_fire_ticks_[side];
        }
    }
    const std::array<int,2> shots_before{native.state.robots[0].shots,native.state.robots[1].shots};
    if (!native.step(effective)) return "Native action rejected; batch unchanged.";
    if(heat_enabled) {
        std::array<std::int64_t,2> actual_shots{};
        for(const auto& event:native.events) if(event.kind==1) {
            if(event.tick!=prior_tick || event.actor<0 || event.actor>1 || event.target!=-1 || event.amount!=0)
                return "Unreconciled native heat shot; batch unchanged.";
            ++actual_shots[event.actor];
        }
        for(int side=0;side<2;++side) {
            if(actual_shots[side]>1 || native.state.robots[side].shots-shots_before[side]!=actual_shots[side])
                return "Unreconciled native heat shot count; batch unchanged.";
            const auto result=heat_[side].apply({t,actual_shots[side],alive[side] && native.state.robots[side].hp<=0});
            if(const auto* error=std::get_if<heat::Error>(&result))
                return "Heat: "+std::string(heat::error_name(*error))+"; batch unchanged.";
        }
    }
    reconcile_damage(hp_before,prior_tick);
    std::array<bool,2> died{};
    std::array<std::array<bool,2>,4> physical{};
    for(int side=0;side<2;++side) died[side]=alive[side] && native.state.robots[side].hp<=0;
    for(int z=0;z<4;++z) for(int side=0;side<2;++side)
        physical[z][side]=detected(native.state.robots[side],objective::ZONES[z]);
    if (auto error=advance_rules(t,physical,died)) return error;
    // Explicitly experimental sampling: capture at the same timestamp happens
    // before this external one-second credit, INCLUDING the final second420.
    if(config_.credits && native.state.tick%60==0) {
        auto result=ledger_.apply({ledger_.last_sequence()+1,control()});
        if(auto e=std::get_if<match::Error>(&result)) return std::string(match::error_name(*e));
    }
    const auto totals=ledger_.totals();
    const std::optional<std::int64_t> red=config_.credits ? std::optional<std::int64_t>{totals[0]} : std::nullopt;
    const std::optional<std::int64_t> blue=config_.credits ? std::optional<std::int64_t>{totals[1]} : std::nullopt;
    // Positive non-destructible base sentinel and one connected, non-dismissed
    // robot/team are explicit experimental inputs to the unchanged judge.
    const auto hp=remaining_robot_hp();
    const auto& attack=damage_.totals().attack_damage;
    match::MatchSnapshot snapshot{t,{{{1,red,damage_complete_ ? std::optional<std::int64_t>{attack[0]} : std::nullopt,
                                          hp ? std::optional<std::int64_t>{(*hp)[0]} : std::nullopt},
                                    {1,blue,damage_complete_ ? std::optional<std::int64_t>{attack[1]} : std::nullopt,
                                           hp ? std::optional<std::int64_t>{(*hp)[1]} : std::nullopt}}}};
    auto result=judge_.update(snapshot);
    if(auto e=std::get_if<match::Error>(&result)) {
        if(t==match::MATCH_DURATION_MS && (*e==match::Error::MissingAttackDamage || *e==match::Error::MissingControlScore || *e==match::Error::MissingSurvivingRobotHp))
            unresolved_=*e;
        else return std::string(match::error_name(*e));
    } else decision_=std::get<match::Decision>(result);
    if(native.state.terminal) {ledger_.seal();damage_.seal();}
    return {};
}
policy::Observation Engine::observation(int self) const {
    std::array<policy::Zone,4> zones;
    for(int z=0;z<4;++z) {
        const auto rect=objective::ZONES[z];
        const auto c=zones_[z].capture.snapshot().control;
        zones[z]={objective::IDS[z],{rect.x1,rect.y1,rect.x2,rect.y2},team(c.original_owner),team(c.controller),degree(c.degree)};
    }
    const auto rates=std::get<match::ControlRate>(match::control_rate(control()));
    if(self<0 || self>1) throw std::out_of_range("Invalid tactical observer.");
    const auto& thermal=heat_[self].snapshot();
    const policy::SelfHeat own_heat{config_.heat_strategy!=HeatStrategy::Disabled,thermal.heat_tenths,
        heat_[self].config().limit_tenths,heat_[self].config().cooling_per_second,
        thermal.overheat_locked,thermal.permanent_latched,thermal.firing_locked()};
    return {combat::make_observation(native,self),std::move(zones),ledger_.totals(),rates.points_per_second,
        time_ms(native.state.tick),match::MATCH_DURATION_MS,config_.credits,own_heat};
}
std::string observation_json(const policy::Observation& obs) {
    std::ostringstream o;
    o << "{\"sdk_version\":" << policy::kSdkVersion << ",\"observation_version\":" << policy::kObservationVersion << ",\"profile\":\"" << policy::kObservationProfile
      << "\",\"combat\":" << combat::observation_json(obs.combat) << ",\"zones\":[";
    for(std::size_t i=0;i<obs.zones.size();++i) {
        if(i) o<<',';
        const auto& z=obs.zones[i];
        o << "{\"id\":\"" << z.id << "\",\"rect\":{\"x1\":" << z.rect.x1 << ",\"y1\":" << z.rect.y1
          << ",\"x2\":" << z.rect.x2 << ",\"y2\":" << z.rect.y2 << "},\"original_owner\":" << z.original_owner
          << ",\"controller\":" << z.controller << ",\"degree\":\"" << z.degree << "\"}";
    }
    o << "],\"score\":";
    if(obs.credits_enabled) o << '[' << obs.score[0] << ',' << obs.score[1] << ']'; else o << "null";
    o << ",\"rate\":[" << obs.rate[0] << ',' << obs.rate[1] << "],\"qualified_ms\":" << obs.qualified_ms
      << ",\"duration_ms\":" << obs.duration_ms << ",\"credits_enabled\":" << boolean(obs.credits_enabled)
      << ",\"self_heat\":{\"enabled\":" << boolean(obs.self_heat.enabled)
      << ",\"heat_tenths\":" << obs.self_heat.heat_tenths << ",\"limit_tenths\":" << obs.self_heat.limit_tenths
      << ",\"cooling_per_second\":" << obs.self_heat.cooling_per_second
      << ",\"overheat_locked\":" << boolean(obs.self_heat.overheat_locked)
      << ",\"permanent_latched\":" << boolean(obs.self_heat.permanent_latched)
      << ",\"firing_locked\":" << boolean(obs.self_heat.firing_locked) << "}}";
    return o.str();
}
std::uint32_t observation_hash(const policy::Observation& o) { return fnv(observation_json(o)); }
std::string policy_metadata_json(const policy::Descriptor& d) {
    if(auto e=combat::validate_descriptor(d)) throw std::invalid_argument(*e);
    std::ostringstream o;
    o << "{\"sdk_version\":" << d.sdk_version << ",\"controller_id\":\"" << d.controller_id
      << "\",\"execution\":\"trusted-local-native\",\"observation\":\"" << policy::kObservationProfile
      << "\",\"state_mode\":\"stateless\",\"decision_hz\":60}";
    return o.str();
}
std::optional<std::string> Engine::advance(I count,const std::array<Request,2>& requests,
                                        const std::array<Controller,2>& controllers) {
    if(count<1 || count>kMaxBatch) return "Step count outside [1,600].";
    if(native.state.terminal) return "Tactical run is terminal; reset to start another run.";
    for(int i=0;i<2;++i) {
        const auto& r=requests[i];
        if(r.kind!="manual" && r.kind!="policy") return "Input kind must be manual or policy.";
        if(r.kind=="manual") { if(auto e=policy::validate_action(r.action)) return e; }
        else {
            const auto* v=std::get_if<I>(&r.action.sdk_version);
            if(!v || *v!=policy::kSdkVersion) return "Requested policy SDK version mismatch.";
            if(auto e=combat::validate_descriptor(controllers[i].descriptor)) return e;
            if(!controllers[i].decide) return "Missing policy entrypoint.";
        }
    }
    try {
        Engine candidate=*this; candidate.frames.clear();
        for(I n=0;n<count && !candidate.native.state.terminal;++n) {
            // Freeze BOTH owning snapshots, including identical public broadcast
            // state, before invoking EITHER callback. No singleton engine handles.
            const std::array<policy::Observation,2> observations{candidate.observation(0),candidate.observation(1)};
            std::array<policy::Action,2> actions;
            for(int i=0;i<2;++i) actions[i]=requests[i].kind=="policy" ? controllers[i].decide(observations[i]) : requests[i].action;
            for(const auto& a:actions) if(auto e=policy::validate_action(a)) return e;
            Frame f; f.tick=candidate.native.state.tick;
            for(int i=0;i<2;++i) { f.inputs[i]=resolved(requests[i],actions[i],controllers[i]); f.observation_hashes[i]=observation_hash(observations[i]); }
            if(auto e=candidate.step_unchecked({native_action(actions[0]),native_action(actions[1])})) return e;
            f.hash=candidate.hash(); candidate.journal.push_back(f); candidate.frames.push_back(f);
        }
        *this=std::move(candidate); return {};
    } catch(...) { return "Policy exception; experiment invalid, batch unchanged."; }
}
std::optional<std::string> Engine::replay_frame(const Frame& f) { return replay_frames({f}); }
std::optional<std::string> Engine::replay_frames(const std::vector<Frame>& expected) {
    if(expected.empty() || expected.size()>std::size_t(kMaxBatch)) return "Replay batch count outside [1,600].";
    try {
        Engine candidate=*this; candidate.frames.clear();
        for(const auto& f:expected) {
            if(f.tick!=candidate.native.state.tick || candidate.native.state.terminal) return "Replay tick is not the next live tick; batch unchanged.";
            for(const auto& input:f.inputs) if(auto e=valid_input(input)) return e;
            for(int i=0;i<2;++i)
                if(f.observation_hashes[i]!=observation_hash(candidate.observation(i))) return "Replay observation hash mismatch; batch unchanged.";
            if(auto e=candidate.step_unchecked({native_action(sdk_action(f.inputs[0])),native_action(sdk_action(f.inputs[1]))})) return e;
            if(f.hash!=candidate.hash()) return "Replay state hash mismatch; batch unchanged.";
            candidate.journal.push_back(f); candidate.frames.push_back(f);
        }
        *this=std::move(candidate); return {};
    } catch(...) { return "Replay failed; batch unchanged."; }
}
std::uint32_t Engine::hash() const {
    std::uint32_t h=2166136261u;
    auto put=[&](std::int64_t v) { auto u=static_cast<std::uint64_t>(v); for(int i=0;i<8;++i) { h^=(u>>(i*8))&255u;h*=16777619u; } };
    put(3); put(native.hash()); put(static_cast<int>(config_.boundary)); put(config_.credits);put(static_cast<int>(config_.damage_strategy));
    put(static_cast<int>(config_.heat_strategy));put(static_cast<int>(config_.heat_threshold));
    for(int side=0;side<2;++side) {
        const auto& q=heat_[side].snapshot();
        const auto& c=heat_[side].config();
        put(c.limit_tenths);put(c.cooling_per_second);put(static_cast<int>(c.threshold));
        put(static_cast<int>(c.boundary));put(static_cast<int>(c.death));
        put(q.time_ms);put(q.heat_tenths);put(q.overheat_locked);put(q.permanent_latched);
        put(q.detected_shots);put(q.last_event_ms);put(blocked_fire_ticks_[side]);
    }
    for(const auto& z:zones_) {
        for(const auto& d:z.detectors) { const auto s=d.snapshot();put(s.time_ms);put(static_cast<int>(s.detection));put(s.logically_active);put(s.deactivation_at_ms.value_or(-1)); }
        const auto c=z.capture.snapshot(); put(c.time_ms);put(static_cast<int>(c.control.kind));put(team(c.control.original_owner));put(team(c.control.controller));put(static_cast<int>(c.control.degree));put(c.active.red);put(c.active.blue);put(c.progress_ms);put(c.completed);put(z.completed_at_ms.value_or(-1));
    }
    put(ledger_.totals()[0]);put(ledger_.totals()[1]);put(static_cast<std::int64_t>(ledger_.last_sequence()));
    const auto& damage_totals=damage_.totals();
    for(const auto* values:{&damage_totals.attack_damage,&damage_totals.actual_hp_loss,&damage_totals.excluded_hp_loss,
                           &damage_totals.penalty_attack_damage,&nominal_projectile_damage_})
        for(auto value:*values)put(value);
    put(damage_complete_);put(damage_.last_sequence());put(damage_.last_time_ms());put(damage_.sealed());
    put(expected_hp_[0]);put(expected_hp_[1]);
    put(static_cast<int>(decision_.outcome));put(static_cast<int>(decision_.reason));put(unresolved_ ? static_cast<int>(*unresolved_) : -1);
    return h;
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
     <<(config_.damage_strategy==DamageStrategy::MissingAdjudicated ? kDamageMissing : kDamageExperimental)
     <<"\",\"complete\":"<<boolean(damage_complete_)<<",\"reason\":";
    if(damage_complete_) o<<"null";
    else o<<'"'<<(config_.damage_strategy==DamageStrategy::MissingAdjudicated ? kDamageMissing : "unadjudicated-native-damage")<<'"';
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
std::string Engine::heat_admin_json() const {
    std::ostringstream o;
    o << "{\"schema\":1,\"module\":\"" << heat::MODULE_VERSION << "\",\"strategy\":\""
      << (config_.heat_strategy==HeatStrategy::Disabled ? "disabled" : kHeatExperimental)
      << "\",\"threshold_policy\":\"" << heat::policy_name(config_.heat_threshold)
      << "\",\"boundary_policy\":\"" << kHeatBoundary << "\",\"robots\":[";
    for(int side=0;side<2;++side) {
        if(side) o << ',';
        const auto& q=heat_[side].snapshot();
        o << "{\"heat_tenths\":" << q.heat_tenths << ",\"overheat_locked\":" << boolean(q.overheat_locked)
          << ",\"permanent_latched\":" << boolean(q.permanent_latched)
          << ",\"firing_locked\":" << boolean(q.firing_locked())
          << ",\"detected_shots\":" << q.detected_shots << ",\"blocked_fire_ticks\":" << blocked_fire_ticks_[side] << '}';
    }
    o << "]}"; return o.str();
}
std::string Engine::json(int observer,bool include_frames) const {
    std::ostringstream o;
    o << "{\"build\":\"" << build_id() << "\",\"schema\":1,\"profile\":\"" << kProfile << "\",\"observation_profile\":\"" << policy::kObservationProfile
      << "\",\"observation_version\":" << policy::kObservationVersion << ",\"official\":false,\"observer\":" << observer << ",\"tick\":" << native.state.tick << ",\"terminal\":" << native.state.terminal
      << ",\"hash\":" << hash() << ",\"config\":" << config_json(config_) << ",\"result\":" << result_json()
      << ",\"damage_admin\":" << damage_admin_json()
      << ",\"heat_admin\":" << heat_admin_json()
      << ",\"observation\":" << observation_json(observation(observer)) << ",\"last_sources\":";
    if(journal.empty()) o<<"null"; else {o<<'[';source_json(o,journal.back().inputs[0]);o<<',';source_json(o,journal.back().inputs[1]);o<<']';}
    o << ",\"frames\":";if(include_frames)frames_json(o,frames);else o<<"[]";o<<'}';return o.str();
}
std::string Engine::replay_json() const {
    std::ostringstream o;
    o << "{\"schema\":\"" << kReplaySchema << "\",\"profile\":\"" << kProfile << "\",\"build\":\"" << build_id()
      << "\",\"engine\":\"" << kEngine << "\",\"observation_profile\":\"" << policy::kObservationProfile
      << "\",\"observation_version\":" << policy::kObservationVersion << ",\"official\":false,\"config\":" << config_json(config_) << ",\"initial_hash\":" << initial_hash_ << ",\"frames\":";
    frames_json(o,journal);o<<",\"final_hash\":"<<hash()<<'}';return o.str();
}
std::string meta_json() {
    std::ostringstream o;
    o << "{\"profile\":\"" << kProfile << "\",\"build\":\"" << build_id() << "\",\"engine\":\"" << kEngine
      << "\",\"official\":false,\"width\":12000,\"height\":7600,\"hz\":60,\"radius\":280,\"duration\":25200,\"duration_ms\":420000"
      << ",\"observation_version\":" << policy::kObservationVersion << ",\"observation_profile\":\"" << policy::kObservationProfile << "\",\"transport\":\"C++ native tactical engine / loopback Python bridge\",\"default_config\":" << config_json({})
      << ",\"boundary_policies\":[\"" << cap::policy_name(cap::BoundaryPolicy::StrictRejectUnadjudicated) << "\",\"" << cap::policy_name(cap::BoundaryPolicy::EngineeringTimerBeforeInputV1)
      << "\"],\"credit_strategies\":[\"disabled\",\"" << kCredit << "\"],\"attack_damage_strategies\":[\""<<kDamageMissing<<"\",\""<<kDamageExperimental
      <<"\"],\"damage_module\":\""<<damage::MODULE_VERSION<<"\",\"damage_source_version\":\""<<damage::SOURCE_VERSION
      <<"\",\"damage_source_sha256\":\""<<damage::SOURCE_SHA256
      <<"\",\"heat_module\":\""<<heat::MODULE_VERSION<<"\",\"heat_source_version\":\""<<heat::SOURCE_VERSION
      <<"\",\"heat_source_sha256\":\""<<heat::SOURCE_SHA256
      <<"\",\"heat_strategies\":[\"disabled\",\""<<kHeatExperimental
      <<"\"],\"heat_threshold_policies\":[\""<<heat::policy_name(heat::ThresholdPolicy::StrictRejectUnadjudicated)
      <<"\",\""<<heat::policy_name(heat::ThresholdPolicy::EngineeringProseStrictGreaterV1)
      <<"\",\""<<heat::policy_name(heat::ThresholdPolicy::EngineeringFlowchartGreaterEqualV1)<<"\"],\"walls\":[";
    for(std::size_t i=0;i<sandbox::WALLS.size();++i) {if(i)o<<',';const auto w=sandbox::WALLS[i];o<<'['<<w.x1<<','<<w.y1<<','<<w.x2<<','<<w.y2<<']';}
    o << "],\"zones\":[";
    for(int z=0;z<4;++z) {if(z)o<<',';const auto r=objective::ZONES[z];o<<"{\"id\":\""<<objective::IDS[z]<<"\",\"label\":\""<<objective::LABELS[z]<<"\",\"rect\":["<<r.x1<<','<<r.y1<<','<<r.x2<<','<<r.y2<<"],\"original_owner\":"<<z%2<<'}';}
    o << "],\"capture_module\":\"" << cap::MODULE_VERSION << "\",\"match_module\":\"" << match::MODULE_VERSION << "\",\"assumptions\":["
      << "\"EXPERIMENTAL native combat plus objective vertical slice, not official RMUC or a win-rate validator.\","
      << "\"One authoritative60 Hz clock; post-combat qualified timestamps ceil(tick*1000/60).\","
      << "\"Placeholder inclusive center-in-rectangle eligibility for one living robot per team; unchanged synthetic sandbox combat.\","
      << "\"Ordinary loss uses verified2 s grace once. Death immediately resets detector Absent and clears grace under an experimental adapter.\","
      << "\"Default playable preset explicitly chooses engineering timer-before-input ordering and experimental end-of-second credits after capture, including420.\","
      << "\"Temporary control is latched for the rest of the run, without restoration or lifecycle adjudication.\","
      << "\"Public objective controller/degree and score/rate are deliberate experimental broadcasts, not inferred sensors. Hidden progress/occupancy/grace are never supplied.\","
      << "\"Disabled robots cannot move/fire and never respawn; no early KO ending. Their collision bodies may remain.\","
      << "\"Base HP1 is a non-destructible alive sentinel. Experimental projectile HP loss requires complete per-tick reconciliation; disabled/incomplete ledgers leave tied-control timeouts unresolved.\","
      << "\"Nominal projectile amount is not actual HP loss. Same-tick opposing hits are grouped per victim before HP clamping, without individual-hit overkill allocation.\","
      << "\"Remaining HP uses the verified aggregator with one connected, non-dismissed robot per team as an explicit experimental assumption. Damage and enemy HP totals are administrative only, never policy observations.\","
      << "\"Heat uses detected native shot events, never attempted fire; heat causes no HP loss or attack credit. Fixed borrowed infantry cooling-priority level1 Q0=40 and C=12 do not change synthetic HP100 or physics.\","
      << "\"Cooling at100ms multiples from epoch0 precedes the effective fire gate and shot; shot heat precedes death reset. These phase choices are experimental, not official simultaneous timing.\","
      << "\"Temporary heat lock lasts until heat0. Death clears heat and temporary reason, not the permanent round-long latch. Q2 equality rejects by default because prose and flowchart conflict.\","
      << "\"Only own heat telemetry enters observation revision2. Action SDK1 remains unchanged; enemy heat and shot/block counts remain administrative.\","
      << "\"Policies are trusted shared-process stateless C++; filtered snapshots are not security isolation.\"],\"unsupported\":["
      << "\"Full official RMUC physics, geometry, cards, multi-robot aggregation or referee timing\","
      << "\"Official death/revival lifecycle, temporary restoration, outpost rebuild and base destruction\","
      << "\"Official damage generation/settlement, referee penalties, friendly-fire credit or contested-to-neither adjudication\","
      << "\"First-person video cutoff/recovery simulation, permanent-only video behavior and any top-down map blackout\","
      << "\"Heat buffs, robot profile changes, 42mm heat, cooling epoch adjudication and other launcher lock reasons\","
      << "\"WASM tactical authority and untrusted-controller process isolation\"]}";
    return o.str();
}
} // namespace tactical
