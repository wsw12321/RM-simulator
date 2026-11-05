#pragma once
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

// Source-linked position control lifecycle (table 5-21) including the parts the
// capture_v1 module deliberately leaves out: temporary-control reversion,
// outpost destruction conversions and the 500 HP outpost rebuild (§5.6.1).
// RMUC 2027 V1.0.0 §5.6.1 (pp94-95), §5.6.2 tables 5-20/5-21 (p96).
// Input is the logical activation of any constituent buff point per team
// (2 s deactivation delay is an upstream concern, see capture_v1).
namespace rmuc2027::position_v1 {
inline constexpr std::string_view MODULE_VERSION = "rmuc2027-position-v1.0.0-module.1";
inline constexpr std::string_view SOURCE_VERSION = "RMUC2027_CN_V1.0.0_20261015";
inline constexpr std::string_view SOURCE_SHA256 = "9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be";
using I = std::int64_t;
inline constexpr I CONVERSION_MS = 5000;
inline constexpr I REBUILD_HP = 500;

enum class Team : std::uint8_t { Red, Blue };
enum class Kind : std::uint8_t { Outpost, Base };
enum class Degree : std::uint8_t { Temporary, Full };
enum class Transition : std::uint8_t {
    CapturedTemporary,     // row 1: challenger activated 5 s (progress regresses / pauses)
    RevertedFull,          // row 2: temporary controller absent 5 s -> other team full
    DestroyedToTemporary,  // row 3: outpost on a fully controlled position destroyed
    DestroyedToFull        // row 4: temporary controller destroyed the outpost
};
enum class Error { InvalidConfig, NonIncreasingTime, NotOutpost, OutpostNotAlive, InvalidDestroyer };
std::string_view error_name(Error);
template<class T> using Result = std::variant<T, Error>;

inline Team other(Team t) { return t == Team::Red ? Team::Blue : Team::Red; }
struct Activation {
    bool red = false, blue = false;
    bool of(Team t) const { return t == Team::Red ? red : blue; }
    bool operator==(const Activation&) const = default;
};
struct Event {
    I time_ms = 0;
    Transition transition{};
    Team controller{};
    Degree degree{};
    bool rebuilt = false;   // outpost restored alive with 500 HP for `controller`
};
struct Snapshot {
    I time_ms = 0;
    Kind kind = Kind::Outpost;
    Team original_owner = Team::Red;
    Team controller = Team::Red;
    Degree degree = Degree::Full;
    Activation active;
    I progress_ms = 0;  // challenger progress toward temporary control
    I absence_ms = 0;   // temporary controller's continuous absence
    bool outpost_alive = true;
    Team outpost_owner = Team::Red;
    I transitions = 0;
    bool operator==(const Snapshot&) const = default;
};
struct Step { Snapshot state; std::vector<Event> events; };

class Position {
public:
    // §5.6.2: each team starts with full control of its own positions.
    static Result<Position> start(Kind, Team original_owner);
    // Time (last, t] passes under the previously supplied activation; transitions
    // happen at the exact millisecond their 5 s accumulate. `next` then applies.
    Result<Step> advance(I t, Activation next);
    // An outpost structure on this position reached 0 HP at t (settle time first).
    Result<Step> outpost_destroyed(I t, Team destroyer);
    const Snapshot& snapshot() const noexcept { return state_; }
    // Base defence (§5.6.1): own outpost position fully controlled by its owner.
    bool full_by(Team t) const { return state_.controller == t && state_.degree == Degree::Full; }
private:
    Snapshot state_;
    explicit Position(Snapshot s) : state_(s) {}
};
} // namespace rmuc2027::position_v1
