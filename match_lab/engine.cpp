#include "engine.hpp"
#include "../src/sim.hpp"
#include <algorithm>
#include <deque>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace match_lab {
namespace {
// ---------------------------------------------------------------- geometry
// SYNTHETIC point-symmetric demonstration field, not official RMUC geometry.
// Position zones equal objective_mode's placeholder rectangles.
constexpr std::array<Rect, 4> ZONES{{{4300, 700, 5900, 2300}, {6100, 5300, 7700, 6900},
                                     {600, 5600, 2200, 7200}, {9800, 400, 11400, 2000}}};
constexpr std::array<Rect, 2> SUPPLY{{{200, 3200, 1200, 4400}, {10800, 3200, 11800, 4400}}};
constexpr std::array<Rect, 2> ASSEMBLY{{{2400, 5800, 3400, 6800}, {8600, 800, 9600, 1800}}};
constexpr const char* ZONE_IDS[4] = {"red-outpost", "blue-outpost", "red-base", "blue-base"};
// SYNTHETIC energy mechanism: each team's mechanism hangs on its side of the
// central block and is engaged from a firing point in front of it.
constexpr std::array<Rect, 2> ENERGY_ZONE{{{3300, 3300, 4300, 4300}, {7700, 3300, 8700, 4300}}};
constexpr int ENERGY_POS[2][2] = {{5250, 3800}, {6750, 3800}};
constexpr I DART_FLIGHT_MS = 1500, DART_LAUNCH_GAP_MS = 8000, DART_FIRST_LAUNCH_MS = 1000; // SYNTHETIC
constexpr I RADAR_PERIOD_MS = 200;                 // 5 Hz, the manual's worked example rate
constexpr int CELL = 100, GW = WIDTH / CELL, GH = HEIGHT / CELL, GRID_RADIUS = 330;
constexpr int SENSOR_MM = 6000, STRUCTURE_RANGE_MM = 7000;
constexpr int MM17_SPEED = 400, MM42_SPEED = 267; // mm/tick: 24 m/s and 16 m/s, below the limits
constexpr int MM17_COOLDOWN = 6, MM42_COOLDOWN = 30, PROJECTILE_LIFE = 60;
constexpr I ASSEMBLY_MS = 15000;                   // SYNTHETIC operator time per difficulty
constexpr std::array<I, 4> ASSEMBLY_UNLOCK_MS{0, 0, 60000, 120000}; // table 5-11 (index = difficulty)
constexpr std::array<I, 4> ASSEMBLY_STREAM{0, 40, 20, 20};          // table 5-12 first completion

int mirror_x(int x) { return WIDTH - x; }
int mirror_y(int y) { return HEIGHT - y; }
std::uint32_t mix(std::uint32_t a) { a ^= a >> 16; a *= 0x7feb352dU; a ^= a >> 15; a *= 0x846ca68bU; a ^= a >> 16; return a; }
const char* role_name(Role r) { return r == Role::Heavy ? "heavy" : r == Role::Sentry ? "sentry" : "infantry"; }
const char* team_name(int t) { return t ? "blue" : "red"; }
pos::Team pteam(int t) { return t ? pos::Team::Blue : pos::Team::Red; }
int iteam(pos::Team t) { return t == pos::Team::Blue ? 1 : 0; }
bool wall_hit(int x, int y, int r) {
    for (auto w : sandbox::WALLS) {
        const I dx = x - std::clamp(x, w.x1, w.x2), dy = y - std::clamp(y, w.y1, w.y2);
        if (dx * dx + dy * dy < I(r) * r) return true;
    }
    return x < r || y < r || x > WIDTH - r || y > HEIGHT - r;
}
// Q16 segment entry parameters, as in roster_mode (bounded inputs).
int rect_entry(int ax, int ay, int bx, int by, const sandbox::Rect& w) {
    I lo = 0, hi = 65536;
    for (int axis = 0; axis < 2; ++axis) {
        const int s = axis ? ay : ax, d = axis ? by - ay : bx - ax, mn = axis ? w.y1 : w.x1, mx = axis ? w.y2 : w.x2;
        if (!d) { if (s < mn || s > mx) return -1; continue; }
        I q1 = I(mn - s) * 65536 / d, q2 = I(mx - s) * 65536 / d;
        if (q1 > q2) std::swap(q1, q2);
        lo = std::max(lo, q1); hi = std::min(hi, q2);
        if (lo > hi) return -1;
    }
    return int(lo);
}
int circle_entry(int ax, int ay, int bx, int by, int cx, int cy, int r) {
    const I dx = bx - ax, dy = by - ay, len = dx * dx + dy * dy;
    auto inside = [&](I t) { const I x = I(ax - cx) + dx * t / 65536, y = I(ay - cy) + dy * t / 65536; return x * x + y * y <= I(r) * r; };
    if (inside(0)) return 0;
    if (!len) return -1;
    const I dot = I(cx - ax) * dx + I(cy - ay) * dy;
    const I nearest = std::clamp<I>(dot * 65536 / len, 0, 65536);
    if (!inside(nearest)) return -1;
    I lo = 0, hi = nearest;
    while (lo + 1 < hi) { const I mid = (lo + hi) / 2; if (inside(mid)) hi = mid; else lo = mid; }
    return int(hi);
}
I dist2(int ax, int ay, int bx, int by) { const I dx = ax - bx, dy = ay - by; return dx * dx + dy * dy; }
} // namespace

std::string build_id() { return MATCH_LAB_BUILD_ID; }

std::optional<std::string> validate(const Config& c) {
    if (c.duration_ticks < 1 || c.duration_ticks > DURATION_TICKS) return "duration_ticks must be 1..25200";
    if (c.frame_every < 0 || c.frame_every > 600) return "frame_every must be 0..600";
    if (c.seed < 0 || c.seed > 999'999'999) return "seed must be 0..999999999";
    for (const auto& t : c.teams) {
        if (t.max_assembly < 0 || t.max_assembly > 3) return "max_assembly must be 0..3 (difficulty 4 unsupported)";
        if (t.gold_reserve < 0 || t.gold_reserve > 5000) return "gold_reserve must be 0..5000";
        if (t.energy_accuracy < 0 || t.energy_accuracy > 100) return "energy_accuracy must be 0..100";
        if (t.dart_accuracy < 0 || t.dart_accuracy > 100) return "dart_accuracy must be 0..100";
        if (t.radar_accuracy < 0 || t.radar_accuracy > 100) return "radar_accuracy must be 0..100";
        if (t.dart_target && std::uint8_t(*t.dart_target) > std::uint8_t(dart::Target::BaseTerminalMoving)) return "invalid dart target";
        for (auto ch : t.chassis) if (ch != grow::Chassis::PowerFirst && ch != grow::Chassis::HpFirst) return "invalid chassis";
        for (auto l : t.launcher) if (l != grow::Launcher::BurstFirst && l != grow::Launcher::CoolingFirst) return "invalid launcher";
    }
    return {};
}

void Engine::fold(I v) {
    auto u = std::uint64_t(v);
    for (int i = 0; i < 8; ++i) { hash_ ^= (u >> (i * 8)) & 255u; hash_ *= 16777619u; }
}

void Engine::event(I t, std::string kind, std::string subject, std::string detail) {
    fold(t);
    for (char c : kind) fold(c);
    for (char c : subject) fold(c);
    events_.push_back({t, std::move(kind), std::move(subject), std::move(detail)});
}

Engine::Engine(Config c)
    : config_(std::move(c)),
      positions_{std::get<pos::Position>(pos::Position::start(pos::Kind::Outpost, pos::Team::Red)),
                 std::get<pos::Position>(pos::Position::start(pos::Kind::Outpost, pos::Team::Blue)),
                 std::get<pos::Position>(pos::Position::start(pos::Kind::Base, pos::Team::Red)),
                 std::get<pos::Position>(pos::Position::start(pos::Kind::Base, pos::Team::Blue))} {
    if (auto e = validate(config_)) throw std::invalid_argument(*e);
    structures_[0] = {0, true, 5100, 1500, 350, 1000, 1000, true};
    structures_[1] = {1, true, mirror_x(5100), mirror_y(1500), 350, 1000, 1000, true};
    structures_[2] = {0, false, 1400, 6400, 500, 10000, 10000, true};
    structures_[3] = {1, false, mirror_x(1400), mirror_y(6400), 500, 10000, 10000, true};
    struct Seat { Role role; int x, y; };
    const Seat seats[4] = {{Role::Heavy, 1700, 3800}, {Role::Infantry1, 1700, 2900}, {Role::Infantry2, 1700, 4700}, {Role::Sentry, 4100, 2600}};
    for (int team = 0; team < 2; ++team) {
        const auto& tc = config_.teams[std::size_t(team)];
        for (const auto& s : seats) {
            Robot r;
            r.team = team;
            r.role = s.role;
            r.id = std::string(team_name(team)) + "-" + (s.role == Role::Heavy ? "heavy" : s.role == Role::Sentry ? "sentry" : s.role == Role::Infantry1 ? "infantry-1" : "infantry-2");
            r.x = r.spawn_x = team ? mirror_x(s.x) : s.x;
            r.y = r.spawn_y = team ? mirror_y(s.y) : s.y;
            if (s.role == Role::Heavy) { r.cls = grow::RobotClass::Heavy; r.radius = 320; r.vmax = 45; r.peak_w = 80; }
            else if (s.role == Role::Sentry) { r.cls = tc.sentry_auto ? grow::RobotClass::SentryAuto : grow::RobotClass::SentrySemi; r.vmax = 50; r.peak_w = 120; }
            else {
                const auto k = std::size_t(s.role == Role::Infantry1 ? 0 : 1);
                r.cls = grow::RobotClass::Infantry;
                r.loadout.chassis = tc.chassis[k];
                r.loadout.launcher = tc.launcher[k];
            }
            r.loadout.robot_class = r.cls;
            const auto perf = std::get<grow::Performance>(grow::performance(r.loadout, 1));
            r.life = std::get<life::RobotLife>(life::RobotLife::start(perf.max_hp));
            if (perf.heat_limit)
                r.heat = std::get<lim::LauncherHeat>(lim::LauncherHeat::start({lim::Caliber::Mm17, *perf.heat_limit, *perf.cooling_per_second,
                                                                               lim::ThresholdPolicy::EngineeringProseStrictGreaterV1}));
            r.allowance = std::get<eco::RobotAllowance>(eco::RobotAllowance::start(std::get<I>(grow::initial_allowance(r.cls)), 0));
            robots_.push_back(std::move(r));
        }
    }
    build_flow_fields();
    died_.assign(robots_.size(), false);
    for (int k = 0; k < 2; ++k)
        teams_[std::size_t(k)].energy = std::get<energy::EnergyMechanism>(
            energy::EnergyMechanism::start(std::uint64_t(config_.seed) * 2 + std::uint64_t(k)));
    for (auto& t : teams_) (void)t.wallet.settle(0);
    event(0, "match-start", PROFILE, "official=false");
}

