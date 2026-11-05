#pragma once
#include "policy_sdk.hpp"
#include "../combat_mode/core.hpp"
#include "../objective_mode/objective.hpp"
#include "../rules/rmuc2027_damage_v1/damage_rules.hpp"
#include "../rules/rmuc2027_heat_v1/heat_rules.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>
#ifndef TACTICAL_BUILD_ID
#define TACTICAL_BUILD_ID "development-unpinned"
#endif
namespace tactical {
namespace cap = rmuc2027::capture_v1;
namespace match = rmuc2027::match_v1;
namespace damage = rmuc2027::damage_v1;
namespace heat = rmuc2027::heat_v1;
using I = combat::I;
using Request = combat::Request;
using Input = combat::Input;
using Frame = combat::Frame;
struct Controller { policy::Descriptor descriptor; policy::DecisionFunction decide = nullptr; };
inline constexpr const char* kProfile = "experimental-tactical-v1";
inline constexpr const char* kReplaySchema = "rmuc-experimental-tactical-replay.v1";
inline constexpr const char* kEngine = "integer-mm-tactical-v3";
inline constexpr const char* kPreset = "EXPERIMENTAL-playable-engineering-v3";
inline constexpr const char* kGeometry = "EXPERIMENTAL-inclusive-center-rectangle-v1";
inline constexpr const char* kEligibility = "EXPERIMENTAL-one-alive-eligible-robot-per-team-v1";
inline constexpr const char* kDeath = "EXPERIMENTAL-immediate-death-deactivation-v1";
inline constexpr const char* kLifecycle = "EXPERIMENTAL-latched-temporary-no-lifecycle-v1";
inline constexpr const char* kClock = "EXPERIMENTAL-ceil-sampled-ms-v1";
inline constexpr const char* kCredit = "EXPERIMENTAL-end-of-second-v1";
inline constexpr const char* kPhase = "EXPERIMENTAL-cooling-gate-combat-shot-heat-death-damage-capture-credit-judge-v3";
inline constexpr const char* kDamageMissing = "missing-adjudicated-attack-damage";
inline constexpr const char* kDamageExperimental = "EXPERIMENTAL-reconciled-projectile-hp-loss-v1";
inline constexpr const char* kRemainingHp = "EXPERIMENTAL-one-robot-per-team-connected-not-dismissed-v1";
inline constexpr const char* kHeatExperimental = "EXPERIMENTAL-fixed-level1-cooling-priority-17mm-v1";
inline constexpr const char* kHeatBoundary = "EXPERIMENTAL-cooling-before-fire-gate-and-shot-v1";
inline constexpr const char* kHeatDeath = "EXPERIMENTAL-observed-shot-before-death-reset-v1";
enum class HeatStrategy : std::uint8_t { Disabled, ExperimentalFixedLevel1CoolingPriority17mmV1 };
enum class DamageStrategy : std::uint8_t { MissingAdjudicated, ExperimentalReconciledProjectileHpLossV1 };
inline constexpr I kMaxBatch = 600;
inline constexpr int kDurationTicks = 25200;
struct Config {
    cap::BoundaryPolicy boundary = cap::BoundaryPolicy::EngineeringTimerBeforeInputV1;
    bool credits = true;
    DamageStrategy damage_strategy = DamageStrategy::ExperimentalReconciledProjectileHpLossV1;
    HeatStrategy heat_strategy = HeatStrategy::ExperimentalFixedLevel1CoolingPriority17mmV1;
    heat::ThresholdPolicy heat_threshold = heat::ThresholdPolicy::StrictRejectUnadjudicated;
};
struct Zone {
    std::array<cap::ActivationDelay,2> detectors;
    cap::CaptureAttempt capture;
    std::optional<std::int64_t> completed_at_ms;
};
std::string build_id();
std::string config_json(const Config&);
std::string observation_json(const policy::Observation&);
std::uint32_t observation_hash(const policy::Observation&);
std::string policy_metadata_json(const policy::Descriptor&);
std::string meta_json();
class Engine {
public:
    // Administrative engine state is never passed to a controller. This is a
    // trusted in-process interface, NOT a security boundary for malicious C++.
    sandbox::Engine native{{kDurationTicks,false,false}};
    std::vector<Frame> journal;
    std::vector<Frame> frames;
    explicit Engine(Config = {});
    void reset(Config = {});
    const Config& config() const { return config_; }
    policy::Observation observation(int self) const;
    std::optional<std::string> advance(I, const std::array<Request,2>&,
                                      const std::array<Controller,2>& = {});
    std::optional<std::string> replay_frame(const Frame&);
    std::optional<std::string> replay_frames(const std::vector<Frame>&);
    std::string json(int observer, bool include_frames = false) const;
    std::string replay_json() const;
    std::uint32_t hash() const;
    static std::int64_t time_ms(int tick) { return (std::int64_t(tick)*1000+59)/60; }
private:
    friend struct TestProbe;
    friend struct ReviewTestProbe;
    friend struct DamageTestProbe;
    friend struct DamageReviewTestProbe;
    friend struct HeatTestProbe;
    friend struct HeatReviewTestProbe;
    Config config_;
    std::vector<Zone> zones_;
    match::ExternalControlLedger ledger_;
    match::MatchJudge judge_;
    match::Decision decision_;
    std::optional<match::Error> unresolved_;
    damage::Ledger damage_;
    std::vector<heat::Heat> heat_;
    std::array<std::int64_t,2> blocked_fire_ticks_{};
    bool damage_complete_ = true;
    std::array<std::int64_t,2> nominal_projectile_damage_{};
    std::array<int,2> expected_hp_{};
    std::uint32_t initial_hash_ = 0;
    match::ControlSnapshot control() const;
    std::optional<std::string> step_unchecked(const std::array<sandbox::Action,2>&);
    std::optional<std::string> advance_rules(std::int64_t,
        const std::array<std::array<bool,2>,4>& physical, const std::array<bool,2>& died);
    std::string result_json() const;
    void reconcile_damage(const std::array<int,2>& hp_before, int prior_tick);
    std::optional<std::array<std::int64_t,2>> remaining_robot_hp() const;
    std::string damage_admin_json() const;
    std::string heat_admin_json() const;
};
} // namespace tactical
