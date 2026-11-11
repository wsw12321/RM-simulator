#pragma once
// Vehicle configuration (rmuc-vehicle.v1). One table in vehicle.cpp defines every
// tunable parameter: its JSON key, unit, range and the struct member it sets.
// The schema served to the garage page, validation and defaults all come from it.
// Mechanical values are ENGINEERING parameters chosen by whoever builds the robot;
// HP, heat and power limits are NOT configurable here: they follow from the rule
// choices (class, chassis type, launcher type) through rules/rmuc2027_growth_v1.
#include "json.hpp"
#include "../rules/rmuc2027_growth_v1/growth_rules.hpp"
#include <string>
#include <vector>

namespace field_sim {
namespace grow = rmuc2027::growth_v1;
inline constexpr const char* VEHICLE_FORMAT = "rmuc-vehicle.v1";
inline constexpr double PI = 3.14159265358979323846;
inline double rad(double deg) { return deg * PI / 180.0; }
inline double deg(double r) { return r * 180.0 / PI; }

enum class Drive : int { Mecanum, Omni, Steering, Differential, Balance };
inline bool holonomic(Drive d) { return d == Drive::Omni || d == Drive::Mecanum || d == Drive::Steering; }
enum class RobotClass : int { Infantry, Heavy, Sentry };
enum class Caliber : int { None, Mm17, Mm42 };
enum class Face : int { Front, Left, Rear, Right };

struct ChassisParams {
    Drive drive = Drive::Mecanum;
    double length = 0.55, width = 0.47, height = 0.48;   // body box; height is the tallest point above the wheels' contact
    double mass = 18.0;
    double wheelbase = 0.40, track = 0.38;
    double collision_radius = 0.33;
    double max_speed = 3.5, max_speed_lateral = 3.0;      // m/s
    double max_accel = 4.5, max_decel = 7.0;              // m/s^2
    double max_yaw_rate = rad(520), max_yaw_accel = rad(2300);
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
struct LauncherParams {
    Caliber caliber = Caliber::Mm17;
    double muzzle_speed = 23.0, speed_sigma = 0.3;   // m/s: set point and shot-to-shot spread
    double max_fire_rate = 15.0;                     // Hz
    double dispersion = 0.004;                       // rad, one sigma per axis
    bool heat_guard = true;                          // firmware refuses a shot that would exceed the heat limit
};
struct ArmorPlate {
    Face face = Face::Front;
    bool large = false;                              // small 135 x 125 mm, large 230 x 127 mm
    double center_height = 0.15, offset = 0.0;       // m above the wheels' contact; sideways along the face
    double width() const { return large ? 0.230 : 0.135; }
    double height() const { return large ? 0.127 : 0.125; }
};
struct CameraParams {
    double hfov = rad(70), vfov = rad(50);
    double max_range = 8.0;                          // m: armour plates are recognised up to here
    double fps = 60.0, latency = 0.02;               // Hz, s
    double angle_noise = 0.0015, range_noise = 0.02; // rad one sigma; fraction of the range
    double max_view_angle = rad(65);                 // a plate turned further away is not recognised
};
struct LidarParams {
    bool enabled = false;
    double beams = 72, max_range = 10.0, rate = 10.0, height = 0.30, noise = 0.01;
};
struct RuleChoice {
    RobotClass robot_class = RobotClass::Infantry;
    grow::Chassis chassis = grow::DEFAULT_CHASSIS;      // infantry: HP first / power first (§5.5.2)
    grow::Launcher launcher = grow::DEFAULT_LAUNCHER;   // infantry: cooling first / burst first
    bool sentry_auto = true;                            // sentry: automatic / semi-automatic (§3.1.3)
};
struct VehicleParams {
    std::string name = "default-infantry", notes;
    RuleChoice rules;
    ChassisParams chassis;
    GimbalParams gimbal;
    LauncherParams launcher;
    std::vector<ArmorPlate> armor{{Face::Front, false, 0.15, 0}, {Face::Left, false, 0.15, 0}, {Face::Rear, false, 0.15, 0}, {Face::Right, false, 0.15, 0}};
    CameraParams camera;
    LidarParams lidar;
};
// Rule-given attributes of a vehicle at a level (heavy / sentry ignore the level).
struct Attributes {
    long long max_hp = 0;
    double chassis_power_limit = 0;     // W
    Caliber caliber = Caliber::None;    // launcher the rules give this robot right now
    long long heat_limit = 0, cooling_per_second = 0;
    double muzzle_speed_limit = 0;      // m/s
    long long initial_allowance = 0;    // rounds
};
grow::Loadout loadout(const VehicleParams&, bool heavy_armed);
// Throws std::runtime_error when the rule module rejects the combination.
Attributes attributes(const VehicleParams&, int level, bool heavy_armed);

VehicleParams default_vehicle();
// Strict: unknown keys, wrong types, out-of-range values and inconsistent geometry throw
// json::Error naming the key. Missing keys take their defaults.
VehicleParams vehicle_from_json(const json::Value&);
// The complete normalised document; vehicle_from_json(vehicle_to_json(v)) reproduces v.
json::Value vehicle_to_json(const VehicleParams&);
// Parameter table for user interfaces: sections, keys, units, ranges, defaults, choices.
json::Value vehicle_schema_json();
} // namespace field_sim
