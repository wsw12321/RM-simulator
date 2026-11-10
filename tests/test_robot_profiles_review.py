#!/usr/bin/env python3
"""Independent source/provenance/native-boundary review, not browser QA.

Expected numeric rows below were independently read from the pinned official
PDF table pixels (printed pp26,91-94), not imported from the production catalog.
"""
import copy
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from robot_profiles_v1 import api
from roster_mode import bridge as native
from roster_mode.build_id import build_id

NATIVE_PIN = '11ea10bb3434847fa9793ac856f7d01a63043ddd81782345aa0d519d081e6af5'
PDF_PIN = '9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be'
HP = {
    'power_priority': [150,175,200,225,250,275,300,325,350,400],
    'hp_priority': [200,225,250,275,300,325,350,375,400,400],
}
HEAT = {
    'burst_priority': ([170,180,190,200,210,220,230,240,250,260],
                       [5,7,9,11,12,13,14,16,18,20]),
    'cooling_priority': ([40,48,56,64,72,80,88,96,104,120],
                         [12,14,16,18,20,22,24,26,28,30]),
}


def digest(value):
    return hashlib.sha256(native.canonical(value)).hexdigest()


def rehash_config(value):
    value['configuration_sha256'] = digest({k:v for k,v in value.items()
                                          if k != 'configuration_sha256'})
    return value


def request(engine, count=1, fire=0):
    config = engine.state('red-1')['config']
    return {'count': count, 'observer': 'red-1', 'controllers': [
        {'id': r['id'], 'kind': 'manual', 'action': {
            'sdk_version': 1, 'dx': 0, 'dy': 0, 'aim_x': 6000,
            'aim_y': 3800, 'fire': fire if r['id'] == 'red-1' else 0}}
        for r in config['robots']]}


