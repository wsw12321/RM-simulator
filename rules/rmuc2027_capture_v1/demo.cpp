#include "capture_rules.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
using namespace rmuc2027::capture_v1;
template<class T> T require(Result<T> r) {
    if (auto e = std::get_if<Error>(&r)) throw std::runtime_error(std::string(error_name(*e)));
    return std::get<T>(std::move(r));
}
int main() {
    try {
        std::cout << "module=" << MODULE_VERSION << '\n'
                  << "manual=" << SOURCE_VERSION << '\n'
                  << "manual_sha256=" << SOURCE_SHA256 << '\n'
                  << "default_policy=" << policy_name(BoundaryPolicy::StrictRejectUnadjudicated) << '\n';
        const auto full = require(initial_control(PositionKind::Outpost, Team::Red));
        auto capture = require(CaptureAttempt::start(0, full, {false, true}));
        const auto end = require(capture.apply(6000));
        std::cout << "logical_capture: temporary_blue_at_ms=" << end.state.time_ms
                  << "; unconsumed_ms=" << end.unconsumed_ms
                  << "; restoration_and_lifecycle=NOT_IMPLEMENTED\n";
        for (const auto& event : capture.trace())
            std::cout << "  trace: at=" << event.time_ms << "; progress_ms=" << event.after.progress_ms
                      << "; completed=" << event.after.completed << "; source=" << event.rule_id
                      << "; policy=" << event.policy << '\n';

        auto activation = require(ActivationDelay::start(0, QualifiedDetection::Present));
        auto downstream = require(CaptureAttempt::start(0, full, {false, true}));
        require(activation.apply(3000, QualifiedDetection::Absent));
        require(downstream.apply(3000));
        // Transaction across components is explicitly owned by this caller.
        // Only one already-qualified blue track is involved. This is NOT a
        // multi-robot/card aggregator or an implicit sensor-latency model.
        auto candidate_activation = activation;
        auto candidate_downstream = downstream;
        auto logical = require(candidate_activation.apply(5000));
        auto boundary = candidate_downstream.apply(5000, LogicalActivation{false, logical.logically_active});
        if (auto e = std::get_if<Error>(&boundary)) {
            std::cout << "3s_detector_plus_2s_grace: " << error_name(*e)
                      << "; neither_component_committed; retained_time_ms=" << activation.snapshot().time_ms << '\n';
        } else throw std::runtime_error("strict ambiguous example unexpectedly committed");

        auto experimental = require(CaptureAttempt::start(0, full, {false, true},
            BoundaryPolicy::EngineeringTimerBeforeInputV1));
        require(experimental.apply(5000, LogicalActivation{false, false}));
        std::cout << "explicit_opt_in: " << policy_name(experimental.policy())
                  << "; capture_before_logical_loss_at_ms=" << experimental.snapshot().time_ms << '\n';
        std::cout << "This is a bounded deterministic subset, not an official referee or full capture simulator.\n";
    } catch (const std::exception& e) {
        std::cerr << "demo failed: " << e.what() << '\n';
        return 1;
    }
}
