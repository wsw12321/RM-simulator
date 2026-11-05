// Independent review fixtures. Synthetic state setup is test-only, never a
// runtime API or claim that physical occupancy is officially adjudicated.
#include "../tactical_mode/core.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace tactical {
struct ReviewTestProbe {
    static const auto& zones(const Engine& e) { return e.zones_; }
    static auto& zones(Engine& e) { return e.zones_; }
    static const auto& ledger(const Engine& e) { return e.ledger_; }
    static auto unresolved(const Engine& e) { return e.unresolved_; }
    static auto decision(const Engine& e) { return e.decision_; }
    static std::optional<std::string> rules(Engine& e, std::int64_t t,
            std::array<std::array<bool,2>,4> physical, std::array<bool,2> died = {}) {
        auto candidate = e;
        auto error = candidate.advance_rules(t, physical, died);
        if (!error) e = std::move(candidate);
        return error;
    }
};
}
namespace {
namespace t = tactical;
namespace p = tactical::policy;
namespace cap = tactical::cap;
namespace match = tactical::match;
using Probe = t::ReviewTestProbe;
using I = p::Integer;
int assertions = 0;
void check(bool condition, const std::string& message) {
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}
void succeeds(const std::optional<std::string>& error, const std::string& message) {
    check(!error, message + (error ? ": " + *error : ""));
}
template<class T> T get(cap::Result<T> result) {
    if (auto error = std::get_if<cap::Error>(&result))
        throw std::runtime_error(std::string(cap::error_name(*error)));
    return std::get<T>(std::move(result));
}
p::Action action(I dx=0,I dy=0,I ax=6000,I ay=3800,I fire=0) {
    return {I{1},dx,dy,ax,ay,fire};
}
auto manual(p::Action red=action(), p::Action blue=action()) {
    return std::array<t::Request,2>{{{"manual",red},{"manual",blue}}};
}
auto policies() { return std::array<t::Request,2>{{{"policy",{}},{"policy",{}}}}; }
auto controllers(p::DecisionFunction decide) {
    return std::array<t::Controller,2>{{{{1,"independent-review-v1"},decide},
                                      {{1,"independent-review-v1"},decide}}};
}
bool same(const p::Action& a, const p::Action& b) {
    return a.sdk_version==b.sdk_version && a.dx==b.dx && a.dy==b.dy &&
           a.aim_x==b.aim_x && a.aim_y==b.aim_y && a.fire==b.fire;
}
auto bytes(const t::Engine& e, int self=0) { return t::observation_json(e.observation(self)); }

#define ABSENT_MEMBER(member) \
 template<class T, class=void> struct has_##member : std::false_type {}; \
 template<class T> struct has_##member<T,std::void_t<decltype(std::declval<T>().member)>> : std::true_type {};
ABSENT_MEMBER(progress_ms) ABSENT_MEMBER(detectors) ABSENT_MEMBER(active)
ABSENT_MEMBER(detected) ABSENT_MEMBER(deactivation_at_ms) ABSENT_MEMBER(completed_at_ms)
ABSENT_MEMBER(hash) ABSENT_MEMBER(events) ABSENT_MEMBER(robots) ABSENT_MEMBER(terminal)
ABSENT_MEMBER(hp) ABSENT_MEMBER(shots) ABSENT_MEMBER(hits)
#undef ABSENT_MEMBER
static_assert(!has_progress_ms<p::Zone>::value && !has_detectors<p::Zone>::value);
static_assert(!has_active<p::Zone>::value && !has_detected<p::Zone>::value);
static_assert(!has_deactivation_at_ms<p::Zone>::value && !has_completed_at_ms<p::Zone>::value);
static_assert(!has_hash<p::Observation>::value && !has_events<p::Observation>::value);
static_assert(!has_robots<p::Observation>::value && !has_terminal<p::Observation>::value);
static_assert(!has_hp<p::Enemy>::value && !has_hits<p::Self>::value && !has_shots<p::Self>::value);
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().combat)>);
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().zones)>);
static_assert(std::is_const_v<decltype(std::declval<p::Observation>().score)>);

