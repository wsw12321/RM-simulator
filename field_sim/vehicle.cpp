#include "vehicle.hpp"
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <variant>

namespace field_sim {
namespace {
enum class Kind { Number, Integer, Bool, Enum };
struct Spec {
    const char* section;    // "" = top level, "sensors.camera" = nested
    const char* key;
    Kind kind;
    const char* unit;
    double min, max, scale; // engine value = document value * scale
    const char* choices;    // enum: names separated by '|', in enumerator order
    double* (*number)(VehicleParams&);
    bool* (*flag)(VehicleParams&);
    int (*get_enum)(const VehicleParams&);
    void (*set_enum)(VehicleParams&, int);
};
#define NUM(section, key, unit, lo, hi, scale, member) \
    Spec{section, key, Kind::Number, unit, lo, hi, scale, nullptr, [](VehicleParams& v) -> double* { return &v.member; }, nullptr, nullptr, nullptr}
#define INT(section, key, unit, lo, hi, member) \
    Spec{section, key, Kind::Integer, unit, lo, hi, 1.0, nullptr, [](VehicleParams& v) -> double* { return &v.member; }, nullptr, nullptr, nullptr}
#define FLAG(section, key, member) \
    Spec{section, key, Kind::Bool, "", 0, 1, 1.0, nullptr, nullptr, [](VehicleParams& v) -> bool* { return &v.member; }, nullptr, nullptr}
#define ENUM(section, key, choices, type, member) \
    Spec{section, key, Kind::Enum, "", 0, 0, 1.0, choices, nullptr, nullptr, \
         [](const VehicleParams& v) { return static_cast<int>(v.member); }, [](VehicleParams& v, int i) { v.member = static_cast<type>(i); }}

const double MM = 0.001, DEG = PI / 180.0;
const Spec SPECS[] = {
    ENUM("", "class", "infantry|heavy|sentry", RobotClass, rules.robot_class),
    ENUM("rules", "chassis_type", "power_first|hp_first", grow::Chassis, rules.chassis),
    ENUM("rules", "launcher_type", "burst_first|cooling_first", grow::Launcher, rules.launcher),
    Spec{"rules", "sentry_mode", Kind::Enum, "", 0, 0, 1.0, "semi|auto", nullptr, nullptr,
         [](const VehicleParams& v) { return v.rules.sentry_auto ? 1 : 0; }, [](VehicleParams& v, int i) { v.rules.sentry_auto = i == 1; }},

    ENUM("chassis", "drive", "mecanum|omni|steering|differential|balance", Drive, chassis.drive),
    NUM("chassis", "length_mm", "mm", 200, 1200, MM, chassis.length),
    NUM("chassis", "width_mm", "mm", 200, 1200, MM, chassis.width),
    NUM("chassis", "height_mm", "mm", 100, 1500, MM, chassis.height),
    NUM("chassis", "mass_kg", "kg", 3, 80, 1, chassis.mass),
    NUM("chassis", "wheelbase_mm", "mm", 100, 1000, MM, chassis.wheelbase),
    NUM("chassis", "track_mm", "mm", 100, 1000, MM, chassis.track),
    NUM("chassis", "collision_radius_mm", "mm", 120, 800, MM, chassis.collision_radius),
    NUM("chassis", "max_speed_mps", "m/s", 0.2, 10, 1, chassis.max_speed),
    NUM("chassis", "max_lateral_speed_mps", "m/s", 0.1, 10, 1, chassis.max_speed_lateral),
    NUM("chassis", "max_accel_mps2", "m/s²", 0.5, 20, 1, chassis.max_accel),
    NUM("chassis", "max_decel_mps2", "m/s²", 0.5, 30, 1, chassis.max_decel),
    NUM("chassis", "max_yaw_rate_dps", "°/s", 30, 1500, DEG, chassis.max_yaw_rate),
    NUM("chassis", "max_yaw_accel_dps2", "°/s²", 100, 20000, DEG, chassis.max_yaw_accel),
    NUM("chassis", "max_step_mm", "mm", 0, 400, MM, chassis.max_step),
    NUM("chassis", "max_slope_deg", "°", 5, 60, 1, chassis.max_slope),
    NUM("chassis", "rolling_resistance", "", 0.005, 0.5, 1, chassis.rolling_resistance),
    NUM("chassis", "drivetrain_efficiency", "", 0.2, 1, 1, chassis.drivetrain_efficiency),
    NUM("chassis", "idle_power_w", "W", 0, 50, 1, chassis.idle_power),
    NUM("chassis", "stall_power_w", "W", 0, 500, 1, chassis.stall_power),
    FLAG("chassis", "power_limiter", chassis.power_limiter),
    NUM("chassis", "buffer_reserve_j", "J", 0, 60, 1, chassis.buffer_reserve),
    NUM("chassis", "supercap_energy_j", "J", 0, 5000, 1, chassis.supercap_energy),
    NUM("chassis", "supercap_power_w", "W", 0, 600, 1, chassis.supercap_power),

    FLAG("gimbal", "yaw_continuous", gimbal.yaw_continuous),
    NUM("gimbal", "yaw_min_deg", "°", -180, 0, DEG, gimbal.yaw_min),
    NUM("gimbal", "yaw_max_deg", "°", 0, 180, DEG, gimbal.yaw_max),
    NUM("gimbal", "pitch_min_deg", "°", -60, 0, DEG, gimbal.pitch_min),
    NUM("gimbal", "pitch_max_deg", "°", 0, 80, DEG, gimbal.pitch_max),
    NUM("gimbal", "max_yaw_rate_dps", "°/s", 30, 2000, DEG, gimbal.max_yaw_rate),
    NUM("gimbal", "max_pitch_rate_dps", "°/s", 30, 2000, DEG, gimbal.max_pitch_rate),
    NUM("gimbal", "max_yaw_accel_dps2", "°/s²", 200, 50000, DEG, gimbal.max_yaw_accel),
    NUM("gimbal", "max_pitch_accel_dps2", "°/s²", 200, 50000, DEG, gimbal.max_pitch_accel),
    NUM("gimbal", "response_per_s", "1/s", 2, 60, 1, gimbal.response),
    NUM("gimbal", "pivot_height_mm", "mm", 50, 1200, MM, gimbal.pivot_height),
    NUM("gimbal", "muzzle_forward_mm", "mm", 0, 600, MM, gimbal.muzzle_forward),

    ENUM("launcher", "caliber", "none|17mm|42mm", Caliber, launcher.caliber),
    NUM("launcher", "muzzle_speed_mps", "m/s", 5, 40, 1, launcher.muzzle_speed),
    NUM("launcher", "speed_sigma_mps", "m/s", 0, 3, 1, launcher.speed_sigma),
    NUM("launcher", "max_fire_rate_hz", "Hz", 0.5, 30, 1, launcher.max_fire_rate),
    NUM("launcher", "dispersion_mrad", "mrad", 0, 50, 0.001, launcher.dispersion),
    FLAG("launcher", "heat_guard", launcher.heat_guard),

    NUM("sensors.camera", "hfov_deg", "°", 10, 180, DEG, camera.hfov),
    NUM("sensors.camera", "vfov_deg", "°", 10, 120, DEG, camera.vfov),
    NUM("sensors.camera", "max_range_m", "m", 1, 30, 1, camera.max_range),
    NUM("sensors.camera", "fps", "Hz", 5, 100, 1, camera.fps),
    NUM("sensors.camera", "latency_ms", "ms", 0, 200, 0.001, camera.latency),
    NUM("sensors.camera", "angle_noise_mrad", "mrad", 0, 50, 0.001, camera.angle_noise),
    NUM("sensors.camera", "range_noise_pct", "%", 0, 30, 0.01, camera.range_noise),
    NUM("sensors.camera", "max_view_angle_deg", "°", 20, 89, DEG, camera.max_view_angle),

    FLAG("sensors.lidar", "enabled", lidar.enabled),
    INT("sensors.lidar", "beams", "", 8, 720, lidar.beams),
    NUM("sensors.lidar", "max_range_m", "m", 1, 40, 1, lidar.max_range),
    NUM("sensors.lidar", "rate_hz", "Hz", 1, 20, 1, lidar.rate),
    NUM("sensors.lidar", "height_mm", "mm", 50, 1200, MM, lidar.height),
    NUM("sensors.lidar", "noise_mm", "mm", 0, 200, MM, lidar.noise),
};
const char* const SECTIONS[] = {"", "rules", "chassis", "gimbal", "launcher", "sensors.camera", "sensors.lidar"};
const char* const FACES[] = {"front", "left", "rear", "right"};
constexpr std::size_t MAX_NAME = 40, MAX_NOTES = 300;

std::vector<std::string> split(const char* choices) {
    std::vector<std::string> out;
    std::string cur;
    for (const char* p = choices; *p; ++p) {
        if (*p == '|') { out.push_back(cur); cur.clear(); } else cur += *p;
    }
    out.push_back(cur);
    return out;
}
// Round a document value so that values like 0.47 m -> 470 mm print without float dust.
double tidy(double v) { return std::round(v * 1e6) / 1e6; }
std::string path(const Spec& s) { return *s.section ? std::string(s.section) + "." + s.key : std::string(s.key); }

json::Value spec_value(const Spec& s, const VehicleParams& v) {
    VehicleParams& m = const_cast<VehicleParams&>(v);   // accessors are shared with the writer
    switch (s.kind) {
    case Kind::Number: return json::Value(tidy(*s.number(m) / s.scale));
    case Kind::Integer: return json::Value(std::round(*s.number(m)));
    case Kind::Bool: return json::Value(*s.flag(m));
    case Kind::Enum: return json::Value(split(s.choices).at(static_cast<std::size_t>(s.get_enum(v))));
    }
    return json::Value();
}
void apply(const Spec& s, const json::Value& value, VehicleParams& v) {
    const std::string where = path(s);
    switch (s.kind) {
    case Kind::Number:
    case Kind::Integer: {
        if (!value.is_number()) throw json::Error(where + " must be a number");
        double x = value.as_number();
        if (s.kind == Kind::Integer && x != std::floor(x)) throw json::Error(where + " must be a whole number");
        if (!(x >= s.min && x <= s.max)) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "%s must be between %g and %g %s", where.c_str(), s.min, s.max, s.unit);
            throw json::Error(buf);
        }
        *s.number(v) = x * s.scale;
        break;
    }
    case Kind::Bool:
        if (!value.is_bool()) throw json::Error(where + " must be true or false");
        *s.flag(v) = value.as_bool();
        break;
    case Kind::Enum: {
        if (!value.is_string()) throw json::Error(where + " must be one of " + s.choices);
        auto names = split(s.choices);
        for (std::size_t i = 0; i < names.size(); ++i)
            if (names[i] == value.as_string()) { s.set_enum(v, static_cast<int>(i)); return; }
        throw json::Error(where + " must be one of " + s.choices);
    }
    }
}
const json::Value* section_of(const json::Value& doc, const char* section) {
    if (!*section) return &doc;
    const char* dot = std::strchr(section, '.');
    if (!dot) return doc.find(section);
    const json::Value* outer = doc.find(std::string(section, dot));
    return outer ? outer->find(dot + 1) : nullptr;
}
std::string checked_text(const json::Value& v, const char* key, std::size_t max_chars, bool required) {
    if (!v.is_string()) throw json::Error(std::string(key) + " must be a string");
    const std::string& s = v.as_string();
    std::size_t chars = 0;
    for (unsigned char c : s) {
        if (c < 0x20 || c == 0x7f) throw json::Error(std::string(key) + " must not contain control characters");
        if ((c & 0xC0) != 0x80) ++chars;
    }
    if (chars > max_chars) throw json::Error(std::string(key) + " is too long (at most " + std::to_string(max_chars) + " characters)");
    if (required && s.find_first_not_of(' ') == std::string::npos) throw json::Error(std::string(key) + " must not be empty");
    return s;
}
void check_consistency(const VehicleParams& v) {
    const ChassisParams& c = v.chassis;
    if (c.wheelbase > c.length) throw json::Error("chassis.wheelbase_mm must not exceed chassis.length_mm");
    if (c.track > c.width) throw json::Error("chassis.track_mm must not exceed chassis.width_mm");
    if (c.collision_radius < 0.5 * std::min(c.length, c.width) - 1e-9)
        throw json::Error("chassis.collision_radius_mm must be at least half of the smaller of length and width");
    if (c.collision_radius > 0.5 * std::hypot(c.length, c.width) + 0.05 + 1e-9)
        throw json::Error("chassis.collision_radius_mm must not exceed half the body diagonal by more than 50 mm");
    if (c.max_speed_lateral > c.max_speed + 1e-9) throw json::Error("chassis.max_lateral_speed_mps must not exceed chassis.max_speed_mps");
    if (v.gimbal.pivot_height > c.height + 1e-9) throw json::Error("gimbal.pivot_height_mm must not exceed chassis.height_mm");
    if (v.lidar.height > c.height + 1e-9) throw json::Error("sensors.lidar.height_mm must not exceed chassis.height_mm");
    // launcher against the rule tables: infantry and sentry carry one 17 mm launcher (tables 3-5, 3-6),
    // the heavy robot may mount the modular 42 mm launcher (tables 3-3, 3-4)
    if (v.rules.robot_class == RobotClass::Heavy) {
        if (v.launcher.caliber == Caliber::Mm17) throw json::Error("launcher.caliber of a heavy robot is 42mm or none");
    } else if (v.launcher.caliber != Caliber::Mm17) throw json::Error("launcher.caliber of an infantry or sentry robot is 17mm");
    bool seen[4] = {false, false, false, false};
    for (const ArmorPlate& p : v.armor) {
        int f = static_cast<int>(p.face);
        if (seen[f]) throw json::Error(std::string("armor: more than one plate on the ") + FACES[f] + " face");
        seen[f] = true;
        double span = (p.face == Face::Front || p.face == Face::Rear) ? c.width : c.length;
        if (std::fabs(p.offset) + p.width() / 2 > span / 2 + 1e-9) throw json::Error(std::string("armor: the ") + FACES[f] + " plate does not fit on that face");
        if (p.center_height - p.height() / 2 < -1e-9 || p.center_height + p.height() / 2 > c.height + 1e-9)
            throw json::Error(std::string("armor: the ") + FACES[f] + " plate must lie between the ground and chassis.height_mm");
    }
}
} // namespace