// --------------------------------------------------------------- navigation
namespace {
Rect field_rect(int f) {
    return f < 4 ? ZONES[std::size_t(f)] : f < 6 ? SUPPLY[std::size_t(f - 4)] : f < 8 ? ASSEMBLY[std::size_t(f - 6)] : ENERGY_ZONE[std::size_t(f - 8)];
}
// Distance (10 per straight cell, 14 per diagonal) from every free cell to the target rectangle.
std::vector<int> distance_field(const Rect& target, const std::vector<char>& free) {
    std::vector<int> d(std::size_t(GW * GH), std::numeric_limits<int>::max());
    std::deque<int> q;
    for (int i = 0; i < GW * GH; ++i) {
        const int x = (i % GW) * CELL + CELL / 2, y = (i / GW) * CELL + CELL / 2;
        if (free[std::size_t(i)] && target.contains(x, y)) { d[std::size_t(i)] = 0; q.push_back(i); }
    }
    while (!q.empty()) {
        const int i = q.front(); q.pop_front();
        const int gx = i % GW, gy = i / GW;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = gx + dx, ny = gy + dy;
                if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= GW || ny >= GH) continue;
                const int j = ny * GW + nx;
                // Diagonal moves need both orthogonal neighbours free (no corner cutting).
                if (dx && dy && (!free[std::size_t(gy * GW + nx)] || !free[std::size_t(ny * GW + gx)])) continue;
                const int cost = d[std::size_t(i)] + (dx && dy ? 14 : 10);
                if (free[std::size_t(j)] && cost < d[std::size_t(j)]) { d[std::size_t(j)] = cost; q.push_back(j); }
            }
    }
    return d;
}
} // namespace

void Engine::build_flow_fields() {
    free_.assign(std::size_t(GW * GH), 0);
    for (int gy = 0; gy < GH; ++gy)
        for (int gx = 0; gx < GW; ++gx) {
            const int x = gx * CELL + CELL / 2, y = gy * CELL + CELL / 2;
            bool ok = !wall_hit(x, y, GRID_RADIUS);
            for (const auto& s : structures_) ok = ok && dist2(x, y, s.x, s.y) >= I(s.radius + GRID_RADIUS) * (s.radius + GRID_RADIUS);
            free_[std::size_t(gy * GW + gx)] = ok;
        }
    for (int f = 0; f < 10; ++f) flow_[std::size_t(f)] = distance_field(field_rect(f), free_);
}

// A robot that makes no progress plans a personal route that treats every other
// robot (alive or not) as an obstacle at its current place. False = no such route.
bool Engine::plan_detour(std::size_t self, int field) {
    auto& r = robots_[self];
    std::vector<char> free = free_;
    for (std::size_t j = 0; j < robots_.size(); ++j) {
        if (j == self) continue;
        const auto& o = robots_[j];
        const int reach = o.radius + GRID_RADIUS;
        for (int gy = std::max(0, (o.y - reach) / CELL); gy <= std::min(GH - 1, (o.y + reach) / CELL); ++gy)
            for (int gx = std::max(0, (o.x - reach) / CELL); gx <= std::min(GW - 1, (o.x + reach) / CELL); ++gx)
                if (dist2(gx * CELL + CELL / 2, gy * CELL + CELL / 2, o.x, o.y) < I(reach) * reach) free[std::size_t(gy * GW + gx)] = 0;
    }
    auto d = distance_field(field_rect(field), free);
    // Reachable from here, or from a cell the two-ring escape of flow_target can step to.
    const int cx = std::clamp(r.x / CELL, 0, GW - 1), cy = std::clamp(r.y / CELL, 0, GH - 1);
    bool reachable = false;
    for (int dy = -2; dy <= 2 && !reachable; ++dy)
        for (int dx = -2; dx <= 2 && !reachable; ++dx) {
            const int nx = cx + dx, ny = cy + dy;
            reachable = nx >= 0 && ny >= 0 && nx < GW && ny < GH && d[std::size_t(ny * GW + nx)] != std::numeric_limits<int>::max();
        }
    if (!reachable) return false;
    r.detour = std::move(d);
    r.detour_field = field;
    r.detour_ticks = 90;
    return true;
}

// Next waypoint: centre of the neighbouring free cell with the lowest distance.
// Steering to cell centres keeps robots on the clearance lattice; a robot pushed
// onto a non-free cell searches two rings outward to escape. nullopt = at goal.
// Equal candidates are taken in scan order; `mirrored` reverses that order so
// that the two teams break ties as point reflections of each other (the field
// is point-symmetric, a fixed order would favour one side).
std::optional<std::pair<int, int>> Engine::flow_target(const std::vector<int>& d, int x, int y, bool mirrored) const {
    const int gx = std::clamp(x / CELL, 0, GW - 1), gy = std::clamp(y / CELL, 0, GH - 1);
    const int here = d[std::size_t(gy * GW + gx)];
    if (here == 0) return std::nullopt;
    const int rings = here == std::numeric_limits<int>::max() ? 2 : 1;
    int best = here, bx = -1, by = -1;
    const int sign = mirrored ? -1 : 1;
    for (int sy = -rings; sy <= rings; ++sy)
        for (int sx = -rings; sx <= rings; ++sx) {
            const int dx = sx * sign, dy = sy * sign;
            const int nx = gx + dx, ny = gy + dy;
            if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= GW || ny >= GH) continue;
            const int v = d[std::size_t(ny * GW + nx)];
            if (v < best) { best = v; bx = nx; by = ny; }
        }
    if (bx < 0) return std::nullopt;
    return std::pair{bx * CELL + CELL / 2, by * CELL + CELL / 2};
}

bool Engine::blocked(int x, int y, int radius, std::size_t self) const {
    if (wall_hit(x, y, radius)) return true;
    for (const auto& s : structures_) if (dist2(x, y, s.x, s.y) < I(s.radius + radius) * (s.radius + radius)) return true;
    for (std::size_t j = 0; j < robots_.size(); ++j) {
        if (j == self) continue;
        const auto& o = robots_[j];
        const I rr = o.radius + radius;
        if (dist2(x, y, o.x, o.y) < rr * rr) return true;
    }
    return false;
}

// ------------------------------------------------------------ rule context
life::Context Engine::context(const Robot& r) const {
    life::Context c;
    c.in_own_supply = SUPPLY[std::size_t(r.team)].contains(r.x, r.y);
    // A dart hit disables the struck structure's buff point for 30 s (§5.7.3).
    for (int z = 0; z < 2; ++z)
        if (ZONES[std::size_t(z)].contains(r.x, r.y) && positions_[std::size_t(z)].full_by(pteam(r.team)) && now_ >= point_disabled_until_[std::size_t(z)])
            c.in_full_own_outpost_buff = true;
    const bool base_point = ZONES[std::size_t(2 + r.team)].contains(r.x, r.y) && now_ >= point_disabled_until_[std::size_t(2 + r.team)];
    c.weak_clearing_card = c.in_own_supply || c.in_full_own_outpost_buff || base_point;
    c.base_hp = structures_[std::size_t(2 + r.team)].hp;
    return c;
}

I Engine::instant_level(const Robot& r) const {
    if (r.cls == grow::RobotClass::Heavy) return std::get<I>(grow::heavy_level(teams_[std::size_t(r.team)].highest_assembly));
    return grow::kill_level(r.cls, r.level);
}

std::vector<I> Engine::defenses(const Robot& r, I t) const {
    std::vector<I> d;
    const auto& team = teams_[std::size_t(r.team)];
    const auto& s = r.life.snapshot();
    if (!s.weak && ZONES[std::size_t(2 + r.team)].contains(r.x, r.y) && t >= point_disabled_until_[std::size_t(2 + r.team)])
        d.push_back(50); // own base buff point
    d.push_back(energy_effect(r.team, t).defense_percent);
    if (r.cls == grow::RobotClass::Heavy) d.push_back(buff::heavy_opening_defense(t, team.launcher_installed && team.confirmation_fired));
    if (team.highest_assembly >= 3) d.push_back(std::get<grow::AssemblyReward>(grow::first_assembly_reward(team.highest_assembly)).team_defense_percent);
    if (r.role == Role::Sentry) d.push_back(r.posture.effect().defense);
    return d;
}
std::vector<I> Engine::vulnerabilities(const Robot& r, I t) const {
    std::vector<I> v;
    if (r.role == Role::Sentry) v.push_back(r.posture.effect().vulnerability);
    v.push_back(radar_vulnerability(r, t));
    return v;
}
// Table 5-30 plus the 30 s "double vulnerability" of the opposing radar.
I Engine::radar_vulnerability(const Robot& r, I t) const {
    const auto& marker = teams_[std::size_t(1 - r.team)];
    return radar::vulnerability_percent(r.mark.snapshot().progress_tenths, marker.radar_double.snapshot().active(t), true);
}
// §5.6.3 / tables 5-22, 5-23. A dart can suspend the held buff: its clock
// keeps running but it has no effect (§5.7.3).
EnergyEffect Engine::energy_effect(int team, I t) const {
    const auto& tm = teams_[std::size_t(team)];
    const auto& s = tm.energy.snapshot();
    if (!s.buff_active() || t < tm.energy_suspended_until_ms) return {};
    if (s.phase == energy::Phase::Small) return {100, buff::SMALL_ENERGY_BUFF.defense_percent, 10};
    const auto b = std::get<buff::EnergyBuff>(buff::big_energy_buff(s.ring_sum, s.hits, s.hits));
    return {b.attack_percent, b.defense_percent, b.cooling_multiplier_tenths};
}
std::uint32_t Engine::roll(std::uint32_t stream, std::uint32_t a, std::uint32_t b) const {
    return mix(mix(mix(std::uint32_t(config_.seed) * 0x9E3779B1u + stream) + a * 0x85EBCA6Bu) + b * 0xC2B2AE35u);
}
int Engine::opposing_outpost(int team) const {
    for (const int z : {1 - team, team})
        if (structures_[std::size_t(z)].alive && structures_[std::size_t(z)].team != team) return z;
    return -1;
}
I Engine::cooling_for(const Robot& r, I t) const {
    if (!r.heat) return 0;
    const auto perf = std::get<grow::Performance>(grow::performance(r.loadout, r.level));
    std::vector<I> mult;
    mult.push_back(energy_effect(r.team, t).cooling_tenths);
    I num = 1, den = 1;
    if (r.role == Role::Sentry) {
        const auto e = r.posture.effect();
        mult.push_back(e.cooling_buff_tenths);
        num = e.cooling_scale_num; den = e.cooling_scale_den;
    }
    return std::get<I>(buff::effective_cooling({*perf.cooling_per_second, mult, {}, num, den}));
}
I Engine::power_limit_for(const Robot& r, I t) const {
    const auto perf = std::get<grow::Performance>(grow::performance(r.loadout, r.level));
    lim::PowerLimitInput in;
    in.base_w = perf.chassis_power_w;
    std::vector<I> buffs;
    if (r.role == Role::Sentry) {
        const auto e = r.posture.effect();
        buffs.push_back(e.power_buff_tenths);
        in.scale_num = e.power_scale_num; in.scale_den = e.power_scale_den; in.override_w = e.power_override_w;
    }
    in.buff_tenths = buffs;
    in.instant_revival_boost = r.life.snapshot().power_boosted(t);
    return std::get<I>(lim::effective_power_limit_w(in));
}

