#pragma once
#include "core.hpp"
#include "policy_sdk.hpp"

namespace layered {
policy::Observation make_observation(const Engine& engine);
std::optional<std::string> validate_policy_descriptor(const policy::Descriptor& descriptor);
std::string policy_metadata_json(const policy::Descriptor& descriptor);
std::optional<std::string> advance_policy(Engine& engine, I count, I requested_sdk,
    const std::string& expected_controller, const policy::Descriptor& descriptor,
    policy::DecisionFunction decide);
}