void privacy() {
    t::Engine engine;
    engine.native.state.robots[0] = {1000,1000,0};
    engine.native.state.robots[1] = {11000,1000,128};
    engine.native.state.bullets = {{5,0,2000,1000,20,0,150},{8,1,3000,1000,-10,0,90}};
    const auto expected = bytes(engine);
    const auto decision = p::decide(engine.observation(0));
    const auto retained = engine.observation(0);
    // Public control, score, rate and time stay fixed. Hidden objective progress,
    // pending grace, presence, completion bookkeeping and native details vary.
    std::mt19937 rng(947801);
    for (int iteration=0; iteration<200; ++iteration) {
        auto changed = engine;
        for (int zi=0; zi<4; ++zi) {
            auto& zone = Probe::zones(changed)[zi];
            const auto control = zone.capture.snapshot().control;
            const auto owner = control.controller == cap::Team::Red;
            zone.capture = get(cap::CaptureAttempt::start(0,control,{!owner,owner},changed.config().boundary));
            get(zone.capture.apply(1+rng()%4998));
            zone.detectors[0] = get(cap::ActivationDelay::start(0,cap::QualifiedDetection::Present,changed.config().boundary));
            get(zone.detectors[0].apply(1+rng()%1000,cap::QualifiedDetection::Absent));
            zone.detectors[1] = get(cap::ActivationDelay::start(0,cap::QualifiedDetection::Absent,changed.config().boundary));
            zone.completed_at_ms = 10000+rng()%10000; // hidden diagnostic only
        }
        auto& native = changed.native;
        native.state.robots[0].shots = int(rng()%120);
        native.state.robots[0].hits = int(rng()%120);
        native.state.robots[1] = {10500+int(rng()%1000),6500+int(rng()%500),int(rng()%256),
            int(rng()%101),int(rng()%121),int(rng()%11),int(rng()%120),int(rng()%120)};
        native.state.next_id = int(rng()%1000000);
        native.state.terminal = int(rng()%2);
        for (auto& bullet : native.state.bullets) {
            bullet.id = int(rng()%1000000); bullet.owner = int(rng()%2); bullet.life = 1+int(rng()%180);
        }
        std::reverse(native.state.bullets.begin(),native.state.bullets.end());
        native.state.bullets.push_back({4,1,11000,1000,260,0,80});
        native.state.bullets.push_back({9,0,6000,3800,-260,0,80});
        native.events.push_back({0,2,1,0,10});
        native.journal.push_back({0,{},std::uint32_t(rng())});
        check(bytes(changed)==expected,"hidden objective/combat mutations changed observation bytes");
        check(same(p::decide(changed.observation(0)),decision),"pure policy changed under fixed public facts");
    }
    check(t::observation_json(retained)==expected,"owning observation changed after other engine mutations");
    auto visible = engine;
    visible.native.state.robots[1] = {4000,1000,128};
    const auto visible_expected = bytes(visible);
    visible.native.state.robots[1].hp=0;
    visible.native.state.robots[1].ammo=0;
    visible.native.state.robots[1].cooldown=10;
    check(bytes(visible)==visible_expected,"visible enemy HP leaked through presence or objective snapshot");
}