void Engine::apply_performance(Robot& r, I t) {
    const auto perf = std::get<grow::Performance>(grow::performance(r.loadout, r.level));
    if (perf.max_hp != r.life.snapshot().max_hp) (void)r.life.set_max_hp(t, perf.max_hp);
    if (perf.heat_limit) {
        if (!r.heat) {
            r.heat = std::get<lim::LauncherHeat>(lim::LauncherHeat::start({perf.caliber == grow::Caliber::Mm42 ? lim::Caliber::Mm42 : lim::Caliber::Mm17,
                                                                           *perf.heat_limit, cooling_for(r, t), lim::ThresholdPolicy::EngineeringProseStrictGreaterV1}));
        }
        const bool zero = r.role == Role::Sentry && r.posture.effect().heat_always_zero;
        const I cooling = cooling_for(r, t);
        const auto& h = r.heat->snapshot();
        if (h.limit != *perf.heat_limit || h.cooling_per_second != cooling || h.heat_always_zero != zero)
            (void)r.heat->set_profile(std::max(t, h.time_ms), *perf.heat_limit, cooling, zero);
    }
}

// --------------------------------------------------------------- the tick
void Engine::settle_time(I t) {
    for (int k = 0; k < 2; ++k) {
        auto& team = teams_[std::size_t(k)];
        const I before = team.wallet.snapshot().income_settled_ms;
        (void)team.wallet.settle(t);
        for (I d = 1; d <= 3; ++d)
            if (team.assembly_done_ms[std::size_t(d)] >= 0) {
                const I gold = std::get<I>(eco::periodic_gold_between(team.assembly_done_ms[std::size_t(d)], 10000, ASSEMBLY_STREAM[std::size_t(d)], before, t));
                if (gold) (void)team.wallet.add_gold(gold);
            }
    }
    // Energy mechanism timers and the dart gate timeline (§5.6.3, §5.7.3).
    for (int k = 0; k < 2; ++k) {
        auto& team = teams_[std::size_t(k)];
        energy_events(k, std::get<energy::Step>(team.energy.advance(t)).events);
        const auto gate = std::get<dart::Step>(team.dart.advance(t));
        for (const auto& e : gate.events) {
            if (e.kind == dart::EventKind::GateOpen) {
                team.dart_next_launch_ms = t + DART_FIRST_LAUNCH_MS;
                team.dart_opening_launches = 0;
                event(t, "dart-gate", team_name(k), "open: 30 s launch window, 40 s detection window");
            } else if (e.kind == dart::EventKind::GateClosed) event(t, "dart-gate", team_name(k), "closed, 15 s cool-down");
        }
    }
    for (auto& r : robots_) {
        const auto ctx = context(r);
        const auto a = std::get<life::Advance>(r.life.advance(t, ctx));
        if (a.revived) {
            event(t, "revived", r.id, std::to_string(a.state.hp) + " hp, readout");
            if (r.role == Role::Heavy) teams_[std::size_t(r.team)].shield.update(t, true, r.allowance.available(eco::Ammo::Mm42));
            (void)r.mark.revived(t); // p113: P is kept, x restarts from 0
        }
        (void)r.allowance.settle(t);
        if (r.role == Role::Sentry) (void)r.posture.advance(t);
    }
    // Radar marking (§5.7.4): the opposing radar reports every ground robot at
    // 5 Hz; the SYNTHETIC accuracy decides the class of each coordinate.
    for (std::size_t i = 0; i < robots_.size(); ++i) {
        auto& r = robots_[i];
        (void)r.mark.advance(t);
        const I accuracy = config_.teams[std::size_t(1 - r.team)].radar_accuracy;
        if (accuracy > 0 && t % RADAR_PERIOD_MS == 0) {
            const I u = roll(4, std::uint32_t(tick_), std::uint32_t(i)) % 100;
            (void)r.mark.report(t, u < accuracy ? radar::Mark::Accurate : u < accuracy + (100 - accuracy) / 2 ? radar::Mark::SemiAccurate : radar::Mark::Wrong);
        }
        const I vulnerable = radar_vulnerability(r, t);
        if (vulnerable > 0 && !r.radar_marked) event(t, "radar-marked", r.id, std::to_string(vulnerable) + "% vulnerability");
        r.radar_marked = vulnerable > 0;
    }
    for (int k = 0; k < 2; ++k) {
        auto& team = teams_[std::size_t(k)];
        (void)team.radar_double.advance(t, team.enemy_marked);
        team.enemy_marked = false;
        for (const auto& o : robots_) team.enemy_marked = team.enemy_marked || (o.team != k && o.radar_marked);
    }
}

void Engine::gain_exp(Robot& r, I tenths, I t) {
    if (!grow::uses_experience(r.cls) || tenths <= 0) return;
    auto& team = teams_[std::size_t(r.team)];
    // Small energy buff: 100% extra, at most 1200 per buff for the team (§5.6.3).
    const I bonus = t < team.energy_suspended_until_ms ? 0 : std::get<I>(team.energy.small_bonus_tenths(t, tenths));
    team.energy_bonus_tenths += bonus;
    r.exp_tenths += tenths + bonus;
}

// An award "split equally among the living infantry" (§5.6.3, §5.7.3), rounded to 0.1.
void Engine::share_exp(int k, I total_tenths, I t) {
    I living = 0;
    for (const auto& r : robots_) living += r.team == k && r.cls == grow::RobotClass::Infantry && r.life.snapshot().alive();
    if (!living) return;
    const I each = std::get<I>(grow::split_tenths(total_tenths, living));
    for (auto& r : robots_)
        if (r.team == k && r.cls == grow::RobotClass::Infantry && r.life.snapshot().alive()) gain_exp(r, each, t);
}

void Engine::energy_events(int k, const std::vector<energy::Event>& events) {
    auto& team = teams_[std::size_t(k)];
    for (const auto& e : events) {
        const std::string phase = e.phase == energy::Phase::Big ? "big" : "small";
        switch (e.kind) {
        case energy::EventKind::ChanceGranted: event(now_, "energy-chance", team_name(k), phase + " x" + std::to_string(e.value)); break;
        case energy::EventKind::Triggered: event(now_, "energy-trigger", team_name(k), phase); break;
        case energy::EventKind::WindowExpired: event(now_, "energy-failed", team_name(k), phase + ": not activated within 20 s"); break;
        case energy::EventKind::SmallAbortedByBigPhase: event(now_, "energy-failed", team_name(k), "small: the small phase ended"); break;
        case energy::EventKind::BuffEnded: event(now_, "energy-ended", team_name(k), phase); break;
        case energy::EventKind::BuffVoided: event(now_, "energy-voided", team_name(k), phase + ": removed by a dart"); break;
        case energy::EventKind::Activated: {
            team.energy_suspended_until_ms = -1; // a suspension only covers the buff held when the dart hit
            std::string detail = "small: 25% defence for 45 s, double experience (max 1200)";
            if (e.phase == energy::Phase::Big) {
                const auto b = std::get<buff::EnergyBuff>(buff::big_energy_buff(e.ring_sum, e.value, e.value));
                const I ring_tenths = e.ring_sum * 10 / e.value;
                detail = "big: " + std::to_string(e.value) + " arms, average ring " + std::to_string(ring_tenths / 10) + "." + std::to_string(ring_tenths % 10) +
                         " -> attack " + std::to_string(b.attack_percent) + "%, defence " + std::to_string(b.defense_percent) + "%, cooling x" +
                         std::to_string(b.cooling_multiplier_tenths / 10) + " for " + std::to_string(b.duration_ms / 1000) + " s";
            }
            event(now_, "energy-activated", team_name(k), detail);
            if (e.phase == energy::Phase::Big) share_exp(k, energy::BIG_ACTIVATION_EXPERIENCE_TENTHS, now_);
            break;
        }
        default: break; // lit groups, single hits and restarts are not reported
        }
    }
}

// SYNTHETIC aim model for one 17 mm round fired at the own energy mechanism.
void Engine::energy_shot(std::size_t i, I t) {
    auto& r = robots_[i];
    auto& team = teams_[std::size_t(r.team)];
    ++r.energy_shots;
    const auto& s = team.energy.snapshot();
    const I accuracy = config_.teams[std::size_t(r.team)].energy_accuracy;
    int lit = -1, dark = -1;
    for (int arm = 0; arm < energy::ARMS; ++arm) {
        if ((s.lit_mask >> arm) & 1) { if (lit < 0) lit = arm; }
        else if (arm != s.first_hit_arm && dark < 0) dark = arm;
    }
    // `accuracy` % land on the aimed lit module, a tenth of the rest on a wrong one.
    const I u = roll(2, std::uint32_t(tick_), std::uint32_t(i)) % 100;
    const int arm = u < accuracy ? lit : u < accuracy + (100 - accuracy) / 10 ? dark : -1;
    // Radial error: uniform within 15 mm at 100%, 3 mm wider per missing percent.
    const I radius = roll(3, std::uint32_t(tick_), std::uint32_t(i)) % std::uint32_t(15 + (100 - accuracy) * 3);
    const I ring = std::get<I>(energy::ring_at_radius_mm(radius));
    if (arm < 0 || ring < 1) return; // missed the mechanism
    energy_events(r.team, std::get<energy::Step>(team.energy.hit(t, arm, ring)).events);
}

