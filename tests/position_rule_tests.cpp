#include "../rules/rmuc2027_position_v1/position_rules.hpp"
#include "rule_check.hpp"

namespace p = rmuc2027::position_v1;
using I = std::int64_t;
using p::Team;

static p::Position make(p::Kind k = p::Kind::Outpost, Team owner = Team::Red) { return std::get<p::Position>(p::Position::start(k, owner)); }
static p::Step go(p::Position& x, I t, bool red, bool blue) { return ok(x.advance(t, {red, blue}), "advance"); }

int main() {
    {   // Row 1: 5 s continuous activation by the challenger -> temporary control.
        auto x = make();
        CHECK(x.full_by(Team::Red) && x.snapshot().outpost_alive && x.snapshot().outpost_owner == Team::Red);
        go(x, 0, false, true);
        CHECK(go(x, 4999, false, true).events.empty() && x.snapshot().progress_ms == 4999);
        const auto s = go(x, 5000, false, true);
        CHECK(s.events.size() == 1 && s.events[0].transition == p::Transition::CapturedTemporary && s.events[0].time_ms == 5000);
        CHECK(x.snapshot().controller == Team::Blue && x.snapshot().degree == p::Degree::Temporary && !x.full_by(Team::Red));
    }
    {   // A long interval transitions at the exact millisecond.
        auto x = make();
        go(x, 1000, false, true);
        const auto s = go(x, 60000, false, true);
        CHECK(s.events.size() == 1 && s.events[0].time_ms == 6000 && x.snapshot().absence_ms == 0);
    }
    {   // Uniform regression and pause while both activate.
        auto x = make();
        go(x, 0, false, true);
        go(x, 3000, false, false);           // 3 s of progress, then leave
        go(x, 4000, false, false);
        CHECK(x.snapshot().progress_ms == 2000);
        go(x, 4000, true, true);             // both present: paused
        go(x, 9000, false, true);
        CHECK(x.snapshot().progress_ms == 2000);
        CHECK(go(x, 11999, false, true).events.empty());
        CHECK(go(x, 12000, false, true).events.size() == 1);
        auto y = make();
        go(y, 0, false, true);
        go(y, 2000, false, false);
        go(y, 10000, false, false);
        CHECK(y.snapshot().progress_ms == 0); // floors at zero
        // The defender alone does not regress faster or block progress forever.
        auto z = make();
        go(z, 0, true, false);
        go(z, 5000, true, false);
        CHECK(z.snapshot().progress_ms == 0 && z.full_by(Team::Red));
    }
    {   // Row 2: temporary controller absent 5 s -> owner regains full control.
        auto x = make();
        go(x, 0, false, true);
        go(x, 5000, false, true);
        go(x, 6000, false, false);           // blue leaves at 6 s
        go(x, 9000, false, true);            // returns at 9 s: absence resets
        CHECK(x.snapshot().absence_ms == 0);
        go(x, 10000, false, false);
        CHECK(go(x, 14999, false, false).events.empty());
        const auto s = go(x, 15000, false, false);
        CHECK(s.events.size() == 1 && s.events[0].transition == p::Transition::RevertedFull && !s.events[0].rebuilt);
        CHECK(x.full_by(Team::Red) && x.snapshot().outpost_alive);
        // The original owner's presence is irrelevant to the reversion timer.
        auto y = make();
        go(y, 0, false, true);
        go(y, 5000, true, false);
        CHECK(go(y, 10000, true, false).events.size() == 1 && y.full_by(Team::Red));
    }
    {   // Row 3: destroying the outpost of a fully controlled position.
        auto x = make();
        go(x, 7000, false, false);
        CHECK(is_error(x.outpost_destroyed(6000, Team::Blue), p::Error::NonIncreasingTime));
        CHECK(is_error(x.outpost_destroyed(7000, Team::Red), p::Error::InvalidDestroyer));
        const auto s = ok(x.outpost_destroyed(7000, Team::Blue));
        CHECK(s.events.size() == 1 && s.events[0].transition == p::Transition::DestroyedToTemporary);
        CHECK(x.snapshot().controller == Team::Red && x.snapshot().degree == p::Degree::Temporary && !x.snapshot().outpost_alive);
        CHECK(!x.full_by(Team::Red)); // red base loses its 100% defence
        CHECK(is_error(x.outpost_destroyed(7000, Team::Blue), p::Error::OutpostNotAlive));
        // Red keeps activating: control stays temporary with a destroyed outpost.
        go(x, 7000, true, false);
        go(x, 20000, true, false);
        CHECK(x.snapshot().degree == p::Degree::Temporary && x.snapshot().controller == Team::Red);
        // Red leaves for 5 s: blue gains full control; outpost rebuilt for blue (500 HP).
        go(x, 20000, false, false);
        const auto r = go(x, 25000, false, false);
        CHECK(r.events.size() == 1 && r.events[0].rebuilt && r.events[0].controller == Team::Blue);
        CHECK(x.full_by(Team::Blue) && x.snapshot().outpost_alive && x.snapshot().outpost_owner == Team::Blue);
    }
    {   // Row 4: temporary controller destroys the outpost -> full control + rebuild.
        auto x = make();
        go(x, 0, false, true);
        go(x, 5000, false, true);
        const auto s = ok(x.outpost_destroyed(5000, Team::Blue));
        CHECK(s.events.size() == 1 && s.events[0].transition == p::Transition::DestroyedToFull && s.events[0].rebuilt);
        CHECK(x.full_by(Team::Blue) && x.snapshot().outpost_owner == Team::Blue && x.snapshot().outpost_alive);
        // The original owner can take it back the same way.
        go(x, 6000, true, false);
        CHECK(go(x, 11000, true, false).events.size() == 1 && x.snapshot().controller == Team::Red);
        CHECK(is_error(x.outpost_destroyed(11000, Team::Blue), p::Error::InvalidDestroyer));
        const auto back = ok(x.outpost_destroyed(11000, Team::Red));
        CHECK(back.events[0].transition == p::Transition::DestroyedToFull && x.full_by(Team::Red) && x.snapshot().outpost_owner == Team::Red);
        CHECK(x.snapshot().transitions == 4);
    }
    {   // Base positions: only the original owner ever holds full control.
        auto b = make(p::Kind::Base, Team::Blue);
        CHECK(!b.snapshot().outpost_alive);
        CHECK(is_error(b.outpost_destroyed(0, Team::Red), p::Error::NotOutpost));
        go(b, 0, true, false);
        go(b, 5000, true, false);
        CHECK(b.snapshot().controller == Team::Red && b.snapshot().degree == p::Degree::Temporary);
        go(b, 5000, false, false);
        go(b, 10000, false, false);
        CHECK(b.full_by(Team::Blue));
        CHECK(is_error(b.advance(9999, {}), p::Error::NonIncreasingTime));
        CHECK(is_error(p::Position::start(static_cast<p::Kind>(7), Team::Red), p::Error::InvalidConfig));
    }
    {   // Randomised invariants over long sequences of activations and destructions.
        std::uint64_t seed = 42;
        auto next = [&] { seed = seed * 6364136223846793005ull + 1442695040888963407ull; return seed >> 33; };
        for (int run = 0; run < 400; ++run) {
            auto x = make(next() % 3 ? p::Kind::Outpost : p::Kind::Base, next() % 2 ? Team::Red : Team::Blue);
            I t = 0;
            for (int i = 0; i < 300; ++i) {
                t += I(next() % 4000);
                const auto before = x.snapshot();
                go(x, t, next() % 2, next() % 2);
                if (x.snapshot().kind == p::Kind::Outpost && x.snapshot().outpost_alive && next() % 6 == 0)
                    (void)x.outpost_destroyed(t, p::other(x.snapshot().outpost_owner));
                const auto& s = x.snapshot();
                CHECK(s.progress_ms >= 0 && s.progress_ms < p::CONVERSION_MS && s.absence_ms >= 0 && s.absence_ms < p::CONVERSION_MS);
                CHECK(s.transitions >= before.transitions);
                if (s.kind == p::Kind::Base) CHECK(s.degree == p::Degree::Temporary || s.controller == s.original_owner);
                if (s.kind == p::Kind::Outpost && s.degree == p::Degree::Full) CHECK(s.outpost_alive && s.outpost_owner == s.controller);
                if (s.degree == p::Degree::Temporary) CHECK(s.progress_ms == 0);
            }
        }
    }
    return finish("position control rules (tables 5-20/5-21, §5.6.1 rebuild)");
}