VehicleParams default_vehicle() { return VehicleParams{}; }

grow::Loadout loadout(const VehicleParams& v, bool heavy_armed) {
    grow::Loadout l;
    switch (v.rules.robot_class) {
    case RobotClass::Infantry: l.robot_class = grow::RobotClass::Infantry; break;
    case RobotClass::Heavy: l.robot_class = grow::RobotClass::Heavy; break;
    case RobotClass::Sentry: l.robot_class = v.rules.sentry_auto ? grow::RobotClass::SentryAuto : grow::RobotClass::SentrySemi; break;
    }
    l.chassis = v.rules.chassis;
    l.launcher = v.rules.launcher;
    l.heavy_stage = heavy_armed ? grow::HeavyStage::Armed : grow::HeavyStage::Default;
    return l;
}

Attributes attributes(const VehicleParams& v, int level, bool heavy_armed) {
    grow::Loadout l = loadout(v, heavy_armed);
    auto perf = grow::performance(l, level);
    auto allowance = grow::initial_allowance(l.robot_class);
    if (!std::holds_alternative<grow::Performance>(perf) || !std::holds_alternative<std::int64_t>(allowance))
        throw std::runtime_error("the rule tables reject this class / level combination");
    const grow::Performance& p = std::get<grow::Performance>(perf);
    Attributes a;
    a.max_hp = p.max_hp;
    a.chassis_power_limit = static_cast<double>(p.chassis_power_w);
    a.caliber = p.caliber == grow::Caliber::Mm17 ? Caliber::Mm17 : p.caliber == grow::Caliber::Mm42 ? Caliber::Mm42 : Caliber::None;
    a.heat_limit = p.heat_limit.value_or(0);
    a.cooling_per_second = p.cooling_per_second.value_or(0);
    a.muzzle_speed_limit = static_cast<double>(p.muzzle_speed_limit_mm_s.value_or(0)) * 0.001;
    a.initial_allowance = std::get<std::int64_t>(allowance);
    return a;
}