void Engine::hurt_structure(int index, I amount, int by_team, dmg::Cause cause, const std::string& by, I t) {
    auto& st = structures_[std::size_t(index)];
    amount = std::min(st.hp, amount);
    if (!st.alive || amount <= 0) return;
    const I before = st.hp;
    st.hp -= amount;
    (void)damage_.apply({damage_.last_sequence() + 1, t, std::uint32_t(100 + index), st.team ? dmg::Team::Blue : dmg::Team::Red,
                         st.outpost ? dmg::EntityKind::Outpost : dmg::EntityKind::Base, cause, before, st.hp,
                         by_team ? dmg::Team::Blue : dmg::Team::Red});
    if (st.hp > 0) return;
    st.alive = false;
    if (!st.outpost) { event(t, "base-destroyed", ZONE_IDS[index], "by " + by); return; }
    event(t, "outpost-destroyed", ZONE_IDS[index], "by " + by);
    const auto step = positions_[std::size_t(index)].outpost_destroyed(t, pteam(by_team));
    if (const auto* ok = std::get_if<pos::Step>(&step))
        for (const auto& e : ok->events) {
            event(e.time_ms, e.degree == pos::Degree::Full ? "full-control" : "temporary-control", ZONE_IDS[index], team_name(iteam(e.controller)));
            if (e.rebuilt) { st.team = iteam(e.controller); st.hp = pos::REBUILD_HP; st.alive = true; event(t, "outpost-rebuilt", ZONE_IDS[index], std::string(team_name(st.team)) + " 500 hp"); }
        }
}

// A dart reaches the opposing side (§5.7.3): SYNTHETIC accuracy decides whether
// it strikes the selected detection module; dart_v1 decides what that is worth.
void Engine::dart_arrival(int k, I t) {
    auto& team = teams_[std::size_t(k)];
    auto& victim = teams_[std::size_t(1 - k)];
    while (!team.darts_in_flight.empty() && team.darts_in_flight.front() <= t) {
        team.darts_in_flight.erase(team.darts_in_flight.begin());
        const auto target = team.dart.snapshot().target;
        const int zone = dart::is_base(target) ? 2 + (1 - k) : opposing_outpost(k);
        const I number = team.dart.snapshot().resolved + 1;
        const bool on_target = zone >= 0 && structures_[std::size_t(zone)].alive &&
                               I(roll(5, std::uint32_t(k), std::uint32_t(number)) % 100) < config_.teams[std::size_t(k)].dart_accuracy;
        if (!on_target) {
            (void)team.dart.missed(t);
            event(t, "dart-miss", team_name(k), "dart " + std::to_string(number) + " at " + std::string(dart::target_name(target)));
            continue;
        }
        const auto step = std::get<dart::HitStep>(team.dart.hit(t, dart::is_base(target) ? dart::Struck::BaseModule : dart::Struck::OutpostModule, true));
        const auto& e = step.effect;
        if (!e.valid) { event(t, "dart-miss", team_name(k), "dart " + std::to_string(number) + " not detected"); continue; }
        buff::DamageInput in;
        in.source = buff::Source::Dart;
        in.target = dart::is_base(target) ? buff::Target::BaseDartModule : buff::Target::OutpostDartModule;
        in.dart = target == dart::Target::BaseFixed ? buff::DartTarget::Fixed : target == dart::Target::BaseRandomFixed ? buff::DartTarget::RandomFixed
                : target == dart::Target::BaseRandomMoving ? buff::DartTarget::RandomMoving
                : target == dart::Target::BaseTerminalMoving ? buff::DartTarget::TerminalMoving : buff::DartTarget::None;
        // Table 5-2 dart column; defence buffs do not apply to darts (table 5-25).
        const I amount = std::min(structures_[std::size_t(zone)].hp, std::get<I>(buff::settle_damage(in)));
        event(t, "dart-hit", team_name(k), std::string(ZONE_IDS[zone]) + " " + std::string(dart::target_name(target)) + ", hit " + std::to_string(e.hit_number) +
                                              ", -" + std::to_string(amount) + " hp, opponent view occluded " + std::to_string(e.occlusion_ms / 1000) + " s");
        team.dart_damage += amount;
        hurt_structure(zone, amount, k, dmg::Cause::Dart, "dart", t);
        point_disabled_until_[std::size_t(zone)] = t + dart::POINT_DISABLE_MS;
        if (e.experience_tenths) share_exp(k, e.experience_tenths, t);
        if (victim.energy.snapshot().buff_active()) {
            if (e.buffs_voided) energy_events(1 - k, std::get<energy::Step>(victim.energy.void_buff(t)).events);
            else if (e.buff_suspend_ms) {
                victim.energy_suspended_until_ms = std::max(victim.energy_suspended_until_ms, t) + e.buff_suspend_ms;
                event(t, "energy-suspended", team_name(1 - k), std::to_string(e.buff_suspend_ms / 1000) + " s by a dart");
            }
        }
        // Moving targets: every living opposing ground robot loses a share of its max HP.
        // ENGINEERING: dart-class damage, so invincible robots are spared (§2 p17); no experience.
        if (e.max_hp_loss_percent)
            for (auto& v : robots_) {
                const auto& vs = v.life.snapshot();
                if (v.team == k || !vs.alive() || vs.invincible(t)) continue;
                const I before = vs.hp;
                const I cut = std::get<I>(dart::max_hp_loss(vs.max_hp, e.max_hp_loss_percent));
                const auto loss = std::get<life::Loss>(v.life.apply_loss(t, cut, life::LossKind::Combat, context(v)));
                if (loss.actual > 0) {
                    (void)damage_.apply({damage_.last_sequence() + 1, t, std::uint32_t(&v - robots_.data()), v.team ? dmg::Team::Blue : dmg::Team::Red,
                                         dmg::EntityKind::Robot, dmg::Cause::Dart, before, before - loss.actual, k ? dmg::Team::Blue : dmg::Team::Red});
                    team.dart_damage += loss.actual;
                }
                if (loss.died) on_death(v, t, -1);
            }
        if (e.base_armor_deployed) {
            victim.base_armor_deployed = true;
            event(t, "base-armor", team_name(1 - k), "deployed by a dart");
        }
    }
}

// Team-level decisions of the scripted commander: radar command and dart station.
void Engine::command(I t) {
    for (int n = 0; n < 2; ++n) {
        const int k = (n + tick_) % 2; // the side that decides first alternates as well
        auto& team = teams_[std::size_t(k)];
        const auto& cfg = config_.teams[std::size_t(k)];
        // "Double vulnerability" as soon as a chance exists and a living opponent is marked.
        if (team.radar_double.snapshot().chances > 0 && !team.radar_double.snapshot().active(t)) {
            bool marked = false;
            for (const auto& o : robots_) marked = marked || (o.team != k && o.radar_marked && o.life.snapshot().alive());
            if (marked && std::holds_alternative<radar::DoubleSnapshot>(team.radar_double.trigger(t)))
                event(t, "radar-double", team_name(k), "radar vulnerability doubled for 30 s");
        }
        if (!cfg.dart_target) continue;
        dart_arrival(k, t);
        const auto d = team.dart.snapshot();
        if (d.gate == dart::Gate::Closed && d.chances > 0 && d.darts_left > 0 && t >= d.cooldown_until_ms) {
            // Open as early as allowed; without an opposing outpost the base is the target.
            auto target = *cfg.dart_target;
            if (target == dart::Target::Outpost && opposing_outpost(k) < 0) target = dart::Target::BaseFixed;
            if (target != d.target) (void)team.dart.select_target(t, target);
            if (std::holds_alternative<dart::Step>(team.dart.open_gate(t)))
                event(t, "dart-gate", team_name(k), "opening, target " + std::string(dart::target_name(target)));
        } else if (d.gate == dart::Gate::Open && d.darts_left > 0 && t >= team.dart_next_launch_ms &&
                   team.dart_opening_launches < (d.openings >= 2 ? dart::DARTS_PER_ROUND : 2)) {
            // Two darts per opening. If the outpost has fallen, use the single
            // outpost -> base switch that a hit of this opening allows, else hold fire.
            if (d.target == dart::Target::Outpost && opposing_outpost(k) < 0) {
                if (!std::holds_alternative<dart::Step>(team.dart.select_target(t, dart::Target::BaseFixed))) continue;
                event(t, "dart-gate", team_name(k), "target switched to base-fixed");
            }
            if (std::holds_alternative<dart::Step>(team.dart.launch(t))) {
                team.darts_in_flight.push_back(t + DART_FLIGHT_MS);
                ++team.dart_opening_launches;
                team.dart_next_launch_ms = t + DART_LAUNCH_GAP_MS;
                event(t, "dart-launch", team_name(k), "dart " + std::to_string(team.dart.snapshot().launched) + "/4 at " + std::string(dart::target_name(team.dart.snapshot().target)));
            }
        }
    }
}

