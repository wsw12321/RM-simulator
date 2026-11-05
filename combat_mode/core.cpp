#include "core.hpp"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace combat {
namespace {
bool visible(const sandbox::Robot& self, int x, int y) {
    const auto dx = I(x) - self.x, dy = I(y) - self.y;
    return dx*dx + dy*dy <= policy::kSensorRangeMm*policy::kSensorRangeMm &&
        !sandbox::line_blocked({self.x,self.y}, {x,y});
}
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
        if (auto e = validate_descriptor({in.sdk_version,in.controller_id})) return e;
    }
    return policy::validate_action(sdk_action(in));
}
}

policy::Observation make_observation(const sandbox::Engine& e, int index) {
    if (index < 0 || index > 1) throw std::invalid_argument("Observer must be 0 or 1.");
    const auto& self = e.state.robots[index];
    const auto& other = e.state.robots[1-index];
    policy::Arena arena;
    for (const auto& wall : sandbox::WALLS) arena.walls.push_back({wall.x1,wall.y1,wall.x2,wall.y2});
    std::optional<policy::Enemy> enemy;
    // HP deliberately does not affect presence: it is not part of the sensor.
    if (visible(self,other.x,other.y)) enemy = policy::Enemy{other.x,other.y,other.yaw};
    std::vector<policy::Projectile> projectiles;
    for (const auto& bullet : e.state.bullets) {
        if (visible(self,bullet.x,bullet.y)) projectiles.push_back({bullet.x,bullet.y,bullet.vx,bullet.vy});
    }
    std::sort(projectiles.begin(), projectiles.end(), [](const auto& a, const auto& b) {
        return std::tie(a.x,a.y,a.vx,a.vy) < std::tie(b.x,b.y,b.vx,b.vy);
    });
    return {policy::kSdkVersion,policy::kHz,e.state.tick,index,policy::kSensorRangeMm,
        {self.x,self.y,self.yaw,self.hp,self.ammo,self.cooldown},std::move(arena),enemy,std::move(projectiles)};
}
std::string observation_json(const policy::Observation& obs) {
    std::ostringstream out;
    out << "{\"sdk_version\":" << obs.sdk_version << ",\"profile\":\"" << policy::kObservationProfile
        << "\",\"hz\":" << obs.hz << ",\"tick\":" << obs.tick << ",\"self_index\":" << obs.self_index
        << ",\"sensor_range_mm\":" << obs.sensor_range_mm << ",\"self\":{\"x\":" << obs.self.x
        << ",\"y\":" << obs.self.y << ",\"yaw\":" << obs.self.yaw << ",\"hp\":" << obs.self.hp
        << ",\"ammo\":" << obs.self.ammo << ",\"cooldown\":" << obs.self.cooldown << "},\"arena\":{\"width\":"
        << obs.arena.width << ",\"height\":" << obs.arena.height << ",\"radius\":" << obs.arena.radius
        << ",\"speed_per_tick\":" << obs.arena.speed_per_tick << ",\"projectile_speed_per_tick\":"
        << obs.arena.projectile_speed_per_tick << ",\"walls\":[";
    for (std::size_t i=0; i<obs.arena.walls.size(); ++i) {
        if (i) out << ',';
        const auto& w = obs.arena.walls[i];
        out << "{\"x1\":" << w.x1 << ",\"y1\":" << w.y1 << ",\"x2\":" << w.x2 << ",\"y2\":" << w.y2 << '}';
    }
    out << "]},\"enemy\":";
    if (!obs.enemy) out << "null";
    else out << "{\"x\":" << obs.enemy->x << ",\"y\":" << obs.enemy->y << ",\"yaw\":" << obs.enemy->yaw << '}';
    out << ",\"projectiles\":[";
    for (std::size_t i=0; i<obs.projectiles.size(); ++i) {
        if (i) out << ',';
        const auto& b = obs.projectiles[i];
        out << "{\"x\":" << b.x << ",\"y\":" << b.y << ",\"vx\":" << b.vx << ",\"vy\":" << b.vy << '}';
    }
    out << "]}";
    return out.str();
}
std::uint32_t observation_hash(const policy::Observation& obs) { return fnv(observation_json(obs)); }
std::optional<std::string> validate_descriptor(const policy::Descriptor& d) {
    if (d.sdk_version != policy::kSdkVersion) return "Compiled controller SDK version mismatch.";
    const auto& id=d.controller_id;
    if (id.empty() || id.size()>64 || !((id[0]>='a'&&id[0]<='z')||(id[0]>='A'&&id[0]<='Z')) ||
        id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)
        return "Controller identifier must be 1..64 bounded ASCII identifier bytes.";
    return std::nullopt;
}
std::string policy_metadata_json(const policy::Descriptor& d) {
    if (auto error = validate_descriptor(d)) throw std::invalid_argument(*error);
    std::ostringstream out;
    out << "{\"sdk_version\":" << d.sdk_version << ",\"controller_id\":\"" << d.controller_id
        << "\",\"execution\":\"trusted-local-native\",\"observation\":\"" << policy::kObservationProfile
        << "\",\"state_mode\":\"stateless\",\"decision_hz\":60}";
    return out.str();
}
std::string build_id() { return COMBAT_BUILD_ID; }
Engine::Engine() { reset(); }
void Engine::reset() {
    native.reset(); journal.clear(); frames.clear(); initial_hash_=native.hash();
}
std::optional<std::string> Engine::advance(I count, const std::array<Request,2>& requests,
                                         const std::array<Controller,2>& controllers) {
    if (count < 1 || count > kMaxBatch) return "Step count outside [1,120].";
    if (native.state.terminal) return "Combat is terminal; reset to start another run.";
    // No controller invocation during request/metadata validation.
    for (int i=0; i<2; ++i) {
        const auto& r=requests[i];
        if (r.kind != "manual" && r.kind != "policy") return "Input kind must be manual or policy.";
        if (r.kind == "manual") {
            if (auto error=policy::validate_action(r.action)) return error;
        }
        if (r.kind == "policy") {
            const auto* version=std::get_if<I>(&r.action.sdk_version);
            if (!version || *version != policy::kSdkVersion) return "Requested policy SDK version mismatch.";
            if (auto e=validate_descriptor(controllers[i].descriptor)) return e;
            if (!controllers[i].decide) return "Missing policy entrypoint.";
        }
    }
    try {
        Engine candidate=*this; candidate.frames.clear();
        for (I n=0; n<count && !candidate.native.state.terminal; ++n) {
            // Freeze BOTH owning snapshots before invoking EITHER callback.
            const std::array<policy::Observation,2> observations{
                make_observation(candidate.native,0), make_observation(candidate.native,1)};
            std::array<policy::Action,2> actions;
            for (int i=0; i<2; ++i)
                actions[i] = requests[i].kind == "policy" ? controllers[i].decide(observations[i]) : requests[i].action;
            // Collect both decisions before validating or applying either.
            for (const auto& action : actions)
                if (auto error=policy::validate_action(action)) return *error;
            Frame frame;
            frame.tick=candidate.native.state.tick;
            for (int i=0; i<2; ++i) {
                frame.inputs[i]=resolved(requests[i],actions[i],controllers[i]);
                frame.observation_hashes[i]=observation_hash(observations[i]);
            }
            if (!candidate.native.step({native_action(actions[0]),native_action(actions[1])}))
                return "Native action rejected; batch unchanged.";
            frame.hash=candidate.native.hash();
            candidate.journal.push_back(frame); candidate.frames.push_back(frame);
        }
        *this=std::move(candidate);
        return std::nullopt;
    } catch (...) {
        // Exception strings may contain unrelated secrets or private paths.
        return "Policy exception; experiment invalid, batch unchanged.";
    }
}
std::optional<std::string> Engine::replay_frame(const Frame& expected) {
    return replay_frames({expected});
}
std::optional<std::string> Engine::replay_frames(const std::vector<Frame>& expected_frames) {
    if (expected_frames.empty() || expected_frames.size() > std::size_t(kMaxBatch)) return "Replay batch count outside [1,120].";
    try {
        Engine candidate=*this; candidate.frames.clear();
        for (const auto& expected : expected_frames) {
            if (expected.tick != candidate.native.state.tick || candidate.native.state.terminal)
                return "Replay tick is not the next live tick; batch unchanged.";
            for (const auto& input : expected.inputs) if (auto error=valid_input(input)) return error;
            for (int i=0; i<2; ++i)
                if (expected.observation_hashes[i] != observation_hash(make_observation(candidate.native,i)))
                    return "Replay observation hash mismatch; batch unchanged.";
            if (!candidate.native.step({native_action(sdk_action(expected.inputs[0])),native_action(sdk_action(expected.inputs[1]))}))
                return "Replay action rejected; batch unchanged.";
            if (candidate.native.hash() != expected.hash) return "Replay state hash mismatch; batch unchanged.";
            candidate.frames.push_back(expected); candidate.journal.push_back(expected);
        }
        *this=std::move(candidate);
        return std::nullopt;
    } catch (...) { return "Replay failed; batch unchanged."; }
}
std::string Engine::json(int observer, bool include_frames) const {
    std::ostringstream out;
    out << "{\"build\":\"" << build_id() << "\",\"schema\":1,\"profile\":\"" << kProfile
        << "\",\"observation_profile\":\"" << policy::kObservationProfile << "\",\"official\":false,\"observer\":"
        << observer << ",\"tick\":" << native.state.tick << ",\"terminal\":" << native.state.terminal
        << ",\"hash\":" << native.hash() << ",\"observation\":" << observation_json(make_observation(native,observer))
        << ",\"last_sources\":";
    if (journal.empty()) out << "null";
    else { out << '['; source_json(out,journal.back().inputs[0]); out << ','; source_json(out,journal.back().inputs[1]); out << ']'; }
    out << ",\"frames\":";
    if (include_frames) frames_json(out,frames); else out << "[]";
    out << '}'; return out.str();
}
std::string Engine::replay_json() const {
    std::ostringstream out;
    out << "{\"schema\":\"" << kReplaySchema << "\",\"profile\":\"" << kProfile << "\",\"build\":\"" << build_id()
        << "\",\"engine\":\"" << kEngine << "\",\"observation_profile\":\"" << policy::kObservationProfile
        << "\",\"official\":false,\"initial_hash\":" << initial_hash_ << ",\"frames\":";
    frames_json(out,journal); out << ",\"final_hash\":" << native.hash() << '}'; return out.str();
}
} // namespace combat
