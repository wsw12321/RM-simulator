#include "energy_rules.hpp"
#include <algorithm>
#include <limits>

namespace rmuc2027::energy_v1 {
namespace {
constexpr I NEVER = std::numeric_limits<I>::max();
std::uint64_t splitmix64(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}
int popcount5(std::uint8_t m) { int n = 0; for (int i = 0; i < ARMS; ++i) n += (m >> i) & 1; return n; }
// The second-hit window of the fifth big group: all five groups count as
// activated once its first module is hit, so the 20 s window no longer applies.
bool final_group_open(const Snapshot& s) { return s.phase == Phase::Big && s.group == ARMS - 1 && s.first_hit_arm >= 0; }
}

std::string_view error_name(Error e) {
    switch (e) {
    case Error::InvalidChoice: return "InvalidChoice";
    case Error::NonIncreasingTime: return "NonIncreasingTime";
    case Error::NotInactive: return "NotInactive";
    case Error::NoChance: return "NoChance";
    case Error::NoBuff: return "NoBuff";
    case Error::NegativeValue: return "NegativeValue";
    case Error::UnsettledTime: return "UnsettledTime";
    }
    return "InvalidError";
}

I lowest_valid_ring(I big_activations) { return big_activations <= 0 ? 1 : big_activations == 1 ? 4 : 7; }

Result<I> ring_at_radius_mm(I radius_mm) {
    if (radius_mm < 0) return Error::NegativeValue;
    return radius_mm >= 150 ? I{0} : 10 - radius_mm / 15;
}

Result<I> big_buff_duration_ms(I lit_arms) {
    switch (lit_arms) {
    case 5: return I{30000};
    case 6: return I{35000};
    case 7: return I{40000};
    case 8: return I{45000};
    case 9: return I{50000};
    case 10: return I{60000};
    default: return Error::InvalidChoice;
    }
}

Result<EnergyMechanism> EnergyMechanism::start(std::uint64_t seed, ChancePolicy policy) {
    if (policy != ChancePolicy::EngineeringConsumeOnTriggerV1 && policy != ChancePolicy::EngineeringConsumeOnActivationV1)
        return Error::InvalidChoice;
    EnergyMechanism m(seed, policy);
    m.state_.small_chances = 1; // the chance granted at match start
    m.state_.chances_granted = 1;
    return m;
}

void EnergyMechanism::light(Snapshot& s, I t, std::vector<Event>& events) const {
    const std::uint64_t r = splitmix64(seed_ + 0x632BE59BD9B4E019ULL * ++s.lightings);
    s.first_hit_arm = -1;
    if (s.phase == Phase::Small) {
        // One of the arms not yet activated in this attempt.
        const int left = ARMS - popcount5(s.done_mask);
        int pick = int(r % std::uint64_t(left));
        for (int arm = 0; arm < ARMS; ++arm)
            if (!((s.done_mask >> arm) & 1) && pick-- == 0) s.lit_mask = std::uint8_t(1u << arm);
    } else {
        // Any two of the five modules: ten unordered pairs.
        int pick = int(r % 10u);
        for (int a = 0; a < ARMS; ++a)
            for (int b = a + 1; b < ARMS; ++b)
                if (pick-- == 0) s.lit_mask = std::uint8_t((1u << a) | (1u << b));
    }
    s.deadline_ms = t + HIT_WINDOW_MS;
    events.push_back({t, EventKind::GroupLit, s.phase, -1, 0, s.group, 0, s.lit_mask});
}

void EnergyMechanism::expire(Snapshot& s, I t, std::vector<Event>& events) const {
    events.push_back({t, EventKind::WindowExpired, s.phase, -1, 0, s.group, 0, s.lit_mask});
    s.state = State::Inactive;
    s.lit_mask = s.done_mask = 0;
    s.first_hit_arm = -1;
    s.group = s.ring_sum = s.hits = 0;
    s.deadline_ms = s.activating_since_ms = -1;
}

void EnergyMechanism::restart(Snapshot& s, I t, EventKind why, std::vector<Event>& events) const {
    ++s.failures;
    events.push_back({t, why, s.phase, -1, 0, s.group, s.ring_sum, s.lit_mask});
    s.group = 0;
    s.done_mask = 0;
    s.ring_sum = s.hits = 0;
    s.first_hit_arm = -1;
    // A failure in the final group's second-hit window can come after the 20 s
    // window: there is no time left for a new attempt.
    if (t >= s.activating_since_ms + ACTIVATING_WINDOW_MS) expire(s, t, events);
    else light(s, t, events);
}

void EnergyMechanism::activate(Snapshot& s, I t, std::vector<Event>& events) const {
    s.state = State::Activated;
    s.lit_mask = 0;
    s.first_hit_arm = -1;
    s.deadline_ms = -1;
    if (s.phase == Phase::Small) {
        ++s.small_activations;
        s.buff_until_ms = t + SMALL_BUFF_MS;
        s.small_bonus_left_tenths = SMALL_BONUS_CAP_TENTHS;
        if (policy_ == ChancePolicy::EngineeringConsumeOnActivationV1) --s.small_chances;
    } else {
        ++s.big_activations;
        s.buff_until_ms = t + std::get<I>(big_buff_duration_ms(s.hits));
        if (policy_ == ChancePolicy::EngineeringConsumeOnActivationV1) --s.big_chances;
    }
    events.push_back({t, EventKind::Activated, s.phase, -1, 0, s.hits, s.ring_sum, 0});
}

void EnergyMechanism::close_group(Snapshot& s, I t, std::vector<Event>& events) const {
    ++s.group;
    if (s.group == ARMS) activate(s, t, events);
    else light(s, t, events);
}

void EnergyMechanism::settle(Snapshot& s, I t, std::vector<Event>& events) const {
    for (;;) {
        // Same-millisecond priority: buff end, phase change, 20 s window, hit
        // window, chance grant.
        const I buff_end = s.state == State::Activated ? s.buff_until_ms : NEVER;
        const I phase_change = s.big_phase ? NEVER : BIG_PHASE_FROM_MS;
        const bool activating = s.state == State::Activating;
        const I window = activating && !final_group_open(s) ? s.activating_since_ms + ACTIVATING_WINDOW_MS : NEVER;
        const I deadline = activating ? s.deadline_ms : NEVER;
        const I chance = s.chances_granted < I(CHANCE_MS.size()) ? CHANCE_MS[std::size_t(s.chances_granted)] : NEVER;
        const I next = std::min({buff_end, phase_change, window, deadline, chance});
        if (next > t) break;
        if (buff_end == next) {
            s.state = State::Inactive;
            s.buff_until_ms = -1;
            s.small_bonus_left_tenths = 0;
            s.ring_sum = s.hits = 0;
            events.push_back({next, EventKind::BuffEnded, s.phase, -1, 0, 0, 0, 0});
        } else if (phase_change == next) {
            s.big_phase = true;
            s.small_chances = 0; // ENGINEERING: small chances cannot activate the big mechanism
            if (activating && s.phase == Phase::Small) {
                events.push_back({next, EventKind::SmallAbortedByBigPhase, Phase::Small, -1, 0, s.group, 0, s.lit_mask});
                s.state = State::Inactive;
                s.lit_mask = s.done_mask = 0;
                s.group = s.ring_sum = s.hits = 0;
                s.deadline_ms = s.activating_since_ms = -1;
            }
        } else if (window == next) {
            expire(s, next, events);
        } else if (deadline == next) {
            if (s.phase == Phase::Big && s.first_hit_arm >= 0) close_group(s, next, events); // 1 s for the other module is over
            else restart(s, next, EventKind::FailedTimeout, events);
        } else {
            const bool big = s.chances_granted >= 2;
            I& held = big ? s.big_chances : s.small_chances;
            ++held;
            ++s.chances_granted;
            events.push_back({next, EventKind::ChanceGranted, big ? Phase::Big : Phase::Small, -1, 0, held, 0, 0});
        }
    }
    s.time_ms = t;
}

Result<Step> EnergyMechanism::advance(I t) {
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    Step out{state_, {}};
    settle(out.state, t, out.events);
    state_ = out.state;
    return out;
}

Result<Step> EnergyMechanism::trigger(I t, Operator who) {
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    if (who != Operator::InfantryOperator && who != Operator::Sentry) return Error::InvalidChoice;
    Step out{state_, {}};
    Snapshot& s = out.state;
    settle(s, t, out.events);
    if (s.state != State::Inactive) return Error::NotInactive;
    const Phase now = phase_at(t);
    I& held = now == Phase::Big ? s.big_chances : s.small_chances;
    if (held <= 0) return Error::NoChance;
    if (policy_ == ChancePolicy::EngineeringConsumeOnTriggerV1) --held;
    s.state = State::Activating;
    s.phase = now;
    s.activating_since_ms = t;
    s.group = 0;
    s.done_mask = 0;
    s.ring_sum = s.hits = 0;
    ++s.attempts;
    out.events.push_back({t, EventKind::Triggered, now, -1, 0, who == Operator::Sentry ? 1 : 0, 0, 0});
    light(s, t, out.events);
    state_ = s;
    return out;
}

Result<Step> EnergyMechanism::hit(I t, int arm, I ring) {
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    if (arm < 0 || arm >= ARMS || ring < 1 || ring > 10) return Error::InvalidChoice;
    Step out{state_, {}};
    Snapshot& s = out.state;
    settle(s, t, out.events);
    if (s.state == State::Activating) {
        const auto bit = std::uint8_t(1u << arm);
        const bool big = s.phase == Phase::Big;
        const bool detectable = !big || ring >= lowest_valid_ring(s.big_activations);
        if (detectable && arm == s.first_hit_arm) {
            // ENGINEERING: a repeated hit on the module already hit in this group is ignored.
        } else if (detectable && (s.lit_mask & bit)) {
            ++s.hits;
            if (big) s.ring_sum += ring;
            out.events.push_back({t, EventKind::ArmHit, s.phase, arm, ring, s.hits, s.ring_sum, s.lit_mask});
            if (!big) {
                s.done_mask |= bit;
                close_group(s, t, out.events);
            } else if (s.first_hit_arm < 0) {
                s.first_hit_arm = arm;
                s.lit_mask = std::uint8_t(s.lit_mask & ~bit);
                s.deadline_ms = t + SECOND_HIT_WINDOW_MS;
            } else {
                close_group(s, t, out.events); // both modules of the group
            }
        } else if (detectable) {
            restart(s, t, EventKind::FailedWrongModule, out.events);
        }
    }
    state_ = s;
    return out;
}

Result<Step> EnergyMechanism::void_buff(I t) {
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    Step out{state_, {}};
    Snapshot& s = out.state;
    settle(s, t, out.events);
    if (s.state != State::Activated) return Error::NoBuff;
    s.state = State::Inactive;
    s.buff_until_ms = -1;
    s.small_bonus_left_tenths = 0;
    s.ring_sum = s.hits = 0;
    out.events.push_back({t, EventKind::BuffVoided, s.phase, -1, 0, 0, 0, 0});
    state_ = s;
    return out;
}

Result<I> EnergyMechanism::small_bonus_tenths(I t, I gained_tenths) {
    if (t != state_.time_ms) return Error::UnsettledTime;
    if (gained_tenths < 0) return Error::NegativeValue;
    if (state_.state != State::Activated || state_.phase != Phase::Small) return I{0};
    const I bonus = std::min(gained_tenths, state_.small_bonus_left_tenths);
    state_.small_bonus_left_tenths -= bonus;
    return bonus;
}
} // namespace rmuc2027::energy_v1
