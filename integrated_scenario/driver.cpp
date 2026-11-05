#include "capture_rules.hpp"
#include "match_rules.hpp"
#include <charconv>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef INTEGRATED_BUILD_ID
#error INTEGRATED_BUILD_ID must identify the adapter, rules and provenance
#endif

namespace {
namespace c = rmuc2027::capture_v1;
namespace m = rmuc2027::match_v1;
constexpr std::string_view PROTOCOL = "rmuc2027-integrated-driver-v1";
constexpr std::string_view PHASE = "ENGINEERING-NOT-OFFICIAL-capture-continuation-credit-judge-v1";
constexpr std::string_view ASSERTION = "TEMPORARY_CONTROL_UNCHANGED_NO_RESTORATION_OR_OUTPOST_LIFECYCLE_TRANSITION";
constexpr std::array<std::string_view,4> POSITIONS = {"red_outpost", "blue_outpost", "red_base", "blue_base"};
constexpr std::size_t MAX_EVENTS=512, MAX_CREDITS=4096;
struct InputError : std::runtime_error { using std::runtime_error::runtime_error; };
struct Tokens {
    std::istringstream in;
    explicit Tokens(const std::string& line): in(line) {}
    std::string word() { std::string s; if (!(in>>s)) throw InputError("missing protocol token"); return s; }
    void require(std::string_view s) { if(word()!=s) throw InputError("unexpected protocol token"); }
    template<class T> T number(T low,T high) {
        auto s=word(); T n{}; auto [end,ec]=std::from_chars(s.data(),s.data()+s.size(),n);
        if(ec!=std::errc{} || end!=s.data()+s.size() || n<low || n>high) throw InputError("invalid protocol integer");
        return n;
    }
    bool boolean() { return number<int>(0,1)!=0; }
    std::optional<std::int64_t> optional() {
        auto s=word(); if(s=="?") return std::nullopt;
        std::int64_t n{}; auto [end,ec]=std::from_chars(s.data(),s.data()+s.size(),n);
        if(ec!=std::errc{} || end!=s.data()+s.size() || n<0) throw InputError("invalid optional protocol integer");
        return n;
    }
    void done() { std::string s; if(in>>s) throw InputError("extra protocol token"); }
};
std::string line() {
    std::string s;
    for(char ch;std::cin.get(ch);) { if(ch=='\n') return s; if(s.size()==16384) throw InputError("protocol line too large"); s+=ch; }
    if(!s.empty()) return s;
    throw InputError("truncated protocol input");
}
struct Continuation { std::int64_t from,through; };
struct Boundary {
    std::uint64_t sequence{};
    m::MatchSnapshot snapshot{};
    std::array<std::optional<c::LogicalActivation>,4> inputs;
    std::array<std::optional<Continuation>,4> continuations;
    std::vector<std::uint64_t> credits;
};
struct Scenario { std::array<c::LogicalActivation,4> initial; std::vector<Boundary> events; };
Scenario parse() {
    Tokens h(line()); h.require("BEGIN"); h.require(PROTOCOL); h.require(PHASE);
    auto count=h.number<std::size_t>(1,MAX_EVENTS); h.done();
    Scenario s;
    Tokens initial(line()); initial.require("INITIAL");
    for(auto& a:s.initial) a={initial.boolean(),initial.boolean()};
    initial.done();
    std::int64_t last=0; std::size_t total_credits=0;
    for(std::size_t i=0;i<count;++i) {
        Boundary b; Tokens t(line()); t.require("BOUNDARY");
        b.sequence=t.number<std::uint64_t>(i+1,i+1);
        b.snapshot.elapsed_ms=t.number<std::int64_t>(1,m::MATCH_DURATION_MS);
        if(b.snapshot.elapsed_ms<=last) throw InputError("boundary time not strictly increasing");
        last=b.snapshot.elapsed_ms;
        auto n=t.number<std::size_t>(0,MAX_CREDITS-total_credits); total_credits+=n;
        auto ruling=t.word();
        if(ruling=="none") b.snapshot.referee_ruling=m::RefereeRuling::None;
        else if(ruling=="round_forfeit") b.snapshot.referee_ruling=m::RefereeRuling::RoundForfeit;
        else if(ruling=="other") b.snapshot.referee_ruling=m::RefereeRuling::Other;
        else throw InputError("invalid referee ruling");
        b.snapshot.simultaneous_destruction_resolved=t.boolean();
        for(auto& team:b.snapshot.teams) {
            team.base_hp=t.number<std::int64_t>(0,std::numeric_limits<std::int64_t>::max());
            team.attack_damage=t.optional(); team.surviving_robot_hp=t.optional();
        }
        t.done();
        for(std::size_t p=0;p<4;++p) {
            Tokens q(line()); q.require("POSITION"); q.require(POSITIONS[p]);
            auto mode=q.word();
            if(mode=="logical") b.inputs[p]=c::LogicalActivation{q.boolean(),q.boolean()};
            else if(mode!="none") throw InputError("invalid logical mode");
            mode=q.word();
            if(mode=="continuation") {
                q.require(ASSERTION);
                b.continuations[p]=Continuation{q.number<std::int64_t>(0,m::MATCH_DURATION_MS),q.number<std::int64_t>(0,m::MATCH_DURATION_MS)};
            } else if(mode!="none") throw InputError("invalid continuation mode");
            q.done();
        }
        for(std::size_t j=0;j<n;++j) {
            Tokens q(line()); q.require("CREDIT"); b.credits.push_back(q.number<std::uint64_t>(1,std::numeric_limits<std::uint64_t>::max())); q.done();
        }
        s.events.push_back(b);
    }
    Tokens end(line()); end.require("END"); end.done();
    if(std::cin.peek()!=std::char_traits<char>::eof()) throw InputError("trailing protocol data");
    return s;
}
const char* boolean(bool v) { return v?"true":"false"; }
const char* team(c::Team t) { return t==c::Team::Red?"red":"blue"; }
void logical_json(std::ostream& o,c::LogicalActivation a) { o<<"{\"red\":"<<boolean(a.red)<<",\"blue\":"<<boolean(a.blue)<<'}'; }
void control_json(std::ostream& o,c::PositionControl p) {
    o<<"{\"kind\":\""<<(p.kind==c::PositionKind::Outpost?"outpost":"base")<<"\",\"original_owner\":\""<<team(p.original_owner)
     <<"\",\"controller\":\""<<team(p.controller)<<"\",\"degree\":\""<<(p.degree==c::ControlDegree::Full?"full":"temporary")<<"\"}";
}
void capture_json(std::ostream& o,const c::CaptureSnapshot& s) {
    o<<"{\"time_ms\":"<<s.time_ms<<",\"control\":";control_json(o,s.control);
    o<<",\"active\":";logical_json(o,s.active);o<<",\"progress_ms\":"<<s.progress_ms<<",\"completed\":"<<boolean(s.completed)<<'}';
}
std::string_view trace_name(c::CaptureTraceKind k) {
    switch(k) {
#define TRACE(x) case c::CaptureTraceKind::x: return #x
        TRACE(Initialized); TRACE(LogicalInputChanged); TRACE(ProgressReachedZero); TRACE(CapturedTemporarily); TRACE(EngineeringOrderApplied);
#undef TRACE
    }
    throw std::logic_error("unknown capture trace");
}
void trace_json(std::ostream& o,const c::CaptureTrace& t) {
    o<<"{\"time_ms\":"<<t.time_ms<<",\"kind\":\""<<trace_name(t.kind)<<"\",\"after\":";capture_json(o,t.after);
    o<<",\"rule_id\":\""<<t.rule_id<<"\",\"policy\":\""<<t.policy<<"\"}";
}
void optional_json(std::ostream& o,std::optional<std::int64_t> n) { if(n)o<<*n;else o<<"null"; }
void totals_json(std::ostream& o,const m::TeamTotals& t) {
    o<<"{\"base_hp\":"<<t.base_hp<<",\"control_score\":";optional_json(o,t.control_score);
    o<<",\"attack_damage\":";optional_json(o,t.attack_damage);o<<",\"surviving_robot_hp\":";optional_json(o,t.surviving_robot_hp);o<<'}';
}
void snapshot_json(std::ostream& o,const m::MatchSnapshot& s) {
    o<<"{\"elapsed_ms\":"<<s.elapsed_ms<<",\"red\":";totals_json(o,s.teams[0]);o<<",\"blue\":";totals_json(o,s.teams[1]);
    o<<",\"referee_ruling\":\""<<(s.referee_ruling==m::RefereeRuling::None?"none":s.referee_ruling==m::RefereeRuling::RoundForfeit?"round_forfeit":"other")
     <<"\",\"simultaneous_destruction_resolved\":"<<boolean(s.simultaneous_destruction_resolved)<<'}';
}
std::string_view outcome(m::Outcome v) {
    switch(v) {case m::Outcome::Ongoing:return "ongoing";case m::Outcome::RedWins:return "red_wins";case m::Outcome::BlueWins:return "blue_wins";case m::Outcome::Draw:return "draw";}
    throw std::logic_error("unknown outcome");
}
std::string_view reason(m::Reason v) {
    switch(v) {
        case m::Reason::NotEnded:return "not_ended";case m::Reason::BaseHp:return "base_hp";case m::Reason::TimeoutControl:return "timeout_control";
        case m::Reason::TimeoutAttackDamage:return "timeout_attack_damage";case m::Reason::TimeoutSurvivingRobotHp:return "timeout_surviving_robot_hp";
        case m::Reason::TimeoutEqual:return "timeout_equal";case m::Reason::ResolvedBothBasesZero:return "resolved_both_bases_zero";
    }
    throw std::logic_error("unknown reason");
}
void decision_json(std::ostream& o,const m::Decision& d) {
    o<<"{\"outcome\":\""<<outcome(d.outcome)<<"\",\"reason\":\""<<reason(d.reason)<<"\",\"rule_id\":\""<<d.rule_id<<"\"}";
}
// Capture objects retain their own semantic traces; snapshots expose only current
// state. Completed capture clocks remain frozen while state.time tracks the
// separately adjudicated integrated boundary. No private restore setters exist.
struct State {
    std::int64_t time=0;
    std::vector<c::CaptureAttempt> captures;
    m::ExternalControlLedger ledger;
    m::MatchJudge judge;
    bool sealed=false;
};
m::ControlSnapshot derived_control(const State& s) {
    m::ControlSnapshot result;
    for(std::size_t i=0;i<4;++i) {
        const auto p=s.captures[i].snapshot().control;
        result.positions[i]={p.controller==c::Team::Red?m::Team::Red:m::Team::Blue,
                            p.degree==c::ControlDegree::Full?m::ControlDegree::Full:m::ControlDegree::Temporary};
    }
    return result;
}
void derived_json(std::ostream& o,const State& s) {
    o<<'{';for(std::size_t i=0;i<4;++i) {if(i)o<<',';o<<'"'<<POSITIONS[i]<<"\":";control_json(o,s.captures[i].snapshot().control);}o<<'}';
}
void state_json(std::ostream& o,const State& s) {
    o<<"{\"last_elapsed_ms\":"<<s.time<<",\"judge_last_elapsed_ms\":"<<s.judge.last_elapsed_ms()<<",\"terminal\":"<<boolean(s.judge.final_decision().has_value())<<",\"captures\":{";
    for(std::size_t i=0;i<4;++i) {if(i)o<<',';o<<'"'<<POSITIONS[i]<<"\":";capture_json(o,s.captures[i].snapshot());}
    o<<"},\"control\":";derived_json(o,s);
    o<<",\"ledger\":{\"totals\":["<<s.ledger.totals()[0]<<','<<s.ledger.totals()[1]<<"],\"last_credit_sequence\":"<<s.ledger.last_sequence()<<",\"sealed\":"<<boolean(s.sealed)<<"},\"final_snapshot\":";
    if(s.judge.final_snapshot()) snapshot_json(o,*s.judge.final_snapshot());else o<<"null";
    o<<",\"final_decision\":";if(s.judge.final_decision())decision_json(o,*s.judge.final_decision());else o<<"null";o<<'}';
}
void identity_json(std::ostream& o) {
    o<<"{\"protocol\":\""<<PROTOCOL<<"\",\"implementation_sha256\":\""<<INTEGRATED_BUILD_ID
     <<"\",\"capture_module_version\":\""<<c::MODULE_VERSION<<"\",\"match_module_version\":\""<<m::MODULE_VERSION
     <<"\",\"capture_source_sha256\":\""<<c::SOURCE_SHA256<<"\",\"match_source_sha256\":\""<<m::SOURCE_SHA256<<"\"}";
}
void run(const Scenario& scenario) {
    State state;
    for(std::size_t i=0;i<4;++i) {
        auto owner=i%2==0?c::Team::Red:c::Team::Blue;
        auto kind=i<2?c::PositionKind::Outpost:c::PositionKind::Base;
        auto control=std::get<c::PositionControl>(c::initial_control(kind,owner));
        state.captures.push_back(std::get<c::CaptureAttempt>(c::CaptureAttempt::start(0,control,scenario.initial[i])));
    }
    std::ostringstream out;out<<"{\"driver\":";identity_json(out);out<<",\"initial_state\":";state_json(out,state);out<<",\"records\":[";
    bool first=true;
    for(const auto& b:scenario.events) {
        if(!first)out<<',';
        first=false;
        out<<"{\"sequence\":"<<b.sequence<<",\"before\":";state_json(out,state);
        // No component is committed until every phase and terminal seal succeeds.
        State candidate=state;
        std::string_view error,source;
        std::optional<m::Decision> decision;
        std::ostringstream captures,continuations,credits;
        captures<<'[';continuations<<'[';credits<<'[';
        bool first_capture=true,first_continuation=true,first_credit=true;
        auto fail=[&](std::string_view e,std::string_view s){error=e;source=s;};
        if(state.judge.final_decision())fail("AlreadyFinished","judge");
        std::array<std::optional<std::int64_t>,4> handoff_from{};
        for(std::size_t i=0;i<4 && error.empty();++i) {
            auto& attempt=candidate.captures[i];
            // A completed attempt cannot consume logical inputs or advance time.
            if(attempt.snapshot().completed) {
                if(b.inputs[i]) {fail("UnsupportedTemporaryLogicalInput","capture");break;}
                handoff_from[i]=state.time;
                continue;
            }
            if(!first_capture)captures<<',';
            first_capture=false;
            captures<<"{\"position\":\""<<POSITIONS[i]<<'"';
            auto trace_start=attempt.trace().size();
            auto advanced=attempt.apply(b.snapshot.elapsed_ms,b.inputs[i]);
            if(auto e=std::get_if<c::Error>(&advanced)) {
                fail(c::error_name(*e),"capture");captures<<",\"error\":\""<<error<<"\"}";break;
            }
            auto a=std::get<c::CaptureAdvance>(advanced);
            captures<<",\"requested_time_ms\":"<<a.requested_time_ms<<",\"actual_endpoint_ms\":"<<a.state.time_ms<<",\"unconsumed_ms\":"<<a.unconsumed_ms<<",\"candidate_state\":";
            capture_json(captures,a.state);captures<<",\"traces\":[";
            for(std::size_t j=trace_start;j<attempt.trace().size();++j) {if(j!=trace_start)captures<<',';trace_json(captures,attempt.trace()[j]);}
            captures<<"]}";
            if(a.unconsumed_ms>0)handoff_from[i]=a.state.time_ms;
        }
        // External assertions cover unsupported intervals exactly. They never
        // mutate the native capture snapshot or manufacture restoration timing.
        for(std::size_t i=0;i<4 && error.empty();++i) {
            if(!handoff_from[i] && !b.continuations[i])continue;
            if(!first_continuation)continuations<<',';
            first_continuation=false;
            continuations<<"{\"position\":\""<<POSITIONS[i]<<'"';
            if(!handoff_from[i])fail("UnexpectedContinuation","continuation");
            else if(!b.continuations[i])fail("ExternalContinuationRequired","continuation");
            else if(b.continuations[i]->from!=*handoff_from[i] || b.continuations[i]->through!=b.snapshot.elapsed_ms)
                fail("ContinuationIntervalMismatch","continuation");
            if(!error.empty())continuations<<",\"error\":\""<<error<<"\"}";
            else continuations<<",\"from_ms\":"<<*handoff_from[i]<<",\"through_ms\":"<<b.snapshot.elapsed_ms<<",\"assertion\":\""<<ASSERTION<<"\"}";
        }
        out<<",\"derived_control\":";
        if(error.empty())derived_json(out,candidate);else out<<"null";
        for(auto sequence:b.credits) {
            if(!error.empty())break;
            if(!first_credit)credits<<',';
            first_credit=false;
            credits<<"{\"sequence\":"<<sequence;
            // There is no caller-supplied control map or score total on this path.
            auto result=candidate.ledger.apply({sequence,derived_control(candidate)});
            if(auto e=std::get_if<m::Error>(&result)) {fail(m::error_name(*e),"credit");credits<<",\"error\":\""<<error<<"\"}";break;}
            auto rate=std::get<m::ControlRate>(result);
            credits<<",\"points\":["<<rate.points_per_second[0]<<','<<rate.points_per_second[1]<<"],\"candidate_totals\":["<<candidate.ledger.totals()[0]<<','<<candidate.ledger.totals()[1]<<"]}";
        }
        captures<<']';continuations<<']';credits<<']';
        out<<",\"capture_attempts\":"<<captures.str()<<",\"continuation_checks\":"<<continuations.str()<<",\"credit_attempts\":"<<credits.str()<<",\"resolved_snapshot\":";
        if(error.empty()) {
            auto resolved=b.snapshot;
            for(std::size_t i=0;i<2;++i)resolved.teams[i].control_score=candidate.ledger.totals()[i];
            snapshot_json(out,resolved);
            auto judged=candidate.judge.update(resolved);
            if(auto e=std::get_if<m::Error>(&judged))fail(m::error_name(*e),"judge");
            else {
                decision=std::get<m::Decision>(judged);
                if(decision->outcome!=m::Outcome::Ongoing) {candidate.ledger.seal();candidate.sealed=true;}
                candidate.time=b.snapshot.elapsed_ms;
                state=std::move(candidate);
            }
        }else out<<"null";
        if(error.empty()) {out<<",\"status\":\"accepted\",\"decision\":";decision_json(out,*decision);}
        else out<<",\"status\":\"rejected\",\"error\":\""<<error<<"\",\"error_source\":\""<<source<<'"';
        out<<",\"after\":";state_json(out,state);out<<'}';
    }
    out<<"],\"final_state\":";state_json(out,state);out<<'}';std::cout<<out.str()<<'\n';
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string_view(argv[1])=="--identity") {identity_json(std::cout);std::cout<<'\n';return 0;}
        if(argc!=1)throw InputError("unsupported argument");
        auto scenario=parse();run(scenario);
    }catch(const InputError& e) {std::cerr<<"ProtocolInputError: "<<e.what()<<'\n';return 2;}
    catch(const std::exception& e) {std::cerr<<"DriverError: "<<e.what()<<'\n';return 2;}
}
