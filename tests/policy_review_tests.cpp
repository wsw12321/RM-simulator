// Independent policy ABI/transaction review. No arbitrary-code containment claim.
#include "../layered_mode/policy_adapter.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {
using layered::Engine;
using layered::I;
namespace p = layered::policy;
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().geometry)>);
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().robot)>);
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().events)>);
static_assert(!std::is_reference_v<decltype(std::declval<p::Observation>().geometry)>);
static_assert(!std::is_pointer_v<decltype(std::declval<p::Observation>().geometry)>);

Engine fixture(bool many_triggers = false) {
    std::ostringstream wire;
    wire << std::string(64, 'a') << ' ' << std::string(64, 'b')
         << " 1 100 50 512 36000 1000 1000 0"
         << " 4 0 0 6000 0 6000 4000 0 4000"
         << " 1 floor ground 0 0 0 0 0 1 4 0 0 6000 0 6000 4000 0 4000"
         << " 0 " << (many_triggers ? 32 : 1);
    if (many_triggers) {
        for (int i = 0; i < 32; ++i)
            wire << " trigger_" << i << " 0 500 4 1020 900 1021 900 1021 1100 1020 1100 1 0";
    } else {
        wire << " trigger 0 500 4 900 900 1300 900 1300 1300 900 1300 1 0";
    }
    wire << " 0";
    Engine result;
    std::istringstream input(wire.str());
    const auto error = result.load(input);
    if (error) std::cerr << *error << '\n';
    assert(!error);
    return result;
}

p::Descriptor descriptor{p::kSdkVersion, "independent-review-v1"};
int calls = 0;
I failure_tick = -1;
int failure_kind = 0;
std::vector<I> ticks, xs;
p::Action output;
p::Action recording(const p::Observation& observation) {
    ++calls;
    ticks.push_back(observation.tick);
    xs.push_back(observation.robot.x_q1024);
    assert(observation.sdk_version == 1 && observation.hz == 60);
    assert(observation.geometry.surfaces.size() == 1);
    // A mutable private copy of nested data cannot write through to the engine.
    auto private_geometry = observation.geometry;
    private_geometry.surfaces[0].id = "changed-copy";
    private_geometry.boundary.clear();
    if (observation.tick == failure_tick) {
        if (failure_kind == 1) throw std::runtime_error("private-path-must-not-be-returned");
        if (failure_kind == 2) throw 17;
        return {p::kSdkVersion, I{1001}, I{0}};
    }
    return {p::kSdkVersion, I{1000}, I{0}};
}
p::Action fixed_output(const p::Observation&) { ++calls; return output; }
p::Action alternating(const p::Observation& observation) {
    return {p::kSdkVersion, I{observation.tick % 2 ? -1000 : 1000}, I{0}};
}
auto run(Engine& engine, I count, p::DecisionFunction decision = recording) {
    return layered::advance_policy(engine, count, 1, descriptor.controller_id, descriptor, decision);
}
void clear_recording() { calls = 0; ticks.clear(); xs.clear(); failure_tick = -1; failure_kind = 0; }
}

