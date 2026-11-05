#pragma once
// C++17 experimental tactical SDK. Depends only on the filtered combat SDK and
// standard-library value types; it does not include any simulator or rule engine.
#include "../combat_mode/policy_sdk.hpp"
#include <array>
#include <string>

namespace tactical::policy {
using Integer = combat::policy::Integer;
using Scalar = combat::policy::Scalar;
using Action = combat::policy::Action;
using Descriptor = combat::policy::Descriptor;
using Point = combat::policy::Point;
using Rect = combat::policy::Rect;
using Arena = combat::policy::Arena;
using Self = combat::policy::Self;
using Enemy = combat::policy::Enemy;
using Projectile = combat::policy::Projectile;
using combat::policy::validate_action;
using combat::policy::aim_direction;
using combat::policy::kDirections;
inline constexpr Integer kSdkVersion = 1;
// Action validation/descriptor version remains1. Observation layout revision2.
inline constexpr Integer kObservationVersion = 2;
inline constexpr Integer kHz = combat::policy::kHz;
inline constexpr const char* kObservationProfile = "experimental-tactical-public-control-v2";

// Deliberate, global PUBLIC broadcasts, not locally sensed objective state.
// Team integers are 0 (red) or 1 (blue); degree is "full" or "temporary".
struct Zone {
    std::string id;
    Rect rect;
    Integer original_owner = 0;
    Integer controller = 0;
    std::string degree = "full";
};
// Only this controller's own heat is visible. Lock is the heat reason, not a
// claim about death, ammunition, cooldown, video, speed penalties or hardware.
struct SelfHeat {
    bool enabled = false;
    Integer heat_tenths = 0;
    Integer limit_tenths = 400;
    Integer cooling_per_second = 12;
    bool overheat_locked = false;
    bool permanent_latched = false;
    bool firing_locked = false;
};
// An owning immutable snapshot. The nested combat snapshot retains its original
// range/LOS filter. Only own heat and public geometry/control/score/clocks are added.
// No capture progress, detection, grace, occupancy, completion timestamps, enemy
// HP/heat, simulator events, hashes, terminal flag, handles, or previous snapshots.
struct Observation {
    const combat::policy::Observation combat;
    const std::array<Zone,4> zones{};
    const std::array<Integer,2> score{};
    const std::array<Integer,2> rate{};
    const Integer qualified_ms = 0;
    const Integer duration_ms = 420000;
    const bool credits_enabled = false;
    const SelfHeat self_heat{};
};
using DecisionFunction = Action (*)(const Observation&);

// Trusted same-process C++ only. Stateless snapshot-dependent policies are the
// supported contract; this interface is not a security or memory-isolation boundary.
Descriptor descriptor();
Action decide(const Observation& observation);
} // namespace tactical::policy
