#pragma once
// field_sim: robots on the extracted official RMUC 2027 battlefield.
// 100 Hz authoritative engine. Terrain and zones come from field/<id>/; robot
// mechanics below are an explicit 2.5-D ENGINEERING model (see README.md).
#include "json.hpp"
#include "terrain.hpp"
#include "../rules/rmuc2027_limits_v1/limits_rules.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#ifndef FIELD_SIM_BUILD_ID
#define FIELD_SIM_BUILD_ID "development-unpinned"
#endif

namespace field_sim {
namespace lim = rmuc2027::limits_v1;
inline constexpr const char* PROFILE = "rmuc2027-field-sim-v1";
inline constexpr int TICK_HZ = 100;
inline constexpr double DT = 1.0 / TICK_HZ;
inline constexpr double GRAVITY = 9.80665;
inline constexpr double PI = 3.14159265358979323846;
inline double rad(double deg) { return deg * PI / 180.0; }
inline double deg(double r) { return r * 180.0 / PI; }
double wrap_angle(double a);   // to (-pi, pi]

// ---- field description (field.json next to the terrain file)
struct Zone {
    std::string id, kind, team;   // team: red | blue | neutral
    bool under_only = false;      // card lies under an overhang (tunnel interior)
    std::vector<std::array<double, 2>> polygon;   // metres
    double min_x = 0, max_x = 0, min_y = 0, max_y = 0;
    bool contains(double x, double y) const;
};
struct Structure { std::string id, kind, team; double x = 0, y = 0; std::array<double, 3> lo{}, hi{}; };
struct Pose { double x = 0, y = 0, yaw = 0; };
struct Field {
    std::string id, terrain_sha256;
    Terrain terrain;
    std::vector<Zone> zones;
    std::vector<Structure> structures;
    std::array<std::vector<Pose>, 2> spawns;   // 0 red, 1 blue
    // Reads <dir>/field.json and the terrain file it names. Throws std::runtime_error.
    static Field load(const std::string& dir);
};

// ---- vehicle parameters (SI units). Defaults are engineering values.
enum class Drive : std::uint8_t { Omni, Mecanum, Steering, Differential, Balance };
inline bool holonomic(Drive d) { return d == Drive::Omni || d == Drive::Mecanum || d == Drive::Steering; }
struct ChassisParams {
    Drive drive = Drive::Mecanum;
    double length = 0.55, width = 0.47, height = 0.48;   // body box; height is the tallest point above the wheels' contact
    double mass = 18.0;
    double wheelbase = 0.40, track = 0.38;
    double collision_radius = 0.33;
    double max_speed = 3.5, max_speed_lateral = 3.0;      // m/s
    double max_accel = 4.5, max_decel = 7.0;              // m/s^2
    double max_yaw_rate = 9.0, max_yaw_accel = 40.0;      // rad/s, rad/s^2
    double max_step = 0.03;                               // climbable vertical step, m
    double max_slope = 30.0;                              // climbable grade, degrees
    double rolling_resistance = 0.07;                     // resisting force / weight
    double drivetrain_efficiency = 0.75;
    double idle_power = 6.0;                              // W
    double stall_power = 60.0;                            // W of loss at full acceleration force
    bool power_limiter = true;                            // on-board limiter keeps the draw inside the allowance
    double buffer_reserve = 20.0;                         // J of referee buffer the limiter leaves untouched
    double supercap_energy = 0.0;                         // J, 0 = none fitted
    double supercap_power = 150.0;                        // W, maximum charge / discharge rate
};
struct GimbalParams {
    bool yaw_continuous = true;                 // slip ring; otherwise limited relative to the chassis
    double yaw_min = rad(-170), yaw_max = rad(170);
    double pitch_min = rad(-25), pitch_max = rad(35);   // relative to the chassis, up positive
    double max_yaw_rate = rad(540), max_pitch_rate = rad(360);
    double max_yaw_accel = rad(6000), max_pitch_accel = rad(4000);
    double response = 18.0;                     // 1/s proportional gain of the angle loop
    double pivot_height = 0.36;                 // m above the wheels' contact
    double muzzle_forward = 0.14;               // m from the pitch axis
};
struct RuleParams {
    double chassis_power_limit = 80.0;          // W. Replaced by the rule tables in a later milestone.
};
struct VehicleParams {
    std::string name = "default-infantry";
    ChassisParams chassis;
    GimbalParams gimbal;
    RuleParams rules;
};

// ---- per-robot command (latest value persists)
enum class ChassisFrame : std::uint8_t { Chassis, Gimbal };
enum class GimbalMode : std::uint8_t { Hold, Angle, Rate };
struct Command {
    double vx = 0, vy = 0, wz = 0;              // m/s forward, m/s left, rad/s counter-clockwise
    ChassisFrame frame = ChassisFrame::Chassis;
    GimbalMode gimbal = GimbalMode::Hold;
    double yaw = 0, pitch = 0;                  // Angle: absolute yaw (field frame) and pitch above the horizon, rad
    double yaw_rate = 0, pitch_rate = 0;        // Rate: rad/s
    bool supercap = true;                       // allow the supercapacitor to assist
};
// Strict parser for the "command" JSON object. Throws json::Error.
Command parse_command(const json::Value&);

struct Robot {
    std::string id;
    int team = 0;                               // 0 red, 1 blue
    VehicleParams veh;
    Command cmd;
    // chassis state (field frame, metres / radians)
    double x = 0, y = 0, z = 0, yaw = 0, pitch = 0, roll = 0;
    double vx = 0, vy = 0, vz = 0, wz = 0;
    bool airborne = false, blocked = false;
    // gimbal state: yaw in the field frame, pitch above the horizon
    double gimbal_yaw = 0, gimbal_pitch = 0, gimbal_yaw_rate = 0, gimbal_pitch_rate = 0;
    double gimbal_yaw_target = 0, gimbal_pitch_target = 0;
    // power
    double motor_power = 0, battery_power = 0;  // W this tick
    double cap_energy = 0;                      // J stored in the supercapacitor
    double window_energy = 0;                   // J drawn from the battery in the current 100 ms referee window
    double measured_power = 0;                  // W, referee average of the last window
    lim::ChassisPower power;                    // referee buffer energy and cut-off (limits_v1)
    double odometer = 0;                        // m travelled
    std::vector<int> zones;                     // indices into Field::zones this robot activates
};

class Engine {
public:
    Engine(Field field, std::uint64_t seed = 2027);
    // Throws std::runtime_error when the pose is outside the field or collides.
    std::size_t add_robot(const std::string& id, int team, const VehicleParams&, const Pose&);
    void set_command(std::size_t robot, const Command& c) { robots_.at(robot).cmd = c; }
    void step();
    int tick() const { return tick_; }
    std::int64_t time_ms() const { return static_cast<std::int64_t>(tick_) * 1000 / TICK_HZ; }
    const Field& field() const { return field_; }
    const std::vector<Robot>& robots() const { return robots_; }
    Robot& robot(std::size_t i) { return robots_.at(i); }
    int find(const std::string& id) const;
    // True when a body of this vehicle cannot stand at the pose (wall, low roof, another robot, outside).
    bool placement_blocked(const VehicleParams&, double x, double y, int ignore = -1) const;
    json::Value state_json() const;
    json::Value robot_json(const Robot&) const;

private:
    Field field_;
    std::uint64_t seed_;
    int tick_ = 0;
    std::vector<Robot> robots_;

    void drive(Robot&);
    void move(Robot&, double dx, double dy);
    int blocked_normals(const Robot&, double x, double y, double z, double (*out)[2], int cap) const;
    void vertical(Robot&);
    void gimbal(Robot&);
    void collide_robots();
    void referee_power(Robot&);
    void detect_zones(Robot&);
    double chassis_power(const Robot&, double ax, double ay, double alpha, double slope_fx, double slope_fy) const;
};
std::string build_id();
VehicleParams default_vehicle();
} // namespace field_sim
