#include "../tactical_mode/core.hpp"
#include <cassert>
#include <iostream>
namespace tactical {
struct DamageTestProbe {
    static auto totals(const Engine& e){return e.damage_.totals();}
    static auto nominal(const Engine& e){return e.nominal_projectile_damage_;}
    static bool complete(const Engine& e){return e.damage_complete_;}
    static auto sequence(const Engine& e){return e.damage_.last_sequence();}
    static bool sealed(const Engine& e){return e.damage_.sealed();}
    static void reconcile(Engine& e,const std::array<int,2>& before,int tick){e.reconcile_damage(before,tick);}
    static void private_damage(Engine& e) {
        auto r=e.damage_.apply({1,0,1,damage::Team::Blue,damage::EntityKind::Robot,damage::Cause::Projectile,100,80,damage::Team::Red});
        assert(std::holds_alternative<damage::Settlement>(r));
    }
    static void decision(Engine& e,match::Outcome outcome,match::Reason reason){e.decision_={outcome,reason,"test-only"};}
};
}
namespace t=tactical;
using I=t::I;
auto action(I dx=0,I dy=0,I ax=6000,I ay=3800,I fire=0){return t::policy::Action{I{1},dx,dy,ax,ay,fire};}
auto manual(t::policy::Action red=action(),t::policy::Action blue=action()) {
    return std::array<t::Request,2>{{{"manual",red},{"manual",blue}}};
}
void ticks(t::Engine& e,int count) {while(count){int n=std::min(count,600);assert(!e.advance(n,manual()));count-=n;}}
void until_end(t::Engine& e) {while(!e.native.state.terminal)assert(!e.advance(600,manual()));}
void hits(t::Engine& e,int victim,int count) {
    const auto target=e.native.state.robots[victim];
    for(int i=0;i<count;++i)e.native.state.bullets.push_back({e.native.state.next_id++,1-victim,target.x,target.y,0,0,2});
}
int main() {
    using Probe=t::DamageTestProbe;
    // Two same-tick overkill hits after90 actual damage: nominal110, actual100.
    {
        t::Engine e;hits(e,1,9);ticks(e,1);assert(e.native.state.robots[1].hp==10);
        hits(e,1,2);ticks(e,1);
        assert(Probe::complete(e)&&Probe::totals(e).attack_damage[0]==100&&Probe::nominal(e)[0]==110);
        assert(Probe::totals(e).actual_hp_loss[1]==100&&Probe::sequence(e)==2&&!e.native.state.terminal);
        const auto obs=t::observation_json(e.observation(0));
        for(auto word:{"attack_damage","damage_admin","actual_hp_loss","nominal_projectile_damage"})assert(obs.find(word)==std::string::npos);
        const auto total=Probe::totals(e);ticks(e,60);assert(Probe::totals(e)==total);
    }
    // Both victim deductions are settled in the same tick, not first-shooter KO.
    {
        t::Engine e;hits(e,0,11);hits(e,1,12);ticks(e,1);
        assert(Probe::complete(e));assert((Probe::totals(e).attack_damage==std::array<std::int64_t,2>{100,100}));
        assert((Probe::nominal(e)==std::array<std::int64_t,2>{120,110}));
        assert(Probe::sequence(e)==2&&!e.native.state.terminal);
        until_end(e);assert(e.json(0).find("timeout-equal")!=std::string::npos&&Probe::sealed(e));
        const auto before=e.json(0,true);assert(e.advance(1,manual()));assert(before==e.json(0,true));
    }
    // Existing projectiles may settle after their owner is disabled; the native
    // adapter must not invent a source-alive condition or discard that loss.
    {
        t::Engine e;hits(e,0,10);
        e.native.state.bullets.push_back({e.native.state.next_id++,0,11000,3800,-100,0,5});
        ticks(e,1);assert(e.native.state.robots[0].hp==0&&e.native.state.robots[1].hp==100);
        ticks(e,2);assert(Probe::complete(e));
        assert((Probe::totals(e).attack_damage==std::array<std::int64_t,2>{10,100}));
    }
    // Private ledger state changes the authoritative hash, never policy bytes.
    {
        t::Engine a,b;Probe::private_damage(b);assert(a.hash()!=b.hash());
        for(int side=0;side<2;++side)assert(t::observation_json(a.observation(side))==t::observation_json(b.observation(side)));
    }
    // Unknown loss and same-source/target events do not become invented zero.
    {
        t::Engine e;e.native.state.robots[1].hp=80;ticks(e,1);assert(!Probe::complete(e));
        e.native.state.robots[1].hp=100;ticks(e,1);assert(!Probe::complete(e));
        until_end(e);assert(e.json(0).find("missing-adjudicated-attack-damage")!=std::string::npos);
        for(int kind:{2,99}) {
            t::Engine f;f.native.state.tick=1;f.native.state.robots[1].hp=90;
            f.native.events={{0,kind,1,1,10}};Probe::reconcile(f,{100,100},0);
            assert(!Probe::complete(f)&&Probe::sequence(f)==0&&Probe::totals(f).attack_damage[0]==0);
        }
    }
    // Native terminal-tick inclusion is an EXPERIMENTAL adapter ordering only.
    {
        t::Engine e;ticks(e,25199);assert(!Probe::sealed(e));hits(e,1,11);ticks(e,1);
        assert(e.native.state.tick==25200&&e.native.state.terminal&&Probe::sealed(e));
        assert(Probe::totals(e).attack_damage[0]==100&&e.json(0).find("timeout-attack-damage")!=std::string::npos);
    }
    // Genuine action-only playable damage tiebreak, replayable from reset.
    {
        t::Engine e;assert(!e.advance(30,manual(action(0,1000))));
        assert(!e.advance(120,manual(action(1000,0))));
        assert(!e.advance(600,manual(action(0,0,10400,3800,1))));
        assert(e.native.state.robots[1].hp==0&&Probe::totals(e).attack_damage[0]==100);
        until_end(e);
        assert((e.observation(0).score==std::array<std::int64_t,2>{0,0}));
        assert(e.json(0).find("\"outcome\":\"red-wins\",\"reason\":\"timeout-attack-damage\"")!=std::string::npos);
        t::Engine replay;
        for(std::size_t begin=0;begin<e.journal.size();begin+=600) {
            auto end=std::min(begin+600,e.journal.size());
            assert(!replay.replay_frames({e.journal.begin()+std::ptrdiff_t(begin),e.journal.begin()+std::ptrdiff_t(end)}));
        }
        assert(e.replay_json()==replay.replay_json()&&e.json(0)==replay.json(0));
        auto tampered=e.journal;tampered[700].hash^=1;t::Engine rejected;
        const auto before=rejected.json(0,true);assert(rejected.replay_frames({tampered.begin(),tampered.begin()+600})==std::nullopt);
        const auto prefix=rejected.json(0,true);
        assert(rejected.replay_frames({tampered.begin()+600,tampered.begin()+900}));assert(rejected.json(0,true)==prefix&&before!=prefix);
    }
    // Result serialization preserves judge reason instead of relabeling winners.
    for(auto reason:{t::match::Reason::TimeoutControl,t::match::Reason::TimeoutAttackDamage,t::match::Reason::TimeoutSurvivingRobotHp}) {
        t::Engine e;Probe::decision(e,t::match::Outcome::BlueWins,reason);
        const auto expected=reason==t::match::Reason::TimeoutControl ? "timeout-control" : reason==t::match::Reason::TimeoutAttackDamage ? "timeout-attack-damage" : "timeout-surviving-robot-hp";
        assert(e.json(0).find(expected)!=std::string::npos);
    }
    std::cout<<"tactical damage tests passed\n";
}
