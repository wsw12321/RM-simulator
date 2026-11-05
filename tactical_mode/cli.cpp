#include "core.hpp"
#include <charconv>
#include <iostream>
#include <limits>
#include <sstream>

namespace {
bool done(std::istream& in) { std::string extra; return !(in >> extra); }
// from_chars rejects '+1', decimals, booleans, exponents and partial tokens.
bool integer(std::istream& in, tactical::I& value) {
    std::string token; if (!(in >> token) || token.size()>21) return false;
    const auto r=std::from_chars(token.data(),token.data()+token.size(),value);
    return r.ec==std::errc{} && r.ptr==token.data()+token.size();
}
bool observer(std::istream& in, int& value) {
    tactical::I n; if (!integer(in,n) || n<0 || n>1) return false;
    value=int(n); return true;
}
bool hash(std::istream& in, std::uint32_t& value) {
    tactical::I n;
    if (!integer(in,n) || n<0 || n>std::numeric_limits<std::uint32_t>::max()) return false;
    value=std::uint32_t(n); return true;
}
bool request(std::istream& in, tactical::Request& request) {
    tactical::I version,dx,dy,ax,ay,fire;
    if (!(in >> request.kind) || request.kind.size()>6 || !integer(in,version) || !integer(in,dx) ||
        !integer(in,dy) || !integer(in,ax) || !integer(in,ay) || !integer(in,fire)) return false;
    request.action={version,dx,dy,ax,ay,fire}; return true;
}
bool frame(std::istream& in, tactical::Frame& frame) {
    if (!integer(in,frame.tick)) return false;
    for (auto& input : frame.inputs) {
        std::string id;
        if (!(in >> input.kind >> id) || input.kind.size()>6 || id.size()>64 || !integer(in,input.sdk_version)) return false;
        input.controller_id=id=="-" ? "" : id;
        for (auto& n : input.action) if (!integer(in,n)) return false;
    }
    return hash(in,frame.hash) && hash(in,frame.observation_hashes[0]) && hash(in,frame.observation_hashes[1]);
}
void error(const std::string& message) {
    std::cout << "{\"error\":\"";
    for (unsigned char c : message) {
        if (c=='"' || c=='\\') std::cout << '\\';
        if (c>=32) std::cout << c;
    }
    std::cout << "\"}" << std::endl;
}
bool bounded_line(std::string& line, bool& oversized) {
    line.clear(); oversized=false; char c; bool any=false;
    while (std::cin.get(c)) {
        any=true; if (c=='\n') return true;
        if (line.size()<524288) line.push_back(c); else oversized=true;
    }
    return any;
}
}
int main() {
    tactical::Engine engine;
    std::string line; bool oversized;
    while (bounded_line(line,oversized)) {
        if (oversized) { error("Native command exceeds byte limit."); continue; }
        std::istringstream in(line); std::string command; in >> command;
        try {
            if (command=="BUILD" && done(in)) {
                std::cout << "{\"build\":\"" << tactical::build_id() << "\"}" << std::endl;
            } else if (command=="META" && done(in)) {
                std::cout << tactical::meta_json() << std::endl;
            } else if (command=="POLICY_META" && done(in)) {
                const auto metadata=tactical::policy_metadata_json(tactical::policy::descriptor());
                std::cout << "{\"policy\":" << metadata << '}' << std::endl;
            } else if (command=="STATE" || command=="RESET") {
                int who;
                if (!observer(in,who)) { error("Expected observer0 or1."); continue; }
                tactical::Config config;
                if (command=="RESET") {
                    tactical::I boundary,credit,damage,heat,threshold;
                    if (!integer(in,boundary) || !integer(in,credit) || !integer(in,damage) || !integer(in,heat) || !integer(in,threshold) || heat<0 || heat>1 || threshold<0 || threshold>2 || boundary<0 || boundary>1 || credit<0 || credit>1 || damage<0 || damage>1) {
                        error("RESET requires observer boundary_index credit_index damage_index heat_index (0 or1), then heat_threshold_index (0..2)."); continue;
                    }
                    config.boundary=boundary ? tactical::cap::BoundaryPolicy::EngineeringTimerBeforeInputV1 : tactical::cap::BoundaryPolicy::StrictRejectUnadjudicated;
                    config.credits=credit!=0;
                    config.damage_strategy=damage ? tactical::DamageStrategy::ExperimentalReconciledProjectileHpLossV1 : tactical::DamageStrategy::MissingAdjudicated;
                    config.heat_strategy=heat ? tactical::HeatStrategy::ExperimentalFixedLevel1CoolingPriority17mmV1 : tactical::HeatStrategy::Disabled;
                    config.heat_threshold=static_cast<tactical::heat::ThresholdPolicy>(threshold);
                }
                if (!done(in)) { error("Unexpected trailing data."); continue; }
                if (command=="RESET") engine.reset(config);
                std::cout << engine.json(who) << std::endl;
            } else if (command=="EXPORT" && done(in)) {
                std::cout << engine.replay_json() << std::endl;
            } else if (command=="STEP") {
                tactical::I count; int who; std::array<tactical::Request,2> requests;
                if (!integer(in,count) || !observer(in,who) || !request(in,requests[0]) || !request(in,requests[1]) || !done(in)) {
                    error("Invalid native step request."); continue;
                }
                if (count<1 || count>tactical::kMaxBatch) { error("Step count outside [1,600]."); continue; }
                if (engine.native.state.terminal) { error("Tactical run is terminal; reset to start another run."); continue; }
                std::optional<std::string> invalid;
                for (const auto& r : requests) {
                    if (r.kind!="manual" && r.kind!="policy") { invalid="Input kind must be manual or policy."; break; }
                    if (r.kind=="manual") invalid=tactical::policy::validate_action(r.action);
                    else if (std::get<tactical::I>(r.action.sdk_version)!=tactical::policy::kSdkVersion)
                        invalid="Requested policy SDK version mismatch.";
                    if (invalid) break;
                }
                if (invalid) { error(*invalid); continue; }
                std::array<tactical::Controller,2> controllers;
                for (int i=0; i<2; ++i)
                    if (requests[i].kind=="policy") controllers[i]={tactical::policy::descriptor(),tactical::policy::decide};
                if (auto e=engine.advance(count,requests,controllers)) error(*e);
                else std::cout << engine.json(who,true) << std::endl;
            } else if (command=="FRAME") {
                tactical::Frame f; int who;
                // FRAME has observer immediately after tick; FRAMES puts it after count.
                if (!integer(in,f.tick) || !observer(in,who)) { error("Invalid native replay frame."); continue; }
                std::string rest; std::getline(in,rest);
                std::istringstream payload(std::to_string(f.tick)+rest);
                if (!frame(payload,f) || !done(payload)) { error("Invalid native replay frame."); continue; }
                if (auto e=engine.replay_frame(f)) error(*e);
                else std::cout << engine.json(who,true) << std::endl;
            } else if (command=="FRAMES") {
                tactical::I count; int who;
                if (!integer(in,count) || count<1 || count>tactical::kMaxBatch || !observer(in,who)) {
                    error("Invalid native replay batch."); continue;
                }
                std::vector<tactical::Frame> batch(static_cast<std::size_t>(count));
                bool valid=true; for (auto& f : batch) if (!frame(in,f)) { valid=false; break; }
                if (!valid || !done(in)) { error("Invalid native replay batch."); continue; }
                if (auto e=engine.replay_frames(batch)) error(*e);
                else std::cout << engine.json(who,true) << std::endl;
            } else error("Unknown native command or trailing data.");
        } catch (...) {
            error("Native controller or request exception; experiment invalid.");
        }
    }
}
