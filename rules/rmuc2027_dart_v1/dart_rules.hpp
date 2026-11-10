#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

// Source-linked dart system rules for ONE team's launch station: gate chances
// and timing, target selection, the detection window and the benefits of a hit.
// RMUC 2027 V1.0.0 §5.7.3 (printed pp110-111). Raw dart damage is table 5-2
// (p65) and lives in buff_v1::raw_damage.
// Input is already adjudicated: commander commands, detected launches and
// detected hits on a dart detection module. It owns no flight or guidance model.
namespace rmuc2027::dart_v1 {
inline constexpr std::string_view MODULE_VERSION = "rmuc2027-dart-v1.0.0-module.1";
inline constexpr std::string_view SOURCE_VERSION = "RMUC2027_CN_V1.0.0_20261015";
inline constexpr std::string_view SOURCE_SHA256 = "9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be";
using I = std::int64_t;

inline constexpr I DARTS_PER_ROUND = 4;
inline constexpr std::array<I, 2> GATE_CHANCE_MS{30000, 240000}; // 30 s and 4 min after the start
inline constexpr I GATE_OPENING_MS = 7000;       // "闸门完全开启耗时约 7 秒"
inline constexpr I GATE_CLOSING_MS = 7000;       // ENGINEERING: the closing time is not stated
inline constexpr I LAUNCH_WINDOW_MS = 30000;     // from fully open
inline constexpr I DETECTION_WINDOW_MS = 40000;  // from fully open
inline constexpr I GATE_COOLDOWN_MS = 15000;     // after the gate has closed
inline constexpr I HIT_BLACKOUT_MS = 2000;       // detection closed after each hit
inline constexpr I POINT_DISABLE_MS = 30000;     // struck structure's buff point, refreshed
inline constexpr std::array<I, 4> OCCLUSION_MS{10000, 5000, 3000, 2000}; // 1st..4th hit
inline constexpr I MOVING_OCCLUSION_MS = 10000;
inline constexpr I FIXED_EXPERIENCE_TENTHS = 2000, RANDOM_FIXED_EXPERIENCE_TENTHS = 6000, MOVING_EXPERIENCE_TENTHS = 25000;
inline constexpr I RANDOM_MOVING_HP_PERCENT = 20, TERMINAL_MOVING_HP_PERCENT = 50;

enum class Target : std::uint8_t { Outpost, BaseFixed, BaseRandomFixed, BaseRandomMoving, BaseTerminalMoving };
enum class Struck : std::uint8_t { OutpostModule, BaseModule };
enum class Gate : std::uint8_t { Closed, Opening, Open, Closing };
enum class EventKind : std::uint8_t { ChanceGranted, GateOpening, GateOpen, GateClosing, GateClosed, TargetSelected, Launched };
enum class Miss : std::uint8_t {
    None, OutsideDetectionWindow, HitBlackout, ModuleMoving, NotSelectedTarget
};
enum class Error {
    InvalidChoice, NonIncreasingTime, GateNotClosed, GateCoolingDown, NoChance, GateNotOpen,
    NoDarts, SelectionLocked, NoDartInFlight, NegativeValue
};
std::string_view error_name(Error);
std::string_view target_name(Target);
template<class T> using Result = std::variant<T, Error>;
inline bool is_base(Target t) { return t != Target::Outpost; }
inline bool is_moving(Target t) { return t == Target::BaseRandomMoving || t == Target::BaseTerminalMoving; }

struct Event { I time_ms = 0; EventKind kind{}; I value = 0; };
struct Snapshot {
    I time_ms = 0;
    Gate gate = Gate::Closed;
    I gate_until_ms = -1;          // end of the current opening / launch / closing stage
    I chances = 0, chances_granted = 0, openings = 0;
    I cooldown_until_ms = -1;      // exclusive
    Target target = Target::Outpost; // ENGINEERING default before any selection
    I darts_left = DARTS_PER_ROUND, launched = 0, resolved = 0, hits = 0;
    I detect_from_ms = -1, detect_until_ms = -1; // detection window [from, until)
    I blackout_until_ms = -1;
    bool outpost_hit_this_opening = false, switched_this_opening = false;
    I fixed_base_hits = 0;         // hits on the base with a (random) fixed target
    bool base_armor_deployed = false;
    // Effects on the OPPOSING team, all exclusive ends.
    I occlusion_until_ms = -1;     // operator view / minimap / custom UI occluded
    I buff_suspended_until_ms = -1; // terrain crossing and energy buffs give no effect
    I outpost_point_disabled_until_ms = -1, base_point_disabled_until_ms = -1;
    bool operator==(const Snapshot&) const = default;
};
struct Step { Snapshot state; std::vector<Event> events; };
// Benefits of one detected hit besides its raw damage (pp111).
struct HitEffect {
    bool valid = false;
    Miss miss = Miss::None;
    Target target = Target::Outpost;
    I hit_number = 0;              // 1..4: valid hits of this station in the round
    I occlusion_ms = 0;            // added to the opponent's occlusion (stacks)
    I experience_tenths = 0;       // split equally among own living infantry
    I buff_suspend_ms = 0;         // opponent terrain/energy buffs: clock runs, no effect
    bool buffs_voided = false;     // opponent terrain/energy buffs are removed
    I max_hp_loss_percent = 0;     // every living opposing ground robot, of its max HP
    bool base_armor_deployed = false; // deployed by this hit
};
struct HitStep { Snapshot state; std::vector<Event> events; HitEffect effect; };

// HP removed from one robot by a moving-target hit: percent of its current
// max HP, rounded half-up (§5.6.4.1 p105 rounding).
Result<I> max_hp_loss(I max_hp, I percent);

// Every call first settles the gate timeline up to t (timers before inputs of
// the same millisecond); an error changes nothing.
class DartStation {
public:
    Result<Step> advance(I t);
    // Before opening the gate any target may be chosen. During one opening the
    // only change allowed is a single switch from "outpost" to a base target
    // after a dart of that opening has hit the outpost.
    Result<Step> select_target(I t, Target);
    Result<Step> open_gate(I t);
    Result<Step> launch(I t);
    // A launched dart was detected on a dart detection module. `module_static`
    // is false while a random/moving base target is still moving (such hits do
    // not count). Each call consumes one dart in flight; darts that hit nothing
    // are reported with missed().
    Result<HitStep> hit(I t, Struck, bool module_static = true);
    Result<Step> missed(I t);
    const Snapshot& snapshot() const noexcept { return state_; }
private:
    Snapshot state_;
    static void settle(Snapshot&, I t, std::vector<Event>&);
};
} // namespace rmuc2027::dart_v1