int main() {
    // Owning observations survive engine destruction and changes to engine data.
    {
        auto retained = [] { auto engine = fixture(); return layered::make_observation(engine); }();
        assert(retained.robot.surface_id == "floor");
        assert(retained.geometry.boundary.size() == 4);
        assert(retained.active_trigger_ids.at(0) == "trigger");
        assert(retained.events.at(0).type == "trigger_enter");
        auto engine = fixture();
        const auto observation = layered::make_observation(engine);
        engine.map.surfaces[0].id = "mutated-engine";
        engine.map.surfaces[0].polygon.clear();
        engine.map.triggers.clear();
        engine.active.clear();
        engine.events.clear();
        engine.position.x = 99;
        assert(observation.robot.x_q1024 == 1000 * layered::Q);
        assert(observation.geometry.surfaces[0].id == "floor");
        assert(observation.geometry.surfaces[0].polygon.size() == 4);
        assert(observation.geometry.triggers.at(0).surface_ids.at(0) == "floor");
        assert(observation.events.at(0).id == "trigger");
    }
    // Exactly one pre-step decision each tick, independent of native hash evidence.
    {
        auto engine = fixture();
        clear_recording();
        assert(!run(engine, 7));
        assert(calls == 7 && engine.tick == 7);
        for (I i = 0; i < 7; ++i) {
            assert(ticks.at(i) == i);
            assert(xs.at(i) == (1000 + i * 50) * layered::Q);
            assert(engine.frames.at(i).tick == i && engine.frames.at(i).dx == 1000);
        }
        auto manual = fixture();
        assert(!manual.advance(7, 1000, 0));
        assert(engine.result_json() == manual.result_json());
    }
    // Same validator governs accepted and rejected manual/policy integer actions.
    {
        auto engine = fixture();
        const auto before = engine.result_json();
        for (I bad : {I{-1001}, I{1001}, std::numeric_limits<I>::min(), std::numeric_limits<I>::max()}) {
            output = {I{1}, bad, I{0}};
            auto validation = p::validate_action(output);
            assert(validation && engine.advance(1, bad, 0) == validation);
            assert(run(engine, 1, fixed_output));
            assert(engine.result_json() == before);
        }
        const p::Action bad_actions[] = {
            {I{0}, I{0}, I{0}}, {I{2}, I{0}, I{0}}, {true, I{0}, I{0}},
            {1.0, I{0}, I{0}}, {I{1}, true, I{0}}, {I{1}, I{0}, false},
            {I{1}, 0.0, I{0}}, {I{1}, I{0}, 1.0},
            {I{1}, std::numeric_limits<double>::quiet_NaN(), I{0}},
            {I{1}, I{0}, std::numeric_limits<double>::infinity()}
        };
        for (const auto& bad : bad_actions) {
            output = bad;
            assert(p::validate_action(output));
            assert(run(engine, 1, fixed_output));
            assert(engine.result_json() == before);
        }
        for (I good : {I{-1000}, I{-1}, I{0}, I{1}, I{1000}}) {
            auto manual = fixture(), policy = fixture();
            output = {I{1}, good, -good};
            assert(!p::validate_action(output));
            assert(!manual.advance(3, good, -good));
            assert(!run(policy, 3, fixed_output));
            assert(manual.result_json() == policy.result_json());
        }
    }
    // Late output rejection and both exception categories preserve *all* batch state.
    {
        for (int failure = 0; failure < 3; ++failure) {
            auto engine = fixture();
            assert(!engine.advance(2, -1000, 0));
            const auto before = engine.result_json();
            clear_recording(); failure_kind = failure; failure_tick = engine.tick + 3;
            const auto error = run(engine, 8);
            assert(error && calls == 4);
            assert(error->find("private-path") == std::string::npos);
            assert(engine.result_json() == before);
        }
    }
    // Invalid metadata/counts reject before any callback, including null entrypoint.
    {
        auto engine = fixture();
        const auto before = engine.result_json();
        clear_recording();
        for (I count : {I{0}, I{-1}, I{601}, std::numeric_limits<I>::max()}) assert(run(engine, count));
        assert(layered::advance_policy(engine, 1, 2, descriptor.controller_id, descriptor, recording));
        assert(layered::advance_policy(engine, 1, 1, "wrong", descriptor, recording));
        assert(layered::advance_policy(engine, 1, 1, descriptor.controller_id, {2, descriptor.controller_id}, recording));
        assert(layered::advance_policy(engine, 1, 1, "bad\"id", {1, "bad\"id"}, recording));
        assert(run(engine, 1, nullptr));
        engine.config.max_ticks = 1;
        assert(run(engine, 2));
        engine.config.max_ticks = 36000;
        assert(calls == 0 && engine.result_json() == before);
    }
    // The cumulative event bound is transactional across the entire policy batch.
    {
        auto engine = fixture(true);
        const auto before = engine.result_json();
        const auto error = run(engine, 200, alternating);
        assert(error && error->find("event bound") != std::string::npos);
        assert(engine.result_json() == before);
        assert(!run(engine, 100, alternating));
        assert(engine.batch_events.size() == 6400);
        assert(engine.tick == 100);
    }
    std::cout << "PASS independent policy SDK review: owning immutable snapshots, once-per-tick pre-step callbacks, shared action validation, late invalid/exception/event-bound rollback, and descriptor rejection before callback.\n";
}
