#!/usr/bin/env python3
"""End-to-end single-world tactical tests through live native loopback APIs."""
import copy
import json
import sys
import unittest
import urllib.error
import urllib.request

BASE = 'http://127.0.0.1:' + str(sys.argv.pop(1) if len(sys.argv) > 1 else 8765)
POLICY = {'kind': 'policy', 'sdk_version': 1, 'controller_id': 'public-capture-combat-v1'}
CONFIG = {'boundary_policy': 'ENGINEERING-NOT-OFFICIAL-timer-before-input-v1',
          'credit_strategy': 'EXPERIMENTAL-end-of-second-v1',
          'attack_damage_strategy': 'EXPERIMENTAL-reconciled-projectile-hp-loss-v1',
          'heat_strategy': 'disabled', 'heat_threshold_policy': 'strict-reject-unadjudicated'}


def manual(dx=0,dy=0,aim_x=6000,aim_y=3800,fire=0):
    return {'kind':'manual','action':{'sdk_version':1,'dx':dx,'dy':dy,'aim_x':aim_x,'aim_y':aim_y,'fire':fire}}


def call(path,data=None,headers=None,raw=None):
    body = raw if raw is not None else None if data is None else json.dumps(data).encode()
    request = urllib.request.Request(BASE+path,data=body,
        headers={'X-Sandbox-Client':'1','Content-Type':'application/json',**(headers or {})})
    try:
        with urllib.request.urlopen(request,timeout=90) as response:return response.status,json.load(response)
    except urllib.error.HTTPError as error:return error.code,json.load(error)


