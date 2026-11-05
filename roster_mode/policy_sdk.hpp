#pragma once
// Standalone C++17 roster SDK v1. Standard-library dependencies only.
// Trusted same-process stateless policies, NOT a malicious-code sandbox.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace roster::policy {
using Integer = std::int64_t;
inline constexpr Integer kSdkVersion = 1;
inline constexpr Integer kHz = 60;
inline constexpr Integer kSensorRangeMm = 6000;
inline constexpr const char* kObservationProfile = "experimental-roster-range-los-public-v1";
// Preserve boolean/floating-point mistakes for the shared validator to reject.
using Scalar = std::variant<Integer, double, bool>;
struct Action {
    Scalar sdk_version = kSdkVersion;
    Scalar dx = Integer{0}, dy = Integer{0};
    Scalar aim_x = Integer{0}, aim_y = Integer{0}, fire = Integer{0};
};
inline std::optional<std::string> validate_action(const Action& a) {
    const auto* version = std::get_if<Integer>(&a.sdk_version);
    if (!version || *version != kSdkVersion)
        return "Unsupported action SDK version; expected integer 1.";
    const Scalar* values[] = {&a.dx, &a.dy, &a.aim_x, &a.aim_y, &a.fire};
    constexpr Integer lows[] = {-1000, -1000, 0, 0, 0};
    constexpr Integer highs[] = {1000, 1000, 12000, 7600, 1};
    for (int i = 0; i < 5; ++i) {
        const auto* value = std::get_if<Integer>(values[i]);
        if (!value || *value < lows[i] || *value > highs[i])
            return "Action requires integer dx/dy in [-1000,1000], aim within [0,12000]x[0,7600], fire 0 or 1; boolean and float are invalid.";
    }
    return std::nullopt;
}
struct Point { Integer x = 0, y = 0; };
struct Rect { Integer x1 = 0, y1 = 0, x2 = 0, y2 = 0; };
struct Profile {
    std::string id = "SYNTHETIC-standard";
    Integer max_hp=100, ammo=120, radius_mm=280, speed_mm_tick=55;
    Integer projectile_speed_mm_tick=260, projectile_damage=10, cooldown_ticks=10;
    Integer sensor_range_mm=6000, heat_limit_tenths=400, cooling_per_second=12;
};
struct Spawn { Integer x=0,y=0,yaw=0; };
struct RosterEntry {
    std::string id;
    Integer team=0;
    Profile profile;
    Spawn spawn;
    bool eligible=true;
};
struct Arena { Integer width=12000,height=7600; std::vector<Rect> walls; };
struct Self { Integer x=0,y=0,yaw=0,hp=0,ammo=0,cooldown=0; };
struct Contact { std::string id; Integer team=0,x=0,y=0,yaw=0; };
struct Projectile { Integer x=0,y=0,vx=0,vy=0; };
struct SelfHeat {
    bool enabled=false;
    Integer heat_tenths=0,limit_tenths=400,cooling_per_second=12;
    bool overheat_locked=false,permanent_latched=false,firing_locked=false;
};
struct Zone { std::string id; Rect rect; Integer original_owner=0,controller=0; std::string degree="full"; };
inline constexpr Integer kObservationVersion=1;
// PUBLIC static roster/configuration and objective broadcasts. Only own current
// private statistics/heat; other robots expose range/LOS pose only, including
// disabled poses without a death flag. Projectiles never expose ownership.
struct Observation {
    const Integer sdk_version=kSdkVersion,observation_version=kObservationVersion,hz=kHz,tick=0;
    const std::string self_id;
    const Integer self_team=0,sensor_range_mm=6000;
    const Self self;
    const Arena arena;
    const std::vector<RosterEntry> roster;
    const std::vector<Contact> contacts;
    const std::vector<Projectile> projectiles;
    const SelfHeat self_heat;
    const std::array<Zone,4> zones{};
    const std::array<Integer,2> score{},rate{};
    const Integer qualified_ms=0,duration_ms=420000;
    const bool credits_enabled=false;
};
struct Descriptor { Integer sdk_version = kSdkVersion; std::string controller_id; };
using DecisionFunction = Action (*)(const Observation&);

