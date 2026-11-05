// Prints every performance row as one JSON object per line, for cross-checks.
#include "growth_rules.hpp"
#include <iostream>

namespace g = rmuc2027::growth_v1;

static void print(const char* role, const char* chassis, const char* launcher, const char* stage,
                  std::int64_t level, const g::Performance& p) {
    std::cout << "{\"role\":\"" << role << "\",\"chassis\":\"" << chassis << "\",\"launcher\":\"" << launcher
              << "\",\"stage\":\"" << stage << "\",\"level\":" << level << ",\"max_hp\":" << p.max_hp
              << ",\"chassis_power_w\":" << p.chassis_power_w << ",\"heat_limit\":"
              << (p.heat_limit ? *p.heat_limit : -1) << ",\"cooling_per_second\":"
              << (p.cooling_per_second ? *p.cooling_per_second : -1) << "}\n";
}

int main() {
    std::cout << "{\"module\":\"" << g::MODULE_VERSION << "\",\"source_sha256\":\"" << g::SOURCE_SHA256 << "\"}\n";
    const std::pair<g::Chassis, const char*> chassis[] = {{g::Chassis::PowerFirst, "power_priority"},
                                                          {g::Chassis::HpFirst, "hp_priority"}};
    const std::pair<g::Launcher, const char*> launchers[] = {{g::Launcher::BurstFirst, "burst_priority_17mm"},
                                                             {g::Launcher::CoolingFirst, "cooling_priority_17mm"}};
    for (auto [c, cn] : chassis)
        for (auto [l, ln] : launchers)
            for (std::int64_t level = 1; level <= g::MAX_LEVEL; ++level)
                print("infantry", cn, ln, "", level,
                      std::get<g::Performance>(g::performance({g::RobotClass::Infantry, c, l}, level)));
    const std::pair<g::HeavyStage, const char*> stages[] = {
        {g::HeavyStage::Default, "opening_default"}, {g::HeavyStage::Assembly1, "after_first_assembly"},
        {g::HeavyStage::Assembly2, "after_second_assembly"},
        {g::HeavyStage::Armed, "third_assembly_or_successful_42mm_installation"}};
    for (auto [s, sn] : stages) {
        g::Loadout l{g::RobotClass::Heavy};
        l.heavy_stage = s;
        print("heavy", "", "", sn, 1, std::get<g::Performance>(g::performance(l, 1)));
    }
    print("sentry", "", "", "automatic", 1, std::get<g::Performance>(g::performance({g::RobotClass::SentryAuto}, 1)));
    print("sentry", "", "", "semi_automatic", 1, std::get<g::Performance>(g::performance({g::RobotClass::SentrySemi}, 1)));
}