class TacticalHTTP(unittest.TestCase):
    def setUp(self):
        status,self.initial=call('/api/tactical/reset',{'observer':0,'config':CONFIG})
        self.assertEqual(status,200,self.initial)

    def step(self,count=1,observer=0,controllers=None):
        status,state=call('/api/tactical/step',{'count':count,'observer':observer,'controllers':controllers or [POLICY,POLICY]})
        self.assertEqual(status,200,state)
        self.assertEqual(state['observer'],observer)
        self.assertEqual(state['observation']['combat']['self_index'],observer)
        self.assertNotIn('hash',state);self.assertNotIn('frames',state);self.assertNotIn('damage_admin',state)
        self.assertNotIn('attack_damage',state['observation']);self.assertNotIn('heat_admin',state)
        return state

    def test_single_run_capture_combat_credit_timeout_replay_and_isolation(self):
        others={p:call(p)[1] for p in ('/api/state','/api/objective/state','/api/layered/state','/api/combat/state')}
        state=self.step(600)
        obs=state['observation']
        self.assertEqual(obs['zones'][1]['controller'],0)
        self.assertEqual(obs['zones'][2]['controller'],1)
        self.assertEqual(obs['rate'],[1,3]);self.assertGreater(obs['score'][0],0)
        self.assertLess(obs['combat']['self']['hp'],100)
        self.assertLess(obs['combat']['self']['ammo'],120)
        # A knockout is not a match ending in this experimental objective scene.
        state=self.step(600)
        self.assertEqual(state['observation']['combat']['self']['hp'],0)
        self.assertEqual(state['terminal'],0)
        while state['tick']<25200:state=self.step(min(600,25200-state['tick']))
        self.assertEqual(state['terminal'],1)
        self.assertEqual(state['observation']['qualified_ms'],420000)
        self.assertEqual(state['observation']['score'],[412,1236])
        self.assertEqual(state['result'],{'outcome':'blue-wins','reason':'timeout-control'})
        _,replay=call('/api/tactical/replay')
        self.assertEqual(len(replay['frames']),25200)
        self.assertLess(len(json.dumps(replay).encode()),48*1024*1024)
        first_fire=min(f['tick'] for f in replay['frames'] if any(v['action'][4] for v in f['inputs']))
        self.assertGreaterEqual(first_fire,530)
        status,restored=call('/api/tactical/import',{'observer':0,'replay':replay})
        self.assertEqual(status,200,restored)
        self.assertEqual(restored['verified_frames'],25200)
        self.assertEqual(restored['observation'],state['observation'])
        self.assertEqual(restored['result'],state['result'])
        self.assertEqual(call('/api/tactical/replay')[1],replay)
        self.assertEqual(call('/api/tactical/step',{'count':1,'observer':0,'controllers':[POLICY,POLICY]})[0],400)
        for path,before in others.items():self.assertEqual(call(path)[1],before,path)

    def test_metadata_and_filtered_public_broadcast_contract(self):
        status,meta=call('/api/tactical/meta');self.assertEqual(status,200,meta)
        self.assertFalse(meta['official']);self.assertEqual(meta['duration'],25200)
        self.assertEqual(meta['policy']['controller_id'],POLICY['controller_id'])
        self.assertEqual(meta['default_config']['preset'],'EXPERIMENTAL-playable-engineering-v3')
        for observer in (0,1):
            status,state=call(f'/api/tactical/state?observer={observer}');self.assertEqual(status,200,state)
            self.assertEqual(set(state),{'build','schema','profile','observation_profile','observation_version','official','observer','tick','terminal','observation','last_sources','config','result'})
            obs=state['observation']
            self.assertEqual(set(obs),{'sdk_version','observation_version','profile','combat','self_heat','zones','score','rate','qualified_ms','duration_ms','credits_enabled'})
            self.assertEqual(obs['score'],[0,0]);self.assertIsNone(obs['combat']['enemy'])
            for zone in obs['zones']:
                self.assertEqual(set(zone),{'id','rect','original_owner','controller','degree'})
                self.assertEqual(zone['controller'],zone['original_owner'])
        for q in ('observer=2','observer=true','observer=0&observer=1','unknown=1'):
            self.assertEqual(call('/api/tactical/state?'+q)[0],400)

    def test_independent_sources_and_config_bound_import(self):
        state=self.step(13,1,[manual(-1000,0,200,100),POLICY])
        _,replay=call('/api/tactical/replay')
        self.assertEqual(replay['frames'][0]['inputs'][0]['action'],[-1000,0,200,100,0])
        self.assertEqual([x['kind'] for x in replay['frames'][0]['inputs']],['manual','policy'])
        self.assertEqual(replay['config'],state['config'])
        custom={**CONFIG,'boundary_policy':'strict-reject-unadjudicated-v1','credit_strategy':'disabled'}
        status,disabled=call('/api/tactical/reset',{'observer':1,'config':custom})
        self.assertEqual(status,200,disabled)
        self.assertIsNone(disabled['observation']['score'])
        self.assertEqual(disabled['config']['preset'],'EXPERIMENTAL-custom-v3')
        status,restored=call('/api/tactical/import',{'observer':1,'replay':replay})
        self.assertEqual(status,200,restored)
        self.assertEqual(restored['config'],state['config'])
        self.assertEqual(restored['observation'],state['observation'])

    def test_bad_requests_and_tampering_are_atomic(self):
        self.step(15,controllers=[manual(400,100),POLICY]);_,before=call('/api/tactical/replay')
        good={'count':1,'observer':0,'controllers':[manual(),POLICY]}
        invalid=[{}, {**good,'count':601}, {**good,'count':True}, {**good,'observer':False},
            {**good,'extra':0}, {**good,'controllers':[POLICY]},
            {**good,'controllers':[manual(),{**POLICY,'controller_id':'different'}]}]
        for field,value in [('dx',1001),('fire',True),('aim_x',12001),('dy',0.5),('sdk_version',2)]:
            request=copy.deepcopy(good);request['controllers'][0]['action'][field]=value;invalid.append(request)
        for request in invalid:
            self.assertEqual(call('/api/tactical/step',request)[0],400,request)
            self.assertEqual(call('/api/tactical/replay')[1],before)
        for mutation in ('hash','observation','action','death','clock','credits','damage','private','build'):
            bad=copy.deepcopy(before)
            if mutation=='hash':bad['frames'][-1]['hash']^=1
            elif mutation=='observation':bad['frames'][-1]['observation_hashes'][1]^=1
            elif mutation=='action':bad['frames'][-1]['inputs'][0]['action'][0]=-1000
            elif mutation=='death':bad['config']['death_strategy']='pretend-official'
            elif mutation=='clock':bad['config']['clock_strategy']='other'
            elif mutation=='credits':bad['config']['credit_strategy']='disabled'
            elif mutation=='damage':bad['config']['attack_damage_strategy']='missing-adjudicated-attack-damage'
            elif mutation=='private':bad['damage_admin']={'attack_damage':[100,0]}
            else:bad['build']='0'*64
            self.assertEqual(call('/api/tactical/import',{'observer':0,'replay':bad})[0],400,mutation)
            self.assertEqual(call('/api/tactical/replay')[1],before)
        self.assertEqual(call('/api/tactical/reset',raw=b'{"observer":0,"observer":1,"config":{}}')[0],400)
        self.assertEqual(call('/api/tactical/reset',{'observer':0,'config':{**CONFIG,'death_strategy':'other'}})[0],400)
        self.assertEqual(call('/api/tactical/replay')[1],before)
        for damage in (None, True, 'nominal-hit-damage'):
            self.assertEqual(call('/api/tactical/reset', {'observer': 0, 'config': {**CONFIG, 'attack_damage_strategy': damage}})[0], 400)
        self.assertEqual(call('/api/tactical/reset', {'observer': 0, 'config': {key: value for key, value in CONFIG.items() if key != 'attack_damage_strategy'}})[0], 400)
        self.assertEqual(call('/api/tactical/replay')[1], before)

    def test_full_timeout_attack_draw_missing_and_replay(self):
        idle = [manual(), manual()]
        for scenario, outcome, reason in (
                ('attack', 'red-wins', 'timeout-attack-damage'),
                ('equal', 'draw', 'timeout-equal'),
                ('missing', 'unresolved-timeout-tie', 'missing-adjudicated-attack-damage')):
            with self.subTest(scenario=scenario):
                config = {**CONFIG}
                if scenario == 'missing':
                    config['attack_damage_strategy'] = 'missing-adjudicated-attack-damage'
                status, state = call('/api/tactical/reset', {'observer': 0, 'config': config})
                self.assertEqual(status, 200, state)
                if scenario == 'attack':
                    # Cross the blue outpost quickly, stop outside it, then shoot.
                    self.step(30, controllers=[manual(dy=1000), manual()])
                    self.step(120, controllers=[manual(dx=1000), manual()])
                    state = self.step(600, controllers=[manual(aim_x=10400, aim_y=3800, fire=1), manual()])
                    self.assertEqual(state['observation']['score'], [0, 0])
                    self.assertEqual(state['terminal'], 0)
                    status, opponent = call('/api/tactical/state?observer=1')
                    self.assertEqual(status, 200, opponent)
                    self.assertEqual(opponent['observation']['combat']['self']['hp'], 0)
                while state['tick'] < 25200:
                    tick = state['tick']
                    state = self.step(min(600, 25200 - tick), controllers=idle)
                    self.assertGreater(state['tick'], tick)
                self.assertEqual(state['result'], {'outcome': outcome, 'reason': reason})
                self.assertEqual(state['observation']['score'], [0, 0])
                status, replay = call('/api/tactical/replay')
                self.assertEqual(status, 200, replay)
                self.assertNotIn('damage_admin', replay)
                self.assertEqual(len(replay['frames']), 25200)
                status, restored = call('/api/tactical/import', {'observer': 0, 'replay': replay})
                self.assertEqual(status, 200, restored)
                self.assertEqual(restored['verified_frames'], 25200)
                self.assertEqual(restored['result'], state['result'])
                self.assertEqual(restored['observation'], state['observation'])
                self.assertEqual(call('/api/tactical/replay')[1], replay)

    def test_default_heat_full_420_seconds_requested_fire_and_replay(self):
        # Default thermal strategy, real accepted manual inputs, no hidden-state injection.
        status, meta = call('/api/tactical/meta')
        self.assertEqual(status, 200, meta)
        config = {key: meta['default_config'][key] for key in CONFIG}
        self.assertEqual(config['heat_strategy'], 'EXPERIMENTAL-fixed-level1-cooling-priority-17mm-v1')
        self.assertEqual(config['heat_threshold_policy'], 'strict-reject-unadjudicated')
        status, state = call('/api/tactical/reset', {'observer': 0, 'config': config})
        self.assertEqual(status, 200, state)
        # Fire left into the arena edge; the opponent stays far to the right.
        controllers = [manual(aim_x=0, aim_y=3800, fire=1), manual()]
        state = self.step(51, controllers=controllers)
        heat = state['observation']['self_heat']
        self.assertTrue(heat['enabled']);self.assertTrue(heat['overheat_locked'])
        self.assertTrue(heat['firing_locked']);self.assertFalse(heat['permanent_latched'])
        self.assertEqual(state['observation']['combat']['self']['ammo'], 115)
        self.assertEqual(state['observation']['combat']['self']['hp'], 100)
        self.assertNotIn('video_locked', heat)
        state = self.step(60, controllers=controllers)
        self.assertEqual(state['observation']['combat']['self']['ammo'], 115,
                         'Held fire attempts during a heat lock do not consume ammo')
        self.assertLess(state['observation']['self_heat']['heat_tenths'], heat['heat_tenths'])
        while state['tick'] < 25200:
            state = self.step(min(600, 25200-state['tick']), controllers=controllers)
        self.assertEqual(state['result'], {'outcome':'draw', 'reason':'timeout-equal'})
        self.assertEqual(state['observation']['combat']['self']['hp'], 100)
        self.assertEqual(state['observation']['combat']['self']['ammo'], 0)
        self.assertEqual(state['observation']['self_heat']['heat_tenths'], 0)
        self.assertFalse(state['observation']['self_heat']['firing_locked'])
        status, other = call('/api/tactical/state?observer=1')
        self.assertEqual(status, 200, other)
        self.assertEqual(other['observation']['combat']['self']['hp'], 100)
        self.assertEqual(other['observation']['self_heat']['heat_tenths'], 0)
        self.assertNotIn('enemy_heat', other['observation'])
        status, replay = call('/api/tactical/replay')
        self.assertEqual(status, 200, replay)
        self.assertEqual(replay['engine'], 'integer-mm-tactical-v3')
        self.assertEqual(replay['observation_version'], 2)
        self.assertEqual(len(replay['frames']), 25200)
        self.assertTrue(all(frame['inputs'][0]['action'][4] == 1 for frame in replay['frames']))
        self.assertNotIn('heat_admin', replay)
        status, restored = call('/api/tactical/import', {'observer': 0, 'replay': replay})
        self.assertEqual(status, 200, restored)
        self.assertEqual(restored['verified_frames'], 25200)
        self.assertEqual(restored['observation'], state['observation'])
        self.assertEqual(restored['result'], state['result'])
        self.assertEqual(call('/api/tactical/replay')[1], replay)

    def test_heat_config_and_replay_rejection_are_atomic(self):
        self.step(7, controllers=[manual(), manual()])
        _, before = call('/api/tactical/replay')
        for key in ('heat_strategy', 'heat_threshold_policy'):
            values = [None, True, 0, {}, 'not-a-strategy']
            for value in values:
                status, _ = call('/api/tactical/reset', {'observer': 0, 'config': {**CONFIG, key:value}})
                self.assertEqual(status, 400, (key,value))
                self.assertEqual(call('/api/tactical/replay')[1], before)
            status, _ = call('/api/tactical/reset', {'observer': 0, 'config': {k:v for k,v in CONFIG.items() if k != key}})
            self.assertEqual(status, 400)
        for mutate in (
            lambda replay: replay['config'].__setitem__('heat_strategy', 'EXPERIMENTAL-fixed-level1-cooling-priority-17mm-v1'),
            lambda replay: replay['config'].__setitem__('heat_threshold_policy', 'ENGINEERING-flowchart-greater-equal-v1'),
            lambda replay: replay['config'].__setitem__('heat_boundary_policy', 'shot-before-cooling'),
            lambda replay: replay['config'].__setitem__('heat_death_policy', 'forget-permanent-lock'),
            lambda replay: replay.__setitem__('observation_version', 1),
            lambda replay: replay.__setitem__('heat_admin', {'robots': []})):
            replay = copy.deepcopy(before); mutate(replay)
            self.assertEqual(call('/api/tactical/import', {'observer': 0, 'replay': replay})[0], 400)
            self.assertEqual(call('/api/tactical/replay')[1], before)

    def test_unknown_endpoint_and_origin_guards(self):
        self.assertEqual(call('/api/tactical/reset',{'observer':0,'config':CONFIG},{'Origin':'https://example.org'})[0],403)
        self.assertEqual(call('/api/tactical/reset',{'observer':0,'config':CONFIG},{'X-Sandbox-Client':'0'})[0],403)
        self.assertEqual(call('/api/tactical/no-such')[0],404)
        self.assertEqual(call('/api/tactical/no-such',{})[0],404)


if __name__=='__main__':unittest.main()