void Engine::think_and_move(I t) {
    const bool power_boundary = t % lim::POWER_PERIOD_MS == 0;
    const I dt = t - time_ms(tick_);
    // Sequential movement sees earlier robots' new positions. The team that
    // moves first alternates every tick while the order of roles stays the same,
    // so neither side has a fixed priority (ENGINEERING fairness).
    for (std::size_t n = 0; n < robots_.size(); ++n) {
        const std::size_t i = (n + (tick_ % 2 ? robots_.size() / 2 : 0)) % robots_.size();
        auto& r = robots_[i];
        auto& team = teams_[std::size_t(r.team)];
        auto& wallet = team.wallet;
        const auto& s = r.life.snapshot();
        const I reserve = config_.teams[std::size_t(r.team)].gold_reserve;
        if (!s.alive()) {
            // Instant revival when the remaining readout is long and gold allows (§5.3.2, table 5-7).
            const I remaining = s.readout_required - s.readout_progress;
            const I cost = std::get<I>(eco::instant_revival_cost(t, instant_level(r)));
            if (remaining >= 15 && wallet.snapshot().gold >= reserve + cost && r.role != Role::Sentry) {
                (void)wallet.spend(cost);
                (void)r.life.instant_revive(t);
                ++team.instant_revivals;
                event(t, "instant-revival", r.id, std::to_string(cost) + " gold");
            }
            if (power_boundary) (void)r.power.settle(t, 0, power_limit_for(r, t));
            r.at_energy = false;
            continue;
        }
        const bool at_supply = SUPPLY[std::size_t(r.team)].contains(r.x, r.y);
        const I hp = s.hp, max_hp = s.max_hp;
        const I a17 = r.allowance.available(eco::Ammo::Mm17), a42 = r.allowance.available(eco::Ammo::Mm42);
        // ---- purchases at the own supply buff point (non-weak only, §5.3.2).
        if (at_supply && !s.weak) {
            if (r.role == Role::Sentry) {
                const I free = std::get<I>(eco::sentry_supply_available(t, r.sentry_claimed));
                if (free > 0) {
                    (void)r.allowance.deliver(t, eco::Ammo::Mm17, free, t);
                    r.sentry_claimed += free;
                    event(t, "sentry-supply", r.id, std::to_string(free) + " rounds");
                }
            }
            if (r.cls != grow::RobotClass::Heavy && a17 < 100 && wallet.snapshot().gold >= reserve + 100) {
                const auto p = wallet.buy_ammo(t, eco::Ammo::Mm17, eco::Channel::Local, 100);
                if (auto ok = std::get_if<eco::Purchase>(&p)) {
                    (void)r.allowance.deliver(t, eco::Ammo::Mm17, ok->rounds, ok->effective_ms);
                    ++r.purchases;
                    event(t, "purchase", r.id, "100x17mm " + std::to_string(ok->cost) + " gold");
                }
            }
            if (r.cls == grow::RobotClass::Heavy && team.launcher_installed && a42 < 5 && wallet.snapshot().gold >= reserve + 100) {
                const auto p = wallet.buy_ammo(t, eco::Ammo::Mm42, eco::Channel::Local, 10);
                if (auto ok = std::get_if<eco::Purchase>(&p)) {
                    (void)r.allowance.deliver(t, eco::Ammo::Mm42, ok->rounds, ok->effective_ms);
                    ++r.purchases;
                    team.shield.update(t, true, r.allowance.available(eco::Ammo::Mm42));
                    event(t, "purchase", r.id, "10x42mm " + std::to_string(ok->cost) + " gold");
                }
            }
            if (r.cls == grow::RobotClass::Heavy && team.launcher_unlocked && !team.launcher_installed) {
                team.launcher_installed = true; // workbench inside the supply zone (SYNTHETIC)
                event(t, "42mm-installed", r.id, "confirmation shot pending");
            }
        }
        // ---- remote HP exchange while out of combat (§5.3.1, table 5-7).
        if (!at_supply && hp * 2 < max_hp && s.out_of_combat(t) && s.pending_heal_due_ms < 0) {
            const I cost = std::get<I>(eco::remote_hp_cost(t));
            if (wallet.snapshot().gold >= reserve + cost && std::holds_alternative<life::Snapshot>(r.life.request_remote_heal(t))) {
                (void)wallet.spend(cost);
                ++team.remote_heals;
                event(t, "remote-heal", r.id, std::to_string(cost) + " gold");
            }
        }
        // ---- choose a destination field.
        const int enemy = 1 - r.team;
        const bool enemy_outpost_held = positions_[std::size_t(enemy)].full_by(pteam(r.team));
        int field = enemy_outpost_held ? 2 + enemy : enemy; // capture enemy outpost position, then push its base
        bool stay = false;
        if (r.role == Role::Sentry) {
            field = r.team; // guard own outpost position
            if (a17 < 30 && std::get<I>(eco::sentry_supply_available(t, r.sentry_claimed)) >= 100) field = 4 + r.team;
        } else if (r.role == Role::Heavy) {
            const I next = team.highest_assembly + 1;
            const bool can_assemble = next <= config_.teams[std::size_t(r.team)].max_assembly && t >= ASSEMBLY_UNLOCK_MS[std::size_t(next)];
            if (can_assemble) {
                if (ASSEMBLY[std::size_t(r.team)].contains(r.x, r.y)) stay = true;
                else field = 6 + r.team;
            } else if (team.launcher_unlocked && !team.launcher_installed) field = 4 + r.team;
            else if (team.launcher_installed && a42 == 0) field = 4 + r.team;
            else if (!team.launcher_installed) field = 2 + r.team; // defend the own base until armed
        }
        // ---- energy duty (§5.6.3): the second infantry activates the own mechanism
        // whenever a chance for the current phase is held.
        const auto& es = team.energy.snapshot();
        const I chances = energy::phase_at(t) == energy::Phase::Big ? es.big_chances : es.small_chances;
        // A new attempt is only started with rounds to spare; a running one is finished.
        const bool energy_wanted = es.state == energy::State::Activating ? a17 > 0 : es.state == energy::State::Inactive && chances > 0 && a17 >= 50;
        if (r.role == Role::Infantry2 && config_.teams[std::size_t(r.team)].energy_accuracy > 0 && energy_wanted) field = 8 + r.team;
        if (r.role != Role::Sentry && (hp * 10 < max_hp * 3 || (a17 == 0 && r.cls != grow::RobotClass::Heavy && wallet.snapshot().gold >= reserve + 100)))
            field = 4 + r.team;
        if (s.weak && !stay) field = 4 + r.team; // a supply card clears "weak" (§5.3.2)
        r.at_energy = field == 8 + r.team && ENERGY_ZONE[std::size_t(r.team)].contains(r.x, r.y);
        if (r.at_energy) {
            stay = true;
            if (es.state == energy::State::Inactive) {
                const auto triggered = team.energy.trigger(t, energy::Operator::InfantryOperator);
                if (const auto* ok = std::get_if<energy::Step>(&triggered)) energy_events(r.team, ok->events);
            }
        }
        // ---- sentry posture (table 5-28): attack near enemies, defend when hurt, else move.
        if (r.role == Role::Sentry) {
            bool enemy_near = false;
            for (const auto& o : robots_)
                if (o.team != r.team && o.life.snapshot().alive() && dist2(r.x, r.y, o.x, o.y) <= I(SENSOR_MM) * SENSOR_MM) enemy_near = true;
            const auto want = hp * 2 < max_hp ? buff::Posture::Defense : enemy_near ? buff::Posture::Attack : buff::Posture::Move;
            if (want != r.posture.state().posture) {
                const auto chosen = r.posture.select(t, want, false);
                if (std::holds_alternative<I>(chosen)) event(t, "posture", r.id, want == buff::Posture::Attack ? "attack" : want == buff::Posture::Defense ? "defense" : "move");
            }
        }
        if (at_supply && (hp * 10 < max_hp * 9 || s.weak)) stay = true; // heal and clear weak first
        // ---- assembly progress (table 5-11/5-12; duration SYNTHETIC).
        if (r.role == Role::Heavy && stay && ASSEMBLY[std::size_t(r.team)].contains(r.x, r.y)) {
            const I next = team.highest_assembly + 1;
            team.assembling = next;
            team.assembly_progress_ms += dt;
            team.assembly_invincible_ms += dt;
            if (team.assembly_progress_ms >= ASSEMBLY_MS) {
                team.highest_assembly = next;
                team.assembly_done_ms[std::size_t(next)] = t;
                team.assembly_progress_ms = 0;
                team.assembly_invincible_ms = 0; // table: refreshed on a new highest level
                team.assembling = 0;
                const auto reward = std::get<grow::AssemblyReward>(grow::first_assembly_reward(next));
                if (reward.unlocks_42mm && !team.launcher_unlocked) { team.launcher_unlocked = true; event(t, "42mm-unlocked", team_name(r.team), "assembly 3"); }
                event(t, "assembly", team_name(r.team), "difficulty " + std::to_string(next) + " heavy level " + std::to_string(reward.heavy_level));
            }
        } else if (r.role == Role::Heavy) team.assembling = 0;
        // ---- movement with chassis power (SYNTHETIC draw model).
        int vx = 0, vy = 0;
        I draw_mw = 0;
        const I limit_w = power_limit_for(r, t);
        if (!stay && r.power.snapshot().chassis_enabled(t)) {
            I gap = 0;
            // The shared distance field, or the personal detour planned when blocked.
            if (r.detour_ticks > 0 && r.detour_field == field) --r.detour_ticks;
            else r.detour_ticks = 0;
            if (const auto waypoint = flow_target(r.detour_ticks > 0 ? r.detour : flow_[std::size_t(field)], r.x, r.y, r.team == 1)) {
                const I dx = waypoint->first - r.x, dy = waypoint->second - r.y;
                gap = I(sandbox::isqrt(std::uint64_t(dx * dx + dy * dy)));
                if (gap > 0) { vx = int(dx * 1000 / gap); vy = int(dy * 1000 / gap); }
            }
            // Speed fraction: stay within the power limit unless the buffer can absorb a sprint.
            const bool sprint = r.power.snapshot().buffer_uj >= 40'000'000;
            const I fraction = sprint ? 1000 : std::min<I>(1000, limit_w * 1000 / r.peak_w);
            if (vx || vy) {
                draw_mw = I(r.peak_w) * fraction;
                vx = int(I(vx) * r.vmax * fraction / 1000 / 1000);
                vy = int(I(vy) * r.vmax * fraction / 1000 / 1000);
                // Never overshoot the waypoint (keeps the robot on cell centres).
                const I step_len = I(sandbox::isqrt(std::uint64_t(I(vx) * vx + I(vy) * vy)));
                if (step_len > gap && step_len > 0) { vx = int(I(vx) * gap / step_len); vy = int(I(vy) * gap / step_len); }
            }
        }
        if (power_boundary) {
            const auto before = r.power.snapshot().cutoffs;
            (void)r.power.settle(t, draw_mw, limit_w * 1000);
            if (r.power.snapshot().cutoffs != before) event(t, "chassis-cutoff", r.id, "buffer energy exhausted");
        }
        if (vx || vy) {
            const int ox = r.x, oy = r.y;
            if (r.evade > 0) {
                // Evasion in progress: keep moving sideways, switching side at an obstacle.
                --r.evade;
                if (!blocked(r.x + r.evade_x, r.y + r.evade_y, r.radius, i)) { r.x += r.evade_x; r.y += r.evade_y; }
                else if (!blocked(r.x - r.evade_x, r.y - r.evade_y, r.radius, i)) { r.evade_x = -r.evade_x; r.evade_y = -r.evade_y; r.x += r.evade_x; r.y += r.evade_y; }
                else r.evade = 0;
            } else if (!blocked(r.x + vx, r.y + vy, r.radius, i)) { r.x += vx; r.y += vy; }
            else if (vx && !blocked(r.x + vx, r.y, r.radius, i)) r.x += vx;
            else if (vy && !blocked(r.x, r.y + vy, r.radius, i)) r.y += vy;
            r.stuck = r.x == ox && r.y == oy ? r.stuck + 1 : 0;
            if (r.stuck) ++r.blocked_ticks;
            if (r.stuck > 20 && r.evade == 0) {
                // Deadlock breaker after a third of a second without progress: plan a route
                // around the other robots. If the previous detour did not help either, or
                // none exists, give way to the own right for a third of a second ("keep
                // right": two robots facing each other step to opposite sides and pass).
                r.stuck = 0;
                if (r.detour_ticks > 0 || !plan_detour(i, field)) {
                    r.detour_ticks = 0;
                    r.evade = 20;
                    r.evade_x = -vy;
                    r.evade_y = vx;
                }
            }
        } else { r.stuck = 0; r.evade = 0; }
    }
}

