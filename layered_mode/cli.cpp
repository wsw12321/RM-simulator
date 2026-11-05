#include "core.hpp"
#include "policy_adapter.hpp"
#include <iostream>
#include <sstream>
namespace {
bool done(std::istream& in) { std::string extra; return !(in >> extra); }
void error(const std::string& message) {
    std::cout << "{\"error\":\"";
    for (char c : message) {
        if (c == '"' || c == '\\') std::cout << '\\';
        if (static_cast<unsigned char>(c) >= 32) std::cout << c;
    }
    std::cout << "\"}" << std::endl;
}
// Bound allocation even when this executable is used without the Python adapter.
bool bounded_line(std::string& line, bool& oversized) {
    line.clear(); oversized = false; char c; bool any = false;
    while (std::cin.get(c)) {
        any = true;
        if (c == '\n') return true;
        if (line.size() < 300000) line.push_back(c); else oversized = true;
    }
    return any;
}
}
int main() {
    layered::Engine engine;
    std::string line; bool oversized;
    while (bounded_line(line, oversized)) {
        if (oversized) { error("Native command exceeds byte limit."); continue; }
        std::istringstream in(line); std::string command; in >> command;
        if (command == "LOAD") {
            if (auto err = engine.load(in)) error(*err);
            else std::cout << engine.json() << std::endl;
        } else if (command == "STATE" && done(in)) {
            std::cout << engine.json() << std::endl;
        } else if (command == "BUILD" && done(in)) {
            std::cout << "{\"build\":\"" << layered::build_id() << "\"}" << std::endl;
        } else if (command == "POLICY_META" && done(in)) {
            try {
                const auto metadata = layered::policy_metadata_json(layered::policy::descriptor());
                std::cout << "{\"policy\":" << metadata << "}" << std::endl;
            } catch (...) { error("Invalid compiled policy descriptor; experiment invalid."); }
        } else if (command == "POLICY") {
            layered::I count, version; std::string controller;
            if (!(in >> count >> version >> controller) || !done(in) || controller.size() > 64) {
                error("Invalid native policy request."); continue;
            }
            try {
                auto err = layered::advance_policy(engine, count, version, controller,
                    layered::policy::descriptor(), layered::policy::decide);
                if (err) error(*err); else std::cout << engine.result_json() << std::endl;
            } catch (...) { error("Policy descriptor exception; experiment invalid, batch unchanged."); }
        } else if (command == "ADVANCE" || command == "FRAME") {
            layered::I first, dx, dy;
            if (!(in >> first >> dx >> dy) || !done(in)) {
                error("Invalid native movement request."); continue;
            }
            auto err = command == "ADVANCE" ? engine.advance(first, dx, dy)
                                            : engine.advance(1, dx, dy, first);
            if (err) error(*err); else std::cout << engine.result_json() << std::endl;
        } else error("Unknown native command or trailing data.");
    }
}
