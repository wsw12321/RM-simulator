#include "engine.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <variant>

namespace field_sim {
namespace {
constexpr double kProbe = 0.04;          // m: distance over which a wall-like rise is measured
constexpr double kSubstep = 0.008;       // m: longest collision-checked displacement
constexpr double kZoneHeight = 0.05;     // m: reference height above the wheels for layer selection
double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
double sign(double v) { return v > 0 ? 1.0 : (v < 0 ? -1.0 : 0.0); }
std::string read_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
int perimeter_samples(double radius) { return std::clamp(static_cast<int>(std::ceil(2 * PI * radius / 0.02)), 16, 128); }
}

double wrap_angle(double a) {
    a = std::fmod(a + PI, 2 * PI);
    if (a <= 0) a += 2 * PI;
    return a - PI;
}
std::string build_id() { return FIELD_SIM_BUILD_ID; }
VehicleParams default_vehicle() { return VehicleParams{}; }

bool Zone::contains(double x, double y) const {
    if (x < min_x || x > max_x || y < min_y || y > max_y) return false;
    bool in = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const auto& a = polygon[i];
        const auto& b = polygon[j];
        if ((a[1] > y) != (b[1] > y) && x < (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]) + a[0]) in = !in;
    }
    return in;
}

Field Field::load(const std::string& dir) {
    json::Value doc = json::parse(read_text(dir + "/field.json"));
    if (doc.at("format").as_string() != "rmuc-field.v1") throw std::runtime_error("unsupported field format");
    Field f;
    f.id = doc.at("id").as_string();
    const json::Value& terrain = doc.at("terrain");
    f.terrain_sha256 = terrain.at("sha256").as_string();
    f.terrain = Terrain::load(dir + "/" + terrain.at("file").as_string());
    const auto& grid = doc.at("grid").as_array();
    if (grid.size() != 2 || grid[0].as_int() != f.terrain.nx() || grid[1].as_int() != f.terrain.ny() ||
        std::fabs(doc.at("cell_mm").as_number() * 0.001 - f.terrain.cell()) > 1e-9)
        throw std::runtime_error("field.json does not match the terrain file");
    for (const auto& z : doc.at("zones").as_array()) {
        Zone zone;
        zone.id = z.at("id").as_string();
        zone.kind = z.at("kind").as_string();
        zone.team = z.at("team").as_string();
        zone.under_only = z.at("layer").as_string() == "under";
        for (const auto& p : z.at("polygon_mm").as_array()) {
            const auto& xy = p.as_array();
            if (xy.size() != 2) throw std::runtime_error("zone polygon point must be [x, y]");
            zone.polygon.push_back({xy[0].as_number() * 0.001, xy[1].as_number() * 0.001});
        }
        if (zone.polygon.size() < 3) throw std::runtime_error("zone polygon needs three points");
        zone.min_x = zone.max_x = zone.polygon[0][0];
        zone.min_y = zone.max_y = zone.polygon[0][1];
        for (const auto& p : zone.polygon) {
            zone.min_x = std::min(zone.min_x, p[0]); zone.max_x = std::max(zone.max_x, p[0]);
            zone.min_y = std::min(zone.min_y, p[1]); zone.max_y = std::max(zone.max_y, p[1]);
        }
        f.zones.push_back(std::move(zone));
    }
    for (const auto& s : doc.at("structures").as_array()) {
        Structure st;
        st.id = s.at("id").as_string();
        st.kind = s.at("kind").as_string();
        st.team = s.at("team").as_string();
        const auto& c = s.at("center_mm").as_array();
        const auto& box = s.at("bbox_mm").as_array();
        if (c.size() != 2 || box.size() != 2) throw std::runtime_error("invalid structure record");
        st.x = c[0].as_number() * 0.001;
        st.y = c[1].as_number() * 0.001;
        for (int k = 0; k < 3; ++k) {
            st.lo[k] = box[0].as_array().at(k).as_number() * 0.001;
            st.hi[k] = box[1].as_array().at(k).as_number() * 0.001;
        }
        f.structures.push_back(std::move(st));
    }
    const json::Value& spawns = doc.at("spawns");
    const char* names[2] = {"red", "blue"};
    for (int team = 0; team < 2; ++team)
        for (const auto& p : spawns.at(names[team]).as_array())
            f.spawns[team].push_back({p.at("x_mm").as_number() * 0.001, p.at("y_mm").as_number() * 0.001, rad(p.at("yaw_deg").as_number())});
    return f;
}

