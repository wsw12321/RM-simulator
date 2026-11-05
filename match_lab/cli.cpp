// Native CLI: rmuc_match_lab BUILD | RUN duration_ticks frame_every credits red_spec blue_spec
// spec = chassis1,launcher1,chassis2,launcher2,sentry,max_assembly,gold_reserve
//   chassis: hp|power   launcher: cooling|burst   sentry: auto|semi
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
    if (parts.size() != 7) return false;
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
    return true;
}
}

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() == 1 && args[0] == "BUILD") { std::cout << "{\"build\":\"" << match_lab::build_id() << "\"}\n"; return 0; }
    match_lab::Config c;
    long long duration = 0, frames = 0, credits = 0;
    if (args.size() != 6 || args[0] != "RUN" || !parse_int(args[1], 1, match_lab::DURATION_TICKS, duration) ||
        !parse_int(args[2], 0, 600, frames) || !parse_int(args[3], 0, 1, credits) || !parse_spec(args[4], c.teams[0]) ||
        !parse_spec(args[5], c.teams[1])) {
        std::cout << "{\"error\":\"usage: RUN duration_ticks frame_every credits red_spec blue_spec\"}\n";
        return 2;
    }
    c.duration_ticks = int(duration);
    c.frame_every = int(frames);
    c.credits = credits == 1;
    if (auto e = match_lab::validate(c)) { std::cout << "{\"error\":\"" << *e << "\"}\n"; return 2; }
    match_lab::Engine engine(c);
    engine.run();
    std::cout << engine.json(c.frame_every > 0) << "\n";
    return 0;
}