VehicleParams vehicle_from_json(const json::Value& doc) {
    if (!doc.is_object()) throw json::Error("a vehicle is a JSON object");
    VehicleParams v;
    v.armor.clear();
    bool has_format = false, has_name = false, has_armor = false;
    for (const auto& kv : doc.as_object()) {
        const std::string& k = kv.first;
        if (k == "format") {
            if (!kv.second.is_string() || kv.second.as_string() != VEHICLE_FORMAT) throw json::Error(std::string("format must be ") + VEHICLE_FORMAT);
            has_format = true;
        } else if (k == "name") { v.name = checked_text(kv.second, "name", MAX_NAME, true); has_name = true; }
        else if (k == "notes") v.notes = checked_text(kv.second, "notes", MAX_NOTES, false);
        else if (k == "armor") {
            has_armor = true;
            if (!kv.second.is_array() || kv.second.as_array().size() > 4) throw json::Error("armor must be a list of at most four plates");
            for (const auto& item : kv.second.as_array()) {
                if (!item.is_object()) throw json::Error("armor: a plate is an object");
                ArmorPlate plate;
                bool has_face = false;
                for (const auto& f : item.as_object()) {
                    if (f.first == "face") {
                        if (!f.second.is_string()) throw json::Error("armor: face must be front, left, rear or right");
                        int found = -1;
                        for (int i = 0; i < 4; ++i) if (f.second.as_string() == FACES[i]) found = i;
                        if (found < 0) throw json::Error("armor: face must be front, left, rear or right");
                        plate.face = static_cast<Face>(found);
                        has_face = true;
                    } else if (f.first == "size") {
                        if (!f.second.is_string() || (f.second.as_string() != "small" && f.second.as_string() != "large")) throw json::Error("armor: size must be small or large");
                        plate.large = f.second.as_string() == "large";
                    } else if (f.first == "center_height_mm" || f.first == "offset_mm") {
                        if (!f.second.is_number() || std::fabs(f.second.as_number()) > 1500) throw json::Error("armor: " + f.first + " must be a number within ±1500 mm");
                        (f.first == "offset_mm" ? plate.offset : plate.center_height) = f.second.as_number() * MM;
                    } else throw json::Error("armor: unknown key '" + f.first + "'");
                }
                if (!has_face) throw json::Error("armor: every plate needs a face");
                v.armor.push_back(plate);
            }
        } else if (k == "class") { /* handled by the table below */ }
        else {
            bool section = false;
            for (const char* s : SECTIONS) {
                std::string top(s);
                if (top.find('.') != std::string::npos) top = top.substr(0, top.find('.'));
                if (!top.empty() && top == k) section = true;
            }
            if (!section) throw json::Error("unknown key '" + k + "'");
            if (!kv.second.is_object()) throw json::Error(k + " must be an object");
        }
    }
    if (!has_format) throw json::Error(std::string("format must be ") + VEHICLE_FORMAT);
    if (!has_name) throw json::Error("name is required");
    if (!has_armor) v.armor = VehicleParams{}.armor;
    // unknown keys inside sections
    for (const char* s : SECTIONS) {
        if (!*s) continue;
        const json::Value* sec = section_of(doc, s);
        if (!sec) continue;
        if (!sec->is_object()) throw json::Error(std::string(s) + " must be an object");
        for (const auto& kv : sec->as_object()) {
            bool known = false;
            for (const Spec& spec : SPECS) if (std::strcmp(spec.section, s) == 0 && kv.first == spec.key) known = true;
            if (!known) throw json::Error(std::string("unknown key '") + s + "." + kv.first + "'");
        }
    }
    if (const json::Value* sensors = doc.find("sensors"))
        for (const auto& kv : sensors->as_object())
            if (kv.first != "camera" && kv.first != "lidar") throw json::Error("unknown key 'sensors." + kv.first + "'");
    for (const Spec& spec : SPECS) {
        const json::Value* sec = section_of(doc, spec.section);
        const json::Value* value = sec ? sec->find(spec.key) : nullptr;
        if (value) apply(spec, *value, v);
    }
    check_consistency(v);
    return v;
}