Command parse_command(const json::Value& v) {
    Command c;
    auto bounded = [](const json::Value& n, double limit, const char* what) {
        double x = n.as_number();
        if (!(std::fabs(x) <= limit)) throw json::Error(std::string(what) + " is out of range");
        return x;
    };
    for (const auto& kv : v.as_object()) {
        if (kv.first == "chassis") {
            for (const auto& f : kv.second.as_object()) {
                if (f.first == "vx") c.vx = bounded(f.second, 20, "chassis.vx");
                else if (f.first == "vy") c.vy = bounded(f.second, 20, "chassis.vy");
                else if (f.first == "wz") c.wz = bounded(f.second, 60, "chassis.wz");
                else if (f.first == "frame") {
                    const std::string& s = f.second.as_string();
                    if (s == "chassis") c.frame = ChassisFrame::Chassis;
                    else if (s == "gimbal") c.frame = ChassisFrame::Gimbal;
                    else throw json::Error("chassis.frame must be 'chassis' or 'gimbal'");
                } else throw json::Error("unknown chassis command key '" + f.first + "'");
            }
        } else if (kv.first == "gimbal") {
            bool mode_set = false;
            for (const auto& f : kv.second.as_object()) {
                if (f.first == "mode") {
                    const std::string& s = f.second.as_string();
                    if (s == "hold") c.gimbal = GimbalMode::Hold;
                    else if (s == "angle") c.gimbal = GimbalMode::Angle;
                    else if (s == "rate") c.gimbal = GimbalMode::Rate;
                    else throw json::Error("gimbal.mode must be 'hold', 'angle' or 'rate'");
                    mode_set = true;
                } else if (f.first == "yaw") c.yaw = bounded(f.second, 1000, "gimbal.yaw");
                else if (f.first == "pitch") c.pitch = bounded(f.second, 2, "gimbal.pitch");
                else if (f.first == "yaw_rate") c.yaw_rate = bounded(f.second, 100, "gimbal.yaw_rate");
                else if (f.first == "pitch_rate") c.pitch_rate = bounded(f.second, 100, "gimbal.pitch_rate");
                else throw json::Error("unknown gimbal command key '" + f.first + "'");
            }
            if (!mode_set) throw json::Error("gimbal.mode is required");
        } else if (kv.first == "supercap") c.supercap = kv.second.as_bool();
        else throw json::Error("unknown command key '" + kv.first + "'");
    }
    return c;
}

Engine::Engine(Field field, std::uint64_t seed) : field_(std::move(field)), seed_(seed) {}

int Engine::find(const std::string& id) const {
    for (std::size_t i = 0; i < robots_.size(); ++i) if (robots_[i].id == id) return static_cast<int>(i);
    return -1;
}

bool Engine::placement_blocked(const VehicleParams& veh, double x, double y, int ignore) const {
    const Terrain& t = field_.terrain;
    const ChassisParams& c = veh.chassis;
    if (!t.inside(x, y)) return true;
    const double low = -1.0e6;                      // stand on the ground layer
    double z = t.support(x, y, low);
    if (t.headroom(x, y, low) < c.height) return true;
    int n = perimeter_samples(c.collision_radius);
    double tan_slope = std::tan(rad(c.max_slope));
    for (int k = 0; k < n; ++k) {
        double a = 2 * PI * k / n;
        double px = x + c.collision_radius * std::cos(a), py = y + c.collision_radius * std::sin(a);
        if (!t.inside(px, py)) return true;
        if (t.headroom(px, py, low) < c.height) return true;
        if (t.surface(px, py, low) - z > c.max_step + c.collision_radius * tan_slope) return true;
    }
    for (std::size_t i = 0; i < robots_.size(); ++i) {
        if (static_cast<int>(i) == ignore) continue;
        const Robot& o = robots_[i];
        if (std::hypot(o.x - x, o.y - y) < o.veh.chassis.collision_radius + c.collision_radius &&
            std::fabs(o.z - z) < std::max(o.veh.chassis.height, c.height)) return true;
    }
    return false;
}

