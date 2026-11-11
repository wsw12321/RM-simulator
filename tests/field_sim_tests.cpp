// Native tests for field_sim: JSON helper, layered terrain, the extracted
// official field and the 2.5-D chassis / gimbal model.
#include "../field_sim/engine.hpp"
#include "rule_check.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>

using namespace field_sim;

namespace {
const char* FIELD_DIR = "field_sim/field/rmuc2027_v1";

bool throws(const std::string& text) {
    try { json::parse(text); } catch (const json::Error&) { return true; }
    return false;
}
template<class F> bool raises(F f) {
    try { f(); } catch (const std::exception&) { return true; }
    return false;
}
Field synthetic(Terrain t) {
    Field f;
    f.id = "synthetic";
    f.terrain = std::move(t);
    return f;
}
Command drive_cmd(double vx, double vy = 0, double wz = 0) {
    Command c;
    c.vx = vx; c.vy = vy; c.wz = wz;
    return c;
}
// The driving tests were calibrated with an 80 W chassis allowance; rule-table values are tested separately.
std::size_t add(Engine& e, const std::string& id, int team, const VehicleParams& v, const Pose& pose, double limit_w = 80.0) {
    std::size_t i = e.add_robot(id, team, v, pose);
    e.robot(i).power_limit = limit_w;
    return i;
}
double rel(const Terrain& t, double x, double y) { return t.surface(x, y, -1e6) - t.floor_z(y); }

void json_tests() {
    json::Value v = json::parse(R"({"a":[1,2.5,-3e2,true,null,"x\n\u00e9\ud83d\ude00"],"b":{"c":{}}})");
    CHECK(v.at("a").as_array().size() == 6);
    CHECK(v.at("a").as_array()[2].as_number() == -300);
    CHECK(v.at("a").as_array()[5].as_string() == "x\n\xc3\xa9\xf0\x9f\x98\x80");
    CHECK(v.at("b").at("c").as_object().empty());
    CHECK(json::parse(v.dump()).dump() == v.dump());
    CHECK(json::Value(0.1).dump() == "0.1");
    CHECK(json::Value(3.0).dump() == "3");
    for (const char* bad : {"", "{", "[1,]", "{\"a\":1,\"a\":2}", "01", "1.", "\"\\x\"", "nul", "[1] 2", "{\"a\" 1}", "\"\\ud800\"", "1e999", "\"\t\""})
        CHECK(throws(bad));
    CHECK(raises([&] { v.at("missing"); }));
    CHECK(raises([&] { v.at("a").as_number(); }));
    CHECK(raises([] { json::parse("1.5").as_int(); }));
    CHECK(raises([] { parse_command(json::parse(R"({"chassis":{"vx":1,"oops":2}})")); }));
    CHECK(raises([] { parse_command(json::parse(R"({"chassis":{"vx":1e9}})")); }));
    CHECK(raises([] { parse_command(json::parse(R"({"gimbal":{"yaw":1}})")); }));
    Command c = parse_command(json::parse(R"({"chassis":{"vx":1,"vy":-2,"wz":3,"frame":"gimbal"},"gimbal":{"mode":"rate","yaw_rate":0.5},"supercap":false})"));
    CHECK(c.vx == 1 && c.vy == -2 && c.wz == 3 && c.frame == ChassisFrame::Gimbal && c.gimbal == GimbalMode::Rate && c.yaw_rate == 0.5 && !c.supercap);
}

void terrain_tests() {
    Terrain t = Terrain::flat(100, 50, 0.02, 0.0);        // 2 m x 1 m
    CHECK(t.inside(0, 0) && t.inside(1.99, 0.99) && !t.inside(2.0, 0.5) && !t.inside(-0.001, 0.5));
    // a 0.2 m block and a roof spanning y in [0.4, 0.6) between z 0.25 and 0.30
    for (int i = 50; i < 60; ++i) for (int j = 0; j < 50; ++j) t.set_ground(i, j, 0.2);
    for (int i = 20; i < 30; ++i) for (int j = 20; j < 30; ++j) t.set_overhang(i, j, 0.25, 0.30);
    CHECK(std::fabs(t.surface(1.1, 0.5, 0.0) - 0.2) < 1e-9);
    CHECK(t.surface(0.5, 0.5, 0.0) == 0.0 && std::fabs(t.surface(0.5, 0.5, 0.26) - 0.30) < 1e-9);
    CHECK(std::fabs(t.headroom(0.5, 0.5, 0.0) - 0.25) < 1e-9 && std::isinf(t.headroom(0.5, 0.5, 0.3)) && std::isinf(t.headroom(0.1, 0.1, 0.0)));
    CHECK(t.solid(1.1, 0.5, 0.1) && !t.solid(1.1, 0.5, 0.21) && t.solid(0.5, 0.5, 0.27) && !t.solid(0.5, 0.5, 0.1) && !t.solid(0.5, 0.5, 0.31));
    CHECK(t.solid(5, 5, 1));
    // support is continuous: halfway up the block edge between the two cell centres
    CHECK(std::fabs(t.support(1.0, 0.5, 0.0) - 0.1) < 1e-9);
    // rays: over the block, into its side, down onto its top, under and into the roof
    CHECK(t.visible({0.2, 0.1, 0.3}, {1.8, 0.1, 0.3}));
    double hit = t.segment_hit({0.2, 0.1, 0.1}, {1.8, 0.1, 0.1});
    CHECK(std::fabs(0.2 + hit * 1.6 - 1.0) < 1e-9);
    hit = t.segment_hit({1.1, 0.1, 1.0}, {1.1, 0.1, 0.0});
    CHECK(std::fabs(hit - 0.8) < 1e-9);
    CHECK(t.visible({0.1, 0.5, 0.1}, {0.9, 0.5, 0.1}));
    hit = t.segment_hit({0.1, 0.5, 0.27}, {0.9, 0.5, 0.27});
    CHECK(std::fabs(0.1 + hit * 0.8 - 0.4) < 1e-9);
    hit = t.segment_hit({0.5, 0.5, 1.0}, {0.5, 0.5, 0.0});
    CHECK(std::fabs(hit - 0.7) < 1e-9);
    hit = t.segment_hit({0.5, 0.5, 0.0}, {0.5, 0.5, 1.0});
    CHECK(std::fabs(hit - 0.25) < 1e-9);
    hit = t.segment_hit({1.9, 0.5, 0.5}, {2.5, 0.5, 0.5});   // leaves the field: the fence
    CHECK(hit <= 1.0 && std::fabs(1.9 + hit * 0.6 - 2.0) < 1e-9);
    CHECK(t.segment_hit({1.1, 0.5, 0.1}, {1.2, 0.5, 0.1}) == 0.0);   // starts inside material
    CHECK(raises([] { Terrain::load("does-not-exist.bin"); }));
    // truncated and corrupted copies of the real file are rejected
    std::ifstream in(std::string(FIELD_DIR) + "/terrain.bin", std::ios::binary);
    std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(raw.size() > 2000000);
    const char* tmp = "build/field_sim_bad_terrain.bin";
    for (int variant = 0; variant < 3; ++variant) {
        std::string bad = raw;
        if (variant == 0) bad.resize(bad.size() - 1);
        if (variant == 1) bad[0] = 'X';
        if (variant == 2) bad[8] = static_cast<char>(bad[8] + 1);
        std::ofstream(tmp, std::ios::binary) << bad;
        CHECK(raises([&] { Terrain::load(tmp); }));
    }
    std::remove(tmp);
}

void official_field_tests() {
    Field f = Field::load(FIELD_DIR);
    const Terrain& t = f.terrain;
    CHECK(t.nx() == 1400 && t.ny() == 750 && std::fabs(t.cell() - 0.02) < 1e-12);
    CHECK(std::fabs(t.length() - 28.0) < 1e-9 && std::fabs(t.width() - 15.0) < 1e-9);   // manual §4.1
    // floor: ridge on the long centre line, 1.5 degrees down to both sides (figure 4-6)
    CHECK(std::fabs(t.surface(20.0, 7.49, -1e6)) < 0.003);
    CHECK(std::fabs(t.surface(4.5, 4.0, -1e6) - t.floor_z(4.0)) < 0.003 && std::fabs(t.floor_z(4.0) + 3.5 * std::tan(rad(1.5))) < 1e-12);
    // heights above the floor: trapezoid highland 200 (§4.3.1), three 200 mm steps to the
    // high point (§4.2.4), central highland deck 300 (figure 4-27), road 200 (figure 4-35)
    CHECK(std::fabs(rel(t, 5.0, 13.0) - 0.200) < 0.003);
    CHECK(std::fabs(rel(t, 3.3, 12.2) - 0.200) < 0.003 && std::fabs(rel(t, 3.0, 12.2) - 0.400) < 0.003);
    CHECK(std::fabs(rel(t, 2.7, 12.2) - 0.600) < 0.003 && std::fabs(rel(t, 1.4, 12.2) - 0.800) < 0.003);
    CHECK(std::fabs(rel(t, 12.0, 5.0) - 0.300) < 0.003);
    CHECK(std::fabs(rel(t, 12.6, 0.6) - 0.200) < 0.003);
    // tunnels: 250 mm of clearance under the far and near highland tunnels
    int i = static_cast<int>(14.0 / t.cell()), j = static_cast<int>(0.6 / t.cell());
    CHECK(t.has_overhang(i, j) && std::fabs(t.overhang_bottom(i, j) - t.ground(i, j) - 0.250) < 0.003);
    i = static_cast<int>(14.9 / t.cell()); j = static_cast<int>(1.55 / t.cell());
    CHECK(t.has_overhang(i, j) && std::fabs(t.overhang_bottom(i, j) - t.ground(i, j) - 0.250) < 0.003);
    // central symmetry (§4.1): compare every 5th cell with its mirror image
    long compared = 0, different = 0;
    for (int b = 2; b < t.ny(); b += 5)
        for (int a = 2; a < t.nx(); a += 5) {
            ++compared;
            if (std::fabs(t.ground(a, b) - t.ground(t.nx() - 1 - a, t.ny() - 1 - b)) > 0.006) ++different;
        }
    CHECK(compared == 42000);
    CHECK(different * 100 < compared);             // under 1 % of cells differ by more than 6 mm (props, model details)
    // sight lines: the central highland hides the two bases from each other at robot height,
    // a high line is clear; a robot inside the far tunnel cannot see out through the roof
    CHECK(!t.visible({2.4, 5.5, 0.3}, {25.6, 9.5, 0.3}));
    CHECK(t.visible({2.4, 5.5, 4.0}, {25.6, 9.5, 4.0}));
    CHECK(!t.visible({14.0, 0.6, t.surface(14.0, 0.6, -1e6) + 0.1}, {14.0, 0.6, 3.0}));
    CHECK(t.visible({4.0, 5.0, 0.3}, {8.0, 5.0, 0.3}));
    // zones and structures
    std::set<std::string> ids;
    for (const Zone& z : f.zones) ids.insert(z.id);
    CHECK(f.zones.size() == 38 && ids.size() == 38);
    auto zone = [&](const std::string& id) -> const Zone& {
        for (const Zone& z : f.zones) if (z.id == id) return z;
        throw std::runtime_error("no zone " + id);
    };
    CHECK(zone("red_supply").contains(1.5, 1.0) && !zone("red_supply").contains(5.0, 1.0));
    CHECK(zone("blue_supply").contains(26.5, 14.0));
    CHECK(zone("red_fortress").contains(6.57, 7.5) && zone("blue_fortress").contains(21.43, 7.5));
    CHECK(zone("red_base").contains(3.5, 7.5) && zone("red_high_point").contains(1.4, 12.2));
    CHECK(zone("far_tunnel_inside_west").contains(14.0, 0.6) && zone("far_tunnel_inside_west").under_only);
    CHECK(zone("central_highland_west").contains(10.7, 7.5) && zone("central_highland_east").contains(17.3, 7.5));
    for (const Zone& z : f.zones) {                 // every zone has a mirrored partner
        int partners = 0;
        for (const Zone& o : f.zones)
            if (o.kind == z.kind && o.id != z.id && o.under_only == z.under_only && o.polygon.size() == z.polygon.size() &&
                std::fabs(o.min_x - (28.0 - z.max_x)) < 1e-9 && std::fabs(o.max_x - (28.0 - z.min_x)) < 1e-9 &&
                std::fabs(o.min_y - (15.0 - z.max_y)) < 1e-9 && std::fabs(o.max_y - (15.0 - z.min_y)) < 1e-9) ++partners;
        CHECK(partners == 1);
    }
    CHECK(f.structures.size() == 7);
    for (const Structure& s : f.structures) {
        if (s.id == "red_base") CHECK(std::fabs(s.x - 2.411) < 1e-9 && std::fabs(s.y - 7.5) < 1e-9);       // figure 4-5
        if (s.id == "blue_outpost") CHECK(std::fabs(s.x - 17.024) < 1e-9 && std::fabs(s.y - 11.4) < 1e-9); // figure 4-5
        if (s.id == "energy_mechanism") CHECK(std::fabs(s.x - 14.0) < 0.01 && std::fabs(s.y - 7.5) < 0.01);
    }
    // all eight start poses are free for the default vehicle and lie in the own base zone
    Engine e(Field::load(FIELD_DIR));
    for (int team = 0; team < 2; ++team) {
        CHECK(e.field().spawns[team].size() == 4);
        for (std::size_t k = 0; k < e.field().spawns[team].size(); ++k) {
            std::size_t index = add(e, std::string(team ? "B" : "R") + std::to_string(k), team, default_vehicle(), e.field().spawns[team][k]);
            const Robot& r = e.robots()[index];
            CHECK(r.zones.size() == 1 && e.field().zones[r.zones[0]].id == (team ? "blue_base" : "red_base"));
        }
    }
    CHECK(raises([&] { add(e, "R0", 0, default_vehicle(), {5.0, 5.0, 0}); }));          // duplicate id
    CHECK(raises([&] { add(e, "X", 0, default_vehicle(), {2.411, 7.5, 0}); }));         // inside the base
    CHECK(raises([&] { add(e, "X", 0, default_vehicle(), {-1.0, 7.5, 0}); }));          // outside
    CHECK(raises([&] { add(e, "X", 0, default_vehicle(), {14.0, 0.6, 0}); }));          // under a 250 mm roof
    CHECK(raises([&] { add(e, "X", 0, default_vehicle(), e.field().spawns[0][0]); }));  // on another robot
    CHECK(raises([&] { add(e, "X", 2, default_vehicle(), {5.0, 5.0, 0}); }));
}

void driving_tests() {
    // flat ground: accelerates within its limits, respects the power allowance, stops at the fence
    Engine e(synthetic(Terrain::flat(1000, 200, 0.02)));          // 20 m x 4 m
    VehicleParams veh = default_vehicle();
    std::size_t a = add(e, "a", 0, veh, {1.0, 2.0, 0});
    e.set_command(a, drive_cmd(10.0));                             // asks for more than the vehicle can do
    double peak = 0, last_v = 0;
    for (int i = 0; i < 400; ++i) {
        e.step();
        const Robot& r = e.robots()[a];
        double v = std::hypot(r.vx, r.vy);
        CHECK(v <= veh.chassis.max_speed + 1e-9);
        CHECK((v - last_v) / DT <= veh.chassis.max_accel + 1e-6);
        CHECK(r.power.snapshot().buffer_uj >= 0 && r.power.snapshot().cutoffs == 0);
        last_v = v;
        peak = std::max(peak, v);
    }
    CHECK(peak > 2.5);
    CHECK(e.robots()[a].power.snapshot().buffer_uj >= static_cast<lim::I>(veh.chassis.buffer_reserve * 1e6) - 2000000);
    for (int i = 0; i < 1200; ++i) e.step();
    CHECK(e.robots()[a].x <= 20.0 - veh.chassis.collision_radius + 0.02 && e.robots()[a].x > 20.0 - veh.chassis.collision_radius - 0.05);
    CHECK(e.robots()[a].blocked);
    CHECK(std::fabs(e.robots()[a].y - 2.0) < 1e-6);
    // sliding: pushing diagonally into the fence keeps the tangential motion
    e.set_command(a, drive_cmd(2.0, 2.0));
    double y0 = e.robots()[a].y;
    for (int i = 0; i < 150; ++i) e.step();
    CHECK(e.robots()[a].y > y0 + 0.3 && e.robots()[a].x <= 20.0 - veh.chassis.collision_radius + 0.02);

    // without the on-board limiter the referee buffer runs dry and the chassis is cut for 5 s (§5.1.4)
    Engine e2(synthetic(Terrain::flat(4000, 200, 0.02)));
    VehicleParams wild = default_vehicle();
    wild.chassis.power_limiter = false;
    wild.chassis.max_speed = 8.0;
    wild.chassis.max_accel = 8.0;
    std::size_t w = add(e2, "w", 0, wild, {1.0, 2.0, 0});
    e2.set_command(w, drive_cmd(8.0, 0, 9.0));
    bool cut = false;
    for (int i = 0; i < 800 && !cut; ++i) { e2.step(); cut = !e2.robots()[w].power.snapshot().chassis_enabled(e2.time_ms()); }
    CHECK(cut && e2.robots()[w].power.snapshot().cutoffs == 1);
    double v_cut = std::hypot(e2.robots()[w].vx, e2.robots()[w].vy);
    e2.set_command(w, drive_cmd(0));
    for (int i = 0; i < 300; ++i) e2.step();
    CHECK(std::hypot(e2.robots()[w].vx, e2.robots()[w].vy) < v_cut * 0.5);          // coasting down, unpowered
    CHECK(e2.robots()[w].battery_power == 0 && !e2.robots()[w].power.snapshot().chassis_enabled(e2.time_ms()));
    for (int i = 0; i < 210; ++i) e2.step();
    CHECK(e2.robots()[w].power.snapshot().chassis_enabled(e2.time_ms()));           // power returns after 5 s

    // a supercapacitor carries the acceleration peak and recharges at rest
    Engine e3(synthetic(Terrain::flat(1500, 200, 0.02)));
    VehicleParams cap = default_vehicle();
    cap.chassis.supercap_energy = 1500;
    std::size_t c0 = add(e3, "cap", 0, cap, {1.0, 1.0, 0});
    std::size_t c1 = add(e3, "plain", 0, default_vehicle(), {1.0, 3.0, 0});
    e3.set_command(c0, drive_cmd(3.5));
    e3.set_command(c1, drive_cmd(3.5));
    for (int i = 0; i < 100; ++i) {
        e3.step();
        CHECK(e3.robots()[c0].battery_power <= 80.0 + 40.0 + 1e-6);   // allowance plus buffer surplus
        CHECK(e3.robots()[c0].power.snapshot().cutoffs == 0);
    }
    CHECK(e3.robots()[c0].x > e3.robots()[c1].x + 0.1 && e3.robots()[c0].cap_energy < 1500);
    double used = e3.robots()[c0].cap_energy;
    e3.set_command(c0, drive_cmd(0));
    for (int i = 0; i < 300; ++i) e3.step();
    CHECK(e3.robots()[c0].cap_energy > used);

    // a differential chassis cannot translate sideways; a holonomic one can
    Engine e4(synthetic(Terrain::flat(500, 1200, 0.02)));
    VehicleParams diff = default_vehicle();
    diff.chassis.drive = Drive::Differential;
    std::size_t d = add(e4, "d", 0, diff, {5.0, 3.0, 0});
    std::size_t h = add(e4, "h", 0, default_vehicle(), {5.0, 7.0, 0});
    e4.set_command(d, drive_cmd(0, 2.0));
    e4.set_command(h, drive_cmd(0, 2.0));
    for (int i = 0; i < 100; ++i) e4.step();
    CHECK(std::fabs(e4.robots()[d].y - 3.0) < 1e-9 && e4.robots()[h].y > 7.5);
    // gimbal-frame driving: with the gimbal turned 90 degrees left, "forward" is +y
    Command gc = drive_cmd(2.0);
    gc.frame = ChassisFrame::Gimbal;
    gc.gimbal = GimbalMode::Angle;
    gc.yaw = PI / 2;
    e4.set_command(d, gc);
    for (int i = 0; i < 100; ++i) e4.step();
    CHECK(std::fabs(e4.robots()[d].y - 3.0) < 1e-6);        // differential: still no sideways motion
    double hx = e4.robots()[h].x, hy = e4.robots()[h].y;
    e4.set_command(h, gc);
    for (int i = 0; i < 150; ++i) e4.step();
    CHECK(std::fabs(e4.robots()[h].x - hx) < 0.6 && e4.robots()[h].y > hy + 1.0);

    // two robots driving at each other do not overlap and neither is pushed through the fence
    Engine e5(synthetic(Terrain::flat(500, 200, 0.02)));
    std::size_t p = add(e5, "p", 0, default_vehicle(), {3.0, 2.0, 0});
    std::size_t q = add(e5, "q", 1, default_vehicle(), {7.0, 2.0, PI});
    e5.set_command(p, drive_cmd(3.0));
    e5.set_command(q, drive_cmd(3.0));
    for (int i = 0; i < 400; ++i) {
        e5.step();
        CHECK(std::hypot(e5.robots()[p].x - e5.robots()[q].x, e5.robots()[p].y - e5.robots()[q].y) >= 0.66 - 1e-6);
    }
    CHECK(std::fabs(e5.robots()[p].x + e5.robots()[q].x - 10.0) < 0.05);     // equal masses: stalemate in the middle
    VehicleParams heavy = default_vehicle();
    heavy.chassis.mass = 60;
    Engine e6(synthetic(Terrain::flat(500, 200, 0.02)));
    p = add(e6, "p", 0, heavy, {3.0, 2.0, 0}, 200.0);
    q = add(e6, "q", 1, default_vehicle(), {4.0, 2.0, PI});
    e6.set_command(p, drive_cmd(3.0));
    for (int i = 0; i < 1500; ++i) e6.step();
    CHECK(e6.robots()[q].x <= 10.0 - 0.33 + 0.03 && e6.robots()[p].x <= e6.robots()[q].x - 0.66 + 1e-6);   // pinned at the fence, not through it
}

void relief_tests() {
    // a 0.2 m platform (x >= 6 m), a 15 degree ramp up to it in the lane y < 2 m, a 0.25 m high tunnel
    Terrain t = Terrain::flat(600, 300, 0.02);                    // 12 m x 6 m
    for (int i = 0; i < 600; ++i)
        for (int j = 0; j < 300; ++j) {
            double x = (i + 0.5) * 0.02, y = (j + 0.5) * 0.02;
            double z = 0;
            if (x >= 6.0) z = 0.2;
            else if (y < 2.0 && x >= 6.0 - 0.2 / std::tan(rad(15))) z = (x - (6.0 - 0.2 / std::tan(rad(15)))) * std::tan(rad(15));
            t.set_ground(i, j, z);
            if (y >= 4.0 && y < 5.0 && x >= 2.0 && x < 3.0) t.set_overhang(i, j, 0.25, 0.30);
        }
    VehicleParams veh = default_vehicle();
    VehicleParams climber = veh;
    climber.chassis.max_step = 0.25;
    VehicleParams low = veh;
    low.chassis.height = 0.20;
    Engine e(synthetic(t));
    std::size_t wall = add(e, "wall", 0, veh, {4.0, 3.0, 0});
    std::size_t step = add(e, "step", 0, climber, {4.0, 4.0, 0});
    std::size_t ramp = add(e, "ramp", 0, veh, {4.0, 1.0, 0});
    std::size_t tall = add(e, "tall", 0, veh, {1.0, 4.5, 0});
    for (std::size_t r : {wall, step, ramp}) e.set_command(r, drive_cmd(2.0));
    e.set_command(tall, drive_cmd(1.0));
    double ramp_power = 0;
    for (int i = 0; i < 400; ++i) {
        e.step();
        ramp_power = std::max(ramp_power, e.robots()[ramp].motor_power);
    }
    CHECK(e.robots()[wall].x < 6.0 - 0.33 + 0.03 && e.robots()[wall].z == 0.0 && e.robots()[wall].blocked);   // 200 mm is a wall for a 30 mm step limit
    CHECK(e.robots()[step].x > 7.0 && std::fabs(e.robots()[step].z - 0.2) < 1e-9);                              // the climber gets up
    CHECK(e.robots()[ramp].x > 7.0 && std::fabs(e.robots()[ramp].z - 0.2) < 1e-9);                              // the ramp works for everyone
    CHECK(e.robots()[tall].x < 2.0 - 0.33 + 0.03 && e.robots()[tall].blocked);                                  // 480 mm body, 250 mm roof
    CHECK(ramp_power > 30);
    // driving back off the platform edge: airborne, then lands on the floor
    e.set_command(step, drive_cmd(-3.0));
    bool flew = false;
    for (int i = 0; i < 700; ++i) { e.step(); flew = flew || e.robots()[step].airborne; }
    CHECK(flew && !e.robots()[step].airborne && e.robots()[step].z == 0.0 && e.robots()[step].x < 5.0);
    // ... and it cannot back up the 200 mm edge without its climbing ability
    Engine e2(synthetic(t));
    std::size_t lowbot = add(e2, "low", 0, low, {1.0, 4.5, 0});
    std::size_t over = add(e2, "over", 0, veh, {8.0, 3.0, PI});
    e2.set_command(lowbot, drive_cmd(1.5));
    e2.set_command(over, drive_cmd(3.0));
    bool under_roof = false;
    for (int i = 0; i < 300; ++i) {
        e2.step();
        const Robot& r = e2.robots()[lowbot];
        if (r.x > 2.2 && r.x < 2.8) under_roof = under_roof || (r.z == 0.0 && std::isfinite(e2.field().terrain.headroom(r.x, r.y, r.z + 0.05)));
    }
    CHECK(under_roof && e2.robots()[lowbot].x > 3.5);                       // the low robot drives through
    CHECK(e2.robots()[over].x < 5.5 && e2.robots()[over].z == 0.0);         // drove off the platform and landed
    e2.set_command(over, drive_cmd(-2.0));
    for (int i = 0; i < 300; ++i) e2.step();
    CHECK(e2.robots()[over].x < 6.0 - 0.33 + 0.03);                         // the edge is a wall from below
    // jumping: leaving a ramp top at speed carries the robot through the air
    Terrain jump = Terrain::flat(800, 200, 0.02);
    for (int i = 0; i < 800; ++i)
        for (int j = 0; j < 200; ++j) {
            double x = (i + 0.5) * 0.02;
            if (x >= 4.0 && x < 5.0) jump.set_ground(i, j, (x - 4.0) * std::tan(rad(17)));
        }
    Engine e3(synthetic(jump));
    VehicleParams fast = default_vehicle();
    fast.chassis.max_slope = 35;
    std::size_t j0 = add(e3, "jump", 0, fast, {1.0, 2.0, 0}, 400.0);
    e3.set_command(j0, drive_cmd(3.5));
    double top = 0, air_start = -1, air_end = -1;
    for (int i = 0; i < 400; ++i) {
        e3.step();
        const Robot& r = e3.robots()[j0];
        top = std::max(top, r.z);
        if (r.airborne && air_start < 0 && r.x > 4.9) air_start = r.x;
        if (air_start >= 0 && air_end < 0 && !r.airborne) air_end = r.x;
    }
    CHECK(top > std::tan(rad(17)) * 1.0 + 0.01);                            // rises above the ramp lip
    CHECK(air_start > 0 && air_end - air_start > 0.5);                      // flies more than half a metre
}

void gimbal_tests() {
    Engine e(synthetic(Terrain::flat(500, 500, 0.02)));
    VehicleParams veh = default_vehicle();
    std::size_t a = add(e, "a", 0, veh, {5, 5, 0});
    Command c;
    c.gimbal = GimbalMode::Angle;
    c.yaw = rad(120);
    c.pitch = rad(20);
    e.set_command(a, c);
    double max_rate = 0;
    int settled = -1;
    for (int i = 0; i < 200; ++i) {
        e.step();
        const Robot& r = e.robots()[a];
        max_rate = std::max(max_rate, std::fabs(r.gimbal_yaw_rate));
        CHECK(std::fabs(r.gimbal_yaw_rate) <= veh.gimbal.max_yaw_rate + 1e-9);
        if (settled < 0 && std::fabs(wrap_angle(r.gimbal_yaw - rad(120))) < rad(0.5) && std::fabs(r.gimbal_pitch - rad(20)) < rad(0.5)) settled = i;
    }
    CHECK(settled > 20 && settled < 80);                    // 120 degrees at up to 540 deg/s
    CHECK(max_rate > rad(300));
    CHECK(std::fabs(wrap_angle(e.robots()[a].gimbal_yaw - rad(120))) < rad(0.05));
    // pitch stops at its mechanical limits
    c.pitch = rad(80);
    e.set_command(a, c);
    for (int i = 0; i < 200; ++i) e.step();
    CHECK(std::fabs(e.robots()[a].gimbal_pitch - veh.gimbal.pitch_max) < 1e-9);
    // the stabilised gimbal keeps its field heading while the chassis spins
    c.gimbal = GimbalMode::Hold;
    c.wz = 6.0;
    e.set_command(a, c);
    for (int i = 0; i < 300; ++i) e.step();
    CHECK(std::fabs(e.robots()[a].wz - 6.0) < 1e-6);
    CHECK(std::fabs(wrap_angle(e.robots()[a].gimbal_yaw - rad(120))) < rad(0.05));
    // rate mode integrates; a limited-yaw gimbal stops at its stop instead of wrapping
    VehicleParams limited = veh;
    limited.gimbal.yaw_continuous = false;
    limited.gimbal.yaw_min = rad(-90);
    limited.gimbal.yaw_max = rad(90);
    std::size_t b = add(e, "b", 0, limited, {2, 2, 0});
    Command rate;
    rate.gimbal = GimbalMode::Rate;
    rate.yaw_rate = rad(90);
    e.set_command(b, rate);
    for (int i = 0; i < 50; ++i) e.step();
    CHECK(std::fabs(e.robots()[b].gimbal_yaw - rad(45)) < rad(3));
    for (int i = 0; i < 300; ++i) e.step();
    CHECK(std::fabs(e.robots()[b].gimbal_yaw - rad(90)) < 1e-9);
    Command back;
    back.gimbal = GimbalMode::Angle;
    back.yaw = rad(-170);                                   // beyond the stop: goes to -90 the long way round
    e.set_command(b, back);
    double low = 10, high = -10;
    for (int i = 0; i < 300; ++i) { e.step(); low = std::min(low, e.robots()[b].gimbal_yaw); high = std::max(high, e.robots()[b].gimbal_yaw); }
    CHECK(std::fabs(e.robots()[b].gimbal_yaw - rad(-90)) < 1e-9 && high <= rad(90) + 1e-9 && low >= rad(-90) - 1e-9);
}

void vehicle_tests() {
    // the default document round-trips and reproduces the default struct
    VehicleParams d = default_vehicle();
    json::Value doc = vehicle_to_json(d);
    CHECK(doc.at("format").as_string() == "rmuc-vehicle.v1");
    CHECK(doc.at("chassis").at("length_mm").as_number() == 550 && doc.at("chassis").at("max_yaw_rate_dps").as_number() == 520);
    CHECK(doc.at("class").as_string() == "infantry" && doc.at("rules").at("chassis_type").as_string() == "hp_first");
    CHECK(doc.at("armor").as_array().size() == 4 && doc.at("sensors").at("camera").at("hfov_deg").as_number() == 70);
    VehicleParams back = vehicle_from_json(doc);
    CHECK(vehicle_to_json(back).dump() == doc.dump());
    CHECK(std::fabs(back.chassis.length - 0.55) < 1e-12 && std::fabs(back.gimbal.max_yaw_rate - rad(540)) < 1e-12 && back.armor.size() == 4);
    // missing keys take defaults; only format and name are required
    VehicleParams minimal = vehicle_from_json(json::parse(R"({"format":"rmuc-vehicle.v1","name":"最小配置"})"));
    json::Value expect = vehicle_to_json(d);
    expect.set("name", "最小配置");
    CHECK(vehicle_to_json(minimal).dump() == expect.dump());
    // every table entry: its default is inside its range, and both bounds are enforced
    json::Value schema = vehicle_schema_json();
    int numbers = 0, enums = 0, flags = 0;
    for (const auto& f : schema.at("fields").as_array()) {
        const std::string& section = f.at("section").as_string();
        const std::string& key = f.at("key").as_string();
        const std::string& type = f.at("type").as_string();
        auto with = [&](json::Value value) {
            // {"format":..,"name":..,<section path>:{key:value}} with consistent companions left at defaults
            json::Value v = json::Value::object();
            v.set("format", "rmuc-vehicle.v1");
            v.set("name", "t");
            if (section.empty()) v.set(key, value);
            else if (section.find('.') == std::string::npos) { json::Value s = json::Value::object(); s.set(key, value); v.set(section, s); }
            else {
                json::Value inner = json::Value::object(); inner.set(key, value);
                json::Value outer = json::Value::object(); outer.set(section.substr(section.find('.') + 1), inner);
                v.set(section.substr(0, section.find('.')), outer);
            }
            return v;
        };
        if (type == "number" || type == "integer") {
            ++numbers;
            double lo = f.at("min").as_number(), hi = f.at("max").as_number(), def = f.at("default").as_number();
            CHECK(def >= lo && def <= hi);
            CHECK(raises([&] { vehicle_from_json(with(json::Value(lo - (type == "integer" ? 1 : 1e-3)))); }));
            CHECK(raises([&] { vehicle_from_json(with(json::Value(hi + (type == "integer" ? 1 : 1e-3)))); }));
            CHECK(raises([&] { vehicle_from_json(with(json::Value("12"))); }));
            CHECK(raises([&] { vehicle_from_json(with(json::Value(true))); }));
            if (type == "integer") CHECK(raises([&] { vehicle_from_json(with(json::Value(lo + 0.5))); }));
        } else if (type == "enum") {
            ++enums;
            CHECK(f.at("choices").as_array().size() >= 2);
            CHECK(raises([&] { vehicle_from_json(with(json::Value("no-such-choice"))); }));
            CHECK(raises([&] { vehicle_from_json(with(json::Value(1))); }));
        } else {
            ++flags;
            CHECK(type == "boolean");
            CHECK(raises([&] { vehicle_from_json(with(json::Value(1))); }));
            CHECK(!raises([&] { vehicle_from_json(with(json::Value(!f.at("default").as_bool()))); }));
        }
    }
    if (numbers != 50 || enums != 6 || flags != 4) std::cerr << "table: " << numbers << " numbers, " << enums << " enums, " << flags << " flags\n";
    CHECK(numbers == 50 && enums == 6 && flags == 4);
    // structure and consistency
    auto bad = [](const std::string& body) {
        return raises([&] { vehicle_from_json(json::parse("{\"format\":\"rmuc-vehicle.v1\",\"name\":\"t\"," + body + "}")); });
    };
    CHECK(raises([] { vehicle_from_json(json::parse(R"({"name":"t"})")); }));
    CHECK(raises([] { vehicle_from_json(json::parse(R"({"format":"rmuc-vehicle.v2","name":"t"})")); }));
    CHECK(raises([] { vehicle_from_json(json::parse(R"({"format":"rmuc-vehicle.v1"})")); }));
    CHECK(raises([] { vehicle_from_json(json::parse(R"({"format":"rmuc-vehicle.v1","name":"  "})")); }));
    CHECK(raises([] { vehicle_from_json(json::parse(R"([])")); }));
    CHECK(bad(R"("wheels":4)") && bad(R"("chassis":{"colour":"red"})") && bad(R"("chassis":5)") && bad(R"("sensors":{"radar":{}})"));
    CHECK(bad(R"("sensors":{"camera":{"zoom":2}})") && bad(R"("notes":5)") && bad("\"name2\":\"x\""));
    CHECK(bad(R"("chassis":{"wheelbase_mm":600})"));                       // longer than the 550 mm body
    CHECK(bad(R"("chassis":{"track_mm":500})"));
    CHECK(bad(R"("chassis":{"collision_radius_mm":200})") && bad(R"("chassis":{"collision_radius_mm":450})"));
    CHECK(bad(R"("chassis":{"max_lateral_speed_mps":4})"));
    CHECK(bad(R"("gimbal":{"pivot_height_mm":600})") && bad(R"("sensors":{"lidar":{"height_mm":600}})"));
    CHECK(bad(R"("launcher":{"caliber":"42mm"})") && bad(R"("launcher":{"caliber":"none"})"));   // infantry: one 17 mm launcher
    CHECK(bad(R"("class":"heavy")"));                                       // heavy with the default 17 mm launcher
    CHECK(!bad(R"("class":"heavy","launcher":{"caliber":"42mm","muzzle_speed_mps":15})") && !bad(R"("class":"heavy","launcher":{"caliber":"none"})"));
    CHECK(bad(R"("armor":[{"face":"front"},{"face":"front"}])") && bad(R"("armor":[{"face":"top"}])") && bad(R"("armor":[{"size":"small"}])"));
    CHECK(bad(R"("armor":[{"face":"front","offset_mm":200}])"));            // 135 mm plate 200 mm off centre on a 470 mm face
    CHECK(bad(R"("armor":[{"face":"left","center_height_mm":460}])") && bad(R"("armor":[{"face":"left","center_height_mm":30}])"));
    CHECK(bad(R"("armor":[{"face":"front","colour":1}])") && bad(R"("armor":{})"));
    CHECK(!bad(R"("armor":[])") && !bad(R"("armor":[{"face":"front","size":"large"},{"face":"rear","size":"large","offset_mm":-100}])"));
    std::string long_name(41, 'x');
    CHECK(bad("\"notes\":\"a\\u0001\"") && raises([&] { vehicle_from_json(json::parse("{\"format\":\"rmuc-vehicle.v1\",\"name\":\"" + long_name + "\"}")); }));
    // rule attributes follow the choices, not free parameters (tables 3-6, 5-16..5-18)
    auto attr = [](const std::string& body, int level) {
        VehicleParams v = vehicle_from_json(json::parse("{\"format\":\"rmuc-vehicle.v1\",\"name\":\"t\"" + body + "}"));
        return attributes(v, level, v.rules.robot_class == RobotClass::Heavy && v.launcher.caliber == Caliber::Mm42);
    };
    Attributes a = attr("", 1);
    CHECK(a.max_hp == 200 && a.chassis_power_limit == 45 && a.heat_limit == 40 && a.cooling_per_second == 12 && a.muzzle_speed_limit == 25 && a.initial_allowance == 0);
    a = attr(R"(,"rules":{"chassis_type":"power_first","launcher_type":"burst_first"})", 1);
    CHECK(a.max_hp == 150 && a.chassis_power_limit == 60 && a.heat_limit == 170 && a.cooling_per_second == 5);
    a = attr(R"(,"rules":{"chassis_type":"power_first","launcher_type":"burst_first"})", 10);
    CHECK(a.max_hp == 400 && a.chassis_power_limit == 100 && a.heat_limit == 260 && a.cooling_per_second == 20);
    a = attr(R"(,"class":"sentry")", 1);
    CHECK(a.max_hp == 400 && a.chassis_power_limit == 100 && a.heat_limit == 260 && a.cooling_per_second == 30 && a.initial_allowance == 300);
    a = attr(R"(,"class":"sentry","rules":{"sentry_mode":"semi"})", 1);
    CHECK(a.max_hp == 200 && a.chassis_power_limit == 60 && a.heat_limit == 100 && a.cooling_per_second == 10);
    a = attr(R"(,"class":"heavy","launcher":{"caliber":"42mm","muzzle_speed_mps":15})", 1);
    CHECK(a.max_hp == 600 && a.chassis_power_limit == 50 && a.caliber == Caliber::Mm42 && a.heat_limit == 300 && a.muzzle_speed_limit == 16.5);
    a = attr(R"(,"class":"heavy","launcher":{"caliber":"none"})", 1);
    CHECK(a.max_hp == 400 && a.caliber == Caliber::None && a.heat_limit == 0);

    // parameters change behaviour: top speed, turn rate, gimbal rate, step height, power allowance
    Engine e(synthetic(Terrain::flat(1500, 400, 0.02)));
    VehicleParams slow = d, quick = d, sentry = d;
    slow.chassis.max_speed = 1.0;
    slow.chassis.max_speed_lateral = 1.0;
    slow.chassis.max_yaw_rate = rad(90);
    slow.gimbal.max_yaw_rate = rad(60);
    quick.chassis.supercap_energy = 2000;
    sentry.rules.robot_class = RobotClass::Sentry;
    std::size_t s0 = e.add_robot("slow", 0, slow, {2, 1, 0});
    std::size_t q0 = e.add_robot("quick", 0, quick, {2, 2, 0});
    std::size_t n0 = e.add_robot("normal", 0, d, {2, 3, 0});
    std::size_t y0 = e.add_robot("sentry", 0, sentry, {2, 3.5 + 0.4, 0});
    CHECK(e.robots()[n0].power_limit == 45 && e.robots()[y0].power_limit == 100 && e.robots()[n0].attr.max_hp == 200);
    Command c = drive_cmd(0, 0, 20.0);                   // spin on the spot while slewing the gimbal
    c.gimbal = GimbalMode::Rate;
    c.yaw_rate = 20.0;
    for (std::size_t i : {s0, q0, n0, y0}) e.set_command(i, c);
    double slow_gimbal = 0, normal_gimbal = 0, slow_spin = 0, sentry_spin = 0;
    for (int i = 0; i < 150; ++i) {
        e.step();
        slow_spin = std::max(slow_spin, std::fabs(e.robots()[s0].wz));
        sentry_spin = std::max(sentry_spin, std::fabs(e.robots()[y0].wz));
        slow_gimbal = std::max(slow_gimbal, std::fabs(e.robots()[s0].gimbal_yaw_rate));
        normal_gimbal = std::max(normal_gimbal, std::fabs(e.robots()[n0].gimbal_yaw_rate));
    }
    CHECK(std::fabs(slow_spin - rad(90)) < 1e-6 && sentry_spin > rad(300));
    CHECK(std::fabs(slow_gimbal - rad(60)) < 1e-9 && std::fabs(normal_gimbal - rad(540)) < 1e-9);
    // straight-line run from rest on a fresh field
    Engine e2(synthetic(Terrain::flat(1500, 400, 0.02)));
    s0 = e2.add_robot("slow", 0, slow, {2, 1, 0});
    q0 = e2.add_robot("quick", 0, quick, {2, 2, 0});
    n0 = e2.add_robot("normal", 0, d, {2, 3, 0});
    y0 = e2.add_robot("sentry", 0, sentry, {2, 4, 0});
    for (std::size_t i : {s0, q0, n0, y0}) e2.set_command(i, drive_cmd(5.0));
    for (int i = 0; i < 300; ++i) {
        e2.step();
        CHECK(std::hypot(e2.robots()[s0].vx, e2.robots()[s0].vy) <= 1.0 + 1e-9);
    }
    double run[4] = {e2.robots()[s0].odometer, e2.robots()[q0].odometer, e2.robots()[n0].odometer, e2.robots()[y0].odometer};
    CHECK(run[1] > run[2] + 0.5);        // the supercapacitor buys acceleration
    CHECK(run[3] > run[2] + 0.5);        // 100 W against 45 W
    CHECK(run[2] > run[0] + 0.5 && run[0] > 2.0 && run[0] < 3.0);
}

void official_driving_tests() {
    VehicleParams veh = default_vehicle();
    VehicleParams climber = veh;
    climber.chassis.max_step = 0.35;
    VehicleParams low = veh;
    low.chassis.height = 0.22;
    Engine e(Field::load(FIELD_DIR));
    const Terrain& t = e.field().terrain;
    // the 300 mm central highland wall stops a normal chassis and not a step climber
    std::size_t wall = add(e, "wall", 0, veh, {9.0, 11.0, 0});
    std::size_t step = add(e, "step", 0, climber, {9.0, 10.0, 0});
    // the 11 degree road ramp (figure 4-35) leads from the floor up to the 200 mm road
    std::size_t ramp = add(e, "ramp", 0, veh, {8.2, 2.45, PI});
    // the far highland tunnel: 250 mm of clearance
    std::size_t tall = add(e, "tall", 0, veh, {12.6, 0.6, 0});
    CHECK(raises([&] { add(e, "clash", 0, low, {12.6, 0.6, 0}); }));
    for (std::size_t r : {wall, step, ramp, tall}) e.set_command(r, drive_cmd(2.0));
    bool wall_stayed_low = true, wall_touched = false, on_card = false;
    for (int i = 0; i < 400; ++i) {
        e.step();
        const Robot& w = e.robots()[wall];
        wall_stayed_low = wall_stayed_low && std::fabs(w.z - t.floor_z(w.y)) < 0.01;
        wall_touched = wall_touched || w.blocked;
        for (int z : e.robots()[step].zones) on_card = on_card || e.field().zones[z].id == "central_highland_west";
    }
    CHECK(wall_stayed_low && wall_touched);          // slides along the highland wall, never climbs it
    CHECK(e.robots()[step].x > 12.0 && std::fabs(e.robots()[step].z - t.floor_z(e.robots()[step].y) - 0.3) < 0.02);
    CHECK(on_card);                                  // ... and the climber crossed the central highland card
    CHECK(e.robots()[ramp].x < 6.0 && std::fabs(e.robots()[ramp].z - t.floor_z(e.robots()[ramp].y) - 0.2) < 0.005);
    CHECK(e.robots()[tall].x < 13.8 - 0.33 + 0.03 && e.robots()[tall].blocked);

    // a low robot drives through the tunnel and reads end card, interior card, end card in order
    Engine e2(Field::load(FIELD_DIR));
    std::size_t l = add(e2, "low", 0, low, {12.6, 0.6, 0});
    e2.set_command(l, drive_cmd(2.0));
    std::vector<std::string> seen;
    for (int i = 0; i < 300; ++i) {
        e2.step();
        for (int z : e2.robots()[l].zones) {
            const std::string& id = e2.field().zones[z].id;
            if (seen.empty() || seen.back() != id) seen.push_back(id);
        }
    }
    CHECK(e2.robots()[l].x > 15.5);
    CHECK(seen.size() == 3 && seen[0] == "far_tunnel_end_a_west" && seen[1] == "far_tunnel_inside_west" && seen[2] == "far_tunnel_end_b_west");

    // determinism: the same commands give the same state, byte for byte
    auto run = [&] {
        Engine d(Field::load(FIELD_DIR));
        for (int team = 0; team < 2; ++team)
            for (std::size_t k = 0; k < 4; ++k) add(d, std::string(team ? "B" : "R") + std::to_string(k), team, veh, d.field().spawns[team][k]);
        for (int i = 0; i < 1500; ++i) {
            if (i % 100 == 0)
                for (std::size_t k = 0; k < d.robots().size(); ++k) {
                    Command c = drive_cmd(3.0 * std::cos(0.7 * static_cast<double>(k) + i * 0.003), 2.0 * std::sin(1.3 * static_cast<double>(k) + i * 0.002), (k % 2) ? 5.0 : -3.0);
                    c.gimbal = GimbalMode::Rate;
                    c.yaw_rate = 1.0;
                    d.set_command(k, c);
                }
            d.step();
            for (const Robot& r : d.robots()) {
                CHECK(d.field().terrain.inside(r.x, r.y));
                CHECK(std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.z) && r.z > -0.25 && r.z < 3.0);
                CHECK(r.power.snapshot().buffer_uj >= 0);
            }
        }
        return d.state_json().dump();
    };
    std::string first = run();
    CHECK(first == run());
    CHECK(first.find("\"tick\":1500") != std::string::npos);
}
}

int main() {
    json_tests();
    terrain_tests();
    official_field_tests();
    driving_tests();
    relief_tests();
    gimbal_tests();
    vehicle_tests();
    official_driving_tests();
    return finish("field_sim (terrain, official field, vehicles, chassis and gimbal)");
}