json::Value vehicle_to_json(const VehicleParams& v) {
    json::Value doc = json::Value::object();
    doc.set("format", VEHICLE_FORMAT);
    doc.set("name", v.name);
    doc.set("notes", v.notes);
    for (const Spec& s : SPECS) if (!*s.section) doc.set(s.key, spec_value(s, v));
    for (const char* name : {"rules", "chassis", "gimbal", "launcher"}) {
        json::Value sec = json::Value::object();
        for (const Spec& s : SPECS) if (std::strcmp(s.section, name) == 0) sec.set(s.key, spec_value(s, v));
        doc.set(name, sec);
    }
    json::Value armor = json::Value::array();
    for (const ArmorPlate& p : v.armor) {
        json::Value plate = json::Value::object();
        plate.set("face", FACES[static_cast<int>(p.face)]);
        plate.set("size", p.large ? "large" : "small");
        plate.set("center_height_mm", tidy(p.center_height / MM));
        plate.set("offset_mm", tidy(p.offset / MM));
        armor.push(plate);
    }
    doc.set("armor", armor);
    json::Value sensors = json::Value::object();
    for (const char* name : {"camera", "lidar"}) {
        json::Value sec = json::Value::object();
        std::string full = std::string("sensors.") + name;
        for (const Spec& s : SPECS) if (full == s.section) sec.set(s.key, spec_value(s, v));
        sensors.set(name, sec);
    }
    doc.set("sensors", sensors);
    return doc;
}