void boundary_timing() {
    using Physical = std::array<std::array<bool,2>,4>;
    // Identical qualifying loss at 3.001 s: ordinary leave retains the verified
    // 2 s grace; experimental death cancels it immediately.
    t::Engine ordinary, death;
    Physical physical{}; physical[0][1]=true;
    succeeds(Probe::rules(ordinary,1,physical),"ordinary acquire");
    succeeds(Probe::rules(death,1,physical),"death acquire");
    physical[0][1]=false;
    succeeds(Probe::rules(ordinary,3001,physical),"ordinary leave");
    succeeds(Probe::rules(death,3001,physical,{false,true}),"immediate death");
    check(Probe::zones(ordinary)[0].detectors[1].snapshot().logically_active,"ordinary leave lost its grace");
    check(!Probe::zones(death)[0].detectors[1].snapshot().logically_active,"death inherited ordinary grace");
    check(!Probe::zones(death)[0].detectors[1].snapshot().deactivation_at_ms,"death retains pending grace");
    succeeds(Probe::rules(ordinary,5002,physical),"ordinary expiry/capture");
    succeeds(Probe::rules(death,5002,physical),"post-death decay");
    check(Probe::zones(ordinary)[0].completed_at_ms==5001,"ordinary capture was rounded to sample");
    check(!Probe::zones(death)[0].capture.snapshot().completed,"dead robot completed capture");
    check(Probe::zones(death)[0].capture.snapshot().progress_ms==999,"dead robot progress failed to decay immediately");

    for (auto boundary : {cap::BoundaryPolicy::StrictRejectUnadjudicated,
                          cap::BoundaryPolicy::EngineeringTimerBeforeInputV1}) {
        t::Engine engine({boundary,true});
        // Actual combat kills red exactly at 5017 ms, its capture deadline.
        engine.native.state.robots[0] = {6900,6100,0,10};
        engine.native.state.robots[1] = {10400,1000,128};
        succeeds(engine.advance(299,manual()),"capture approach");
        engine.native.state.bullets.push_back({1,1,6300,6100,260,0,100});
        const auto before = engine.replay_json();
        const auto before_state = engine.json(0,true);
        const auto error = engine.advance(2,manual());
        if (boundary == cap::BoundaryPolicy::StrictRejectUnadjudicated) {
            check(error && error->find("UnadjudicatedCoincidence")!=std::string::npos,"strict death/completion ambiguity accepted");
            check(engine.replay_json()==before && engine.json(0,true)==before_state,"late strict error committed prefix combat/objective state");
            check(engine.native.state.robots[0].hp==10 && engine.native.state.tick==299,"strict rollback did not preserve live combat");
        } else {
            succeeds(error,"engineering timer-before-death");
            check(engine.native.state.robots[0].hp==0 && !engine.native.state.terminal,"disable incorrectly ended the run");
            check(Probe::zones(engine)[1].completed_at_ms==5017,"named timer-first boundary did not capture at exact deadline");
            check(!Probe::zones(engine)[1].detectors[0].snapshot().logically_active,"dead detector remained active after completion");
            succeeds(engine.advance(600,manual(action(1000,1000,0,0,1))),"continue after disable");
            check(engine.native.state.robots[0].x==6900 && engine.native.state.robots[0].hp==0,"disabled robot moved or respawned");
        }

        // Neither ordering invents a rule for a contested position becoming
        // empty. Simultaneous combat deaths must reject atomically as well.
        t::Engine contested({boundary,true});
        contested.native.state.robots[0]={6500,5700,0,10};
        contested.native.state.robots[1]={7300,6500,128,10};
        succeeds(contested.advance(10,manual()),"establish contested position");
        contested.native.state.bullets={{1,1,6500,5700,0,0,100},{2,0,7300,6500,0,0,100}};
        const auto contested_before=contested.replay_json();
        const auto contested_state=contested.json(0,true);
        const auto contested_error=contested.advance(1,manual());
        check(contested_error && contested_error->find("UnadjudicatedContestExitToNone")!=std::string::npos,
              "simultaneous deaths silently adjudicated contested-to-neither");
        check(contested.replay_json()==contested_before && contested.json(0,true)==contested_state,
              "unadjudicated simultaneous deaths mutated the run");
    }
    // Capture immediately before an end-of-second sample gets that credit,
    // never the preceding one. Exact same-time completion also precedes credit.
    t::Engine exact;
    Physical physical_exact{}; physical_exact[1][0]=true;
    succeeds(Probe::rules(exact,1000,physical_exact),"exact-second activation");
    // Test-only alignment of qualified objective clock and simulation clock.
    exact.native.state.tick=60;
    exact.native.state.robots[0]={6900,6100,0};
    exact.native.state.robots[1]={10400,1000,128};
    succeeds(exact.advance(299,manual()),"before exact-second capture");
    check(exact.observation(0).score[0]==0,"score arrived before capture");
    succeeds(exact.advance(1,manual()),"capture at exact credit boundary");
    check(Probe::zones(exact)[1].completed_at_ms==6000,"exact second capture timestamp");
    check(exact.observation(0).score[0]==1,"capture did not precede same-time credit");
}