std::size_t Engine::add_robot(const std::string& id, int team, const VehicleParams& veh, const Pose& pose) {
    if (id.empty() || id.size() > 32) throw std::runtime_error("robot id must be 1..32 characters");
    if (find(id) >= 0) throw std::runtime_error("duplicate robot id '" + id + "'");
    if (team != 0 && team != 1) throw std::runtime_error("team must be 0 (red) or 1 (blue)");
    if (robots_.size() >= 16) throw std::runtime_error("at most 16 robots");
    if (placement_blocked(veh, pose.x, pose.y)) throw std::runtime_error("robot '" + id + "' cannot be placed at its start pose");
    Robot r;
    r.id = id;
    r.team = team;
    r.veh = veh;
    r.x = pose.x;
    r.y = pose.y;
    r.yaw = wrap_angle(pose.yaw);
    r.z = field_.terrain.support(r.x, r.y, -1.0e6);
    r.gimbal_yaw = r.gimbal_yaw_target = r.yaw;
    r.cap_energy = veh.chassis.supercap_energy;
    robots_.push_back(std::move(r));
    detect_zones(robots_.back());
    return robots_.size() - 1;
}

// Perimeter directions in which the body at (x, y, z) presses against something:
// the field boundary, a wall-like rise higher than the wheels can climb, or a roof
// lower than the body.
int Engine::blocked_normals(const Robot& r, double x, double y, double z, double (*out)[2], int cap) const {
    const Terrain& t = field_.terrain;
    const ChassisParams& c = r.veh.chassis;
    int n = perimeter_samples(c.collision_radius), count = 0;
    double z_ref = z + c.max_step + 0.02;
    double limit = c.max_step + kProbe * std::tan(rad(c.max_slope));
    for (int k = 0; k < n && count < cap; ++k) {
        double a = 2 * PI * k / n, ux = std::cos(a), uy = std::sin(a);
        double px = x + c.collision_radius * ux, py = y + c.collision_radius * uy;
        bool hit = !t.inside(px, py);
        if (!hit && t.headroom(px, py, z_ref) < c.height) hit = true;
        if (!hit) {
            double outer = t.surface(px, py, z_ref);
            if (outer > z + c.max_step && outer - t.surface(px - kProbe * ux, py - kProbe * uy, z_ref) > limit) hit = true;
        }
        if (hit) { out[count][0] = ux; out[count][1] = uy; ++count; }
    }
    return count;
}

void Engine::move(Robot& r, double dx, double dy) {
    double normals[128][2];
    auto offending = [&](double x, double y, double ddx, double ddy, double& nx, double& ny) {
        int n = blocked_normals(r, x, y, r.z, normals, 128);
        double tol = 1e-4 * std::hypot(ddx, ddy);
        nx = ny = 0;
        int bad = 0;
        for (int i = 0; i < n; ++i)
            if (ddx * normals[i][0] + ddy * normals[i][1] > tol) { nx += normals[i][0]; ny += normals[i][1]; ++bad; }
        return bad;
    };
    double nx, ny;
    if (!offending(r.x + dx, r.y + dy, dx, dy, nx, ny)) { r.x += dx; r.y += dy; return; }
    r.blocked = true;
    double len = std::hypot(nx, ny);
    if (len < 1e-9) return;
    nx /= len; ny /= len;
    double along = dx * nx + dy * ny;
    double sx = dx - along * nx, sy = dy - along * ny;          // slide along the obstacle
    double v_along = r.vx * nx + r.vy * ny;
    if (v_along > 0) { r.vx -= v_along * nx; r.vy -= v_along * ny; }
    if (std::hypot(sx, sy) < 1e-7) return;                       // head-on: stay
    // The slide may clip the 20 mm staircase of a diagonal wall; step slightly away from it then.
    for (double push : {0.0, 0.5 * field_.terrain.cell(), field_.terrain.cell()}) {
        double ex = sx - push * nx, ey = sy - push * ny, mx, my;
        if (!offending(r.x + ex, r.y + ey, ex, ey, mx, my)) { r.x += ex; r.y += ey; return; }
    }
}