class IndependentCatalogReview(unittest.TestCase):
    def setUp(self):
        self.catalog = api.catalog()
        self.presets = self.catalog['presets']

    def test_native_source_identity_is_unchanged(self):
        self.assertEqual(build_id(), NATIVE_PIN)
        self.assertEqual(api.NATIVE_SOURCE_PIN, NATIVE_PIN)
        self.assertIs(self.catalog['official'], False)
        self.assertIs(self.catalog['complete'], False)
        self.assertIn(PDF_PIN, json.dumps(self.catalog['documents']))

    def test_exact_role_cardinality_and_no_older_season_roles(self):
        roles = [p['role'] for p in self.presets]
        self.assertEqual(set(roles), {'infantry','heavy','sentry'})
        self.assertEqual({role:roles.count(role) for role in set(roles)},
                         {'infantry':40,'heavy':4,'sentry':2})
        self.assertEqual(len({p['id'] for p in self.presets}), 46)
        for p in self.presets:
            expected = {'max_hp','heat_limit_tenths','cooling_per_second'} if p['role']=='infantry' else {'max_hp'}
            self.assertEqual(set(p['overrides']), expected)
            self.assertEqual(set(p['field_provenance']), expected)
        self.assertEqual(sorted(p['overrides']['max_hp'] for p in self.presets if p['role']=='heavy'), [400,450,500,600])
        self.assertEqual(sorted(p['overrides']['max_hp'] for p in self.presets if p['role']=='sentry'), [200,400])

    def test_every_infantry_row_matches_independently_read_tables(self):
        # Compare complete value multiplicities, including the two non-linear
        # level-10 table entries and the hp-priority level9/10 plateau.
        expected = []
        for hps in HP.values():
            for limits, coolings in HEAT.values():
                expected += [(hps[i], limits[i]*10, coolings[i]) for i in range(10)]
        actual = [tuple(p['overrides'][k] for k in
                        ('max_hp','heat_limit_tenths','cooling_per_second'))
                  for p in self.presets if p['role']=='infantry']
        self.assertCountEqual(actual, expected)
        seen = set()
        for p in self.presets:
            if p['role'] != 'infantry': continue
            c = p['conditions']
            level = c['level']
            self.assertIs(type(level),int)
            self.assertIn(level,range(1,11))
            key = (c['chassis'],c['launcher'],level)
            self.assertNotIn(key,seen)
            seen.add(key)
            launcher = c['launcher'].removesuffix('_17mm')
            limits,coolings = HEAT[launcher]
            self.assertEqual(p['overrides'],{'max_hp':HP[c['chassis']][level-1],
                'heat_limit_tenths':limits[level-1]*10,'cooling_per_second':coolings[level-1]})
            self.assertIs(c['static_snapshot'],True)
            self.assertEqual(c['external_buffs'],'none')
            self.assertEqual(c['dynamic_progression'],'not_simulated')

    def test_all_presets_resolve_with_exact_sourced_and_synthetic_partition(self):
        for preset in self.presets:
            with self.subTest(preset=preset['id']):
                selection = api.example_selection(preset['id'])
                original = copy.deepcopy(selection)
                envelope = api.resolve_config(selection)
                self.assertEqual(selection, original, 'resolver must not mutate caller selection')
                self.assertEqual(api.validate_config_envelope(envelope), envelope)
                self.assertEqual(envelope['configuration_sha256'], digest({k:v for k,v in envelope.items() if k!='configuration_sha256'}))
                for robot, record in zip(envelope['resolved_config']['robots'], envelope['provenance']['robots']):
                    self.assertEqual(record['id'], robot['id'])
                    self.assertIs(record['official'], False)
                    self.assertIs(record['complete'], False)
                    self.assertEqual(set(record['fields']), set(native.PROFILE_BOUNDS))
                    for key, value in record['fields'].items():
                        self.assertEqual(value['value'], robot['profile'][key])
                        if key in preset['overrides']:
                            self.assertEqual(value['classification'],'sourced-parameter')
                            self.assertEqual(value['value'],preset['overrides'][key])
                            self.assertEqual(value['source'],preset['field_provenance'][key])
                        else:
                            self.assertEqual(value['classification'],'user-supplied-synthetic-assumption')
                            self.assertIsNone(value['source'])

    def test_every_role_requires_each_residual_and_forbids_source_overrides(self):
        for role in ('infantry','heavy','sentry'):
            preset = next(p for p in self.presets if p['role']==role)
            template = api.example_selection(preset['id'])
            for key in template['robots'][0]['synthetic']:
                value = copy.deepcopy(template)
                del value['robots'][0]['synthetic'][key]
                with self.subTest(role=role,missing=key), self.assertRaises(ValueError):
                    api.resolve_config(value)
            for key, official_value in preset['overrides'].items():
                value = copy.deepcopy(template)
                value['robots'][0]['synthetic'][key] = official_value
                with self.subTest(role=role,override=key), self.assertRaises(ValueError):
                    api.resolve_config(value)

    def test_every_residual_uses_native_numeric_boundaries_and_strict_types(self):
        template = api.example_selection(next(p['id'] for p in self.presets if p['role']=='heavy'))
        for key in template['robots'][0]['synthetic']:
            if key == 'id': continue
            low, high = native.PROFILE_BOUNDS[key]
            for value in (True,False,float(low),str(low),None,low-1,high+1):
                changed = copy.deepcopy(template)
                changed['robots'][0]['synthetic'][key] = value
                with self.subTest(field=key,value=value), self.assertRaises(ValueError):
                    api.resolve_config(changed)
            for value in (low,high):
                changed = copy.deepcopy(template)
                changed['robots'][0]['synthetic'][key] = value
                with self.subTest(field=key,boundary=value):
                    api.resolve_config(changed)

    def test_unknown_options_roles_parameters_and_changed_conditions_reject(self):
        template = api.example_selection()
        for bad in (None,False,1,[],{},'hero','engineer','aerial','radar','dart','infantry-level-11','official'):
            value = copy.deepcopy(template)
            value['robots'][0]['preset_id'] = bad
            with self.subTest(preset_id=bad), self.assertRaises(ValueError): api.resolve_config(value)
        for key in ('official','complete','role','level','posture','weapon_caliber','xp','buff','initial_hp','power_limit_W'):
            value = copy.deepcopy(template)
            value['robots'][0][key] = True
            with self.subTest(extra=key), self.assertRaises(ValueError): api.resolve_config(value)
        for p in self.presets:
            value = api.example_selection(p['id'])
            value['robots'][0]['conditions'] = {}
            with self.subTest(conditions=p['id']), self.assertRaises(ValueError): api.resolve_config(value)
            for key, current in p['conditions'].items():
                bad_values = [None, 'unsupported']
                if type(current) is bool: bad_values += [int(current)]
                if type(current) is int: bad_values += [True,float(current),0,11]
                for bad in bad_values:
                    value = api.example_selection(p['id'])
                    value['robots'][0]['conditions'][key] = bad
                    with self.subTest(preset=p['id'],condition=key,value=bad), self.assertRaises(ValueError):
                        api.resolve_config(value)

    def test_stale_catalog_version_source_hash_and_claims_reject(self):
        template = api.example_selection()
        for key in template['catalog']:
            changed = copy.deepcopy(template)
            changed['catalog'][key] = 'forged-or-stale'
            with self.subTest(key=key), self.assertRaises(ValueError): api.resolve_config(changed)
        for key in ('official','complete','sources','source_version'):
            changed = copy.deepcopy(template)
            changed['catalog'][key] = True
            with self.subTest(key=key), self.assertRaises(ValueError): api.resolve_config(changed)

    def test_catalog_reads_are_owned_copies_and_disk_pins_are_checked_each_time(self):
        initial = api.catalog()
        changed = api.catalog()
        changed['presets'][0]['overrides']['max_hp'] = 999
        self.assertEqual(api.catalog(), initial)
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            for name in ('catalog.json','source_audit.json'):
                (folder/name).write_bytes((api.ROOT/name).read_bytes())
            with patch.object(api,'ROOT',folder):
                self.assertEqual(api.catalog(),initial)
                for name in ('catalog.json','source_audit.json'):
                    original = (folder/name).read_bytes()
                    (folder/name).write_bytes(original+b'\n')
                    with self.subTest(file=name), self.assertRaises(ValueError): api.catalog()
                    (folder/name).write_bytes(original)

    def test_rehashed_fake_provenance_and_native_values_reject(self):
        original = api.resolve_config(api.example_selection())
        mutations = [
            lambda e:e.__setitem__('official',True),
            lambda e:e.__setitem__('complete',True),
            lambda e:e.__setitem__('official',0),
            lambda e:e.__setitem__('scope','official robot simulation'),
            lambda e:e['provenance'].__setitem__('documents',[]),
            lambda e:e['provenance'].__setitem__('limitations',[]),
            lambda e:e['provenance'].__setitem__('native_source_pin','0'*64),
            lambda e:e['provenance']['robots'][0].__setitem__('official',True),
            lambda e:e['provenance']['robots'][0].__setitem__('label','Verified complete robot'),
            lambda e:e['provenance']['robots'][0]['fields']['ammo'].__setitem__('classification','sourced-parameter'),
            lambda e:e['provenance']['robots'][0]['fields']['max_hp'].__setitem__('source',{}),
            lambda e:e['provenance']['robots'][0]['fields']['max_hp'].__setitem__('value',1.0),
            lambda e:e['resolved_config']['robots'][0]['profile'].__setitem__('max_hp',999),
            lambda e:e['resolved_config']['robots'][0]['profile'].__setitem__('max_hp',float(e['resolved_config']['robots'][0]['profile']['max_hp'])),
        ]
        for i, mutate in enumerate(mutations):
            value = copy.deepcopy(original); mutate(value); rehash_config(value)
            with self.subTest(mutation=i), self.assertRaises(ValueError): api.validate_config_envelope(value)

    def test_profile_identity_reuse_and_order_preserve_native_contract(self):
        original = api.example_selection()
        resolved = api.resolve_config(original)
        reversed_selection = copy.deepcopy(original)
        reversed_selection['robots'].reverse()
        self.assertEqual(api.resolve_config(reversed_selection),resolved)
        conflicting = copy.deepcopy(original)
        conflicting['robots'][1]['synthetic']['id'] = conflicting['robots'][0]['synthetic']['id']
        with self.assertRaises(ValueError): api.resolve_config(conflicting)
        for bad in ('OFFICIAL-infantry','rmuc2027','',False,'SYNTHETIC-'+'x'*40):
            value = copy.deepcopy(original)
            value['robots'][0]['synthetic']['id'] = bad
            with self.subTest(id=bad), self.assertRaises(ValueError): api.resolve_config(value)

    def test_duplicate_nonfinite_and_oversized_json_reject(self):
        for raw in ('{"schema":1,"schema":2}','{"nested":{"x":1,"x":2}}','NaN','Infinity','-Infinity','{"x":NaN}'):
            with self.subTest(raw=raw), self.assertRaises(ValueError): api._json(raw,'review')
        selection = api.example_selection()
        selection['robots'][0]['conditions']['oversized'] = 'x'*api.MAX_CONFIG_BYTES
        with self.assertRaises(ValueError): api.resolve_config(selection)

    def test_source_locator_values_units_and_page_mapping_are_consistent(self):
        for preset in self.presets:
            for key,source in preset['field_provenance'].items():
                with self.subTest(preset=preset['id'],field=key):
                    self.assertEqual(source['source_sha256'],PDF_PIN)
                    self.assertEqual(source['pdf_page'],source['printed_page']+1)
                    self.assertTrue(source['source_quote_fragments'])
                    self.assertTrue(source['statement_id'])
                    if key=='heat_limit_tenths':
                        self.assertEqual(source['transform'],'source_value * 10')
                        self.assertEqual(preset['overrides'][key],source['source_value']*10)
                        self.assertIn(source['printed_page'],(93,94))
                    else:
                        self.assertEqual(source['transform'],'identity')
                        self.assertEqual(preset['overrides'][key],source['source_value'])
                    if key=='max_hp':
                        self.assertEqual(source['source_unit'],'HP')
                        self.assertIn(source['printed_page'],(26,91,92,93))