void Engine::update_positions(I t) {
    std::array<pos::Activation, 4> act{};
    for (auto& r : robots_) {
        const auto& s = r.life.snapshot();
        for (int z = 0; z < 4; ++z) {
            auto& d = r.detectors[std::size_t(z)];
            const bool present = s.alive() && !s.weak && ZONES[std::size_t(z)].contains(r.x, r.y);
            const auto want = present ? cap::QualifiedDetection::Present : cap::QualifiedDetection::Absent;
            if (!s.alive() && d.snapshot().logically_active) // immediate death deactivation (EXPERIMENTAL, as roster)
                d = std::get<cap::ActivationDelay>(cap::ActivationDelay::start(t, cap::QualifiedDetection::Absent, cap::BoundaryPolicy::EngineeringTimerBeforeInputV1));
            else if (want != d.snapshot().detection) (void)d.apply(t, want);
            else if (d.snapshot().deactivation_at_ms && *d.snapshot().deactivation_at_ms <= t) (void)d.apply(t);
            if (d.snapshot().logically_active) (r.team ? act[std::size_t(z)].blue : act[std::size_t(z)].red) = true;
        }
    }
    for (int z = 0; z < 4; ++z) {
        const auto step = std::get<pos::Step>(positions_[std::size_t(z)].advance(t, act[std::size_t(z)]));
        for (const auto& e : step.events) {
            const char* what = e.transition == pos::Transition::CapturedTemporary ? "temporary-control" : "full-control";
            event(e.time_ms, what, ZONE_IDS[z], team_name(iteam(e.controller)));
            if (e.rebuilt) {
                auto& st = structures_[std::size_t(z)];
                st.team = iteam(e.controller);
                st.hp = pos::REBUILD_HP;
                st.alive = true;
                event(e.time_ms, "outpost-rebuilt", ZONE_IDS[z], std::string(team_name(st.team)) + " 500 hp");
            }
        }
    }
}

void Engine::fire(I t) {
    tick_shots_.assign(robots_.size(), 0);
    for (std::size_t i = 0; i < robots_.size(); ++i) {
        auto& r = robots_[i];
        if (r.cooldown > 0) --r.cooldown;
        auto& team = teams_[std::size_t(r.team)];
        const auto& s = r.life.snapshot();
        const bool mm42 = r.cls == grow::RobotClass::Heavy;
        if (!s.alive() || !s.launcher_allowed() || r.cooldown > 0) continue;
        if (mm42 && (!team.launcher_installed || team.assembling)) continue;
        if (!r.heat && !(mm42 && team.launcher_installed && !team.confirmation_fired)) continue;
        const auto ammo = mm42 ? eco::Ammo::Mm42 : eco::Ammo::Mm17;
        const I allowance = r.allowance.available(ammo);
        if (allowance <= 0) continue;
        if (r.heat) {
            auto preview = *r.heat;
            (void)preview.apply(t, 0);
            // Stay below the over-heat line: Q1 + one shot must not exceed Q0.
            const I step = mm42 ? 1000 : 100;
            if (preview.snapshot().firing_locked() || preview.snapshot().heat_tenths + step > preview.snapshot().limit * 10) continue;
        }
        // ---- target selection (6 m sensor + wall line of sight). A robot on
        // energy duty fires at the own mechanism while it is "activating".
        const bool at_mechanism = r.at_energy && !mm42 && team.energy.snapshot().state == energy::State::Activating;
        int tx = at_mechanism ? ENERGY_POS[r.team][0] : 0, ty = at_mechanism ? ENERGY_POS[r.team][1] : 0;
        I best = std::numeric_limits<I>::max();
        bool found = at_mechanism;
        if (!found) for (const auto& o : robots_) {
            if (o.team == r.team || !o.life.snapshot().alive()) continue;
            const I d = dist2(r.x, r.y, o.x, o.y);
            if (d < best && d <= I(SENSOR_MM) * SENSOR_MM && !sandbox::line_blocked({r.x, r.y}, {o.x, o.y})) { best = d; tx = o.x; ty = o.y; found = true; }
        }
        if (!found)
            for (const auto& st : structures_) {
                if (st.team == r.team || !st.alive) continue;
                const I d = dist2(r.x, r.y, st.x, st.y);
                if (d < best && d <= I(STRUCTURE_RANGE_MM) * STRUCTURE_RANGE_MM && !sandbox::line_blocked({r.x, r.y}, {st.x, st.y})) { best = d; tx = st.x; ty = st.y; found = true; }
            }
        if (!found) continue;
        // Deterministic SYNTHETIC dispersion: lateral offset up to 200 mm + 6% of range.
        const I dx = tx - r.x, dy = ty - r.y, len = std::max<I>(1, I(sandbox::isqrt(std::uint64_t(dx * dx + dy * dy))));
        const int jitter = int(roll(1, std::uint32_t(tick_), std::uint32_t(i)) % 2001) - 1000;
        const I lateral = (200 + len * 6 / 100) * jitter / 1000;
        const I ax = tx - dy * lateral / len, ay = ty + dx * lateral / len;
        const I adx = ax - r.x, ady = ay - r.y, alen = std::max<I>(1, I(sandbox::isqrt(std::uint64_t(adx * adx + ady * ady))));
        const int speed = mm42 ? MM42_SPEED : MM17_SPEED;
        Projectile p;
        p.owner = int(i);
        p.mm42 = mm42;
        p.vx = int(adx * speed / alen);
        p.vy = int(ady * speed / alen);
        p.x = r.x + int(adx * (r.radius + 30) / alen);
        p.y = r.y + int(ady * (r.radius + 30) / alen);
        p.life = PROJECTILE_LIFE;
        const bool confirmation = mm42 && !team.confirmation_fired;
        p.counts = !confirmation && !at_mechanism; // §5.7.1: no heat, speed or damage for that shot
        if (!confirmation) ++tick_shots_[i];
        (void)r.allowance.consume(t, ammo, 1);
        (void)r.life.record_fire(t);
        r.cooldown = mm42 ? MM42_COOLDOWN : MM17_COOLDOWN;
        ++r.shots;
        if (mm42) team.shield.launch(t, true, allowance);
        if (confirmation) {
            team.confirmation_fired = true;
            event(t, "42mm-confirmed", r.id, "installation shot (no heat/damage)");
        }
        gain_exp(r, grow::shot_experience_tenths(r.cls, 1), t);
        if (at_mechanism) energy_shot(i, t); // the drawn round is cosmetic; the aim model decides
        if (!sandbox::line_blocked({r.x, r.y}, {p.x, p.y})) projectiles_.push_back(p);
    }
}

void Engine::on_death(Robot& r, I t, int killer) {
    auto& team = teams_[std::size_t(r.team)];
    died_[std::size_t(&r - robots_.data())] = true;
    r.at_energy = false;
    r.power.reset_on_death(t);
    for (auto& d : r.detectors) d = std::get<cap::ActivationDelay>(cap::ActivationDelay::start(t, cap::QualifiedDetection::Absent, cap::BoundaryPolicy::EngineeringTimerBeforeInputV1));
    if (r.role == Role::Heavy) team.shield.heavy_died(t);
    std::string detail = "readout " + std::to_string(r.life.snapshot().readout_required);
    if (killer >= 0) {
        auto& k = robots_[std::size_t(killer)];
        ++k.kills;
        if (grow::uses_experience(k.cls)) {
            const I victim_level = r.cls == grow::RobotClass::Heavy ? std::get<I>(grow::heavy_level(team.highest_assembly)) : grow::kill_level(r.cls, r.level);
            const I gain = std::get<I>(grow::kill_experience_tenths(victim_level, k.level));
            gain_exp(k, gain, t);
            detail += ", " + k.id + " +" + std::to_string(gain / 10) + " exp";
        }
        detail = "by " + k.id + ", " + detail;
    }
    event(t, "death", r.id, detail);
}

void Engine::projectiles_and_damage(I t) {
    std::vector<Projectile> keep;
    for (auto& p : projectiles_) {
        const int bx = p.x + p.vx, by = p.y + p.vy;
        int first = 65537, kind = 0, index = -1; // kind 1 robot, 2 structure
        for (auto w : sandbox::WALLS) { const int e = rect_entry(p.x, p.y, bx, by, w); if (e >= 0 && e < first) { first = e; kind = 0; index = -1; } }
        const auto& shooter = robots_[std::size_t(p.owner)];
        for (std::size_t j = 0; j < robots_.size(); ++j) {
            const auto& o = robots_[j];
            if (o.team == shooter.team || !o.life.snapshot().alive()) continue;
            const int e = circle_entry(p.x, p.y, bx, by, o.x, o.y, o.radius + 20);
            if (e >= 0 && e < first) { first = e; kind = 1; index = int(j); }
        }
        for (std::size_t z = 0; z < 4; ++z) {
            const auto& st = structures_[z];
            if (st.team == shooter.team || !st.alive) continue;
            const int e = circle_entry(p.x, p.y, bx, by, st.x, st.y, st.radius);
            if (e >= 0 && e < first) { first = e; kind = 2; index = int(z); }
        }
        if (first > 65536) {
            p.x = bx; p.y = by;
            if (--p.life > 0 && bx >= 0 && by >= 0 && bx <= WIDTH && by <= HEIGHT) keep.push_back(p);
            continue;
        }
        if (index < 0 || !p.counts) continue; // wall, or the 42 mm confirmation shot
        auto& attacker = robots_[std::size_t(p.owner)];
        const auto source = p.mm42 ? buff::Source::Projectile42 : buff::Source::Projectile17;
        const bool shield = p.mm42 && teams_[std::size_t(attacker.team)].shield.shielded(t);
        const std::array<I, 1> attack{energy_effect(attacker.team, t).attack_percent}; // big energy buff (table 5-22)
        if (kind == 1) {
            auto& v = robots_[std::size_t(index)];
            const auto def = defenses(v, t), vul = vulnerabilities(v, t);
            const bool heavy_safe = v.cls == grow::RobotClass::Heavy &&
                (SUPPLY[std::size_t(v.team)].contains(v.x, v.y) || (teams_[std::size_t(v.team)].assembling && teams_[std::size_t(v.team)].assembly_invincible_ms <= 45000));
            buff::DamageInput in;
            in.target = buff::Target::RobotArmor; in.source = source;
            in.attack_percent = attack;
            in.defense_percent = def; in.vulnerability_percent = vul;
            in.invincible = v.life.snapshot().invincible(t) || heavy_safe;
            in.shield_42mm = shield;
            const I amount = std::get<I>(buff::settle_damage(in));
            ++attacker.hits;
            if (amount <= 0) continue;
            const I before = v.life.snapshot().hp;
            const auto loss = std::get<life::Loss>(v.life.apply_loss(t, amount, life::LossKind::Combat, context(v)));
            if (loss.actual > 0) {
                (void)damage_.apply({damage_.last_sequence() + 1, t, std::uint32_t(index), v.team ? dmg::Team::Blue : dmg::Team::Red, dmg::EntityKind::Robot,
                                     dmg::Cause::Projectile, before, before - loss.actual, attacker.team ? dmg::Team::Blue : dmg::Team::Red});
                attacker.damage_dealt += loss.actual;
                gain_exp(attacker, std::get<I>(grow::damage_experience_tenths(grow::TargetKind::Robot, loss.actual)), t);
            }
            if (loss.died) on_death(v, t, p.owner);
        } else {
            auto& st = structures_[std::size_t(index)];
            std::vector<I> def;
            const auto& owner_team = teams_[std::size_t(st.team)];
            if (!st.outpost && positions_[std::size_t(st.team)].full_by(pteam(st.team))) def.push_back(buff::BASE_OUTPOST_DEFENSE);
            if (owner_team.highest_assembly >= 3) def.push_back(25);
            def.push_back(energy_effect(st.team, t).defense_percent); // the owner's energy buff also covers outpost and base
            buff::DamageInput in;
            in.target = st.outpost ? buff::Target::OutpostMiddleArmor : buff::Target::BaseArmor;
            in.source = source;
            in.attack_percent = attack;
            in.defense_percent = def;
            in.shield_42mm = shield;
            const I amount = std::min(st.hp, std::get<I>(buff::settle_damage(in)));
            ++attacker.hits;
            if (amount <= 0) continue;
            const bool outpost = st.outpost;
            attacker.damage_dealt += amount;
            hurt_structure(index, amount, attacker.team, dmg::Cause::Projectile, attacker.id, t);
            gain_exp(attacker, std::get<I>(grow::damage_experience_tenths(outpost ? grow::TargetKind::Outpost : grow::TargetKind::Base, amount)), t);
        }
    }
    projectiles_ = std::move(keep);
    // Heat: one event per tick per robot - detected shots first, then a death reset.
    for (std::size_t i = 0; i < robots_.size(); ++i) {
        auto& r = robots_[i];
        if (!r.heat) continue;
        (void)r.heat->apply(t, tick_shots_[i], died_[i]);
    }
}