json::Value vehicle_schema_json() {
    VehicleParams defaults;
    json::Value fields = json::Value::array();
    for (const Spec& s : SPECS) {
        json::Value f = json::Value::object();
        f.set("section", s.section);
        f.set("key", s.key);
        f.set("type", s.kind == Kind::Number ? "number" : s.kind == Kind::Integer ? "integer" : s.kind == Kind::Bool ? "boolean" : "enum");
        if (s.kind == Kind::Number || s.kind == Kind::Integer) { f.set("unit", s.unit); f.set("min", s.min); f.set("max", s.max); }
        if (s.kind == Kind::Enum) {
            json::Value choices = json::Value::array();
            for (const auto& c : split(s.choices)) choices.push(c);
            f.set("choices", choices);
        }
        f.set("default", spec_value(s, defaults));
        fields.push(f);
    }
    json::Value schema = json::Value::object();
    schema.set("format", VEHICLE_FORMAT);
    schema.set("max_name_chars", MAX_NAME);
    schema.set("max_notes_chars", MAX_NOTES);
    schema.set("fields", fields);
    json::Value armor = json::Value::object();
    json::Value faces = json::Value::array();
    for (const char* f : FACES) faces.push(f);
    armor.set("faces", faces);
    json::Value sizes = json::Value::object();
    json::Value small = json::Value::array(); small.push(135); small.push(125);
    json::Value large = json::Value::array(); large.push(230); large.push(127);
    sizes.set("small", small); sizes.set("large", large);
    armor.set("sizes_mm", sizes);
    schema.set("armor", armor);
    schema.set("default", vehicle_to_json(defaults));
    return schema;
}
} // namespace field_sim
