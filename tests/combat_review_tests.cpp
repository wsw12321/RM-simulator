// Independent adversarial review of the supplied observation data and transaction
// contract. Trusted native callbacks are NOT a security isolation boundary.
#include "../combat_mode/core.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {
namespace p = combat::policy;
using I = p::Integer;
#define ABSENT_MEMBER(member) \
 template<class T, class=void> struct has_##member : std::false_type {}; \
 template<class T> struct has_##member<T,std::void_t<decltype(std::declval<T>().member)>> : std::true_type {};
ABSENT_MEMBER(hash) ABSENT_MEMBER(journal) ABSENT_MEMBER(events)
ABSENT_MEMBER(terminal) ABSENT_MEMBER(winner) ABSENT_MEMBER(robots)
ABSENT_MEMBER(shots) ABSENT_MEMBER(hits) ABSENT_MEMBER(id)
ABSENT_MEMBER(owner) ABSENT_MEMBER(life) ABSENT_MEMBER(hp)
ABSENT_MEMBER(ammo) ABSENT_MEMBER(cooldown)
#undef ABSENT_MEMBER
static_assert(!has_hash<p::Observation>::value && !has_journal<p::Observation>::value);
static_assert(!has_events<p::Observation>::value && !has_terminal<p::Observation>::value);
static_assert(!has_winner<p::Observation>::value && !has_robots<p::Observation>::value);
static_assert(!has_shots<p::Self>::value && !has_hits<p::Self>::value);
static_assert(!has_hp<p::Enemy>::value && !has_ammo<p::Enemy>::value && !has_cooldown<p::Enemy>::value);
static_assert(!has_id<p::Projectile>::value && !has_owner<p::Projectile>::value && !has_life<p::Projectile>::value);
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().self)>);
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().arena)>);
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().enemy)>);
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().projectiles)>);
static_assert(!std::is_reference_v<decltype(std::declval<p::Observation>().arena)>);
static_assert(!std::is_pointer_v<decltype(std::declval<p::Observation>().arena)>);