std::array<p::Action,2> callback_actions{action(),action()};
std::vector<std::pair<int,std::string>> observed;
int callback_failure_tick=-1;
p::Action callback(const p::Observation& o) {
    observed.emplace_back(int(o.combat.self_index),t::observation_json(o));
    if (o.combat.self_index==1 && o.combat.tick==callback_failure_tick)
        throw std::runtime_error("private callback diagnostic");
    return callback_actions[o.combat.self_index];
}
void transaction_and_replay() {
    t::Engine engine, reference;
    callback_actions={action(1000,0),action(-1000,0)};
    observed.clear();
    succeeds(engine.advance(9,policies(),controllers(callback)),"two callback advance");
    check(observed.size()==18,"wrong callback count");
    for (int tick=0;tick<9;++tick) {
        for (int self=0;self<2;++self) {
            check(observed[tick*2+self].first==self,"callback order metadata");
            check(observed[tick*2+self].second==bytes(reference,self),"callback was not frozen at common pre-step state");
        }
        succeeds(reference.advance(1,manual(callback_actions[0],callback_actions[1])),"manual reference advance");
    }
    check(engine.hash()==reference.hash(),"manual/policy shared physical path diverged");
    const auto before=engine.replay_json();
    const auto before_state=engine.json(0,true);
    callback_failure_tick=13;
    auto error=engine.advance(10,policies(),controllers(callback));
    check(error.has_value(),"late callback exception accepted");
    check(error->find("private callback diagnostic")==std::string::npos,"callback exception detail leaked");
    check(engine.replay_json()==before && engine.json(0,true)==before_state,"late callback failure committed prefix");
    callback_failure_tick=-1;
    for (const p::Scalar bad : {p::Scalar{true},p::Scalar{1.0},p::Scalar{I{2}}}) {
        auto invalid=action(); invalid.fire=bad;
        callback_actions[1]=invalid;
        const auto manual_error=engine.advance(1,manual(action(),invalid));
        const auto policy_error=engine.advance(1,policies(),controllers(callback));
        check(manual_error==p::validate_action(invalid) && policy_error==manual_error,"manual and policy validation differ");
        check(engine.replay_json()==before,"invalid action changed world");
    }
    t::Engine replay;
    succeeds(replay.replay_frames(engine.journal),"callback-free action replay");
    check(replay.replay_json()==engine.replay_json(),"replay did not preserve exact journal");
    auto bad_frames=engine.journal; bad_frames.back().hash^=1;
    t::Engine failed;
    const auto initial=failed.replay_json();
    check(failed.replay_frames(bad_frames).has_value(),"late replay divergence accepted");
    check(failed.replay_json()==initial,"late replay failure committed prefix");
}

