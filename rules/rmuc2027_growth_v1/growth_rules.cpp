#include "growth_rules.hpp"
#include <limits>

namespace rmuc2027::growth_v1 {
namespace {
using I = std::int64_t;
constexpr I kMax = std::numeric_limits<I>::max();

// Table 5-15, printed p91. Index = level-1.
constexpr std::array<I, 10> kLevelExperience{0, 550, 1100, 1650, 2200, 2750, 3300, 3850, 4400, 5000};
// Table 5-17, printed pp92-93: {max HP, chassis power W}.
constexpr std::array<std::array<I, 2>, 10> kPowerFirst{{
    {150, 60}, {175, 65}, {200, 70}, {225, 75}, {250, 80},
    {275, 85}, {300, 90}, {325, 95}, {350, 100}, {400, 100}}};
constexpr std::array<std::array<I, 2>, 10> kHpFirst{{
    {200, 45}, {225, 50}, {250, 55}, {275, 60}, {300, 65},
    {325, 70}, {350, 75}, {375, 80}, {400, 90}, {400, 100}}};
// Table 5-18, printed pp93-94: {heat limit, cooling per second}.
constexpr std::array<std::array<I, 2>, 10> kBurstFirst{{
    {170, 5}, {180, 7}, {190, 9}, {200, 11}, {210, 12},
    {220, 13}, {230, 14}, {240, 16}, {250, 18}, {260, 20}}};
constexpr std::array<std::array<I, 2>, 10> kCoolingFirst{{
    {40, 12}, {48, 14}, {56, 16}, {64, 18}, {72, 20},
    {80, 22}, {88, 24}, {96, 26}, {104, 28}, {120, 30}}};
constexpr I kMuzzle17MmS = 25000;   // tables 3-5/3-6: 25 m/s
constexpr I kMuzzle42MmS = 16500;   // tables 3-3/5-16: 16.5 m/s

bool valid_level(I level) { return level >= 1 && level <= MAX_LEVEL; }
// Round a non-negative rational half up (四舍五入).
I round_half_up(I numerator, I denominator) { return (2 * numerator + denominator) / (2 * denominator); }
} // namespace

std::string_view error_name(Error e) {
    switch (e) {
    case Error::InvalidClass: return "InvalidClass";
    case Error::InvalidChoice: return "InvalidChoice";
    case Error::InvalidLevel: return "InvalidLevel";
    case Error::NegativeValue: return "NegativeValue";
    case Error::IntegerOverflow: return "IntegerOverflow";
    case Error::NoRecipients: return "NoRecipients";
    case Error::NotExperienceRobot: return "NotExperienceRobot";
    case Error::UnadjudicatedNonProjectileKiller: return "UnadjudicatedNonProjectileKiller";
    }
    return "InvalidError";
}

Result<Performance> performance(const Loadout& l, I level) {
    switch (l.robot_class) {
    case RobotClass::Infantry: {
        if (!valid_level(level)) return Error::InvalidLevel;
        const auto row = std::size_t(level - 1);
        const std::array<I, 2>* chassis = nullptr;
        const std::array<I, 2>* launcher = nullptr;
        if (l.chassis == Chassis::PowerFirst) chassis = &kPowerFirst[row];
        else if (l.chassis == Chassis::HpFirst) chassis = &kHpFirst[row];
        if (l.launcher == Launcher::BurstFirst) launcher = &kBurstFirst[row];
        else if (l.launcher == Launcher::CoolingFirst) launcher = &kCoolingFirst[row];
        if (!chassis || !launcher) return Error::InvalidChoice;
        return Performance{(*chassis)[0], (*chassis)[1], Caliber::Mm17, (*launcher)[0], (*launcher)[1], kMuzzle17MmS};
    }
    case RobotClass::Heavy:
        // Table 5-16, printed pp91-92. Heat values only after installation.
        switch (l.heavy_stage) {
        case HeavyStage::Default: return Performance{400, 50, Caliber::None, {}, {}, {}};
        case HeavyStage::Assembly1: return Performance{450, 50, Caliber::None, {}, {}, {}};
        case HeavyStage::Assembly2: return Performance{500, 50, Caliber::None, {}, {}, {}};
        case HeavyStage::Armed: return Performance{600, 50, Caliber::Mm42, 300, 100, kMuzzle42MmS};
        }
        return Error::InvalidChoice;
    // Table 3-6, printed p26. Sentry posture multipliers are applied elsewhere.
    case RobotClass::SentryAuto: return Performance{400, 100, Caliber::Mm17, 260, 30, kMuzzle17MmS};
    case RobotClass::SentrySemi: return Performance{200, 60, Caliber::Mm17, 100, 10, kMuzzle17MmS};
    }
    return Error::InvalidClass;
}

Result<I> initial_allowance(RobotClass c) {
    switch (c) {
    case RobotClass::Heavy: case RobotClass::Infantry: return I{0};
    case RobotClass::SentryAuto: case RobotClass::SentrySemi: return I{300};
    }
    return Error::InvalidClass;
}

bool uses_experience(RobotClass c) { return c == RobotClass::Infantry; }
I kill_level(RobotClass c, I level) {
    return c == RobotClass::SentryAuto || c == RobotClass::SentrySemi ? 1 : level;
}

I experience_for_level_tenths(I level) { return valid_level(level) ? kLevelExperience[std::size_t(level - 1)] * 10 : 0; }

Result<I> level_for_experience_tenths(I exp, I cap) {
    if (exp < 0) return Error::NegativeValue;
    if (!valid_level(cap)) return Error::InvalidLevel;
    I level = 1;
    for (I l = 2; l <= cap; ++l)
        if (exp >= kLevelExperience[std::size_t(l - 1)] * 10) level = l;
    return level;
}

Result<I> team_level_cap(I highest) {
    if (highest < 0 || highest > 4) return Error::InvalidChoice;
    return highest >= 3 ? I{10} : highest >= 2 ? I{7} : I{5};
}
Result<I> heavy_level(I highest) {
    if (highest < 0 || highest > 4) return Error::InvalidChoice;
    return highest >= 3 ? I{10} : highest == 2 ? I{7} : highest == 1 ? I{5} : I{1};
}

Result<AssemblyReward> first_assembly_reward(I d) {
    // Table 5-12, printed p83.
    switch (d) {
    case 1: return AssemblyReward{5, 5, 0, 40, false};
    case 2: return AssemblyReward{7, 7, 0, 20, false};
    case 3: return AssemblyReward{10, 10, 25, 20, true};
    case 4: return AssemblyReward{10, 10, 50, 40, false};
    default: return Error::InvalidChoice;
    }
}
Result<I> repeat_gold_per_10s(I d) {
    switch (d) {
    case 1: return I{4};
    case 2: return I{8};
    case 3: return I{12};
    default: return Error::InvalidChoice; // d4: 仅限完成一次
    }
}

I shot_experience_tenths(RobotClass c, I rounds) {
    if (!uses_experience(c) || rounds <= 0 || rounds > kMax / 10) return 0;
    return rounds * 10;
}

Result<I> damage_experience_tenths(TargetKind target, I damage) {
    if (damage < 0) return Error::NegativeValue;
    if (damage > kMax / 40) return Error::IntegerOverflow;
    switch (target) {
    case TargetKind::Robot: return damage * 40;
    case TargetKind::Outpost: return damage * 20;
    case TargetKind::Base: return (damage + 1) / 2 * 10; // 1 exp per 2 damage, odd rounds up
    }
    return Error::InvalidChoice;
}

Result<I> kill_experience_tenths(I victim, I killer) {
    if (!valid_level(victim) || !valid_level(killer)) return Error::InvalidLevel;
    const I gap = victim >= killer ? victim - killer : 0;
    // 50*Lv*(1+0.2*gap) == 50*Lv + 10*Lv*gap exactly.
    return (50 * victim + 10 * victim * gap) * 10;
}

Result<I> split_tenths(I total, I recipients) {
    if (total < 0) return Error::NegativeValue;
    if (recipients <= 0) return Error::NoRecipients;
    if (total > kMax / 2) return Error::IntegerOverflow;
    return round_half_up(total, recipients);
}

Result<I> unattributed_kill_share_tenths(const UnattributedKill& k) {
    if (!valid_level(k.victim_level) || !valid_level(k.level_cap)) return Error::InvalidLevel;
    if (!k.projectile && k.policy != KillerPolicy::EngineeringAverageLevelForNonProjectileV1)
        return Error::UnadjudicatedNonProjectileKiller;
    if (k.recipient_experience_tenths.empty()) return Error::NoRecipients;
    I sum = 0;
    for (I e : k.recipient_experience_tenths) {
        if (e < 0) return Error::NegativeValue;
        if (sum > kMax - e) return Error::IntegerOverflow;
        sum += e;
    }
    const I n = I(k.recipient_experience_tenths.size());
    // The level of the average is a threshold comparison on exact sum/n.
    I level = 1;
    for (I l = 2; l <= k.level_cap; ++l)
        if (sum >= kLevelExperience[std::size_t(l - 1)] * 10 * n) level = l;
    const auto total = kill_experience_tenths(k.victim_level, level);
    return split_tenths(std::get<I>(total), n);
}

Result<I> hp_after_max_change(I hp, I old_max, I new_max) {
    if (hp < 0 || old_max < 0 || new_max < 0) return Error::NegativeValue;
    if (hp > old_max) return Error::InvalidChoice;
    if (new_max > old_max) return hp + (new_max - old_max);
    return hp < new_max ? hp : new_max; // a lower max never leaves HP above it
}
} // namespace rmuc2027::growth_v1
