// Line protocol adapter for the field_sim engine: one JSON object per line in,
// one JSON object per line out. Errors never change the simulation state.
#include "engine.hpp"
#include <iostream>
#include <memory>
#include <optional>

using namespace field_sim;
using json::Value;

namespace {
std::unique_ptr<Engine> engine;

Value fail(const std::string& message) {
    Value o = Value::object();
    o.set("error", message);
    return o;
}

Value init(const Value& req) {
    Field field = Field::load(req.at("field").as_string());
    long long seed = req.find("seed") ? req.at("seed").as_int() : 2027;
    if (seed < 0) throw json::Error("seed must not be negative");
    auto next = std::make_unique<Engine>(std::move(field), static_cast<std::uint64_t>(seed));
    std::array<std::size_t, 2> used{0, 0};
    for (const auto& r : req.at("robots").as_array()) {
        const std::string& team_name = r.at("team").as_string();
        if (team_name != "red" && team_name != "blue") throw json::Error("team must be 'red' or 'blue'");
        int team = team_name == "red" ? 0 : 1;
        Pose pose;
        if (const Value* spawn = r.find("spawn")) {
            pose.x = spawn->at("x").as_number();
            pose.y = spawn->at("y").as_number();
            pose.yaw = spawn->number_or("yaw", team == 0 ? 0.0 : PI);
        } else {
            const auto& slots = next->field().spawns[static_cast<std::size_t>(team)];
            if (used[static_cast<std::size_t>(team)] >= slots.size()) throw json::Error("no free start pose for team " + team_name);
            pose = slots[used[static_cast<std::size_t>(team)]++];
        }
        next->add_robot(r.at("id").as_string(), team, default_vehicle(), pose);
    }
    engine = std::move(next);
    Value o = Value::object();
    o.set("ok", true);
    o.set("build", build_id());
    o.set("state", engine->state_json());
    return o;
}

Value step(const Value& req) {
    if (!engine) throw json::Error("not initialised");
    long long ticks = req.find("ticks") ? req.at("ticks").as_int() : 1;
    if (ticks < 0 || ticks > 6000) throw json::Error("ticks must be 0..6000");
    std::vector<std::pair<std::size_t, Command>> parsed;
    if (const Value* commands = req.find("commands"))
        for (const auto& kv : commands->as_object()) {
            int index = engine->find(kv.first);
            if (index < 0) throw json::Error("unknown robot '" + kv.first + "'");
            parsed.emplace_back(static_cast<std::size_t>(index), parse_command(kv.second));
        }
    for (const auto& pc : parsed) engine->set_command(pc.first, pc.second);
    for (long long i = 0; i < ticks; ++i) engine->step();
    Value o = Value::object();
    o.set("ok", true);
    o.set("state", engine->state_json());
    return o;
}

Value probe(const Value& req) {
    if (!engine) throw json::Error("not initialised");
    const Terrain& t = engine->field().terrain;
    double x = req.at("x").as_number(), y = req.at("y").as_number();
    Value o = Value::object();
    o.set("ok", true);
    o.set("inside", t.inside(x, y));
    if (t.inside(x, y)) {
        int i = static_cast<int>(x / t.cell()), j = static_cast<int>(y / t.cell());
        o.set("ground", t.ground(i, j));
        o.set("floor", t.floor_z(y));
        if (t.has_overhang(i, j)) { o.set("overhang_bottom", t.overhang_bottom(i, j)); o.set("overhang_top", t.overhang_top(i, j)); }
        Value zones = Value::array();
        for (const Zone& z : engine->field().zones) if (z.contains(x, y)) zones.push(z.id);
        o.set("zones", zones);
    }
    return o;
}

Value ray(const Value& req) {
    if (!engine) throw json::Error("not initialised");
    auto point = [](const Value& v) {
        const auto& a = v.as_array();
        if (a.size() != 3) throw json::Error("a point is [x, y, z]");
        return Vec3{a[0].as_number(), a[1].as_number(), a[2].as_number()};
    };
    double t = engine->field().terrain.segment_hit(point(req.at("from")), point(req.at("to")));
    Value o = Value::object();
    o.set("ok", true);
    o.set("clear", t > 1.0);
    if (t <= 1.0) o.set("t", t);
    return o;
}

Value handle(const std::string& line) {
    Value req = json::parse(line);
    const std::string& cmd = req.at("cmd").as_string();
    if (cmd == "init") return init(req);
    if (cmd == "step") return step(req);
    if (cmd == "probe") return probe(req);
    if (cmd == "ray") return ray(req);
    if (cmd == "state") {
        if (!engine) throw json::Error("not initialised");
        Value o = Value::object();
        o.set("ok", true);
        o.set("state", engine->state_json());
        return o;
    }
    if (cmd == "meta") {
        Value o = Value::object();
        o.set("ok", true);
        o.set("profile", PROFILE);
        o.set("build", build_id());
        o.set("tick_hz", TICK_HZ);
        return o;
    }
    throw json::Error("unknown cmd '" + cmd + "'");
}
}

int main() {
    std::ios::sync_with_stdio(false);
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        Value out;
        try {
            out = handle(line);
        } catch (const std::exception& e) {
            out = fail(e.what());
        }
        std::cout << out.dump() << '\n' << std::flush;
    }
    return 0;
}