p::Action toward(const sandbox::Robot& robot, int x, int y, int fire=0) {
    return action(std::clamp((x-robot.x)*1000/55,-1000,1000),
                  std::clamp((y-robot.y)*1000/55,-1000,1000),x,y,fire);
}
void full_vertical_run() {
    t::Engine engine;
    for (int i=0;i<520;++i) {
        const auto& red=engine.native.state.robots[0];
        const auto& blue=engine.native.state.robots[1];
        auto red_action = red.y<5199 && red.x<6800 ? toward(red,1600,5200) :
                          red.x<6899 ? toward(red,6900,5200) : toward(red,6900,6100);
        auto blue_action = blue.y<5199 ? toward(blue,10400,5200) : toward(blue,7700,5200);
        succeeds(engine.advance(1,manual(red_action,blue_action)),"physical route to objective");
    }
    const auto capture=Probe::zones(engine)[1].completed_at_ms;
    check(capture.has_value(),"same-run physical movement did not capture blue outpost");
    check(engine.observation(0).rate[0]==1 && engine.observation(0).score[0]>0,"capture did not cause public rate and credit");
    const auto& blue=engine.native.state.robots[1];
    // Heat-enabled default needs a cooling pause between bursts. Keep this a
    // genuine action-only kill rather than bypassing the heat gate in the fixture.
    succeeds(engine.advance(600,manual(action(0,0,blue.x,blue.y,1),action())),"same-run firing with heat");
    check(engine.native.state.robots[1].hp==0,"same-run projectile damage did not disable opponent");
    check(!engine.native.state.terminal,"robot disable ended tactical match early");
    const auto post_kill=engine.native.state.robots[1];
    while (!engine.native.state.terminal)
        succeeds(engine.advance(std::min<I>(t::kMaxBatch,t::kDurationTicks-engine.native.state.tick),manual()),"finish genuine 420 s run");
    check(engine.native.state.tick==25200 && t::Engine::time_ms(engine.native.state.tick)==420000,"timeout is not exactly 420 s");
    check(Probe::ledger(engine).last_sequence()==420,"missing/duplicate end-of-second credit including final second");
    const auto first_credit=(*capture+999)/1000;
    check(engine.observation(0).score[0]==421-first_credit,"end-of-second capture scoring arithmetic differs");
    check(Probe::decision(engine).outcome==match::Outcome::RedWins &&
          Probe::decision(engine).reason==match::Reason::TimeoutControl,"unequal score not decided by verified control priority");
    check(engine.native.state.robots[1].hp==0 && engine.native.state.robots[1].x==post_kill.x,"disabled opponent respawned or moved");
    const auto terminal=engine.replay_json();
    check(engine.advance(1,manual()).has_value(),"terminal continuation accepted");
    check(engine.replay_json()==terminal,"terminal continuation changed replay");
    t::Engine replay;
    for (std::size_t at=0;at<engine.journal.size();at+=t::kMaxBatch) {
        const auto end=std::min(engine.journal.size(),at+std::size_t(t::kMaxBatch));
        succeeds(replay.replay_frames({engine.journal.begin()+at,engine.journal.begin()+end}),"full 420 s resolved-action replay");
    }
    check(replay.replay_json()==terminal,"full combat/capture/credit/timeout replay diverged");
    // Tied control does not invent an official damage ledger or use HP as a
    // shortcut, even when the two HP values are visibly unequal.
    t::Engine tied;
    tied.native.state.robots[0].hp=10;
    while (!tied.native.state.terminal)
        succeeds(tied.advance(t::kMaxBatch,manual()),"tied timeout");
    check(Probe::unresolved(tied)==match::Error::MissingAttackDamage,"tied score fabricated attack damage or HP tiebreak");
    check(Probe::decision(tied).outcome==match::Outcome::Ongoing,"unadjudicated tie claimed a winner/draw");
}
}
int main() {
    try {
        privacy(); boundary_timing(); transaction_and_replay(); full_vertical_run();
        std::cout << "PASS independent tactical review: " << assertions
                  << " assertions; fixed-public-fact privacy, death/grace and exact-second boundaries,"
                     " transactional callbacks/replay, one real combat/capture/credit/420 s run,"
                     " unresolved attack-ledger tie and terminal continuation.\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL independent tactical review: " << error.what() << '\n';
        return 1;
    }
}
