#include "match_rules.hpp"
#include <iostream>
using namespace rmuc2027::match_v1;
int main() {
    // This is a rule fixture, NOT a simulation of how these totals were earned.
    MatchSnapshot snapshot{420000, {{{9000, 10, 100, 500}, {1000, 11, 0, 1}}}};
    auto result = evaluate(snapshot);
    if (auto error = std::get_if<Error>(&result)) {
        std::cerr << error_name(*error) << '\n';
        return 1;
    }
    auto decision = std::get<Decision>(result);
    std::cout << MODULE_VERSION << '\n'
              << "Fixture: timeout; red base HP 9000, control 10; blue base HP 1000, control 11.\n"
              << "Winner: " << (decision.outcome == Outcome::BlueWins ? "blue" : "unexpected")
              << "; rule: " << decision.rule_id << " (manual printed p114).\n"
              << "The browser sandbox is unchanged. This module has no automatic control-scoring scheduler.\n";
}
