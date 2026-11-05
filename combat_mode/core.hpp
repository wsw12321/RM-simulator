#pragma once
#include "policy_sdk.hpp"
#include "../src/sim.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>

#ifndef COMBAT_BUILD_ID
#define COMBAT_BUILD_ID "development-unpinned"
#endif
namespace combat {
using I = policy::Integer;
inline constexpr const char* kProfile = "filtered-combat-v1";
inline constexpr const char* kReplaySchema = "rmuc-filtered-combat-replay.v1";
inline constexpr const char* kEngine = "integer-mm-combat-v1";
inline constexpr I kMaxBatch = 120;
struct Request { std::string kind = "manual"; policy::Action action; };
struct Controller { policy::Descriptor descriptor; policy::DecisionFunction decide = nullptr; };
struct Input {
    std::string kind = "manual";
    I sdk_version = policy::kSdkVersion;
    std::string controller_id; // empty is JSON null / native '-' for manual
    std::array<I,5> action{};
};
struct Frame {
    I tick = 0;
    std::array<Input,2> inputs;
    std::array<std::uint32_t,2> observation_hashes{};
    std::uint32_t hash = 0;
};
policy::Observation make_observation(const sandbox::Engine&, int self);
std::string observation_json(const policy::Observation&);
std::uint32_t observation_hash(const policy::Observation&);
std::optional<std::string> validate_descriptor(const policy::Descriptor&);
std::string policy_metadata_json(const policy::Descriptor&);
std::string build_id();
class Engine {
public:
    sandbox::Engine native;
    std::vector<Frame> journal;
    std::vector<Frame> frames; // latest successful batch only
    Engine();
    void reset();
    std::optional<std::string> advance(I count, const std::array<Request,2>&,
                                      const std::array<Controller,2>& = {});
    // Resolved replay inputs only; never calls descriptor() or decide().
    std::optional<std::string> replay_frame(const Frame& expected);
    std::optional<std::string> replay_frames(const std::vector<Frame>& expected);
    std::string json(int observer, bool include_frames = false) const;
    std::string replay_json() const;
private:
    std::uint32_t initial_hash_ = 0;
};
} // namespace combat