double Engine::chassis_power(const Robot& r, double ax, double ay, double alpha, double slope_fx, double slope_fy) const {
    const ChassisParams& c = r.veh.chassis;
    double v = std::hypot(r.vx, r.vy), resist = c.rolling_resistance * c.mass * GRAVITY;
    double fx = c.mass * ax + slope_fx + (v > 1e-3 ? resist * r.vx / v : 0);
    double fy = c.mass * ay + slope_fy + (v > 1e-3 ? resist * r.vy / v : 0);
    double arm = 0.5 * std::hypot(c.wheelbase, c.track);
    double inertia = c.mass * (c.length * c.length + c.width * c.width) / 12.0;
    double torque = inertia * alpha + (std::fabs(r.wz) > 1e-3 ? resist * arm * sign(r.wz) : 0);
    double mech = fx * r.vx + fy * r.vy + torque * r.wz;
    double full = c.mass * c.max_accel;
    double loss = c.stall_power * (fx * fx + fy * fy + (torque / arm) * (torque / arm)) / (full * full);
    return c.idle_power + std::max(0.0, mech) / c.drivetrain_efficiency + loss;
}

void Engine::drive(Robot& r) {
    const ChassisParams& c = r.veh.chassis;
    const Terrain& t = field_.terrain;
    bool powered = r.power.snapshot().chassis_enabled(time_ms());
    double limit = r.veh.rules.chassis_power_limit;
    double cy = std::cos(r.yaw), sy = std::sin(r.yaw);
    double z_ref = r.z + c.max_step + 0.02;
    const double e = 0.1;
    double gx = (t.support(r.x + e, r.y, z_ref) - t.support(r.x - e, r.y, z_ref)) / (2 * e);
    double gy = (t.support(r.x, r.y + e, z_ref) - t.support(r.x, r.y - e, z_ref)) / (2 * e);
    gx = clampd(gx, -1.5, 1.5); gy = clampd(gy, -1.5, 1.5);
    double norm = std::sqrt(1 + gx * gx + gy * gy);
    double slope_fx = c.mass * GRAVITY * gx / norm, slope_fy = c.mass * GRAVITY * gy / norm;   // force needed to hold on the slope
    double ax = 0, ay = 0, alpha = 0;
    r.motor_power = 0;
    if (r.airborne) {
        // no traction: velocities are kept
    } else if (!powered) {
        double decel = c.rolling_resistance * GRAVITY, v = std::hypot(r.vx, r.vy);
        ax = -slope_fx / c.mass; ay = -slope_fy / c.mass;                 // rolls downhill
        if (v > 1e-6) { double d = std::min(v / DT, decel); ax -= d * r.vx / v; ay -= d * r.vy / v; }
        else if (std::hypot(ax, ay) <= decel) ax = ay = 0;               // static friction holds
        alpha = -sign(r.wz) * std::min(std::fabs(r.wz) / DT, 4.0 * decel);
    } else {
        double ref = r.cmd.frame == ChassisFrame::Gimbal ? r.gimbal_yaw : r.yaw;
        double wx = r.cmd.vx * std::cos(ref) - r.cmd.vy * std::sin(ref), wy = r.cmd.vx * std::sin(ref) + r.cmd.vy * std::cos(ref);
        double bx = wx * cy + wy * sy, by = -wx * sy + wy * cy;         // body frame
        if (!holonomic(c.drive)) by = 0;
        double s = std::hypot(bx / c.max_speed, by / c.max_speed_lateral);
        if (s > 1) { bx /= s; by /= s; }
        double tx = bx * cy - by * sy, ty = bx * sy + by * cy;
        if (!holonomic(c.drive)) {
            // wheels grip sideways: the lateral velocity is removed at the braking limit
            double lat = -r.vx * sy + r.vy * cy, kill = std::min(std::fabs(lat), c.max_decel * DT) * sign(lat);
            r.vx += kill * sy; r.vy -= kill * cy;
        }
        ax = (tx - r.vx) / DT; ay = (ty - r.vy) / DT;
        double amax = (ax * r.vx + ay * r.vy >= 0) ? c.max_accel : c.max_decel, a = std::hypot(ax, ay);
        if (a > amax) { ax *= amax / a; ay *= amax / a; }
        alpha = clampd((clampd(r.cmd.wz, -c.max_yaw_rate, c.max_yaw_rate) - r.wz) / DT, -c.max_yaw_accel, c.max_yaw_accel);
        if (c.power_limiter) {
            double allow = limit;
            if (c.supercap_energy > 0 && r.cmd.supercap) allow += std::min(c.supercap_power, r.cap_energy / 0.1);
            double buffer = r.power.snapshot().buffer_uj * 1e-6;
            if (buffer > c.buffer_reserve) allow += buffer - c.buffer_reserve;      // spend the surplus over about a second
            if (chassis_power(r, ax, ay, alpha, slope_fx, slope_fy) > allow) {
                if (chassis_power(r, 0, 0, 0, slope_fx, slope_fy) <= allow) {
                    double lo = 0, hi = 1;
                    for (int i = 0; i < 14; ++i) {
                        double mid = 0.5 * (lo + hi);
                        (chassis_power(r, ax * mid, ay * mid, alpha * mid, slope_fx, slope_fy) <= allow ? lo : hi) = mid;
                    }
                    ax *= lo; ay *= lo; alpha *= lo;
                } else {
                    // even holding the present speed costs too much: shed speed
                    double v = std::hypot(r.vx, r.vy);
                    double brake = std::min(c.max_decel, 0.5 * c.max_decel + 1.0);
                    ax = v > 1e-6 ? -brake * r.vx / v : 0; ay = v > 1e-6 ? -brake * r.vy / v : 0;
                    alpha = -sign(r.wz) * std::min(std::fabs(r.wz) / DT, 0.5 * c.max_yaw_accel);
                    // braking itself is free of traction power in this model
                    r.motor_power = c.idle_power;
                }
            }
        }
        if (r.motor_power == 0) r.motor_power = chassis_power(r, ax, ay, alpha, slope_fx, slope_fy);
    }
    // battery side: the supercapacitor covers peaks and recharges from the unused allowance
    r.battery_power = r.motor_power;
    if (powered && c.supercap_energy > 0) {
        if (r.motor_power > limit && r.cmd.supercap) {
            double out = std::min({r.motor_power - limit, c.supercap_power, r.cap_energy / DT});
            r.cap_energy -= out * DT;
            r.battery_power -= out;
        } else if (r.motor_power < limit) {
            double in = std::min({limit - r.motor_power, c.supercap_power, (c.supercap_energy - r.cap_energy) / (0.9 * DT)});
            in = std::max(0.0, in);
            r.cap_energy += 0.9 * in * DT;
            r.battery_power += in;
        }
    }
    r.window_energy += r.battery_power * DT;

    r.vx += ax * DT; r.vy += ay * DT; r.wz += alpha * DT;
    if (std::hypot(r.vx, r.vy) < 1e-5) r.vx = r.vy = 0;
    r.blocked = false;
    double dx = r.vx * DT, dy = r.vy * DT, dist = std::hypot(dx, dy);
    int steps = std::max(1, static_cast<int>(std::ceil(dist / kSubstep)));
    double x0 = r.x, y0 = r.y;
    for (int i = 0; i < steps; ++i) move(r, r.vx * DT / steps, r.vy * DT / steps);
    r.odometer += std::hypot(r.x - x0, r.y - y0);
    r.yaw = wrap_angle(r.yaw + r.wz * DT);
}

