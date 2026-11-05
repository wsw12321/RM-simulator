#include "position_rules.hpp"
#include <algorithm>

namespace rmuc2027::position_v1 {
std::string_view error_name(Error e) {
    switch (e) {
    case Error::InvalidConfig: return "InvalidConfig";
    case Error::NonIncreasingTime: return "NonIncreasingTime";
    case Error::NotOutpost: return "NotOutpost";
    case Error::OutpostNotAlive: return "OutpostNotAlive";
    case Error::InvalidDestroyer: return "InvalidDestroyer";
    }
    return "InvalidError";
}

Result<Position> Position::start(Kind kind, Team owner) {
    if ((kind != Kind::Outpost && kind != Kind::Base) || (owner != Team::Red && owner != Team::Blue)) return Error::InvalidConfig;
    Snapshot s;
    s.kind = kind;
    s.original_owner = s.controller = s.outpost_owner = owner;
    s.outpost_alive = kind == Kind::Outpost;
    return Position(s);
}

namespace {
// Becoming fully controlled while the outpost is down rebuilds it for the new
// controller (ENGINEERING reading of "通过前哨站被击毁的方式 ... 转化").
void make_full(Snapshot& s, Team to, I t, Transition why, std::vector<Event>& events) {
    s.controller = to;
    s.degree = Degree::Full;
    s.progress_ms = s.absence_ms = 0;
    bool rebuilt = false;
    if (s.kind == Kind::Outpost && (!s.outpost_alive || s.outpost_owner != to)) {
        s.outpost_alive = true;
        s.outpost_owner = to;
        rebuilt = true;
    }
    ++s.transitions;
    events.push_back({t, why, to, Degree::Full, rebuilt});
}
}

Result<Step> Position::advance(I t, Activation next) {
    if (t < state_.time_ms) return Error::NonIncreasingTime;
    Step out{state_, {}};
    Snapshot& s = out.state;
    I now = s.time_ms;
    while (now < t) {
        const I dt = t - now;
        if (s.degree == Degree::Full) {
            const Team challenger = other(s.controller);
            const bool c = s.active.of(challenger), d = s.active.of(s.controller);
            if (c && !d) {
                const I need = CONVERSION_MS - s.progress_ms;
                if (need <= dt) {
                    now += need;
                    s.controller = challenger;
                    s.degree = Degree::Temporary;
                    s.progress_ms = s.absence_ms = 0;
                    ++s.transitions;
                    out.events.push_back({now, Transition::CapturedTemporary, challenger, Degree::Temporary, false});
                    continue;
                }
                s.progress_ms += dt;
            } else if (!c) {
                s.progress_ms = std::max<I>(0, s.progress_ms - dt); // uniform regression
            } // both active: paused
            now = t;
        } else {
            if (s.active.of(s.controller)) { s.absence_ms = 0; now = t; continue; }
            const I need = CONVERSION_MS - s.absence_ms;
            if (need <= dt) {
                now += need;
                make_full(s, other(s.controller), now, Transition::RevertedFull, out.events);
                continue;
            }
            s.absence_ms += dt;
            now = t;
        }
    }
    s.time_ms = t;
    // A newly present temporary controller resets its absence immediately.
    s.active = next;
    if (s.degree == Degree::Temporary && s.active.of(s.controller)) s.absence_ms = 0;
    state_ = s;
    return out;
}

Result<Step> Position::outpost_destroyed(I t, Team destroyer) {
    if (state_.kind != Kind::Outpost) return Error::NotOutpost;
    if (t != state_.time_ms) return Error::NonIncreasingTime; // advance(t) first
    if (!state_.outpost_alive) return Error::OutpostNotAlive;
    if (destroyer == state_.outpost_owner) return Error::InvalidDestroyer;
    Step out{state_, {}};
    Snapshot& s = out.state;
    s.outpost_alive = false;
    if (s.degree == Degree::Full) {
        // Row 3: full control by the outpost's owner becomes temporary control.
        s.degree = Degree::Temporary;
        s.progress_ms = s.absence_ms = 0;
        ++s.transitions;
        out.events.push_back({t, Transition::DestroyedToTemporary, s.controller, Degree::Temporary, false});
    } else if (s.controller == destroyer) {
        // Row 4: the temporary controller destroyed it -> full control + rebuild.
        make_full(s, destroyer, t, Transition::DestroyedToFull, out.events);
    }
    // Otherwise the temporary controller owned the destroyed outpost (unreachable
    // through table 5-21 paths); control is unchanged and only the outpost falls.
    state_ = s;
    return out;
}
} // namespace rmuc2027::position_v1
