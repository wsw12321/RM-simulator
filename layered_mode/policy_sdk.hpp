#pragma once
// Public C++17 SDK. This header deliberately has no Engine or bridge dependency.
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace layered::policy {
using Integer = std::int64_t;
inline constexpr Integer kSdkVersion = 1;
inline constexpr Integer kHz = 60;
inline constexpr Integer kXYScale = 1024;
inline constexpr Integer kZScale = 1048576;
inline constexpr const char* kObservationProfile = "full-authored-geometry";

// The tagged values deliberately preserve bool/float mistakes until validation.
// Plain integer literals and Integer values are valid; bool and double are not.
using Scalar = std::variant<Integer, double, bool>;
struct Action {
    Scalar sdk_version = kSdkVersion;
    Scalar dx = Integer{0};
    Scalar dy = Integer{0};
};
inline std::optional<std::string> validate_action(const Action& action) {
    const auto* version = std::get_if<Integer>(&action.sdk_version);
    if (!version || *version != kSdkVersion) return "Unsupported action SDK version; expected integer 1.";
    for (const auto* component : {&action.dx, &action.dy}) {
        const auto* value = std::get_if<Integer>(component);
        if (!value || *value < -1000 || *value > 1000)
            return "Action components must be integers in [-1000,1000], not boolean or float.";
    }
    return std::nullopt;
}

struct Point { Integer x_q1024 = 0, y_q1024 = 0; };
using Polygon = std::vector<Point>; // 3..16 vertices, owning storage
struct Surface {
    std::string id, layer_id; // validated ASCII identifiers, each <=64 bytes
    Polygon polygon;
    Integer z_mm = 0, dx_q1024 = 0, dy_q1024 = 0;
    std::optional<Integer> ceiling_z_mm;
    bool traversable = true;
};
struct Volume {
    std::string id;
    Polygon polygon;
    Integer z_min_mm = 0, z_max_mm = 0;
    std::vector<std::string> surface_ids; // trigger scopes; empty for global solids
};
struct Portal {
    std::string id, from_surface, to_surface;
    Point a, b;
    bool bidirectional = false; // all SDK v1 portals have zero step
};
struct Geometry {
    Polygon boundary;
    std::vector<Surface> surfaces; // <=32, authored order
    std::vector<Volume> solids;   // <=64, global regardless of layer
    std::vector<Volume> triggers; // <=32, informational only
    std::vector<Portal> portals;  // <=64
};
struct Configuration {
    Integer radius_mm = 0, height_mm = 0, speed_mm_per_tick = 0;
    Integer max_slope_q1024 = 0, max_ticks = 0;
};
struct Robot {
    Integer x_q1024 = 0, y_q1024 = 0, z_q1048576 = 0;
    std::string surface_id, layer_id;
};
struct Contact { std::string kind, id; };
struct Event {
    Integer tick = 0;
    int substep = 0;
    std::string type, id, from_surface, to_surface;
};
// An owning, immutable pre-step snapshot: no engine references, pointers, hashes,
// journal, provenance paths/URLs, calibration text, or external/plugin data.
// Contacts/events describe the previous completed tick (initial enters at tick 0).
// Full authored geometry is intentional; this is not an official sensing model.
struct Observation {
    const Integer sdk_version = kSdkVersion;
    const Integer hz = kHz;
    const Integer tick = 0;
    const Robot robot;
    const Configuration config;
    const Geometry geometry;
    const std::vector<Contact> contacts; // <=68
    const std::vector<std::string> active_trigger_ids; // <=32
    const std::vector<Event> events; // <=3250
};
struct Descriptor {
    Integer sdk_version = kSdkVersion;
    std::string controller_id;
};
using DecisionFunction = Action (*)(const Observation&);

// Implement these two entrypoints in example_policy.cpp, then rebuild locally.
// Only stateless policies depending on the supplied snapshot are supported.
Descriptor descriptor();
Action decide(const Observation& observation);
} // namespace layered::policy
