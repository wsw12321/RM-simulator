// Native CLI: rmuc_match_lab BUILD | RUN duration_ticks frame_every credits seed red_spec blue_spec
// spec = chassis1,launcher1,chassis2,launcher2,sentry,max_assembly,gold_reserve,energy,dart,dart_accuracy,radar
//   chassis: hp|power   launcher: cooling|burst   sentry: auto|semi
//   energy / dart_accuracy / radar: synthetic skill 0..100 (0 = system unused)
//   dart: none|outpost|base-fixed|base-random-fixed|base-random-moving|base-terminal-moving
#include "engine.hpp"
#include <iostream>
#include <sstream>

namespace {
bool parse_int(const std::string& s, long long lo, long long hi, long long& out) {
    if (s.empty() || s.size() > 9) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    out = std::stoll(s);
    return out >= lo && out <= hi;
}
bool parse_spec(const std::string& spec, match_lab::TeamConfig& t) {
    std::vector<std::string> parts;
    std::stringstream ss(spec);
    for (std::string p; std::getline(ss, p, ',');) parts.push_back(p);
    if (parts.size() != 11) return false;
    namespace g = rmuc2027::growth_v1;
    for (int k = 0; k < 2; ++k) {
        const auto& c = parts[std::size_t(2 * k)];
        const auto& l = parts[std::size_t(2 * k + 1)];
        if (c != "hp" && c != "power") return false;
        if (l != "cooling" && l != "burst") return false;
        t.chassis[std::size_t(k)] = c == "hp" ? g::Chassis::HpFirst : g::Chassis::PowerFirst;
        t.launcher[std::size_t(k)] = l == "cooling" ? g::Launcher::CoolingFirst : g::Launcher::BurstFirst;
    }
    if (parts[4] != "auto" && parts[4] != "semi") return false;
    t.sentry_auto = parts[4] == "auto";
    long long a = 0, r = 0;
    if (!parse_int(parts[5], 0, 3, a) || !parse_int(parts[6], 0, 5000, r)) return false;
    t.max_assembly = a;
    t.gold_reserve = r;
    long long energy = 0, dart_accuracy = 0, radar = 0;
    if (!parse_int(parts[7], 0, 100, energy) || !parse_int(parts[9], 0, 100, dart_accuracy) || !parse_int(parts[10], 0, 100, radar)) return false;
    t.energy_accuracy = energy;
    t.dart_accuracy = dart_accuracy;
    t.radar_accuracy = radar;
    namespace d = rmuc2027::dart_v1;
    t.dart_target.reset();
    for (const auto target : {d::Target::Outpost, d::Target::BaseFixed, d::Target::BaseRandomFixed, d::Target::BaseRandomMoving, d::Target::BaseTerminalMoving})
        if (parts[8] == d::target_name(target)) t.dart_target = target;
    if (!t.dart_target && parts[8] != "none") return false;
    return true;
}
}

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() == 1 && args[0] == "BUILD") { std::cout << "{\"build\":\"" << match_lab::build_id() << "\"}\n"; return 0; }
    match_lab::Config c;
    long long duration = 0, frames = 0, credits = 0, seed = 0;
    if (args.size() != 7 || args[0] != "RUN" || !parse_int(args[1], 1, match_lab::DURATION_TICKS, duration) ||
        !parse_int(args[2], 0, 600, frames) || !parse_int(args[3], 0, 1, credits) || !parse_int(args[4], 0, 999999999, seed) ||
        !parse_spec(args[5], c.teams[0]) || !parse_spec(args[6], c.teams[1])) {
        std::cout << "{\"error\":\"usage: RUN duration_ticks frame_every credits seed red_spec blue_spec\"}\n";
        return 2;
    }
    c.duration_ticks = int(duration);
    c.frame_every = int(frames);
    c.credits = credits == 1;
    c.seed = seed;
    if (auto e = match_lab::validate(c)) { std::cout << "{\"error\":\"" << *e << "\"}\n"; return 2; }
    match_lab::Engine engine(c);
    engine.run();
    std::cout << engine.json(c.frame_every > 0) << "\n";
    return 0;
}