class IndependentNativeProjectionReview(unittest.TestCase):
    def setUp(self):
        self.engine = native.RosterProcess(os.environ.get('ROSTER_REVIEW_BINARY'))
        self.addCleanup(self.engine.close)

    def envelope(self,role='infantry'):
        preset = next(p for p in api.catalog()['presets'] if p['role']==role)
        return api.resolve_config(api.example_selection(preset['id']))

    def test_all_roles_reset_exactly_the_existing_native_profile_shape(self):
        for role in ('infantry','heavy','sentry'):
            envelope = self.envelope(role)
            state = api.reset_process(self.engine,'red-1',envelope)
            self.assertEqual(state['config'],envelope['resolved_config'])
            self.assertEqual(state['observation']['roster'],state['config']['robots'])
            self.assertIs(state['official'],False)
            self.assertNotIn('provenance',state)
            for robot in state['config']['robots']:
                self.assertEqual(set(robot),{'id','team','profile','spawn','eligible'})
                self.assertEqual(set(robot['profile']),{'id',*native.PROFILE_BOUNDS})
                self.assertEqual(self.engine.state(robot['id'])['observation']['self']['hp'],robot['profile']['max_hp'])

    def test_each_infantry_projection_has_exact_native_hp_q_and_100ms_cooling(self):
        for p in api.catalog()['presets']:
            if p['role']!='infantry': continue
            with self.subTest(preset=p['id']):
                envelope = api.resolve_config(api.example_selection(p['id']))
                state = api.reset_process(self.engine,'red-1',envelope)
                self.assertEqual(state['observation']['self']['hp'],p['overrides']['max_hp'])
                own_heat = state['observation']['self_heat']
                self.assertEqual(own_heat['limit_tenths'],p['overrides']['heat_limit_tenths'])
                self.assertEqual(own_heat['cooling_per_second'],p['overrides']['cooling_per_second'])
                state = self.engine.step(request(self.engine,1,1))
                self.assertEqual(state['observation']['self_heat']['heat_tenths'],100)
                state = self.engine.step(request(self.engine,5))
                self.assertEqual(state['tick'],6)
                self.assertEqual(state['observation']['self_heat']['heat_tenths'],100-p['overrides']['cooling_per_second'])

    def test_heavy_and_sentry_heat_remain_explicit_synthetic_residuals(self):
        for role in ('heavy','sentry'):
            p = next(p for p in api.catalog()['presets'] if p['role']==role)
            selection = api.example_selection(p['id'])
            for robot in selection['robots']:
                robot['synthetic']['heat_limit_tenths'] = 1234
                robot['synthetic']['cooling_per_second'] = 7
            envelope = api.resolve_config(selection)
            state = api.reset_process(self.engine,'red-1',envelope)
            self.assertEqual(state['observation']['self_heat']['limit_tenths'],1234)
            self.assertEqual(state['observation']['self_heat']['cooling_per_second'],7)
            fields = envelope['provenance']['robots'][0]['fields']
            for key in ('heat_limit_tenths','cooling_per_second'):
                self.assertEqual(fields[key]['classification'],'user-supplied-synthetic-assumption')
                self.assertIsNone(fields[key]['source'])

    def test_wrapped_replay_and_raw_replay_remain_separate_and_deterministic(self):
        envelope = self.envelope()
        api.reset_process(self.engine,'red-1',envelope)
        self.engine.step(request(self.engine,17,1))
        raw = self.engine.replay()
        raw_bytes = native.canonical(raw)
        wrapped = api.wrap_replay(envelope,raw)
        self.assertEqual(native.canonical(wrapped['native_replay']),raw_bytes)
        self.assertEqual(raw['schema'],native.REPLAY_SCHEMA)
        self.assertNotIn('provenance',raw)
        self.assertNotIn('configuration',raw)
        with self.assertRaises(ValueError): api.validate_replay_wrapper(raw)
        with self.assertRaises(ValueError): api.import_process(self.engine,'red-1',raw)
        self.engine.reset({'observer':'red-1','config':native.DEFAULT_CONFIG})
        state = api.import_process(self.engine,'blue-2',wrapped)
        self.assertEqual(state['verified_frames'],17)
        self.assertEqual(native.canonical(self.engine.replay()),raw_bytes)
        self.engine.reset({'observer':'red-1','config':native.DEFAULT_CONFIG})
        self.engine.import_replay({'observer':'red-2','replay':raw})
        self.assertEqual(native.canonical(self.engine.replay()),raw_bytes)

    def test_fake_wrapper_metadata_never_touches_native_process(self):
        envelope = self.envelope()
        api.reset_process(self.engine,'red-1',envelope)
        wrapped = api.wrap_replay(envelope,self.engine.replay())
        process = Mock()
        for key in ('official','complete','schema','native_replay_sha256'):
            value = copy.deepcopy(wrapped)
            value[key] = True if key in ('official','complete') else 'forged'
            with self.subTest(key=key), self.assertRaises(ValueError): api.import_process(process,'red-1',value)
        process.import_replay.assert_not_called()
        process.reset.assert_not_called()

    def test_invalid_envelopes_observers_and_source_drift_are_atomic(self):
        envelope = self.envelope('sentry')
        self.engine.step(request(self.engine,3))
        before = self.engine.replay()
        for observer in ('missing','',True):
            with self.subTest(observer=observer), self.assertRaises(ValueError): api.reset_process(self.engine,observer,envelope)
            self.assertEqual(self.engine.replay(),before)
        bad = copy.deepcopy(envelope)
        bad['provenance']['robots'][0]['fields']['ammo']['source'] = {'official':True}
        rehash_config(bad)
        with self.assertRaises(ValueError): api.reset_process(self.engine,'red-1',bad)
        self.assertEqual(self.engine.replay(),before)
        with patch.object(api.native,'build_id',return_value='0'*64):
            with self.assertRaises(ValueError): api.reset_process(self.engine,'red-1',envelope)
        self.assertEqual(self.engine.replay(),before)

    def test_rehashed_action_and_observation_tampering_fail_native_replay_atomically(self):
        envelope = self.envelope()
        api.reset_process(self.engine,'red-1',envelope)
        self.engine.step(request(self.engine,12,1))
        raw = self.engine.replay()
        wrapped = api.wrap_replay(envelope,raw)
        self.engine.reset({'observer':'red-1','config':native.DEFAULT_CONFIG})
        self.engine.step(request(self.engine,2))
        before = self.engine.replay()
        mutations = [
            lambda r:r['frames'][0]['inputs'][0]['action'].__setitem__(0,1000),
            lambda r:r['frames'][0]['inputs'][0].__setitem__('observation_hash',r['frames'][0]['inputs'][0]['observation_hash']^1),
            lambda r:r.__setitem__('initial_hash',r['initial_hash']^1),
        ]
        for i,mutate in enumerate(mutations):
            value = copy.deepcopy(wrapped)
            mutate(value['native_replay'])
            value['native_replay_sha256'] = digest(value['native_replay'])
            # Structural validation is not a deterministic replay claim.
            api.validate_replay_wrapper(value)
            with self.subTest(mutation=i), self.assertRaises(ValueError): api.import_process(self.engine,'red-1',value)
            self.assertEqual(self.engine.replay(),before)

    def test_native_candidate_response_failure_does_not_commit(self):
        envelope = self.envelope('heavy')
        self.engine.step(request(self.engine,4))
        before = self.engine.replay()
        with patch.object(self.engine,'_check_state',side_effect=ValueError('independent malformed response fixture')):
            with self.assertRaises(ValueError): api.reset_process(self.engine,'red-1',envelope)
        self.assertEqual(self.engine.replay(),before)


