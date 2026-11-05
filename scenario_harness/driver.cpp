#include "match_rules.hpp"
#include <charconv>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef SCENARIO_BUILD_ID
#error SCENARIO_BUILD_ID must identify the adapter and rule sources
#endif

// This executable consumes a private, strict token protocol from cli.py.
// It never schedules scoring, derives control from occupancy, or derives damage.
namespace {
using namespace rmuc2027::match_v1;
constexpr std::size_t MAX_EVENTS = 4096, MAX_CREDITS = 8192;
constexpr std::string_view PROTOCOL = "rmuc2027-scenario-driver-v1";
struct InputError : std::runtime_error { using std::runtime_error::runtime_error; };
struct Tokens {
    std::istringstream in;
    explicit Tokens(const std::string& line) : in(line) {}
    std::string word() {
        std::string s;
        if (!(in >> s)) throw InputError("missing protocol token");
        return s;
    }
    void require(std::string_view expected) {
        if (word() != expected) throw InputError("unexpected protocol token");
    }
    template<class T> T number(T minimum, T maximum) {
        auto s = word(); T value{};
        auto [end, ec] = std::from_chars(s.data(), s.data()+s.size(), value);
        if (ec != std::errc{} || end != s.data()+s.size() || value < minimum || value > maximum)
            throw InputError("invalid protocol integer");
        return value;
    }
    std::optional<std::int64_t> optional() {
        auto s = word();
        if (s == "?") return std::nullopt;
        std::int64_t value{};
        auto [end, ec] = std::from_chars(s.data(), s.data()+s.size(), value);
        if (ec != std::errc{} || end != s.data()+s.size() || value < 0)
            throw InputError("invalid optional protocol integer");
        return value;
    }
    void done() { std::string extra; if (in >> extra) throw InputError("extra protocol token"); }
};
std::string line() {
    // Bound memory before parsing, even when invoked without the Python adapter.
    std::string s;
    for (char c; std::cin.get(c);) {
        if (c == '\n') return s;
        if (s.size() == 1024*1024) throw InputError("protocol line too large");
        s += c;
    }
    if (!s.empty()) return s;
    throw InputError("truncated protocol input");
}
struct Boundary {
    std::uint64_t sequence;
    MatchSnapshot snapshot;
    std::vector<ExternalOneSecondCredit> credits;
};
std::pair<bool, std::vector<Boundary>> parse() {
    Tokens header(line()); header.require("BEGIN"); header.require(PROTOCOL);
    auto mode = header.word();
    if (mode != "external_totals" && mode != "manual_one_second_credits") throw InputError("invalid control mode");
    bool manual = mode == "manual_one_second_credits";
    auto count = header.number<std::size_t>(1, MAX_EVENTS); header.done();
    std::vector<Boundary> events;
    std::int64_t previous_time = -1;
    std::size_t total_credits = 0;
    for (std::size_t i = 0; i < count; ++i) {
        Tokens t(line()); t.require("BOUNDARY");
        Boundary b{};
        b.sequence = t.number<std::uint64_t>(i+1, i+1);
        b.snapshot.elapsed_ms = t.number<std::int64_t>(0, MATCH_DURATION_MS);
        if (b.snapshot.elapsed_ms < previous_time) throw InputError("boundary time regression");
        previous_time = b.snapshot.elapsed_ms;
        auto credits = t.number<std::size_t>(0, MAX_CREDITS-total_credits);
        total_credits += credits;
        if (!manual && credits) throw InputError("credits conflict with external totals");
        auto ruling = t.word();
        if (ruling == "none") b.snapshot.referee_ruling = RefereeRuling::None;
        else if (ruling == "round_forfeit") b.snapshot.referee_ruling = RefereeRuling::RoundForfeit;
        else if (ruling == "other") b.snapshot.referee_ruling = RefereeRuling::Other;
        else throw InputError("invalid referee ruling");
        b.snapshot.simultaneous_destruction_resolved = t.number<int>(0, 1) != 0;
        for (auto& team : b.snapshot.teams) {
            team.base_hp = t.number<std::int64_t>(0, std::numeric_limits<std::int64_t>::max());
            team.control_score = t.optional(); team.attack_damage = t.optional(); team.surviving_robot_hp = t.optional();
            if (manual && team.control_score) throw InputError("external score conflicts with manual ledger");
        }
        t.done();
        for (std::size_t c = 0; c < credits; ++c) {
            Tokens ct(line()); ct.require("CREDIT"); ExternalOneSecondCredit credit{};
            credit.sequence = ct.number<std::uint64_t>(1, std::numeric_limits<std::uint64_t>::max());
            for (auto& position : credit.adjudicated_control.positions) {
                auto team = ct.word(), degree = ct.word();
                if (team == "red") position.controller = Team::Red;
                else if (team == "blue") position.controller = Team::Blue;
                else throw InputError("invalid control team");
                if (degree == "temporary") position.degree = ControlDegree::Temporary;
                else if (degree == "full") position.degree = ControlDegree::Full;
                else throw InputError("invalid control degree");
            }
            ct.done(); b.credits.push_back(credit);
        }
        events.push_back(b);
    }
    Tokens end(line()); end.require("END"); end.done();
    // Extra lines, including blank lines, are not ignored.
    if (std::cin.peek() != std::char_traits<char>::eof()) throw InputError("trailing protocol data");
    return {manual, events};
}
void optional_json(std::ostream& o, std::optional<std::int64_t> n) { if (n) o << *n; else o << "null"; }
void totals_json(std::ostream& o, const TeamTotals& t) {
    o << "{\"base_hp\":" << t.base_hp << ",\"control_score\":"; optional_json(o,t.control_score);
    o << ",\"attack_damage\":"; optional_json(o,t.attack_damage);
    o << ",\"surviving_robot_hp\":"; optional_json(o,t.surviving_robot_hp); o << '}';
}
void snapshot_json(std::ostream& o, const MatchSnapshot& s) {
    o << "{\"elapsed_ms\":" << s.elapsed_ms << ",\"red\":"; totals_json(o,s.teams[0]);
    o << ",\"blue\":"; totals_json(o,s.teams[1]);
    const char* ruling = s.referee_ruling == RefereeRuling::None ? "none" : s.referee_ruling == RefereeRuling::RoundForfeit ? "round_forfeit" : "other";
    o << ",\"referee_ruling\":\"" << ruling << "\",\"simultaneous_destruction_resolved\":"
      << (s.simultaneous_destruction_resolved ? "true" : "false") << '}';
}
std::string_view outcome_name(Outcome o) {
    switch(o) { case Outcome::Ongoing: return "ongoing"; case Outcome::RedWins: return "red_wins";
        case Outcome::BlueWins: return "blue_wins"; case Outcome::Draw: return "draw"; }
    throw std::logic_error("unmapped outcome");
}
std::string_view reason_name(Reason r) {
    switch(r) {
        case Reason::NotEnded: return "not_ended"; case Reason::BaseHp: return "base_hp";
        case Reason::TimeoutControl: return "timeout_control"; case Reason::TimeoutAttackDamage: return "timeout_attack_damage";
        case Reason::TimeoutSurvivingRobotHp: return "timeout_surviving_robot_hp"; case Reason::TimeoutEqual: return "timeout_equal";
        case Reason::ResolvedBothBasesZero: return "resolved_both_bases_zero";
    }
    throw std::logic_error("unmapped reason");
}
void decision_json(std::ostream& o, const Decision& d) {
    o << "{\"outcome\":\"" << outcome_name(d.outcome) << "\",\"reason\":\"" << reason_name(d.reason)
      << "\",\"rule_id\":\"" << d.rule_id << "\"}";
}
struct State {
    MatchJudge judge;
    ExternalControlLedger ledger;
    bool sealed = false;
};
void state_json(std::ostream& o, const State& s, bool manual) {
    o << "{\"last_elapsed_ms\":" << s.judge.last_elapsed_ms() << ",\"terminal\":" << (s.judge.final_decision() ? "true" : "false")
      << ",\"ledger\":";
    if (manual) o << "{\"totals\":[" << s.ledger.totals()[0] << ',' << s.ledger.totals()[1]
                  << "],\"last_credit_sequence\":" << s.ledger.last_sequence() << ",\"sealed\":" << (s.sealed ? "true" : "false") << '}';
    else o << "null";
    o << ",\"final_snapshot\":";
    if (s.judge.final_snapshot()) snapshot_json(o,*s.judge.final_snapshot()); else o << "null";
    o << ",\"final_decision\":";
    if (s.judge.final_decision()) decision_json(o,*s.judge.final_decision()); else o << "null";
    o << '}';
}
void driver_json(std::ostream& out) {
    out << "{\"protocol\":\"" << PROTOCOL << "\",\"implementation_sha256\":\"" << SCENARIO_BUILD_ID
        << "\",\"module_version\":\"" << MODULE_VERSION << "\",\"source_version\":\"" << SOURCE_VERSION
        << "\",\"source_sha256\":\"" << SOURCE_SHA256 << "\"}";
}
void run(bool manual, const std::vector<Boundary>& events) {
    State state;
    std::ostringstream out;
    out << "{\"driver\":"; driver_json(out); out << ",\"records\":[";
    bool first = true;
    for (const auto& b : events) {
        if (!first) out << ',';
        first = false;
        out << "{\"sequence\":" << b.sequence << ",\"before\":"; state_json(out,state,manual);
        State candidate = state;
        MatchSnapshot resolved = b.snapshot;
        std::optional<Error> error;
        const char* error_source = "judge";
        std::ostringstream credits;
        credits << '[';
        bool first_credit = true;
        // A terminal judge rejects the entire transaction before any credit is attempted.
        if (state.judge.final_decision()) {
            auto result = candidate.judge.update(resolved);
            error = std::get<Error>(result);
        } else {
            for (const auto& c : b.credits) {
                if (!first_credit) credits << ',';
                first_credit = false;
                auto applied = candidate.ledger.apply(c);
                credits << "{\"sequence\":" << c.sequence;
                if (auto e = std::get_if<Error>(&applied)) {
                    error = *e; error_source = "credit";
                    credits << ",\"error\":\"" << error_name(*e) << "\"}";
                    break;
                }
                auto rate = std::get<ControlRate>(applied);
                credits << ",\"points\":[" << rate.points_per_second[0] << ',' << rate.points_per_second[1]
                        << "],\"candidate_totals\":[" << candidate.ledger.totals()[0] << ',' << candidate.ledger.totals()[1] << "]}";
            }
        }
        credits << ']';
        out << ",\"credit_attempts\":" << credits.str() << ",\"resolved_snapshot\":";
        std::optional<Decision> decision;
        if (!error) {
            if (manual) for (std::size_t i=0; i<2; ++i) resolved.teams[i].control_score = candidate.ledger.totals()[i];
            snapshot_json(out,resolved);
            auto evaluated = candidate.judge.update(resolved);
            if (auto e = std::get_if<Error>(&evaluated)) error = *e;
            else {
                decision = std::get<Decision>(evaluated);
                if (decision->outcome != Outcome::Ongoing) { candidate.ledger.seal(); candidate.sealed = true; }
                state = candidate;
            }
        } else out << "null";
        if (error) out << ",\"status\":\"rejected\",\"error\":\"" << error_name(*error) << "\",\"error_source\":\"" << error_source << '"';
        else { out << ",\"status\":\"accepted\",\"decision\":"; decision_json(out,*decision); }
        out << ",\"after\":"; state_json(out,state,manual); out << '}';
    }
    out << "],\"final_state\":"; state_json(out,state,manual); out << '}';
    std::cout << out.str() << '\n';
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--identity") { driver_json(std::cout); std::cout << '\n'; return 0; }
        if (argc != 1) throw InputError("unsupported argument");
        auto [manual, events] = parse(); run(manual, events);
    }
    catch (const InputError& e) { std::cerr << "ProtocolInputError: " << e.what() << '\n'; return 2; }
    catch (const std::exception& e) { std::cerr << "DriverError: " << e.what() << '\n'; return 2; }
}
