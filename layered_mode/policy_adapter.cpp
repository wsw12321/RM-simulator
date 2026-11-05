#include "policy_adapter.hpp"
#include <sstream>
#include <stdexcept>

namespace layered {
namespace {
policy::Point point(Point p) { return {p.x, p.y}; }
policy::Polygon polygon(const Polygon& source) {
    policy::Polygon result;
    result.reserve(source.size());
    for (const auto& p : source) result.push_back(point(p));
    return result;
}
}
policy::Observation make_observation(const Engine& e) {
    if (!e.ready) throw std::runtime_error("Cannot observe an unloaded native engine.");
    policy::Geometry geometry;
    geometry.boundary = polygon(e.map.boundary);
    for (const auto& s : e.map.surfaces) {
        geometry.surfaces.push_back({s.id, s.layer, polygon(s.polygon), s.z, s.dx, s.dy,
            s.has_ceiling ? std::optional<I>(s.ceiling) : std::nullopt, s.traversable});
    }
    auto volumes = [&](const std::vector<Volume>& source, std::vector<policy::Volume>& target) {
        for (const auto& v : source) {
            policy::Volume out{v.id, polygon(v.polygon), v.low, v.high, {}};
            for (int index : v.scopes) out.surface_ids.push_back(e.map.surfaces.at(index).id);
            target.push_back(std::move(out));
        }
    };
    volumes(e.map.solids, geometry.solids);
    volumes(e.map.triggers, geometry.triggers);
    for (const auto& p : e.map.portals)
        geometry.portals.push_back({p.id, e.map.surfaces.at(p.from).id,
            e.map.surfaces.at(p.to).id, point(p.a), point(p.b), p.bidirectional});
    std::vector<policy::Contact> contacts;
    for (const auto& c : e.contacts) contacts.push_back({c.kind, c.id});
    std::vector<std::string> active;
    for (std::size_t i = 0; i < e.active.size(); ++i)
        if (e.active[i]) active.push_back(e.map.triggers.at(i).id);
    std::vector<policy::Event> events;
    for (const auto& event : e.events)
        events.push_back({event.tick, event.substep, event.type, event.id, event.from, event.to});
    const auto& s = e.map.surfaces.at(e.surface);
    const I z = s.z * Q * Q + s.dx * e.position.x + s.dy * e.position.y;
    return {policy::kSdkVersion, policy::kHz, e.tick,
        {e.position.x, e.position.y, z, s.id, s.layer},
        {e.config.radius, e.config.height, e.config.speed, e.config.max_slope, e.config.max_ticks},
        std::move(geometry), std::move(contacts), std::move(active), std::move(events)};
}
std::optional<std::string> validate_policy_descriptor(const policy::Descriptor& d) {
    if (d.sdk_version != policy::kSdkVersion) return "Compiled controller SDK version mismatch.";
    const auto& id = d.controller_id;
    if (id.empty() || id.size() > 64 ||
        !((id[0] >= 'a' && id[0] <= 'z') || (id[0] >= 'A' && id[0] <= 'Z')) ||
        id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != std::string::npos)
        return "Controller identifier must be 1..64 bounded ASCII identifier bytes.";
    return std::nullopt;
}
std::string policy_metadata_json(const policy::Descriptor& d) {
    if (auto error = validate_policy_descriptor(d)) throw std::runtime_error(*error);
    std::ostringstream out;
    out << "{\"sdk_version\":" << d.sdk_version << ",\"controller_id\":\"" << d.controller_id
        << "\",\"execution\":\"trusted-local-native\",\"observation\":\""
        << policy::kObservationProfile
        << "\",\"state_mode\":\"stateless\",\"decision_hz\":60,\"watchdog_seconds\":20}";
    return out.str();
}
std::optional<std::string> advance_policy(Engine& engine, I count, I requested_sdk,
    const std::string& expected_controller, const policy::Descriptor& descriptor,
    policy::DecisionFunction decide) {
    if (!engine.ready) return "Native map is not loaded.";
    if (count < 1 || count > 600) return "Policy count outside [1,600].";
    if (requested_sdk != policy::kSdkVersion) return "Requested policy SDK version mismatch.";
    if (auto error = validate_policy_descriptor(descriptor)) return error;
    if (descriptor.controller_id != expected_controller) return "Requested controller identity mismatch.";
    if (!decide) return "Missing policy entrypoint.";
    if (count > engine.config.max_ticks - engine.tick) return "Native duration exhausted; no judgement is fabricated.";
    try {
        Engine candidate = engine;
        std::vector<Frame> frames;
        std::vector<Event> batch_events;
        for (I i = 0; i < count; ++i) {
            const auto snapshot = make_observation(candidate);
            const auto action = decide(snapshot); // exactly once, before this tick
            if (auto error = policy::validate_action(action))
                return "Policy output invalid; experiment invalid, batch unchanged: " + *error;
            if (auto error = candidate.advance(1, std::get<I>(action.dx), std::get<I>(action.dy))) return error;
            if (batch_events.size() + candidate.events.size() > 8192)
                return "Native batch event bound exceeded; reduce count. Existing run unchanged.";
            frames.push_back(candidate.frames.front());
            batch_events.insert(batch_events.end(), candidate.events.begin(), candidate.events.end());
        }
        candidate.frames = std::move(frames);
        candidate.batch_events = std::move(batch_events);
        engine = std::move(candidate);
        return std::nullopt;
    } catch (const std::exception&) {
        // Do not return arbitrary exception text from trusted user source as protocol data.
        return "Policy exception; experiment invalid, batch unchanged.";
    } catch (...) {
        return "Unknown policy exception; experiment invalid, batch unchanged.";
    }
}
} // namespace layered
