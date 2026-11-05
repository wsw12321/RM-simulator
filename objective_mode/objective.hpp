#pragma once
#include "sim.hpp"
#include "capture_rules.hpp"
#include "match_rules.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>
#ifndef OBJECTIVE_BUILD_ID
#define OBJECTIVE_BUILD_ID "development-unpinned"
#endif
namespace objective {
namespace cap=rmuc2027::capture_v1;
namespace match=rmuc2027::match_v1;
inline constexpr const char* PROFILE="objective-experimental-v1";
inline constexpr const char* CREDIT="EXPERIMENTAL-end-of-second-v1";
inline constexpr const char* LIFECYCLE="EXPERIMENTAL-latched-temporary-no-lifecycle-v1";
inline constexpr const char* CLOCK="EXPERIMENTAL-ceil-sampled-ms-v1";
inline constexpr int DURATION_TICKS=25200;
inline constexpr std::array<sandbox::Rect,4> ZONES{{
    {4300,700,5900,2300},{6100,5300,7700,6900},
    {600,5600,2200,7200},{9800,400,11400,2000}}};
inline constexpr std::array<const char*,4> IDS{{"red-outpost","blue-outpost","red-base","blue-base"}};
inline constexpr std::array<const char*,4> LABELS{{"Red outpost","Blue outpost","Red base","Blue base"}};
struct Config {
    cap::BoundaryPolicy boundary{cap::BoundaryPolicy::StrictRejectUnadjudicated};
    bool credits{};
};
struct Zone {
    std::array<cap::ActivationDelay,2> detectors;
    cap::CaptureAttempt capture;
    std::optional<std::int64_t> completed_at_ms;
};
struct Frame {
    int tick;
    std::array<sandbox::Action,2> actions;
    std::uint32_t hash;
};
class Engine {
public:
    explicit Engine(Config={});
    // Each public mutation is a transaction. Errors commit nothing, including journal.
    std::optional<std::string> advance(int count,int mode,sandbox::Action human);
    std::optional<std::string> frame(int tick,const std::array<sandbox::Action,2>&);
    std::string json() const;
    std::string replay_json() const;
    std::uint32_t hash() const;
    const sandbox::State& world() const { return movement_.state; }
    const std::vector<Zone>& zones() const { return zones_; }
    const match::ExternalControlLedger& ledger() const { return ledger_; }
    const Config& config() const { return config_; }
    static std::int64_t time_ms(int tick) { return (std::int64_t(tick)*1000+59)/60; }
    static bool detected(sandbox::Robot,sandbox::Rect);
private:
    // Friend declared only for white-box boundary regression fixtures; no runtime API.
    friend struct AdapterTestProbe;
    Config config_;
    sandbox::Engine movement_{{DURATION_TICKS,true,false}};
    std::vector<Zone> zones_;
    match::ExternalControlLedger ledger_;
    match::MatchJudge judge_;
    match::Decision decision_;
    std::vector<Frame> journal_;
    std::optional<std::string> step_unchecked(const std::array<sandbox::Action,2>&);
    std::optional<std::string> advance_rules(std::int64_t,const std::array<std::array<bool,2>,4>&);
    match::ControlSnapshot control() const;
    sandbox::Action policy(int);
};
std::string meta_json();
}