void Engine::vertical(Robot& r) {
    const ChassisParams& c = r.veh.chassis;
    const Terrain& t = field_.terrain;
    double z_ref = r.z + c.max_step + 0.02;
    double support = t.support(r.x, r.y, z_ref);
    double free_z = r.z + r.vz * DT - 0.5 * GRAVITY * DT * DT;
    if (free_z <= support + 1e-4) {
        r.z = support;
        r.airborne = false;
        // vertical speed of terrain following: the grade of the half wheelbase just driven over,
        // so that a robot leaving a ramp top keeps its upward speed and flies
        double v = std::hypot(r.vx, r.vy);
        if (v > 1e-3) {
            double half = c.wheelbase * 0.5;
            double slope = (support - t.support(r.x - r.vx / v * half, r.y - r.vy / v * half, z_ref)) / half;
            r.vz = v * clampd(slope, -1.0, 1.0);
        } else r.vz = 0;
        double cy = std::cos(r.yaw), sy = std::sin(r.yaw);
        double hw = c.wheelbase * 0.5, ht = c.track * 0.5;
        auto h = [&](double fx, double fy) {
            double v0 = t.support(r.x + fx * cy - fy * sy, r.y + fx * sy + fy * cy, z_ref);
            return clampd(v0, r.z - 0.3, r.z + 0.3);
        };
        double front = 0.5 * (h(hw, ht) + h(hw, -ht)), rear = 0.5 * (h(-hw, ht) + h(-hw, -ht));
        double left = 0.5 * (h(hw, ht) + h(-hw, ht)), right = 0.5 * (h(hw, -ht) + h(-hw, -ht));
        double pitch = std::atan2(front - rear, c.wheelbase), roll = std::atan2(left - right, c.track);
        r.pitch += 0.3 * (pitch - r.pitch);
        r.roll += 0.3 * (roll - r.roll);
    } else {
        r.z = free_z;
        r.vz -= GRAVITY * DT;
        r.airborne = true;
    }
}