void Engine::update_progression(I t) {
    for (auto& r : robots_) {
        auto& team = teams_[std::size_t(r.team)];
        if (r.cls == grow::RobotClass::Infantry) {
            const I cap = std::get<I>(grow::team_level_cap(team.highest_assembly));
            const I level = std::get<I>(grow::level_for_experience_tenths(r.exp_tenths, cap));
            if (level > r.level) {
                r.level = level;
                event(t, "level-up", r.id, "level " + std::to_string(level));
            }
        } else if (r.cls == grow::RobotClass::Heavy) {
            const bool armed = team.highest_assembly >= 3 || (team.launcher_installed && team.confirmation_fired);
            const auto stage = armed ? grow::HeavyStage::Armed : team.highest_assembly == 2 ? grow::HeavyStage::Assembly2
                             : team.highest_assembly == 1 ? grow::HeavyStage::Assembly1 : grow::HeavyStage::Default;
            if (stage != r.loadout.heavy_stage) { r.loadout.heavy_stage = stage; event(t, "heavy-stage", r.id, std::to_string(int(stage))); }
        }
        apply_performance(r, t);
    }
    // §5.7.1 time-based unlock: 300 - M/50 - 2N/3 seconds (M,N clamped at 0, ENGINEERING).
    for (int k = 0; k < 2; ++k) {
        auto& team = teams_[std::size_t(k)];
        const I base_gap = structures_[std::size_t(2 + 1 - k)].hp - structures_[std::size_t(2 + k)].hp;
        const I control_gap = ledger_.totals()[std::size_t(1 - k)] - ledger_.totals()[std::size_t(k)];
        team.max_base_deficit = std::max(team.max_base_deficit, base_gap);
        team.max_control_deficit = std::max(team.max_control_deficit, control_gap);
        team.base_lowest = std::min(team.base_lowest, structures_[std::size_t(2 + k)].hp);
        const I unlock_ms = 300000 - team.max_base_deficit * 1000 / 50 - team.max_control_deficit * 2000 / 3;
        if (!team.launcher_unlocked && t >= unlock_ms) { team.launcher_unlocked = true; event(t, "42mm-unlocked", team_name(k), "time path"); }
    }
}

void Engine::credit_and_judge(I t) {
    judge::ControlSnapshot control;
    for (int z = 0; z < 4; ++z) {
        const auto& s = positions_[std::size_t(z)].snapshot();
        control.positions[std::size_t(z)] = {s.controller == pos::Team::Red ? judge::Team::Red : judge::Team::Blue,
                                             s.degree == pos::Degree::Full ? judge::ControlDegree::Full : judge::ControlDegree::Temporary};
    }
    if (config_.credits && t % 1000 == 0) (void)ledger_.apply({ledger_.last_sequence() + 1, control});
    std::vector<judge::RemainingHpFact> facts;
    for (std::size_t i = 0; i < robots_.size(); ++i) {
        const auto& s = robots_[i].life.snapshot();
        facts.push_back({std::uint32_t(i), robots_[i].team ? judge::Team::Blue : judge::Team::Red, judge::EntityKind::Robot, s.hp, true, false});
    }
    const auto hp = std::get<std::array<I, 2>>(judge::surviving_robot_hp(facts));
    judge::MatchSnapshot snap;
    snap.elapsed_ms = t;
    for (int k = 0; k < 2; ++k)
        snap.teams[std::size_t(k)] = {structures_[std::size_t(2 + k)].hp,
                                      config_.credits ? std::optional<I>{ledger_.totals()[std::size_t(k)]} : std::nullopt,
                                      damage_.totals().attack_damage[std::size_t(k)], hp[std::size_t(k)]};
    const auto r = judge_.update(snap);
    if (const auto* d = std::get_if<judge::Decision>(&r)) {
        if (d->outcome != judge::Outcome::Ongoing) { decision_ = *d; terminal_ = true; }
    } else {
        unresolved_ = std::string(judge::error_name(std::get<judge::Error>(r)));
        terminal_ = true;
    }
}

void Engine::record(I t) {
    for (const auto& r : robots_) { fold(r.x); fold(r.y); fold(r.life.snapshot().hp); fold(r.exp_tenths); fold(r.heat ? r.heat->snapshot().heat_tenths : -1); }
    for (const auto& s : structures_) fold(s.hp);
    for (const auto& k : teams_) fold(k.wallet.snapshot().gold);
    for (const auto& p : positions_) { fold(int(p.snapshot().controller)); fold(int(p.snapshot().degree)); fold(p.snapshot().progress_ms); }
    fold(I(projectiles_.size()));
    for (const auto& r : robots_) fold(r.mark.snapshot().progress_tenths);
    for (const auto& k : teams_) {
        const auto& e = k.energy.snapshot();
        fold(int(e.state)); fold(e.lit_mask); fold(e.hits); fold(e.ring_sum); fold(e.buff_until_ms); fold(k.energy_suspended_until_ms);
        fold(int(k.dart.snapshot().gate)); fold(k.dart.snapshot().hits); fold(k.dart.snapshot().darts_left); fold(k.radar_double.snapshot().active_until_ms);
    }
    for (const I until : point_disabled_until_) fold(until);
    if (config_.frame_every && tick_ % config_.frame_every == 0) {
        Frame f{tick_, {}, {}};
        for (const auto& r : robots_) {
            const auto& s = r.life.snapshot();
            f.robots.push_back({r.x, r.y, int(s.hp), (s.alive() ? 1 : 0) | (s.weak ? 2 : 0) | (s.invincible(t) ? 4 : 0)});
        }
        for (const auto& p : projectiles_) f.projectiles.push_back({p.x, p.y, p.mm42 ? 1 : 0, robots_[std::size_t(p.owner)].team});
        frames_.push_back(std::move(f));
    }
    if (t % 1000 == 0 || terminal_) {
        std::ostringstream o;
        o << "{\"t\":" << t << ",\"base\":[" << structures_[2].hp << ',' << structures_[3].hp << "],\"outpost\":[" << structures_[0].hp << ','
          << structures_[1].hp << "],\"outpost_owner\":[\"" << team_name(structures_[0].team) << "\",\"" << team_name(structures_[1].team)
          << "\"],\"gold\":[" << teams_[0].wallet.snapshot().gold << ',' << teams_[1].wallet.snapshot().gold << "],\"control\":["
          << ledger_.totals()[0] << ',' << ledger_.totals()[1] << "],\"attack_damage\":[" << damage_.totals().attack_damage[0] << ','
          << damage_.totals().attack_damage[1] << "],\"positions\":[";
        for (int z = 0; z < 4; ++z) {
            const auto& s = positions_[std::size_t(z)].snapshot();
            o << (z ? "," : "") << "\"" << team_name(iteam(s.controller)) << (s.degree == pos::Degree::Full ? "-full" : "-temporary") << "\"";
        }
        // energy: 0 inactive, 1 activating, 2 small buff, 3 big buff, +4 while a dart suspends the buff.
        // dart: gate 0 closed, 1 opening, 2 open, 3 closing. Robot rows end with the radar vulnerability %.
        o << "],\"energy\":[";
        for (int k = 0; k < 2; ++k) {
            const auto& e = teams_[std::size_t(k)].energy.snapshot();
            const int code = e.state == energy::State::Inactive ? 0 : e.state == energy::State::Activating ? 1 : e.phase == energy::Phase::Small ? 2 : 3;
            o << (k ? "," : "") << (code >= 2 && t < teams_[std::size_t(k)].energy_suspended_until_ms ? code + 4 : code);
        }
        o << "],\"dart\":[" << int(teams_[0].dart.snapshot().gate) << ',' << int(teams_[1].dart.snapshot().gate) << "],\"robots\":[";
        for (std::size_t i = 0; i < robots_.size(); ++i) {
            const auto& s = robots_[i].life.snapshot();
            const I level = robots_[i].cls == grow::RobotClass::Infantry ? robots_[i].level : instant_level(robots_[i]);
            o << (i ? "," : "") << "[" << s.hp << ',' << s.max_hp << ',' << level << ',' << (s.alive() ? 1 : 0) << ',' << radar_vulnerability(robots_[i], t) << ']';
        }
        o << "]}";
        timeline_.push_back(o.str());
    }
}

bool Engine::step() {
    if (terminal_) return false;
    const I t = time_ms(tick_ + 1);
    now_ = t;
    died_.assign(robots_.size(), false);
    settle_time(t);
    think_and_move(t);
    update_positions(t);
    command(t); // after the positions are settled at t: a dart may destroy an outpost
    fire(t);
    projectiles_and_damage(t);
    update_progression(t);
    ++tick_;
    if (!terminal_) credit_and_judge(t);
    if (!terminal_ && tick_ >= config_.duration_ticks) terminal_ = true;
    record(t);
    if (terminal_) {
        ledger_.seal();
        damage_.seal();
        const char* outcome = "ongoing";
        if (decision_) outcome = decision_->outcome == judge::Outcome::RedWins ? "red-wins" : decision_->outcome == judge::Outcome::BlueWins ? "blue-wins" : "draw";
        event(t, "match-end", outcome, unresolved_.empty() ? (decision_ ? std::string(decision_->rule_id) : "duration-cap") : unresolved_);
    }
    return !terminal_;
}

