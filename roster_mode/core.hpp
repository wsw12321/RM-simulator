#pragma once
#include "policy_sdk.hpp"
#include "../src/sim.hpp"
#include "../objective_mode/objective.hpp"
#include "../rules/rmuc2027_damage_v1/damage_rules.hpp"
#include "../rules/rmuc2027_heat_v1/heat_rules.hpp"
#ifndef ROSTER_BUILD_ID
#define ROSTER_BUILD_ID "development-unpinned"
#endif
namespace roster {
namespace cap=rmuc2027::capture_v1;
namespace match=rmuc2027::match_v1;
namespace damage=rmuc2027::damage_v1;
namespace heat=rmuc2027::heat_v1;
using I=policy::Integer;
inline constexpr const char* kProfile="experimental-roster-tactical-v1";
inline constexpr const char* kReplaySchema="rmuc-experimental-roster-replay.v1";
inline constexpr const char* kEngine="integer-mm-roster-v1";
inline constexpr const char* kHeat="SYNTHETIC-per-robot-17mm-v1";
inline constexpr const char* kCredit="EXPERIMENTAL-end-of-second-v1";
inline constexpr const char* kDamage="EXPERIMENTAL-reconciled-projectile-hp-loss-v1";
inline constexpr const char* kDamageMissing="missing-adjudicated-attack-damage";
inline constexpr const char* kHeatBoundary="EXPERIMENTAL-cooling-before-fire-gate-and-shot-v1";
inline constexpr I kMaxBatch=600,kDurationTicks=25200;
using Profile=policy::Profile;
using RosterEntry=policy::RosterEntry;
struct Config {
    cap::BoundaryPolicy boundary=cap::BoundaryPolicy::EngineeringTimerBeforeInputV1;
    bool credits=true,damage_enabled=true,heat_enabled=true;
    heat::ThresholdPolicy heat_threshold=heat::ThresholdPolicy::StrictRejectUnadjudicated;
    std::vector<RosterEntry> robots;
    Config();
};
struct Robot { int x=0,y=0,yaw=0,hp=0,ammo=0,cooldown=0,shots=0,hits=0; };
struct Bullet { int id=0,owner=0,x=0,y=0,vx=0,vy=0,life=180,damage=0; };
struct State { int tick=0,next_id=1,terminal=0; std::vector<Robot> robots; std::vector<Bullet> bullets; };
struct Event { int tick=0,kind=0,actor=0,target=-1,amount=0; };
struct Request { std::string id,kind="manual"; policy::Action action; };
struct Controller { std::string id; policy::Descriptor descriptor; policy::DecisionFunction decide=nullptr; };
struct Input {
    std::string id,kind="manual";
    I sdk_version=policy::kSdkVersion;
    std::string controller_id;
    std::array<I,5> action{};
    std::uint32_t observation_hash=0;
};
struct Frame { I tick=0; std::vector<Input> inputs; std::uint32_t hash=0; };
struct Zone {
    std::vector<cap::ActivationDelay> detectors;
    cap::CaptureAttempt capture;
    std::optional<I> completed_at_ms;
};
std::string build_id();
std::optional<std::string> validate_identifier(const std::string&,std::size_t maximum=32);
std::optional<std::string> validate_descriptor(const policy::Descriptor&);
std::optional<std::string> validate_config(const Config&);
std::string config_json(const Config&);
std::string observation_json(const policy::Observation&);
std::uint32_t observation_hash(const policy::Observation&);
std::string policy_metadata_json(const policy::Descriptor&);
std::string meta_json();
class Engine {
public:
    // Administrative state. Never supplied to trusted policies or public bridge.
    State state;
    std::vector<Event> events;
    std::vector<Frame> journal,frames;
    explicit Engine(Config={});
    void reset(Config={}); // invalid config throws without mutation
    const Config& config() const {return config_;}
    std::size_t index(const std::string&) const;
    policy::Observation observation(const std::string&) const;
    std::optional<std::string> advance(I,const std::vector<Request>&,const std::vector<Controller>& = {});
    std::optional<std::string> replay_frame(const Frame&);
    std::optional<std::string> replay_frames(const std::vector<Frame>&);
    std::uint32_t hash() const;
    std::string json(const std::string&,bool include_frames=false) const;
    std::string replay_json() const;
    static I time_ms(int tick) {return (I(tick)*1000+59)/60;}
private:
    friend struct TestProbe;
    friend struct ReviewTestProbe;
    Config config_;
    std::vector<Zone> zones_;
    match::ExternalControlLedger ledger_;
    match::MatchJudge judge_;
    match::Decision decision_;
    std::optional<match::Error> unresolved_;
    damage::Ledger damage_;
    std::vector<heat::Heat> heat_;
    std::vector<I> blocked_fire_ticks_;
    bool damage_complete_=true;
    std::array<I,2> nominal_projectile_damage_{};
    std::vector<int> expected_hp_;
    std::uint32_t initial_hash_=0;
    void initialize(Config);
    match::ControlSnapshot control() const;
    std::optional<std::string> step_unchecked(const std::vector<sandbox::Action>&);
    std::optional<std::string> advance_rules(I,const std::vector<bool>&);
    std::optional<std::string> reconcile_damage(const std::vector<int>&);
    std::optional<std::array<I,2>> remaining_robot_hp() const;
    std::string result_json() const;
    std::string damage_admin_json() const;
    std::string heat_admin_json() const;
};
} // namespace roster