void Engine::gimbal(Robot& r) {
    const GimbalParams& g = r.veh.gimbal;
    // pitch limits are mechanical, i.e. relative to the chassis attitude under the barrel
    double rel = wrap_angle(r.gimbal_yaw - r.yaw);
    double base_pitch = r.pitch * std::cos(rel) + r.roll * std::sin(rel);
    double pitch_lo = g.pitch_min + base_pitch, pitch_hi = g.pitch_max + base_pitch;
    // Angle loop: proportional, bounded by the rate limit and by the braking distance.
    auto seek = [](double err, double gain, double max_rate, double max_accel) {
        return sign(err) * std::min({gain * std::fabs(err), std::sqrt(2 * max_accel * std::fabs(err)), max_rate});
    };
    auto slew = [](double& rate, double want, double max_accel) { rate += clampd(want - rate, -max_accel * DT, max_accel * DT); };
    bool rate_mode = r.cmd.gimbal == GimbalMode::Rate;
    if (r.cmd.gimbal == GimbalMode::Angle) { r.gimbal_yaw_target = wrap_angle(r.cmd.yaw); r.gimbal_pitch_target = r.cmd.pitch; }
    r.gimbal_pitch_target = clampd(r.gimbal_pitch_target, pitch_lo, pitch_hi);

    if (g.yaw_continuous) {
        double want = rate_mode ? clampd(r.cmd.yaw_rate, -g.max_yaw_rate, g.max_yaw_rate)
                                : seek(wrap_angle(r.gimbal_yaw_target - r.gimbal_yaw), g.response, g.max_yaw_rate, g.max_yaw_accel);
        slew(r.gimbal_yaw_rate, want, g.max_yaw_accel);
        r.gimbal_yaw = wrap_angle(r.gimbal_yaw + r.gimbal_yaw_rate * DT);
    } else {
        // limited travel: work in the chassis frame so that the stop is never crossed
        double want_rel = clampd(wrap_angle(r.gimbal_yaw_target - r.yaw), g.yaw_min, g.yaw_max);
        double want = rate_mode ? clampd(r.cmd.yaw_rate, -g.max_yaw_rate, g.max_yaw_rate) - r.wz
                                : seek(want_rel - rel, g.response, g.max_yaw_rate, g.max_yaw_accel);
        slew(r.gimbal_yaw_rate, want, g.max_yaw_accel);
        double next = clampd(rel + r.gimbal_yaw_rate * DT, g.yaw_min, g.yaw_max);
        if (next == g.yaw_min || next == g.yaw_max) r.gimbal_yaw_rate = 0;
        r.gimbal_yaw = wrap_angle(r.yaw + next);
        if (!rate_mode) r.gimbal_yaw_target = wrap_angle(r.yaw + want_rel);
    }
    double want_pitch = rate_mode ? clampd(r.cmd.pitch_rate, -g.max_pitch_rate, g.max_pitch_rate)
                                  : seek(r.gimbal_pitch_target - r.gimbal_pitch, g.response, g.max_pitch_rate, g.max_pitch_accel);
    slew(r.gimbal_pitch_rate, want_pitch, g.max_pitch_accel);
    double next_pitch = r.gimbal_pitch + r.gimbal_pitch_rate * DT;
    r.gimbal_pitch = clampd(next_pitch, pitch_lo, pitch_hi);
    if (r.gimbal_pitch != next_pitch) r.gimbal_pitch_rate = 0;
    if (rate_mode) { r.gimbal_yaw_target = r.gimbal_yaw; r.gimbal_pitch_target = r.gimbal_pitch; }   // a later hold keeps the present aim
}

