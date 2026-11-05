#include "../rules/rmuc2027_economy_v1/economy_rules.hpp"
#include "rule_check.hpp"

namespace e = rmuc2027::economy_v1;
using I = std::int64_t;

int main() {
    // --- Table 5-6 schedule: 800 + 5*100 + 300 = 1600 over the round.
    CHECK(ok(e::scheduled_gold_between(-1, 0)) == 800);
    CHECK(ok(e::scheduled_gold_between(0, 59999)) == 0);
    CHECK(ok(e::scheduled_gold_between(0, 60000)) == 100);
    CHECK(ok(e::scheduled_gold_between(-1, 420000)) == 1600);
    CHECK(ok(e::scheduled_gold_between(300000, 360000)) == 300);
    CHECK(is_error(e::scheduled_gold_between(10, 5), e::Error::NonIncreasingTime));
    CHECK(is_error(e::scheduled_gold_between(-1, 420001), e::Error::TimeOutsideMatch));
    // Periodic streams (table 5-12 "每 10 秒"): first payment 10 s after completion.
    CHECK(ok(e::periodic_gold_between(50000, 10000, 40, -1, 59999)) == 0);
    CHECK(ok(e::periodic_gold_between(50000, 10000, 40, -1, 60000)) == 40);
    CHECK(ok(e::periodic_gold_between(50000, 10000, 40, 60000, 100000)) == 160);
    CHECK(ok(e::periodic_gold_between(50000, 10000, 40, 0, 420000)) == 37 * 40);
    {   // Splitting an interval anywhere never changes the total.
        I sum = 0;
        for (I t = 0; t < 420000; t += 777) sum += ok(e::periodic_gold_between(12345, 10000, 8, t, std::min<I>(t + 777, 420000)));
        CHECK(sum + ok(e::periodic_gold_between(12345, 10000, 8, -1, 0)) == ok(e::periodic_gold_between(12345, 10000, 8, -1, 420000)));
    }
    CHECK(is_error(e::periodic_gold_between(0, 0, 4, 0, 1), e::Error::InvalidChoice));

    // --- Tables 5-7/5-8/5-9 prices and units.
    CHECK(ok(e::minimum_unit(e::Ammo::Mm17, e::Channel::Local)) == 10 && ok(e::minimum_unit(e::Ammo::Mm42, e::Channel::Local)) == 1);
    CHECK(ok(e::minimum_unit(e::Ammo::Mm17, e::Channel::Remote)) == 100 && ok(e::minimum_unit(e::Ammo::Mm42, e::Channel::Remote)) == 10);
    CHECK(ok(e::ammo_cost(e::Ammo::Mm17, e::Channel::Local, 10)) == 10);
    CHECK(ok(e::ammo_cost(e::Ammo::Mm17, e::Channel::Local, 250)) == 250);
    CHECK(ok(e::ammo_cost(e::Ammo::Mm17, e::Channel::Remote, 100)) == 150);
    CHECK(ok(e::ammo_cost(e::Ammo::Mm17, e::Channel::Remote, 300)) == 450);
    CHECK(ok(e::ammo_cost(e::Ammo::Mm42, e::Channel::Local, 1)) == 10);
    CHECK(ok(e::ammo_cost(e::Ammo::Mm42, e::Channel::Local, 7)) == 70);
    CHECK(ok(e::ammo_cost(e::Ammo::Mm42, e::Channel::Remote, 10)) == 150);
    CHECK(is_error(e::ammo_cost(e::Ammo::Mm17, e::Channel::Local, 15), e::Error::InvalidUnit));
    CHECK(is_error(e::ammo_cost(e::Ammo::Mm17, e::Channel::Remote, 50), e::Error::InvalidUnit));
    CHECK(is_error(e::ammo_cost(e::Ammo::Mm42, e::Channel::Remote, 5), e::Error::InvalidUnit));
    CHECK(is_error(e::ammo_cost(e::Ammo::Mm17, e::Channel::Local, 0), e::Error::InvalidUnit));
    CHECK(is_error(e::ammo_cost(e::Ammo::Mm17, e::Channel::Local, -10), e::Error::NegativeValue));
    // HP and instant revival prices.
    CHECK(ok(e::remote_hp_cost(0)) == 50);
    CHECK(ok(e::remote_hp_cost(1)) == 51);            // any started fraction rounds up
    CHECK(ok(e::remote_hp_cost(60000)) == 70);
    CHECK(ok(e::remote_hp_cost(90000)) == 80);
    CHECK(ok(e::remote_hp_cost(420000)) == 190);
    CHECK(ok(e::instant_revival_cost(0, 1)) == 20);
    CHECK(ok(e::instant_revival_cost(1, 1)) == 100);
    CHECK(ok(e::instant_revival_cost(60000, 5)) == 180);
    CHECK(ok(e::instant_revival_cost(300000, 2)) == 440); // remaining 120 s: 5*80+40
    CHECK(ok(e::instant_revival_cost(420000, 10)) == 760);
    CHECK(is_error(e::instant_revival_cost(0, 0), e::Error::InvalidLevel));
    // Sentry supply: p77 example - never claimed, remaining 30 s -> 600.
    CHECK(ok(e::sentry_supply_available(390000, 0)) == 600);
    CHECK(ok(e::sentry_supply_available(59999, 0)) == 0 && ok(e::sentry_supply_available(60000, 0)) == 100);
    CHECK(ok(e::sentry_supply_available(180000, 200)) == 100);
    CHECK(is_error(e::sentry_supply_available(60000, 200), e::Error::InsufficientAllowance));

    // --- Wallet: atomic purchases, team caps, remote delay.
    {
        e::TeamWallet w;
        CHECK(ok(w.settle(0)) == 800 && w.snapshot().gold == 800);
        CHECK(ok(w.settle(0)) == 0);
        auto p = ok(w.buy_ammo(1000, e::Ammo::Mm17, e::Channel::Local, 200));
        CHECK(p.cost == 200 && p.effective_ms == 1000 && w.snapshot().gold == 600);
        p = ok(w.buy_ammo(2000, e::Ammo::Mm17, e::Channel::Remote, 300));
        CHECK(p.cost == 450 && p.effective_ms == 8000 && w.snapshot().gold == 150);
        const auto before = w.snapshot();
        CHECK(is_error(w.buy_ammo(3000, e::Ammo::Mm42, e::Channel::Remote, 20), e::Error::InsufficientGold)); // 300 > 150
        CHECK(w.snapshot() == before);
        CHECK(ok(w.settle(420000)) == 800);
        CHECK(is_error(w.settle(1000), e::Error::NonIncreasingTime));
        CHECK(ok(w.add_gold(4000)).gold == 4950);
        CHECK(ok(w.buy_ammo(420000, e::Ammo::Mm17, e::Channel::Local, 500)).cost == 500);
        CHECK(is_error(w.buy_ammo(420000, e::Ammo::Mm17, e::Channel::Local, 10), e::Error::TeamCapExceeded));
        CHECK(w.snapshot().purchased_17mm == 1000);
        CHECK(ok(w.buy_ammo(420000, e::Ammo::Mm42, e::Channel::Local, 100)).cost == 1000);
        CHECK(is_error(w.buy_ammo(420000, e::Ammo::Mm42, e::Channel::Local, 1), e::Error::TeamCapExceeded));
        CHECK(ok(w.spend(190)).gold == 4950 - 500 - 1000 - 190);
        CHECK(is_error(w.spend(1'000'000), e::Error::InsufficientGold));
        CHECK(w.snapshot().total_income == 1600 + 4000 && w.snapshot().total_spent == 200 + 450 + 500 + 1000 + 190);
        CHECK(w.snapshot().gold == w.snapshot().total_income - w.snapshot().total_spent);
    }
    // --- Robot allowance with delayed delivery and over-fire record.
    {
        auto a = ok(e::RobotAllowance::start(300, 0));
        CHECK(ok(a.consume(1000, e::Ammo::Mm17, 10)).mm17 == 290);
        ok(a.deliver(2000, e::Ammo::Mm42, 10, 8000));
        CHECK(a.available(e::Ammo::Mm42) == 0);
        CHECK(is_error(a.consume(7999, e::Ammo::Mm42, 1), e::Error::InsufficientAllowance));
        CHECK(a.snapshot().pending.size() == 1);
        CHECK(ok(a.consume(8000, e::Ammo::Mm42, 1)).mm42 == 9);
        CHECK(a.snapshot().pending.empty());
        CHECK(ok(a.record_overfire(9000, 2)).overfired_42mm == 2);
        CHECK(is_error(a.consume(8500, e::Ammo::Mm17, 1), e::Error::NonIncreasingTime));
        CHECK(is_error(e::RobotAllowance::start(-1, 0), e::Error::NegativeValue));
    }
    return finish("economy rules (tables 5-6..5-9)");
}
