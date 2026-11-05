#!/usr/bin/env python3
"""Fast boundary tests independent of native process compilation."""
import copy
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from roster_mode import bridge as b


class RosterBridgeBoundaries(unittest.TestCase):
    def test_config_is_canonical_copy_and_never_remaps_ids(self):
        config = copy.deepcopy(b.DEFAULT_CONFIG)
        original = copy.deepcopy(config)
        config['robots'].reverse()
        normalized = b.validate_config(config)
        self.assertEqual(normalized,original)
        self.assertNotEqual(config['robots'],original['robots'])
        normalized['robots'][0]['spawn']['x'] = 123
        self.assertNotEqual(normalized,original)

    def test_valid_identifier_edges(self):
        for value in ('0','a','Z'*32,'red.unit_1-2'):
            self.assertEqual(b.robot_id(value),value)
        for value in ('','x'*33,'-start','a b','a\n','机器人',1,True,None):
            with self.subTest(value=value),self.assertRaises(ValueError):
                b.robot_id(value)

    def test_profile_reuse_must_be_identical(self):
        bad = copy.deepcopy(b.DEFAULT_CONFIG)
        bad['robots'][0]['profile']['max_hp'] += 1
        with self.assertRaisesRegex(ValueError,'identical'):
            b.validate_config(bad)
        bad['robots'][0]['profile']['id'] = 'SYNTHETIC-independent'
        b.validate_config(bad)

    def test_reject_all_bool_float_physical_parameters(self):
        for key in b.PROFILE_BOUNDS:
            for value in (True,1.0):
                config = copy.deepcopy(b.DEFAULT_CONFIG)
                config['robots'][0]['profile'][key] = value
                with self.subTest(key=key,value=value),self.assertRaises(ValueError):
                    b.validate_config(config)

    def test_native_not_launched_for_invalid_configuration(self):
        process = b.RosterProcess.__new__(b.RosterProcess)
        process.binary = Path('/not-needed')
        process._native = None
        process._build = b.build_id()
        config = copy.deepcopy(b.DEFAULT_CONFIG)
        config['robots'][0]['eligible'] = 1
        with patch.object(b,'_Native') as launch,self.assertRaises(ValueError):
            process.reset({'observer':'red-1','config':config})
        launch.assert_not_called()

    def test_q16_cover_filtering_matches_conservative_boundary(self):
        self.assertTrue(b.line_blocked({'x':4000,'y':3800},{'x':8000,'y':3800}))
        self.assertTrue(b.line_blocked({'x':8000,'y':3800},{'x':4000,'y':3800}))
        self.assertFalse(b.line_blocked({'x':4000,'y':1000},{'x':8000,'y':1000}))
        self.assertTrue(b.line_blocked({'x':4000,'y':2800},{'x':8000,'y':2800}))
        self.assertFalse(b.line_blocked({'x':1600,'y':2900},{'x':1600,'y':4700}))

    def test_shapes_cannot_hide_privileged_dynamic_fields(self):
        config = b.validate_config(b.DEFAULT_CONFIG)
        sources = [{'id':rid,'kind':'manual','sdk_version':1,'controller_id':None} for rid in b.ids_of(config)]
        b.validate_sources(sources,config,'sources')
        for key in ('hp','heat_tenths','observation','hash','action'):
            bad = copy.deepcopy(sources)
            bad[0][key] = 0
            with self.subTest(key=key),self.assertRaises(ValueError):
                b.validate_sources(bad,config,'sources')
        bad = copy.deepcopy(sources)
        bad[-1]['id'] = bad[0]['id']
        with self.assertRaises(ValueError):
            b.validate_sources(bad,config,'sources')

    def initial_fixture(self):
        config = b.validate_config(b.DEFAULT_CONFIG)
        own = next(robot for robot in config['robots'] if robot['id']=='red-1')
        teammate = next(robot for robot in config['robots'] if robot['id']=='red-2')
        profile = own['profile']
        heat = {'enabled':True,'heat_tenths':0,'limit_tenths':400,'cooling_per_second':12,
            'overheat_locked':False,'permanent_latched':False,'firing_locked':False}
        obs = {'self':{**own['spawn'],'hp':profile['max_hp'],'ammo':profile['ammo'],'cooldown':0},
            'contacts':[{'id':teammate['id'],'team':teammate['team'],**teammate['spawn']}],
            'projectiles':[],'zones':[{**zone,'controller':zone['original_owner'],'degree':'full'} for zone in b.ZONES],
            'rate':[0,0],'score':[0,0],'self_heat':heat}
        records = [{'id':robot['id'],'heat_tenths':0,'overheat_locked':False,'permanent_latched':False,
            'firing_locked':False,'detected_shots':0,'blocked_fire_ticks':0} for robot in config['robots']]
        damage = {'remaining_robot_hp':[180,180],'complete':True,'attack_damage':[0,0],
            'actual_hp_loss':[0,0],'excluded_hp_loss':[0,0],'penalty_attack_damage':[0,0],
            'nominal_projectile_damage':[0,0],'last_sequence':0}
        return config,{'observation':obs,'heat_admin':{'robots':records},'damage_admin':damage}

    def test_tick_zero_requires_exact_public_initial_state(self):
        config,state = self.initial_fixture()
        b.validate_initial_state(state,'red-1',config)
        changes = [lambda s:s['observation']['self'].update(x=1601),
            lambda s:s['observation']['self'].update(hp=99),
            lambda s:s['observation']['self'].update(ammo=119),
            lambda s:s['observation']['self'].update(cooldown=1),
            lambda s:s['observation'].update(contacts=[]),
            lambda s:s['observation']['contacts'][0].update(yaw=1),
            lambda s:s['observation'].update(projectiles=[{'x':1600,'y':2900,'vx':0,'vy':1}]),
            lambda s:s['observation']['zones'][0].update(degree='temporary'),
            lambda s:s['observation'].update(score=[1,0]),
            lambda s:s['observation'].update(rate=[1,0]),
            lambda s:s['heat_admin']['robots'][0].update(blocked_fire_ticks=1),
            lambda s:s['damage_admin'].update(remaining_robot_hp=[179,180]),
            lambda s:s['damage_admin'].update(complete=False),
            lambda s:s['damage_admin'].update(nominal_projectile_damage=[1,0])]
        for change in changes:
            altered = copy.deepcopy(state)
            change(altered)
            with self.assertRaises(ValueError):
                b.validate_initial_state(altered,'red-1',config)
        config['credit_strategy']='disabled'
        config['heat_strategy']='disabled'
        config['attack_damage_strategy']=b.DAMAGE_STRATEGIES[0]
        state['observation']['score']=None
        state['observation']['self_heat']['enabled']=False
        state['damage_admin'].update(complete=False,attack_damage=None)
        b.validate_initial_state(state,'red-1',config)

    def test_native_duplicate_json_keys_rejected(self):
        with self.assertRaises(ValueError):
            json.loads('{"schema":1,"schema":2}',object_pairs_hook=b._unique_object)
        self.assertEqual(json.loads('{"schema":1}',object_pairs_hook=b._unique_object),{'schema':1})

    def test_largest_valid_frame_bounds(self):
        inputs = [{'id':str(i)+'Z'*31,'kind':'policy','sdk_version':1,'controller_id':'Z'*64,
            'action':[-1000,-1000,12000,7600,1],'observation_hash':0xffffffff} for i in range(8)]
        frame = {'tick':25199,'inputs':inputs,'hash':0xffffffff}
        line = ' '.join(map(str,['FRAMES',600,'0'+'Z'*31,8,*b.frame_tokens(frame)*600]))+'\n'
        self.assertLess(len(line.encode()),b.MAX_LINE_BYTES)
        self.assertLess((len(b.canonical(frame))+1)*b.MAX_TICKS+1024*1024,b.MAX_BYTES)

    def test_tail_integrity_includes_sources_even_without_frames(self):
        process = b.RosterProcess.__new__(b.RosterProcess)
        frame = {'hash':55,'inputs':[{'id':'red-1','kind':'manual','sdk_version':1,'controller_id':None,'action':[0]*5,'observation_hash':0}]}
        state = {'hash':55,'last_sources':[{'id':'red-1','kind':'manual','sdk_version':1,'controller_id':None}]}
        process._check_history_tail(state,[frame],0)
        for key,value in [('hash',56),('last_sources',None)]:
            bad = {**state,key:value}
            with self.assertRaises(ValueError):
                process._check_history_tail(bad,[frame],0)


if __name__=='__main__':
    unittest.main()