void Engine::collide_robots() {
    for (std::size_t i = 0; i < robots_.size(); ++i)
        for (std::size_t j = i + 1; j < robots_.size(); ++j) {
            Robot& a = robots_[i];
            Robot& b = robots_[j];
            if (std::fabs(a.z - b.z) >= std::max(a.veh.chassis.height, b.veh.chassis.height)) continue;
            double dx = b.x - a.x, dy = b.y - a.y, d = std::hypot(dx, dy);
            double need = a.veh.chassis.collision_radius + b.veh.chassis.collision_radius;
            if (d >= need) continue;
            double nx = d > 1e-9 ? dx / d : 1.0, ny = d > 1e-9 ? dy / d : 0.0, overlap = need - d;
            double ma = a.veh.chassis.mass, mb = b.veh.chassis.mass, total = ma + mb;
            move(a, -nx * overlap * mb / total, -ny * overlap * mb / total);
            move(b, nx * overlap * ma / total, ny * overlap * ma / total);
            // whatever one robot could not yield (a wall behind it) the other gives back
            double moved = std::hypot(b.x - a.x, b.y - a.y);
            if (moved < need - 1e-6) {
                double rest = need - moved;
                double ax1 = a.x, ay1 = a.y;
                move(a, -nx * rest, -ny * rest);
                double got = std::hypot(a.x - ax1, a.y - ay1);
                if (got < rest - 1e-6) move(b, nx * (rest - got), ny * (rest - got));
            }
            double rel = (b.vx - a.vx) * nx + (b.vy - a.vy) * ny;
            if (rel < 0) {            // approaching: perfectly inelastic along the normal
                double impulse = -rel / (1 / ma + 1 / mb);
                a.vx -= impulse / ma * nx; a.vy -= impulse / ma * ny;
                b.vx += impulse / mb * nx; b.vy += impulse / mb * ny;
            }
            a.blocked = b.blocked = true;
        }
}

