#include "policy_sdk.hpp"
#include <stdexcept>

namespace layered::policy {
Descriptor descriptor() { return {kSdkVersion, "synthetic-ramp-deck-v1"}; }

Action decide(const Observation& observation) {
    if (observation.sdk_version != kSdkVersion) throw std::runtime_error("SDK mismatch");
    // Fixture-specific, stateless feedback. No clock, random source, static state,
    // engine pointer, replay access, or user data. Every decision uses this pose.
    const auto& robot = observation.robot;
    const Integer x = robot.x_q1024, y = robot.y_q1024;
    auto move = [](Integer dx, Integer dy) { return Action{kSdkVersion, dx, dy}; };
    auto direction = [&](Integer current, Integer target) -> Integer {
        const Integer delta = target - current;
        // A 1 mm tolerance avoids oscillation at supported non-divisor speeds
        // and poses produced by manual quantized inputs. Default route is exact.
        if (delta >= -kXYScale && delta <= kXYScale) return 0;
        const Integer scaled = delta * 1000 / (observation.config.speed_mm_per_tick * kXYScale);
        return scaled < -1000 ? -1000 : scaled > 1000 ? 1000 : scaled;
    };
    if (robot.surface_id == "west_floor") {
        if (const auto dy = direction(y, 4500 * kXYScale)) return move(0, dy);
        return move(1000, 0);
    }
    if (robot.surface_id == "ramp") return move(1000, 0);
    if (robot.surface_id == "upper_platform") {
        // First clear the crest/seam corner, then move to the deck portal lane.
        if (y > 1500 * kXYScale + kXYScale && x < 7000 * kXYScale - kXYScale)
            return move(direction(x, 7000 * kXYScale), 0);
        if (const auto dy = direction(y, 1500 * kXYScale)) return move(0, dy);
        return move(-1000, 0);
    }
    if (robot.surface_id == "upper_deck") {
        if (const auto dy = direction(y, 1500 * kXYScale)) return move(0, dy);
        return move(direction(x, 4000 * kXYScale), 0);
    }
    // Other geometry/surfaces are outside this example's advertised route.
    return move(0, 0);
}
} // namespace layered::policy