void Engine::run() { while (step()) {} }

std::string Engine::json(bool include_frames) const {
    std::ostringstream o;
    auto esc = [](const std::string& s) { std::string r; for (char c : s) { if (c == '"' || c == '\\') r += '\\'; r += c; } return r; };
    const char* outcome = "ongoing";
    const char* reason = "not-ended";
    if (decision_) {
        outcome = decision_->outcome == judge::Outcome::RedWins ? "red-wins" : decision_->outcome == judge::Outcome::BlueWins ? "blue-wins" : "draw";
        switch (decision_->reason) {
        case judge::Reason::BaseHp: reason = "base-destroyed"; break;
        case judge::Reason::TimeoutControl: reason = "timeout-control"; break;
        case judge::Reason::TimeoutAttackDamage: reason = "timeout-attack-damage"; break;
        case judge::Reason::TimeoutSurvivingRobotHp: reason = "timeout-surviving-robot-hp"; break;
        case judge::Reason::TimeoutEqual: reason = "timeout-equal"; break;
        default: break;
        }
    } else if (!unresolved_.empty()) { outcome = "unresolved"; reason = unresolved_.c_str(); }
    o << "{\"schema\":\"rmuc2027-rules-lab-result.v2\",\"profile\":\"" << PROFILE << "\",\"build\":\"" << build_id()
      << "\",\"official\":false,\"seed\":" << config_.seed << ",\"ticks\":" << tick_ << ",\"elapsed_ms\":" << time_ms(tick_) << ",\"hash\":" << hash_
      << ",\"result\":{\"outcome\":\"" << outcome << "\",\"reason\":\"" << reason << "\"},\"modules\":[\""
      << grow::MODULE_VERSION << "\",\"" << life::MODULE_VERSION << "\",\"" << eco::MODULE_VERSION << "\",\"" << buff::MODULE_VERSION << "\",\""
      << lim::MODULE_VERSION << "\",\"" << pos::MODULE_VERSION << "\",\"" << cap::MODULE_VERSION << "\",\"" << dmg::MODULE_VERSION << "\",\""
      << judge::MODULE_VERSION << "\",\"" << energy::MODULE_VERSION << "\",\"" << radar::MODULE_VERSION << "\",\"" << dart::MODULE_VERSION << "\"],\"teams\":[";
    for (int k = 0; k < 2; ++k) {
        const auto& t = teams_[std::size_t(k)];
        const auto& w = t.wallet.snapshot();
        o << (k ? "," : "") << "{\"team\":\"" << team_name(k) << "\",\"base_hp\":" << structures_[std::size_t(2 + k)].hp
          << ",\"control_score\":" << ledger_.totals()[std::size_t(k)] << ",\"attack_damage\":" << damage_.totals().attack_damage[std::size_t(k)]
          << ",\"gold\":" << w.gold << ",\"gold_income\":" << w.total_income << ",\"gold_spent\":" << w.total_spent
          << ",\"purchased_17mm\":" << w.purchased_17mm << ",\"purchased_42mm\":" << w.purchased_42mm
          << ",\"highest_assembly\":" << t.highest_assembly << ",\"launcher_unlocked\":" << (t.launcher_unlocked ? "true" : "false")
          << ",\"launcher_installed\":" << (t.launcher_installed && t.confirmation_fired ? "true" : "false")
          << ",\"instant_revivals\":" << t.instant_revivals << ",\"remote_heals\":" << t.remote_heals;
        const auto& en = t.energy.snapshot();
        const auto& da = t.dart.snapshot();
        o << ",\"energy\":{\"small_activations\":" << en.small_activations << ",\"big_activations\":" << en.big_activations << ",\"attempts\":" << en.attempts
          << ",\"restarts\":" << en.failures << ",\"bonus_experience\":" << t.energy_bonus_tenths / 10 << "." << t.energy_bonus_tenths % 10
          << "},\"dart\":{\"target\":\"" << (config_.teams[std::size_t(k)].dart_target ? std::string(dart::target_name(da.target)) : std::string("none"))
          << "\",\"openings\":" << da.openings << ",\"launched\":" << da.launched << ",\"hits\":" << da.hits << ",\"damage\":" << t.dart_damage
          << "},\"radar\":{\"double_chances\":" << t.radar_double.snapshot().chances << ",\"double_triggers\":" << t.radar_double.snapshot().triggers
          << "},\"base_armor_deployed\":" << (t.base_armor_deployed ? "true" : "false") << "}";
    }
    o << "],\"structures\":[";
    for (int z = 0; z < 4; ++z) {
        const auto& s = structures_[std::size_t(z)];
        const auto& p = positions_[std::size_t(z)].snapshot();
        o << (z ? "," : "") << "{\"id\":\"" << ZONE_IDS[z] << "\",\"owner\":\"" << team_name(s.team) << "\",\"hp\":" << s.hp
          << ",\"alive\":" << (s.alive ? "true" : "false") << ",\"controller\":\"" << team_name(iteam(p.controller)) << "\",\"degree\":\""
          << (p.degree == pos::Degree::Full ? "full" : "temporary") << "\",\"transitions\":" << p.transitions << "}";
    }
    o << "],\"robots\":[";
    for (std::size_t i = 0; i < robots_.size(); ++i) {
        const auto& r = robots_[i];
        const auto& s = r.life.snapshot();
        o << (i ? "," : "") << "{\"id\":\"" << r.id << "\",\"role\":\"" << role_name(r.role) << "\",\"level\":" << (r.cls == grow::RobotClass::Infantry ? r.level : instant_level(r))
          << ",\"experience\":" << r.exp_tenths / 10 << "." << r.exp_tenths % 10 << ",\"hp\":" << s.hp << ",\"max_hp\":" << s.max_hp
          << ",\"alive\":" << (s.alive() ? "true" : "false") << ",\"deaths\":" << s.deaths << ",\"kills\":" << r.kills
          << ",\"instant_revivals\":" << s.instant_revivals << ",\"shots\":" << r.shots << ",\"hits\":" << r.hits
          << ",\"damage_dealt\":" << r.damage_dealt << ",\"purchases\":" << r.purchases
          << ",\"allowance_17mm\":" << r.allowance.available(eco::Ammo::Mm17) << ",\"allowance_42mm\":" << r.allowance.available(eco::Ammo::Mm42)
          << ",\"heat_limit\":" << (r.heat ? r.heat->snapshot().limit : 0) << ",\"cooling_per_second\":" << (r.heat ? r.heat->snapshot().cooling_per_second : 0)
          << ",\"chassis_cutoffs\":" << r.power.snapshot().cutoffs << ",\"blocked_ticks\":" << r.blocked_ticks << ",\"energy_shots\":" << r.energy_shots
          << ",\"radar_progress\":" << r.mark.snapshot().progress_tenths / 10 << "." << r.mark.snapshot().progress_tenths % 10 << "}";
    }
    o << "],\"events\":[";
    for (std::size_t i = 0; i < events_.size(); ++i) {
        const auto& e = events_[i];
        o << (i ? "," : "") << "{\"t\":" << e.time_ms << ",\"kind\":\"" << esc(e.kind) << "\",\"subject\":\"" << esc(e.subject) << "\",\"detail\":\"" << esc(e.detail) << "\"}";
    }
    o << "],\"timeline\":[";
    for (std::size_t i = 0; i < timeline_.size(); ++i) o << (i ? "," : "") << timeline_[i];
    o << "]";
    if (include_frames) {
        o << ",\"field\":{\"width\":" << WIDTH << ",\"height\":" << HEIGHT << ",\"walls\":[";
        for (std::size_t i = 0; i < sandbox::WALLS.size(); ++i) { const auto& w = sandbox::WALLS[i]; o << (i ? "," : "") << '[' << w.x1 << ',' << w.y1 << ',' << w.x2 << ',' << w.y2 << ']'; }
        o << "],\"zones\":[";
        for (int z = 0; z < 4; ++z) { const auto& r = ZONES[std::size_t(z)]; o << (z ? "," : "") << "{\"id\":\"" << ZONE_IDS[z] << "\",\"rect\":[" << r.x1 << ',' << r.y1 << ',' << r.x2 << ',' << r.y2 << "]}"; }
        o << "],\"supply\":[";
        for (int k = 0; k < 2; ++k) { const auto& r = SUPPLY[std::size_t(k)]; o << (k ? "," : "") << '[' << r.x1 << ',' << r.y1 << ',' << r.x2 << ',' << r.y2 << ']'; }
        o << "],\"assembly\":[";
        for (int k = 0; k < 2; ++k) { const auto& r = ASSEMBLY[std::size_t(k)]; o << (k ? "," : "") << '[' << r.x1 << ',' << r.y1 << ',' << r.x2 << ',' << r.y2 << ']'; }
        o << "],\"energy\":[";
        for (int k = 0; k < 2; ++k) { const auto& r = ENERGY_ZONE[std::size_t(k)]; o << (k ? "," : "") << "{\"pos\":[" << ENERGY_POS[k][0] << ',' << ENERGY_POS[k][1] << "],\"zone\":[" << r.x1 << ',' << r.y1 << ',' << r.x2 << ',' << r.y2 << "]}"; }
        o << "],\"structures\":[";
        for (int z = 0; z < 4; ++z) { const auto& s = structures_[std::size_t(z)]; o << (z ? "," : "") << '[' << s.x << ',' << s.y << ',' << s.radius << ']'; }
        o << "],\"robots\":[";
        for (std::size_t i = 0; i < robots_.size(); ++i) o << (i ? "," : "") << "{\"id\":\"" << robots_[i].id << "\",\"team\":" << robots_[i].team << ",\"role\":\"" << role_name(robots_[i].role) << "\",\"radius\":" << robots_[i].radius << "}";
        o << "]},\"frames\":[";
        for (std::size_t i = 0; i < frames_.size(); ++i) {
            const auto& f = frames_[i];
            o << (i ? "," : "") << "{\"tick\":" << f.tick << ",\"r\":[";
            for (std::size_t j = 0; j < f.robots.size(); ++j) { const auto& a = f.robots[j]; o << (j ? "," : "") << '[' << a[0] << ',' << a[1] << ',' << a[2] << ',' << a[3] << ']'; }
            o << "],\"p\":[";
            for (std::size_t j = 0; j < f.projectiles.size(); ++j) { const auto& a = f.projectiles[j]; o << (j ? "," : "") << '[' << a[0] << ',' << a[1] << ',' << a[2] << ',' << a[3] << ']'; }
            o << "]}";
        }
        o << "]";
    }
    o << "}";
    return o.str();
}
} // namespace match_lab