sandbox::Engine world() {
    sandbox::Engine engine;
    engine.state.robots[0] = {1000,1000,0};
    engine.state.robots[1] = {4000,1000,128};
    return engine;
}
std::string observe(const sandbox::Engine& engine, int self = 0) {
    return combat::observation_json(combat::make_observation(engine,self));
}
auto projectile_key(const p::Projectile& projectile) {
    return std::make_tuple(projectile.x,projectile.y,projectile.vx,projectile.vy);
}
void observation_review() {
    // Owned nested data remains valid after destruction and mutation of the source.
    const auto retained = [] {
        auto engine = world();
        engine.state.bullets.push_back({7,1,2000,1000,-10,20,170});
        return combat::make_observation(engine,0);
    }();
    assert(retained.self.x == 1000 && retained.enemy->x == 4000);
    assert(retained.projectiles.at(0).vx == -10 && retained.arena.walls.size() == 3);
    auto copy = retained.arena;
    copy.walls.clear();
    assert(retained.arena.walls.size() == 3);

    // All forbidden bookkeeping is observationally irrelevant, even when visible.
    auto engine = world();
    engine.state.bullets = {{31,0,2000,1000,20,30,50},{32,1,3000,1000,-10,-20,80}};
    const auto expected = observe(engine);
    engine.state.next_id = 2000000000;
    engine.state.terminal = 1;
    engine.state.robots[0].shots = 118;
    engine.state.robots[0].hits = 119;
    engine.state.robots[1].hp = 0; // presence cannot act as a hidden death oracle
    engine.state.robots[1].ammo = 1;
    engine.state.robots[1].cooldown = 9;
    engine.state.robots[1].shots = 117;
    engine.state.robots[1].hits = 116;
    for (auto& bullet : engine.state.bullets) {
        bullet.id += 100;
        bullet.owner ^= 1;
        bullet.life = 1;
    }
    std::reverse(engine.state.bullets.begin(),engine.state.bullets.end());
    engine.events.push_back({0,2,1,0,10});
    engine.journal.push_back({0,{},123456});
    assert(observe(engine) == expected);

    // A hidden enemy and hidden bullets cannot affect serialized bytes or sorting.
    engine = world();
    engine.state.robots[1].x = 11000;
    engine.state.bullets = {{300,1,3000,1000,0,20,1},{100,0,2000,1000,20,0,179}};
    const auto hidden_expected = observe(engine);
    engine.state.robots[1] = {10500,7000,17,9,4,9,87,79};
    engine.state.bullets.push_back({1,1,11000,1000,10,0,10});
    engine.state.bullets.push_back({2,0,6000,3800,-10,0,179}); // behind central cover
    assert(observe(engine) == hidden_expected);
    engine.state.bullets.back() = {999999,1,7000,4000,50,-30,1};
    assert(observe(engine) == hidden_expected);

    // Exact inclusive center range. Robots and bullets use the same visibility rule.
    engine = world();
    engine.state.robots[1].x = 7000;
    assert(combat::make_observation(engine,0).enemy);
    engine.state.robots[1].x = 7001;
    assert(!combat::make_observation(engine,0).enemy);
    engine.state.robots[1].x = 4600;
    engine.state.robots[1].y = 5800; // 3600^2+4800^2 == 6000^2
    assert(combat::make_observation(engine,0).enemy);
    engine.state.robots[1].y = 5801;
    assert(!combat::make_observation(engine,0).enemy);
    engine.state.bullets = {{1,0,7000,1000,0,0,1},{2,1,7001,1000,0,0,1}};
    assert(combat::make_observation(engine,0).projectiles.size() == 1);

    // Closed cover edges block. A parallel ray one millimeter outside is clear.
    engine = world();
    engine.state.robots[0].y = 1350;
    engine.state.robots[1].y = 1350;
    assert(!combat::make_observation(engine,0).enemy);
    engine.state.robots[0].y = 1349;
    engine.state.robots[1].y = 1349;
    assert(combat::make_observation(engine,0).enemy);
    engine.state.robots[0] = {1000,3800,0};
    engine.state.robots[1] = {7000,3800,0};
    assert(!combat::make_observation(engine,0).enemy);

    // Order solely by visible fields, including identical entries and tie-breaks.
    engine = world();
    engine.state.bullets = {
        {9,0,2000,1000,20,2,10},{1,1,2000,1000,20,1,5},
        {8,1,2000,1000,-20,8,1},{7,0,2000,999,100,5,4},
        {2,0,1999,1100,20,2,10},{3,1,2000,1000,20,1,179}};
    const auto ordered = combat::make_observation(engine,0);
    assert(ordered.projectiles.size() == 6);
    for (std::size_t i=1;i<ordered.projectiles.size();++i)
        assert(projectile_key(ordered.projectiles[i-1]) <= projectile_key(ordered.projectiles[i]));
    const auto ordered_json = observe(engine);
    std::mt19937 random(771904);
    for (int iteration=0;iteration<200;++iteration) {
        std::shuffle(engine.state.bullets.begin(),engine.state.bullets.end(),random);
        for (auto& bullet : engine.state.bullets) {
            bullet.id = static_cast<int>(random()%1000000);
            bullet.owner = static_cast<int>(random()%2);
            bullet.life = 1+static_cast<int>(random()%180);
        }
        assert(observe(engine) == ordered_json);
    }

    // Public SDK helper exactly reproduces native heading selection in arena bounds.
    for (int i=0;i<1000;++i) {
        sandbox::Vec a{static_cast<int>(random()%12001),static_cast<int>(random()%7601)};
        sandbox::Vec b{static_cast<int>(random()%12001),static_cast<int>(random()%7601)};
        assert(p::aim_direction({a.x,a.y},{b.x,b.y}) == sandbox::aim_direction(a,b));
    }
    assert(p::aim_direction({1000,1000},{1000,1000}) == sandbox::aim_direction({1000,1000},{1000,1000}));
}
std::array<p::Action,2> outputs;
std::vector<std::pair<int,std::string>> callback_inputs;
I failure_tick = -1;
int failure_kind = 0;
int calls = 0;
p::Action controlled(const p::Observation& observation) {
    ++calls;
    callback_inputs.emplace_back(int(observation.self_index),combat::observation_json(observation));
    if (observation.self_index == 1 && observation.tick == failure_tick) {
        if (failure_kind == 1) throw std::runtime_error("private-callback-detail-must-not-escape");
        if (failure_kind == 2) throw 123;
        return {I{1},I{0},I{0},I{0},I{0},I{2}};
    }
    return outputs.at(observation.self_index);
}
p::Action deterministic(const p::Observation& observation) {
    ++calls;
    return {I{1}, I{observation.tick%19<9 ? 1000 : -1000}, I{observation.self_index ? 500 : -500},
            I{6000}, I{3800}, I{0}};
}
auto controllers(p::DecisionFunction decide = controlled) {
    return std::array<combat::Controller,2>{{{{1,"review-controller-v1"},decide},
                                           {{1,"review-controller-v1"},decide}}};
}
auto requests() {
    return std::array<combat::Request,2>{{{"policy",{}},{"policy",{}}}};
}
void clean_callbacks() {
    calls = 0; callback_inputs.clear(); failure_tick = -1; failure_kind = 0;
    outputs = {{{I{1},I{1000},I{0},I{4000},I{1000},I{0}},
                {I{1},I{-1000},I{0},I{1000},I{1000},I{0}}}};
}
void transaction_review() {
    // Both robots see the same pre-step world, exactly one callback per tick.
    {
        combat::Engine engine;
        engine.native = world();
        auto expected_world = engine.native;
        clean_callbacks();
        assert(!engine.advance(9,requests(),controllers()));
        assert(calls == 18 && engine.journal.size() == 9 && engine.frames.size() == 9);
        for (int tick=0;tick<9;++tick) {
            for (int self=0;self<2;++self) {
                const auto& received = callback_inputs.at(tick*2+self);
                assert(received.first == self && received.second == observe(expected_world,self));
                const auto& frame = engine.journal.at(tick);
                assert(frame.tick == tick && frame.inputs[self].kind == "policy");
                assert(frame.inputs[self].sdk_version == 1);
                assert(frame.observation_hashes[self] == combat::observation_hash(combat::make_observation(expected_world,self)));
            }
            assert(expected_world.step({sandbox::Action{1000,0,4000,1000,0},sandbox::Action{-1000,0,1000,1000,0}}));
        }
        assert(engine.native.json() == expected_world.json());
    }
    // Identical strict output validation for manual and policy, all six fields.
    {
        const p::Action valid{I{1},I{0},I{0},I{6000},I{3800},I{0}};
        std::vector<p::Action> bad_actions;
        const p::Scalar wrong_types[] = {true,false,1.0,std::numeric_limits<double>::quiet_NaN(),
                                        std::numeric_limits<double>::infinity()};
        for (const auto& bad : wrong_types) for (int field=0;field<6;++field) {
            auto action = valid;
            p::Scalar* parts[] = {&action.sdk_version,&action.dx,&action.dy,&action.aim_x,&action.aim_y,&action.fire};
            *parts[field] = bad;
            bad_actions.push_back(action);
        }
        const I lows[] = {1,-1000,-1000,0,0,0}, highs[] = {1,1000,1000,12000,7600,1};
        for (int field=0;field<6;++field) for (I bad : {lows[field]-1,highs[field]+1,std::numeric_limits<I>::min(),std::numeric_limits<I>::max()}) {
            auto action = valid;
            p::Scalar* parts[] = {&action.sdk_version,&action.dx,&action.dy,&action.aim_x,&action.aim_y,&action.fire};
            *parts[field] = bad;
            bad_actions.push_back(action);
        }
        for (const auto& bad : bad_actions) {
            combat::Engine manual, policy;
            clean_callbacks(); outputs[0] = bad;
            auto manual_request = requests();
            manual_request[0] = {"manual",bad};
            const auto before_manual = manual.json(0,true), before_policy = policy.json(0,true);
            auto manual_error = manual.advance(1,manual_request,controllers());
            auto policy_error = policy.advance(1,requests(),controllers());
            assert(p::validate_action(bad) && manual_error == p::validate_action(bad));
            assert(policy_error == manual_error);
            assert(manual.json(0,true) == before_manual && policy.json(0,true) == before_policy);
        }
        for (I dx : {I{-1000},I{-1},I{0},I{1},I{1000}}) {
            combat::Engine manual, policy;
            clean_callbacks(); outputs[0] = {I{1},dx,-dx,I{12000},I{7600},I{0}};
            auto mixed = requests(); mixed[0] = {"manual",outputs[0]};
            assert(!manual.advance(7,mixed,controllers()));
            assert(!policy.advance(7,requests(),controllers()));
            assert(manual.native.json() == policy.native.json());
        }
    }
    // Failure by the second callback late in a batch restores all simulation/log state.
    for (int failure=0;failure<3;++failure) {
        combat::Engine engine;
        clean_callbacks();
        assert(!engine.advance(3,requests(),controllers()));
        const auto before = engine.json(0,true), replay_before = engine.replay_json();
        const auto native_before = engine.native.json();
        clean_callbacks(); failure_tick = 6; failure_kind = failure;
        const auto error = engine.advance(8,requests(),controllers());
        assert(error && calls == 8);
        assert(error->find("private-callback") == std::string::npos);
        assert(engine.json(0,true) == before && engine.replay_json() == replay_before);
        assert(engine.native.json() == native_before);
    }
    // Invalid request metadata is rejected before *any* callback.
    {
        combat::Engine engine;
        const auto before = engine.replay_json();
        clean_callbacks();
        for (I count : {I{-1},I{0},combat::kMaxBatch+1,std::numeric_limits<I>::max()})
            assert(engine.advance(count,requests(),controllers()));
        auto malformed = controllers(); malformed[1].descriptor.controller_id = "not a safe ID";
        assert(engine.advance(1,requests(),malformed));
        malformed = controllers(); malformed[1].descriptor.sdk_version = 2;
        assert(engine.advance(1,requests(),malformed));
        malformed = controllers(); malformed[1].decide = nullptr;
        assert(engine.advance(1,requests(),malformed));
        auto bad_requests = requests(); bad_requests[1].kind = "unknown";
        assert(engine.advance(1,bad_requests,controllers()));
        bad_requests = requests(); bad_requests[1].action.sdk_version = true;
        assert(engine.advance(1,bad_requests,controllers()));
        bad_requests = requests(); bad_requests[1] = {"manual",{I{1},I{0},I{0},I{0},I{0},true}};
        assert(engine.advance(1,bad_requests,controllers()));
        assert(calls == 0 && engine.replay_json() == before);
    }
    // Original physics remains unchanged across randomized manual inputs.
    {
        combat::Engine adapter;
        sandbox::Engine original;
        std::mt19937 random(59831);
        for (int tick=0;tick<600;++tick) {
            std::array<combat::Request,2> inputs;
            std::array<sandbox::Action,2> actions;
            for (int self=0;self<2;++self) {
                actions[self] = {int(random()%2001)-1000,int(random()%2001)-1000,int(random()%12001),int(random()%7601),0};
                const auto& a = actions[self];
                inputs[self] = {"manual",{I{1},I{a.dx},I{a.dy},I{a.aim_x},I{a.aim_y},I{a.fire}}};
            }
            assert(!adapter.advance(1,inputs));
            assert(original.step(actions));
            assert(adapter.native.json() == original.json());
            assert(adapter.journal.back().hash == original.hash());
        }
    }
    // Partition-independent stateless decisions and action-only replay, with mixed control.
    {
        combat::Engine batched, fragmented;
        clean_callbacks();
        assert(!batched.advance(120,requests(),controllers(deterministic)));
        for (int i=0;i<24;++i) assert(!fragmented.advance(5,requests(),controllers(deterministic)));
        assert(batched.replay_json() == fragmented.replay_json());
        auto mixed = requests(); mixed[0] = {"manual",{I{1},I{0},I{1000},I{6000},I{0},I{0}}};
        assert(!batched.advance(73,mixed,controllers(deterministic)));
        assert(!fragmented.advance(73,mixed,controllers(deterministic)));
        assert(batched.replay_json() == fragmented.replay_json());
        // Multi-frame replay rejects a late failure without committing its valid prefix.
        combat::Engine rejected_batch;
        const auto untouched = rejected_batch.json(0,true);
        auto invalid_batch = std::vector<combat::Frame>(batched.journal.begin(),batched.journal.begin()+120);
        invalid_batch[97].observation_hashes[1] ^= 1;
        assert(rejected_batch.replay_frames(invalid_batch));
        assert(rejected_batch.json(0,true) == untouched && rejected_batch.journal.empty());
        invalid_batch[97] = batched.journal[97]; invalid_batch[119].inputs[1].action[4] = 2;
        assert(rejected_batch.replay_frames(invalid_batch));
        assert(rejected_batch.json(0,true) == untouched && rejected_batch.journal.empty());
        assert(rejected_batch.replay_frames({}));
        invalid_batch.push_back(batched.journal[120]);
        assert(rejected_batch.replay_frames(invalid_batch));
        combat::Engine replay;
        const int callbacks_before = calls;
        for (auto frame : batched.journal) assert(!replay.replay_frame(frame));
        assert(calls == callbacks_before && replay.replay_json() == batched.replay_json());
        assert(!replay.advance(11,requests(),controllers(deterministic)));
        assert(!batched.advance(11,requests(),controllers(deterministic)));
        assert(replay.replay_json() == batched.replay_json());
        combat::Engine changed;
        auto frame = batched.journal.front();
        const auto before = changed.replay_json();
        frame.observation_hashes[1] ^= 1;
        assert(changed.replay_frame(frame) && changed.replay_json() == before);
        frame = batched.journal.front(); frame.hash ^= 1;
        assert(changed.replay_frame(frame) && changed.replay_json() == before);
        frame = batched.journal.front(); frame.inputs[1].action[0] = 1001;
        assert(changed.replay_frame(frame) && changed.replay_json() == before);
        frame = batched.journal.front(); frame.tick = 1;
        assert(changed.replay_frame(frame) && changed.replay_json() == before);
        // Source IDs are declared history, not proof of controller identity.
        frame = batched.journal.front(); frame.inputs[1].controller_id = "historical-controller-v0";
        assert(!changed.replay_frame(frame));
        assert(changed.native.hash() == batched.journal.front().hash);
    }
}

}

int main() {
    observation_review();
    transaction_review();
    std::cout << "PASS independent combat observation review: owning snapshots, hidden-mutation invariance, range/cover boundaries, stable visible-field ordering, helper parity, simultaneous pre-step inputs, shared validation, atomic rollback, physics parity, and action replay.\n";
}