// Public direction table: identical 256 integer headings to sandbox-v1, scale 1000.
// Included here so an SDK consumer needs this one header and standard C++ only.
inline constexpr std::array<Point, 256> kDirections{{
// Versioned integer unit-circle table, scale 1000, 256 headings.
{1000,0},
{1000,25},
{999,49},
{997,74},
{995,98},
{992,122},
{989,147},
{985,171},
{981,195},
{976,219},
{970,243},
{964,267},
{957,290},
{950,314},
{942,337},
{933,360},
{924,383},
{914,405},
{904,428},
{893,450},
{882,471},
{870,493},
{858,514},
{845,535},
{831,556},
{818,576},
{803,596},
{788,615},
{773,634},
{757,653},
{741,672},
{724,690},
{707,707},
{690,724},
{672,741},
{653,757},
{634,773},
{615,788},
{596,803},
{576,818},
{556,831},
{535,845},
{514,858},
{493,870},
{471,882},
{450,893},
{428,904},
{405,914},
{383,924},
{360,933},
{337,942},
{314,950},
{290,957},
{267,964},
{243,970},
{219,976},
{195,981},
{171,985},
{147,989},
{122,992},
{98,995},
{74,997},
{49,999},
{25,1000},
{0,1000},
{-25,1000},
{-49,999},
{-74,997},
{-98,995},
{-122,992},
{-147,989},
{-171,985},
{-195,981},
{-219,976},
{-243,970},
{-267,964},
{-290,957},
{-314,950},
{-337,942},
{-360,933},
{-383,924},
{-405,914},
{-428,904},
{-450,893},
{-471,882},
{-493,870},
{-514,858},
{-535,845},
{-556,831},
{-576,818},
{-596,803},
{-615,788},
{-634,773},
{-653,757},
{-672,741},
{-690,724},
{-707,707},
{-724,690},
{-741,672},
{-757,653},
{-773,634},
{-788,615},
{-803,596},
{-818,576},
{-831,556},
{-845,535},
{-858,514},
{-870,493},
{-882,471},
{-893,450},
{-904,428},
{-914,405},
{-924,383},
{-933,360},
{-942,337},
{-950,314},
{-957,290},
{-964,267},
{-970,243},
{-976,219},
{-981,195},
{-985,171},
{-989,147},
{-992,122},
{-995,98},
{-997,74},
{-999,49},
{-1000,25},
{-1000,0},
{-1000,-25},
{-999,-49},
{-997,-74},
{-995,-98},
{-992,-122},
{-989,-147},
{-985,-171},
{-981,-195},
{-976,-219},
{-970,-243},
{-964,-267},
{-957,-290},
{-950,-314},
{-942,-337},
{-933,-360},
{-924,-383},
{-914,-405},
{-904,-428},
{-893,-450},
{-882,-471},
{-870,-493},
{-858,-514},
{-845,-535},
{-831,-556},
{-818,-576},
{-803,-596},
{-788,-615},
{-773,-634},
{-757,-653},
{-741,-672},
{-724,-690},
{-707,-707},
{-690,-724},
{-672,-741},
{-653,-757},
{-634,-773},
{-615,-788},
{-596,-803},
{-576,-818},
{-556,-831},
{-535,-845},
{-514,-858},
{-493,-870},
{-471,-882},
{-450,-893},
{-428,-904},
{-405,-914},
{-383,-924},
{-360,-933},
{-337,-942},
{-314,-950},
{-290,-957},
{-267,-964},
{-243,-970},
{-219,-976},
{-195,-981},
{-171,-985},
{-147,-989},
{-122,-992},
{-98,-995},
{-74,-997},
{-49,-999},
{-25,-1000},
{0,-1000},
{25,-1000},
{49,-999},
{74,-997},
{98,-995},
{122,-992},
{147,-989},
{171,-985},
{195,-981},
{219,-976},
{243,-970},
{267,-964},
{290,-957},
{314,-950},
{337,-942},
{360,-933},
{383,-924},
{405,-914},
{428,-904},
{450,-893},
{471,-882},
{493,-870},
{514,-858},
{535,-845},
{556,-831},
{576,-818},
{596,-803},
{615,-788},
{634,-773},
{653,-757},
{672,-741},
{690,-724},
{707,-707},
{724,-690},
{741,-672},
{757,-653},
{773,-634},
{788,-615},
{803,-596},
{818,-576},
{831,-556},
{845,-535},
{858,-514},
{870,-493},
{882,-471},
{893,-450},
{904,-428},
{914,-405},
{924,-383},
{933,-360},
{942,-337},
{950,-314},
{957,-290},
{964,-267},
{970,-243},
{976,-219},
{981,-195},
{985,-171},
{989,-147},
{992,-122},
{995,-98},
{997,-74},
{999,-49},
{1000,-25},
}};
// Precondition: both points are arena coordinates, x in [0,12000], y in [0,7600].
// Simulator-supplied observations and accepted aim actions satisfy these bounds.
inline int aim_direction(Point from, Point target) {
    int best = 0;
    Integer score = std::numeric_limits<Integer>::min();
    for (int i = 0; i < 256; ++i) {
        const auto d = kDirections[i];
        const auto s = (target.x-from.x)*d.x + (target.y-from.y)*d.y;
        const auto cross = std::llabs((target.x-from.x)*d.y-(target.y-from.y)*d.x);
        const auto prior = kDirections[best];
        const auto old_cross = std::llabs((target.x-from.x)*prior.y-(target.y-from.y)*prior.x);
        if (s > score || (s == score && cross < old_cross)) { score = s; best = i; }
    }
    return best;
}
// Edit these entrypoints and rebuild locally. Every robot calls decide independently
// with their own snapshot. Only stateless, snapshot-dependent policies are supported.
Descriptor descriptor();
Action decide(const Observation& observation);
} // namespace roster::policy
