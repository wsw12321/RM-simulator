#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

// Source-linked energy mechanism activation state machine for ONE team.
// RMUC 2027 V1.0.0 §5.6.3 (printed pp97-101), tables 5-23/5-24, §5.6.3.1.
// Input is already adjudicated: operator triggers and detected 17 mm hits on a
// named arm and ring. It owns no ballistics, rotation or buff percentages
// (see buff_v1 tables 5-22/5-23 for the effect of an activation).
namespace rmuc2027::energy_v1 {
inline constexpr std::string_view MODULE_VERSION = "rmuc2027-energy-v1.0.0-module.1";
inline constexpr std::string_view SOURCE_VERSION = "RMUC2027_CN_V1.0.0_20261015";
inline constexpr std::string_view SOURCE_SHA256 = "9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be";
using I = std::int64_t;

inline constexpr int ARMS = 5;
inline constexpr I BIG_PHASE_FROM_MS = 180000;                      // "比赛开始 3 分钟后"
inline constexpr std::array<I, 5> CHANCE_MS{0, 90000, 180000, 255000, 330000}; // 2 small, 3 big
inline constexpr I ACTIVATING_WINDOW_MS = 20000;                    // back to inactive if not activated
inline constexpr I HIT_WINDOW_MS = 2500;                            // hit a lit module within 2.5 s
inline constexpr I SECOND_HIT_WINDOW_MS = 1000;                     // big: the other lit module
inline constexpr I SMALL_BUFF_MS = 45000;
inline constexpr I SMALL_BONUS_CAP_TENTHS = 12000;                  // 1200 extra experience per buff
inline constexpr I BIG_ACTIVATION_EXPERIENCE_TENTHS = 7500;         // 750 split among living infantry

enum class Phase : std::uint8_t { Small, Big };
enum class State : std::uint8_t { Inactive, Activating, Activated };
// §5.6.3: an infantry operator (player client) or the sentry (serial port).
enum class Operator : std::uint8_t { InfantryOperator, Sentry };
// The manual grants "chances to activate" without saying when one is spent.
enum class ChancePolicy : std::uint8_t {
    EngineeringConsumeOnTriggerV1,     // spent when the mechanism enters "activating"
    EngineeringConsumeOnActivationV1   // spent only by a successful activation
};
enum class EventKind : std::uint8_t {
    ChanceGranted,          // value = chances of that phase now held
    Triggered,
    GroupLit,               // mask = lit modules, value = group index 0..4
    ArmHit,                 // arm, ring; value = arms lit so far in this attempt
    FailedTimeout,          // no lit module hit within 2.5 s: attempt restarts
    FailedWrongModule,      // a module that was not randomly lit was hit: attempt restarts
    WindowExpired,          // 20 s without activation: back to inactive
    SmallAbortedByBigPhase, // ENGINEERING: a small attempt cannot outlive the small phase
    Activated,              // value = lit arms, ring_sum = sum of hit rings
    BuffEnded,
    BuffVoided              // dart moving-target hit removes the buff (§5.7.3)
};
enum class Error {
    InvalidChoice, NonIncreasingTime, NotInactive, NoChance, NoBuff, NegativeValue, UnsettledTime
};
std::string_view error_name(Error);
template<class T> using Result = std::variant<T, Error>;

inline Phase phase_at(I t) { return t < BIG_PHASE_FROM_MS ? Phase::Small : Phase::Big; }
// Table 5-24: rings that still detect hits after n successful big activations.
I lowest_valid_ring(I big_activations);              // 0 -> 1, 1 -> 4, >=2 -> 7
// Figure 5-17: ten rings of 15 mm radial width, ring 10 at the centre (Ø30)
// out to ring 1 (Ø300). Returns 0 outside the module. ENGINEERING: a radius
// exactly on a ring boundary belongs to the outer (lower) ring.
Result<I> ring_at_radius_mm(I radius_mm);
// Table 5-23: lit arms 5..10 -> 30/35/40/45/50/60 s.
Result<I> big_buff_duration_ms(I lit_arms);

struct Event {
    I time_ms = 0;
    EventKind kind{};
    Phase phase = Phase::Small;
    int arm = -1;
    I ring = 0, value = 0, ring_sum = 0;
    std::uint8_t mask = 0;
};
struct Snapshot {
    I time_ms = 0;
    State state = State::Inactive;
    Phase phase = Phase::Small;      // phase of the current attempt / buff
    I small_chances = 0, big_chances = 0;
    I chances_granted = 0;           // next index into CHANCE_MS
    bool big_phase = false;
    I small_activations = 0, big_activations = 0;
    I activating_since_ms = -1;
    I group = 0;                     // groups completed in the current attempt
    std::uint8_t lit_mask = 0;       // modules currently lit and awaiting a hit
    std::uint8_t done_mask = 0;      // small: arms already activated in this attempt
    int first_hit_arm = -1;          // big: module already hit in the current group
    I deadline_ms = -1;              // exclusive end of the current hit window
    I ring_sum = 0, hits = 0;        // detected valid hits of the attempt, then of the buff
    I buff_until_ms = -1;            // exclusive end of the buff
    I small_bonus_left_tenths = 0;
    I attempts = 0, failures = 0;
    std::uint64_t lightings = 0;     // selection counter
    bool operator==(const Snapshot&) const = default;
    bool buff_active() const { return state == State::Activated; }
};
struct Step { Snapshot state; std::vector<Event> events; };

// Timers expire at their exact millisecond BEFORE inputs of that millisecond
// (ENGINEERING, same convention as capture_v1's timer-before-input policy).
// Every call first settles time up to t; an error changes nothing.
class EnergyMechanism {
public:
    // `seed` drives the "random" module selection with SplitMix64 (ENGINEERING:
    // the manual only says random; uniform selection is assumed).
    static Result<EnergyMechanism> start(std::uint64_t seed,
                                         ChancePolicy = ChancePolicy::EngineeringConsumeOnTriggerV1);
    Result<Step> advance(I t);
    // Operator command: an inactive mechanism with a chance for the current
    // phase enters "activating" and lights the first group.
    Result<Step> trigger(I t, Operator);
    // A detected 17 mm hit on module `arm` (0..4) at `ring` (1..10). Hits while
    // not activating, and big-mechanism hits outside the valid rings, are not
    // detected and change nothing.
    Result<Step> hit(I t, int arm, I ring);
    // §5.7.3: a dart hit on a moving base target voids the held energy buff.
    Result<Step> void_buff(I t);
    // §5.6.3: while the small buff lasts every infantry experience gain earns
    // 100% extra, at most 1200 per buff period for the whole team. Returns the
    // extra amount granted for `gained_tenths`. t must already be settled.
    Result<I> small_bonus_tenths(I t, I gained_tenths);
    const Snapshot& snapshot() const noexcept { return state_; }
    ChancePolicy policy() const noexcept { return policy_; }
private:
    Snapshot state_;
    std::uint64_t seed_ = 0;
    ChancePolicy policy_ = ChancePolicy::EngineeringConsumeOnTriggerV1;
    EnergyMechanism(std::uint64_t seed, ChancePolicy p) : seed_(seed), policy_(p) {}
    void settle(Snapshot&, I t, std::vector<Event>&) const;
    void light(Snapshot&, I t, std::vector<Event>&) const;
    void restart(Snapshot&, I t, EventKind, std::vector<Event>&) const;
    void expire(Snapshot&, I t, std::vector<Event>&) const;
    void activate(Snapshot&, I t, std::vector<Event>&) const;
    void close_group(Snapshot&, I t, std::vector<Event>&) const;
};
} // namespace rmuc2027::energy_v1
