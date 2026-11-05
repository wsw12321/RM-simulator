#!/usr/bin/env python3
"""Live HTTP/native integration for the separate SYNTHETIC roster experiment."""
import copy
import json
import sys
import unittest
import urllib.error
import urllib.request

BASE = 'http://127.0.0.1:' + str(sys.argv.pop(1) if len(sys.argv)>1 else 8765)


def call(path,data=None,raw=None):
    body = raw.encode() if raw is not None else None if data is None else json.dumps(data).encode()
    headers = {'Content-Type':'application/json','X-Sandbox-Client':'1'} if body is not None else {}
    request = urllib.request.Request(BASE+path,data=body,headers=headers)
    try:
        with urllib.request.urlopen(request,timeout=60) as response:
            return response.status,json.load(response)
    except urllib.error.HTTPError as error:
        return error.code,json.load(error)


def manual(rid,dx=0,dy=0,aim_x=6000,aim_y=3800,fire=0):
    return {'id':rid,'kind':'manual','action':{'sdk_version':1,'dx':dx,'dy':dy,'aim_x':aim_x,'aim_y':aim_y,'fire':fire}}


class RosterHTTP(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        status,cls.meta = call('/api/roster/meta')
        if status!=200:
            raise AssertionError(cls.meta)
        cls.config = cls.meta['default_config']
        cls.ids = [item['id'] for item in cls.config['robots']]
        cls.observer = 'red-1'
        cls.original = {route:call(route)[1] for route in
            ['/api/state','/api/objective/state','/api/layered/state','/api/combat/replay','/api/tactical/replay']}

    def setUp(self):
        self.assertEqual(call('/api/roster/reset',{'observer':self.observer,'config':self.config})[0],200)

    def read(self,observer=None):
        status,body = call('/api/roster/state'+('' if observer is None else '?observer='+observer))
        self.assertEqual(status,200,body)
        return body

    def step(self,count=1,controllers=None,observer=None):
        status,body = call('/api/roster/step',{'count':count,'observer':observer or self.observer,
            'controllers':controllers if controllers is not None else [manual(rid) for rid in self.ids]})
        self.assertEqual(status,200,body)
        return body

    def test_meta_and_filtered_2v2_state(self):
        self.assertEqual(self.meta['max_robots'],8)
        self.assertEqual(self.meta['max_team_robots'],4)
        self.assertEqual(len(self.ids),4)
        self.assertEqual(self.ids,sorted(self.ids))
        self.assertEqual([sum(robot['team']==team for robot in self.config['robots']) for team in (0,1)],[2,2])
        state = self.read(self.observer)
        obs = state['observation']
        self.assertEqual(state['observer'],self.observer)
        self.assertEqual(obs['self_id'],self.observer)
        self.assertEqual(obs['roster'],self.config['robots'])
        self.assertEqual(obs['score'],[0,0])
        self.assertEqual([c['id'] for c in obs['contacts']],['red-2'])
        self.assertEqual(set(state),{'build','schema','profile','observation_profile','observation_version',
            'official','observer','tick','terminal','config','result','observation','last_sources'})
        for contact in obs['contacts']:
            self.assertEqual(set(contact),{'id','team','x','y','yaw'})
        for zone in obs['zones']:
            self.assertEqual(set(zone),{'id','rect','original_owner','controller','degree'})
        for key in ('hash','frames','damage_admin','heat_admin'):
            self.assertNotIn(key,state)
        for robot in self.config['robots']:
            self.assertTrue(robot['profile']['id'].startswith('SYNTHETIC-'))
        self.assertEqual(self.read()['observer'],self.ids[0])

    def test_all_ids_order_independent_actions_and_sources(self):
        before = {rid:self.read(rid)['observation']['self'] for rid in self.ids}
        controls = [manual(rid,dx=1000 if rid=='red-1' else -1000 if rid=='blue-2' else 0) for rid in reversed(self.ids)]
        state = self.step(1,controls)
        self.assertEqual([source['id'] for source in state['last_sources']],self.ids)
        for rid in self.ids:
            after = self.read(rid)['observation']['self']
            delta = 55 if rid=='red-1' else -65 if rid=='blue-2' else 0
            self.assertEqual(after['x'],before[rid]['x']+delta)
        policy = self.meta['policy']
        controls = [{'id':rid,'kind':'policy','sdk_version':1,'controller_id':policy['controller_id']} if rid in ('red-2','blue-1') else manual(rid) for rid in self.ids]
        self.step(30,controls)
        replay = call('/api/roster/replay')[1]
        self.assertEqual(len(replay['frames']),31)
        for frame in replay['frames'][1:]:
            self.assertEqual([item['id'] for item in frame['inputs']],self.ids)
            self.assertEqual([item['kind'] for item in frame['inputs']],['policy','manual','manual','policy'])
            for item in frame['inputs']:
                self.assertEqual(set(item),{'id','kind','sdk_version','controller_id','action','observation_hash'})

    def test_replay_id_reordering_roundtrip_and_atomic_failures(self):
        self.step(15,[manual(rid,dy=1000 if rid=='red-1' else 0) for rid in self.ids])
        original = call('/api/roster/replay')[1]
        shuffled = copy.deepcopy(original)
        shuffled['config']['robots'].reverse()
        for frame in shuffled['frames']:
            frame['inputs'].reverse()
        status,body = call('/api/roster/import',{'observer':'red-2','replay':shuffled})
        self.assertEqual(status,200,body)
        self.assertEqual(body['verified_frames'],15)
        self.assertEqual(call('/api/roster/replay')[1],original)
        mutations = [lambda r:r.update(build='stale'),lambda r:r.update(observation_version=2),
            lambda r:r.update(final_hash=(r['final_hash']+1)&0xffffffff),
            lambda r:r['frames'][0]['inputs'][0].update(id='unknown'),
            lambda r:r['frames'][0]['inputs'][0].update(observation_hash=True),
            lambda r:r['frames'][0]['inputs'][0].update(action=[0,0,0,0,True]),
            lambda r:r['frames'][0].update(hidden_hp=[1,2]),
            lambda r:r['config']['robots'][0]['profile'].update(max_hp=101)]
        for mutate in mutations:
            bad = copy.deepcopy(original)
            mutate(bad)
            status,_ = call('/api/roster/import',{'observer':self.observer,'replay':bad})
            self.assertEqual(status,400)
            self.assertEqual(call('/api/roster/replay')[1],original)
        duplicate = json.dumps({'observer':self.observer,'replay':original}).replace('"schema":','"schema":"duplicate","schema":',1)
        self.assertEqual(call('/api/roster/import',raw=duplicate)[0],400)
        self.assertEqual(call('/api/roster/replay')[1],original)

    def test_invalid_configs_inputs_and_queries_are_atomic(self):
        baseline = call('/api/roster/replay')[1]
        for query in ('?observer=','?observer=red-1&observer=blue-1','?observer=red-1&x=1','?observer=unknown','?observer=red%201','?observer='+('x'*33)):
            self.assertEqual(call('/api/roster/state'+query)[0],400,query)
        for route in ('/api/roster/meta?x=1','/api/roster/replay?observer=red-1'):
            self.assertEqual(call(route)[0],400)
        mutations = [lambda c:c['robots'].append(copy.deepcopy(c['robots'][0])),
            lambda c:c['robots'][0].update(team=True),
            lambda c:c['robots'][0].update(eligible=1),
            lambda c:c['robots'][0]['profile'].update(max_hp=1.0),
            lambda c:c['robots'][0]['profile'].update(id='official-infantry'),
            lambda c:c['robots'][0]['profile'].update(radius_mm=99999),
            lambda c:c['robots'][0]['spawn'].update(x=6000,y=3800),
            lambda c:c['robots'][0]['spawn'].update(cooldown=0),
            lambda c:c['robots'][0].update(spawn=copy.deepcopy(c['robots'][1]['spawn'])),
            lambda c:c.update(heat_threshold_policy='silent-guess'),
            lambda c:c.update(robots=c['robots'][:2])]
        for mutate in mutations:
            config = copy.deepcopy(self.config)
            mutate(config)
            status,_ = call('/api/roster/reset',{'observer':self.observer,'config':config})
            self.assertEqual(status,400)
            self.assertEqual(call('/api/roster/replay')[1],baseline)
        good = [manual(rid) for rid in self.ids]
        for controllers in (good[:-1],good+[good[0]],[good[0]]*4,[manual('unknown')]+good[1:]):
            self.assertEqual(call('/api/roster/step',{'count':1,'observer':self.observer,'controllers':controllers})[0],400)
        for count in (True,1.0,0,601):
            self.assertEqual(call('/api/roster/step',{'count':count,'observer':self.observer,'controllers':good})[0],400)
        bad_policy = [{'id':rid,'kind':'policy','sdk_version':1,'controller_id':'uncompiled'} for rid in self.ids]
        self.assertEqual(call('/api/roster/step',{'count':1,'observer':self.observer,'controllers':bad_policy})[0],400)
        self.assertEqual(call('/api/roster/replay')[1],baseline)

    def test_reset_1v1_and_4v4_config_and_default_observer(self):
        for size in (1,4):
            config = copy.deepcopy(self.config)
            config['robots'] = []
            for team,color in ((0,'red'),(1,'blue')):
                template = next(robot for robot in self.config['robots'] if robot['team']==team)
                for number in range(size):
                    robot = copy.deepcopy(template)
                    robot['id'] = color+'-unit-'+str(number)
                    robot['spawn']['x'] = 1600 if team==0 else 10400
                    robot['spawn']['y'] = 900+number*1800
                    config['robots'].append(robot)
            status,state = call('/api/roster/reset',{'observer':'red-unit-0','config':config})
            self.assertEqual(status,200,state)
            ids = [r['id'] for r in state['config']['robots']]
            self.assertEqual(len(ids),2*size)
            self.assertEqual(self.read()['observer'],ids[0])
            state = self.step(2,[manual(rid) for rid in reversed(ids)],'red-unit-0')
            self.assertEqual(state['tick'],2)
            replay = call('/api/roster/replay')[1]
            self.assertEqual(call('/api/roster/import',{'observer':'red-unit-0','replay':replay})[0],200)

    def test_synthetic_profile_heat_and_public_objective_capture(self):
        config = copy.deepcopy(self.config)
        robot = next(r for r in config['robots'] if r['id']=='red-1')
        robot['profile'].update(id='SYNTHETIC-low-heat',max_hp=137,ammo=57,heat_limit_tenths=100,cooling_per_second=0)
        robot['spawn'].update(x=6900,y=6100)
        self.assertEqual(call('/api/roster/reset',{'observer':self.observer,'config':config})[0],200)
        state = self.step(360,[manual(rid,fire=1 if rid=='red-1' else 0) for rid in self.ids])
        obs = state['observation']
        self.assertEqual(obs['self']['hp'],137)
        self.assertEqual(obs['self']['ammo'],55)
        self.assertEqual(obs['self_heat']['heat_tenths'],200)
        self.assertTrue(obs['self_heat']['firing_locked'])
        self.assertEqual(obs['zones'][1]['controller'],0)
        self.assertEqual(obs['rate'][0],1)
        self.assertGreater(obs['score'][0],0)
        for contact in obs['contacts']:
            self.assertEqual(set(contact),{'id','team','x','y','yaw'})
        self.assertTrue(all(set(p)=={'x','y','vx','vy'} for p in obs['projectiles']))
        replay = call('/api/roster/replay')[1]
        self.assertEqual(len(replay['frames']),360)
        self.assertTrue(all(next(i for i in frame['inputs'] if i['id']=='red-1')['action'][4]==1 for frame in replay['frames']))

    def test_old_modes_remain_independent(self):
        self.step(3)
        for route,original in self.original.items():
            self.assertEqual(call(route)[1],original,route)


if __name__=='__main__':
    unittest.main()