class IndependentSourceSessionReview(unittest.TestCase):
    def setUp(self):
        import server
        self.server_api = server
        self.engine = native.RosterProcess(os.environ.get('ROSTER_REVIEW_BINARY'))
        self.addCleanup(self.engine.close)
        self.server = SimpleNamespace(roster_engine=self.engine)
        self.envelope = api.resolve_config(api.example_selection())

    def activate(self):
        return self.mutate('reset',{'observer':'red-1','configuration':self.envelope})

    def mutate(self,action,data,revision=None,tick=None):
        packet = self.packet()
        return self.server_api.roster_profiles_post(self.server,'/api/roster/profiles/'+action,{
            'expected_revision':packet['run_revision'] if revision is None else revision,
            'expected_tick':packet['state']['tick'] if tick is None else tick,
            'request':data})

    def packet(self):
        return self.server_api.roster_profiles_get(self.server,
            '/api/roster/profiles/session/state','observer=red-1')

    def test_matching_raw_values_never_promote_and_raw_import_clears_badge(self):
        self.server_api.roster_post(self.server,'/api/roster/reset',
            {'observer':'red-1','config':self.envelope['resolved_config']})
        self.assertEqual(self.packet()['provenance_status'],'synthetic')
        self.assertIsNone(self.packet()['active_config'])
        with self.assertRaises(ValueError):
            self.server_api.roster_profiles_get(self.server,'/api/roster/profiles/replay','')
        self.activate()
        self.assertEqual(self.packet()['provenance_status'],'sourced-partial')
        raw = self.engine.replay()
        self.server_api.roster_post(self.server,'/api/roster/import',{'observer':'red-1','replay':raw})
        self.assertEqual(self.packet()['provenance_status'],'synthetic')
        self.assertIsNone(self.packet()['active_config'])
        self.assertEqual(self.engine.replay(),raw)

    def test_successful_steps_carry_only_bound_context_and_raw_reset_clears(self):
        packet = self.activate()
        first_revision = packet['run_revision']
        previous_native = self.engine._native
        self.server_api.roster_post(self.server,'/api/roster/step',request(self.engine,3))
        self.assertIsNot(self.engine._native,previous_native)
        packet = self.packet()
        self.assertEqual(packet['provenance_status'],'sourced-partial')
        self.assertEqual(packet['active_config'],self.envelope)
        self.assertEqual(packet['run_revision'],first_revision+1)
        raw = self.engine.replay()
        exported = self.server_api.roster_profiles_get(self.server,'/api/roster/profiles/replay','')
        self.assertEqual(exported['native_replay'],raw)
        self.server_api.roster_post(self.server,'/api/roster/reset',
            {'observer':'red-1','config':self.envelope['resolved_config']})
        self.assertEqual(self.packet()['provenance_status'],'synthetic')
        self.assertIsNone(self.packet()['active_config'])

    def test_failed_raw_and_sourced_transactions_preserve_world_and_context(self):
        self.activate()
        self.server_api.roster_post(self.server,'/api/roster/step',request(self.engine,2))
        before,packet = self.engine.replay(),self.packet()
        bad = copy.deepcopy(self.envelope)
        bad['provenance']['robots'][0]['label'] = 'Official whole robot'
        rehash_config(bad)
        requests = [
            ('/api/roster/profiles/reset',{'observer':'red-1','configuration':bad}),
            ('/api/roster/profiles/reset',{'observer':'missing','configuration':self.envelope}),
            ('/api/roster/profiles/session/reset',{'observer':'missing','config':native.DEFAULT_CONFIG}),
            ('/api/roster/profiles/session/import',{'observer':'red-1','replay':{'official':True}}),
        ]
        for path,data in requests:
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.mutate(path.removeprefix('/api/roster/profiles/'),data)
            self.assertEqual(self.engine.replay(),before)
            self.assertEqual(self.packet(),packet)

    def test_context_cannot_follow_an_uncoordinated_world_replacement(self):
        self.activate()
        # An out-of-band native reset has matching numbers but is a new run.
        self.engine.reset({'observer':'red-1','config':self.envelope['resolved_config']})
        packet = self.packet()
        self.assertIsNone(packet['active_config'])
        self.assertEqual(packet['provenance_status'],'unverified')
        self.assertEqual(self.packet()['provenance_status'],'synthetic')

    def test_catalog_drift_hides_source_badge_without_breaking_raw_state(self):
        self.activate()
        raw = self.engine.replay()
        with patch.object(api,'_trusted_catalog',side_effect=ValueError('catalog drift fixture')):
            packet = self.packet()
            self.assertIsNone(packet['active_config'])
            self.assertEqual(packet['provenance_status'],'unverified')
            self.assertEqual(self.engine.replay(),raw)
            with self.assertRaises(ValueError):
                self.server_api.roster_profiles_get(self.server,'/api/roster/profiles/replay','')

    def test_read_only_prepare_does_not_apply_or_infer_source_selection(self):
        original,packet = self.engine.replay(),self.packet()
        config = copy.deepcopy(native.DEFAULT_CONFIG)
        presets = api.catalog()['presets']
        choices = {r['id']:presets[i]['id'] for i,r in enumerate(config['robots'])}
        envelope = self.server_api.roster_profiles_post(self.server,'/api/roster/profiles/prepare',
            {'config':config,'presets':choices})
        api.validate_config_envelope(envelope)
        self.assertEqual(self.engine.replay(),original)
        self.assertEqual(self.packet(),packet)
        self.assertEqual(config,native.DEFAULT_CONFIG)
        for robot in envelope['selection']['robots']:
            self.assertEqual(robot['preset_id'],choices[robot['id']])

    def test_stale_revision_or_tick_reject_before_mutation_and_require_resync(self):
        self.activate()
        initial = self.packet()
        self.mutate('session/step',request(self.engine,1))
        before,packet = self.engine.replay(),self.packet()
        candidates = [
            (initial['run_revision'],packet['state']['tick']),
            (packet['run_revision'],initial['state']['tick']),
            (initial['run_revision'],initial['state']['tick']),
        ]
        for revision,tick in candidates:
            for action,data in (
                ('session/step',request(self.engine,1)),
                ('session/reset',{'observer':'red-1','config':native.DEFAULT_CONFIG}),
                ('reset',{'observer':'red-1','configuration':self.envelope}),
            ):
                with self.subTest(revision=revision,tick=tick,action=action):
                    with self.assertRaises(self.server_api.RosterStaleRequest) as error:
                        self.mutate(action,data,revision,tick)
                    self.assertIs(error.exception.resync_required,True)
                    self.assertEqual(self.engine.replay(),before)
                    self.assertEqual(self.packet(),packet)


if __name__ == '__main__':
    unittest.main()