void Engine::referee_power(Robot& r) {
    if (tick_ % (TICK_HZ / 10) != 0) return;
    double average = r.window_energy / 0.1;
    r.window_energy = 0;
    r.measured_power = average;
    auto result = r.power.settle(time_ms(), static_cast<lim::I>(std::llround(average * 1000.0)),
                                 static_cast<lim::I>(std::llround(r.veh.rules.chassis_power_limit * 1000.0)));
    if (std::holds_alternative<lim::Error>(result))
        throw std::runtime_error("chassis power settlement failed: " + std::string(lim::error_name(std::get<lim::Error>(result))));
}

void Engine::detect_zones(Robot& r) {
    r.zones.clear();
    if (r.airborne) return;
    double z_ref = r.z + kZoneHeight;
    bool roofed = std::isfinite(field_.terrain.headroom(r.x, r.y, z_ref));
    for (std::size_t i = 0; i < field_.zones.size(); ++i) {
        const Zone& z = field_.zones[i];
        if (z.under_only && !roofed) continue;
        if (z.contains(r.x, r.y)) r.zones.push_back(static_cast<int>(i));
    }
}

void Engine::step() {
    ++tick_;
    for (Robot& r : robots_) drive(r);
    collide_robots();
    for (Robot& r : robots_) {
        vertical(r);
        gimbal(r);
        referee_power(r);
        detect_zones(r);
    }
}

json::Value Engine::robot_json(const Robot& r) const {
    json::Value o = json::Value::object();
    o.set("id", r.id);
    o.set("team", r.team == 0 ? "red" : "blue");
    o.set("vehicle", r.veh.name);
    o.set("x", r.x); o.set("y", r.y); o.set("z", r.z);
    o.set("yaw", r.yaw); o.set("pitch", r.pitch); o.set("roll", r.roll);
    o.set("vx", r.vx); o.set("vy", r.vy); o.set("vz", r.vz); o.set("wz", r.wz);
    o.set("airborne", r.airborne);
    o.set("blocked", r.blocked);
    o.set("radius", r.veh.chassis.collision_radius);
    o.set("length", r.veh.chassis.length); o.set("width", r.veh.chassis.width); o.set("height", r.veh.chassis.height);
    json::Value g = json::Value::object();
    g.set("yaw", r.gimbal_yaw); g.set("pitch", r.gimbal_pitch);
    g.set("yaw_rate", r.gimbal_yaw_rate); g.set("pitch_rate", r.gimbal_pitch_rate);
    o.set("gimbal", g);
    const lim::PowerSnapshot& p = r.power.snapshot();
    json::Value pw = json::Value::object();
    pw.set("motor_w", r.motor_power);
    pw.set("battery_w", r.battery_power);
    pw.set("measured_w", r.measured_power);
    pw.set("limit_w", r.veh.rules.chassis_power_limit);
    pw.set("buffer_j", p.buffer_uj * 1e-6);
    pw.set("supercap_j", r.cap_energy);
    pw.set("chassis_enabled", p.chassis_enabled(time_ms()));
    pw.set("cutoffs", static_cast<long long>(p.cutoffs));
    o.set("power", pw);
    json::Value zones = json::Value::array();
    for (int z : r.zones) zones.push(field_.zones[static_cast<std::size_t>(z)].id);
    o.set("zones", zones);
    o.set("odometer", r.odometer);
    return o;
}

json::Value Engine::state_json() const {
    json::Value o = json::Value::object();
    o.set("profile", PROFILE);
    o.set("field", field_.id);
    o.set("seed", static_cast<long long>(seed_));
    o.set("tick", tick_);
    o.set("time_ms", static_cast<long long>(time_ms()));
    json::Value robots = json::Value::array();
    for (const Robot& r : robots_) robots.push(robot_json(r));
    o.set("robots", robots);
    return o;
}
} // namespace field_sim
