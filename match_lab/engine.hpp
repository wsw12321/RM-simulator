#pragma once
// RMUC 2027 rules lab: an integrated, deterministic, headless 7-minute match that
// composes the source-linked rule modules. Geometry, robot physics, assembly
// timing and the scripted AI are SYNTHETIC engineering choices (see README.md).
#include "../rules/rmuc2027_buff_v1/buff_rules.hpp"
#include "../rules/rmuc2027_capture_v1/capture_rules.hpp"
#include "../rules/rmuc2027_damage_v1/damage_rules.hpp"
#include "../rules/rmuc2027_economy_v1/economy_rules.hpp"
#include "../rules/rmuc2027_growth_v1/growth_rules.hpp"
#include "../rules/rmuc2027_lifecycle_v1/lifecycle_rules.hpp"
#include "../rules/rmuc2027_limits_v1/limits_rules.hpp"
#include "../rules/rmuc2027_position_v1/position_rules.hpp"
#include "../rules/rmuc2027_v1/match_rules.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#ifndef MATCH_LAB_BUILD_ID
#define MATCH_LAB_BUILD_ID "development-unpinned"
#endif

namespace match_lab {
namespace buff = rmuc2027::buff_v1;
namespace cap = rmuc2027::capture_v1;
namespace dmg = rmuc2027::damage_v1;
namespace eco = rmuc2027::economy_v1;
namespace grow = rmuc2027::growth_v1;
namespace life = rmuc2027::lifecycle_v1;
namespace lim = rmuc2027::limits_v1;
namespace pos = rmuc2027::position_v1;
namespace judge = rmuc2027::match_v1;
using I = std::int64_t;

inline constexpr const char* PROFILE = "rmuc2027-rules-lab-v1";
inline constexpr int HZ = 60;
inline constexpr int DURATION_TICKS = 25200; // 420 s
inline constexpr int WIDTH = 12000, HEIGHT = 7600;
inline I time_ms(int tick) { return (I(tick) * 1000 + 59) / 60; }

struct Rect { int x1, y1, x2, y2; bool contains(int x, int y) const { return x >= x1 && x <= x2 && y >= y1 && y <= y2; } };
enum class Role : std::uint8_t { Heavy, Infantry1, Infantry2, Sentry };
enum Zone : int { RedOutpost = 0, BlueOutpost = 1, RedBase = 2, BlueBase = 3 };

struct TeamConfig {
    std::array<grow::Chassis, 2> chassis{grow::Chassis::HpFirst, grow::Chassis::HpFirst};
    std::array<grow::Launcher, 2> launcher{grow::Launcher::CoolingFirst, grow::Launcher::CoolingFirst};
    bool sentry_auto = true;
    I max_assembly = 3;    // highest difficulty the heavy attempts (0..3)
    I gold_reserve = 200;  // AI keeps this much gold before buying ammunition
};
struct Config {
    std::array<TeamConfig, 2> teams;
    int duration_ticks = DURATION_TICKS;
    int frame_every = 0;   // record robot poses every N ticks (0 = none)
    bool credits = true;   // EXPERIMENTAL end-of-second control credits
};
std::optional<std::string> validate(const Config&);

struct Robot {
    std::string id;
    int team = 0;
    Role role = Role::Infantry1;
    grow::RobotClass cls = grow::RobotClass::Infantry;
    grow::Loadout loadout;
    int x = 0, y = 0, radius = 280, vmax = 55, peak_w = 80;
    int spawn_x = 0, spawn_y = 0;
    int cooldown = 0;
    int stuck = 0;          // consecutive ticks a wanted move was fully blocked
    I level = 1, exp_tenths = 0;
    life::RobotLife life = std::get<life::RobotLife>(life::RobotLife::start(200));
    std::optional<lim::LauncherHeat> heat;
    lim::ChassisPower power;
    eco::RobotAllowance allowance = std::get<eco::RobotAllowance>(eco::RobotAllowance::start(0, 0));
    buff::SentryPosture posture;
    std::array<cap::ActivationDelay, 4> detectors{
        std::get<cap::ActivationDelay>(cap::ActivationDelay::start(0, cap::QualifiedDetection::Absent, cap::BoundaryPolicy::EngineeringTimerBeforeInputV1)),
        std::get<cap::ActivationDelay>(cap::ActivationDelay::start(0, cap::QualifiedDetection::Absent, cap::BoundaryPolicy::EngineeringTimerBeforeInputV1)),
        std::get<cap::ActivationDelay>(cap::ActivationDelay::start(0, cap::QualifiedDetection::Absent, cap::BoundaryPolicy::EngineeringTimerBeforeInputV1)),
        std::get<cap::ActivationDelay>(cap::ActivationDelay::start(0, cap::QualifiedDetection::Absent, cap::BoundaryPolicy::EngineeringTimerBeforeInputV1))};
    I sentry_claimed = 0;
    // statistics
    I shots = 0, hits = 0, damage_dealt = 0, kills = 0, purchases = 0;
};
struct Structure { int team = 0; bool outpost = true; int x = 0, y = 0, radius = 400; I hp = 0, max_hp = 0; bool alive = true; };
struct Projectile { int owner = 0; bool mm42 = false; bool counts = true; int x = 0, y = 0, vx = 0, vy = 0, life = 0; };
struct Team {
    eco::TeamWallet wallet;
    I highest_assembly = 0;
    I assembling = 0;           // difficulty currently being assembled (0 = none)
    I assembly_progress_ms = 0, assembly_invincible_ms = 0;
    std::array<I, 4> assembly_done_ms{-1, -1, -1, -1};
    bool launcher_unlocked = false, launcher_installed = false, confirmation_fired = false;
    I max_base_deficit = 0, max_control_deficit = 0; // M and N of §5.7.1
    I base_lowest = 10000;
    buff::Shield42 shield;
    I instant_revivals = 0, remote_heals = 0;
};
struct Event { I time_ms; std::string kind; std::string subject; std::string detail; };
struct Frame { int tick; std::vector<std::array<int, 4>> robots; std::vector<std::array<int, 4>> projectiles; };

class Engine {
public:
    explicit Engine(Config = {});
    void run();               // until base destruction or duration
    bool step();              // one 60 Hz tick; false once terminal
    bool terminal() const { return terminal_; }
    int tick() const { return tick_; }
    std::uint32_t hash() const { return hash_; }
    std::string json(bool include_frames) const;
    const std::vector<Robot>& robots() const { return robots_; }
    const std::array<Structure, 4>& structures() const { return structures_; }
    const std::array<Team, 2>& teams() const { return teams_; }
    const std::vector<Event>& events() const { return events_; }
    const std::array<pos::Position, 4>& positions() const { return positions_; }
    const judge::ExternalControlLedger& control_ledger() const { return ledger_; }
    const dmg::Ledger& damage_ledger() const { return damage_; }
    std::optional<judge::Decision> decision() const { return decision_; }
    const std::string& unresolved() const { return unresolved_; }
private:
    Config config_;
    int tick_ = 0;
    bool terminal_ = false;
    std::uint32_t hash_ = 2166136261u;
    std::vector<Robot> robots_;
    std::array<Structure, 4> structures_{};
    std::array<pos::Position, 4> positions_;
    std::array<Team, 2> teams_{};
    std::vector<Projectile> projectiles_;
    std::vector<I> tick_shots_;           // heat-counted detected launches in the current tick
    std::vector<Event> events_;
    std::vector<Frame> frames_;
    std::vector<std::string> timeline_;
    judge::ExternalControlLedger ledger_;
    judge::MatchJudge judge_;
    std::optional<judge::Decision> decision_;
    std::string unresolved_;
    dmg::Ledger damage_;
    std::array<std::vector<int>, 8> flow_; // distance fields: 4 zones, 2 supplies, 2 assembly zones

    void event(I t, std::string kind, std::string subject, std::string detail = {});
    void settle_time(I t);
    void think_and_move(I t);
    void fire(I t);
    void projectiles_and_damage(I t);
    void update_positions(I t);
    void update_progression(I t);
    void credit_and_judge(I t);
    void record(I t);
    life::Context context(const Robot&) const;
    void apply_performance(Robot&, I t);
    void on_death(Robot&, I t, int killer);
    std::vector<I> defenses(const Robot&, I t) const;
    std::vector<I> vulnerabilities(const Robot&, I t) const;
    I cooling_for(const Robot&, I t) const;
    I power_limit_for(const Robot&, I t) const;
    std::optional<std::pair<int, int>> flow_target(int field, int x, int y) const;
    void build_flow_fields();
    bool blocked(int x, int y, int radius, std::size_t self) const;
    I instant_level(const Robot&) const;
    void fold(I v);
};
std::string build_id();
} // namespace match_lab
