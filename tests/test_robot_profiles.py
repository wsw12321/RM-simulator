#!/usr/bin/env python3
"""Sourced catalog boundaries, exact provenance and legacy-engine isolation."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from robot_profiles_v1 import api as p
from roster_mode import bridge as n


class SourcedProfiles(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = p.catalog()
        cls.presets = cls.catalog['presets']

    def selection(self, preset=None):
        return p.example_selection(preset)

    def envelope(self, preset=None):
        return p.resolve_config(self.selection(preset))

    def raw_fixture(self, envelope):
        return {'schema':n.REPLAY_SCHEMA,'profile':n.PROFILE,'engine':n.ENGINE,
            'build':p.NATIVE_SOURCE_PIN,'observation_profile':n.OBSERVATION,
            'observation_version':1,'official':False,
            'config':copy.deepcopy(envelope['resolved_config']),
            'initial_hash':123,'frames':[],'final_hash':123}

    def test_pinned_legacy_source_identity_unchanged(self):
        self.assertEqual(n.build_id(), p.NATIVE_SOURCE_PIN)
        self.assertEqual(n.REPLAY_SCHEMA,'rmuc-experimental-roster-replay.v1')
        self.assertEqual(n.DEFAULT_CONFIG['robots'][0]['profile']['id'],'SYNTHETIC-standard')

    def test_every_exposed_role_snapshot_projects_only_verified_supported_fields(self):
        self.assertGreater(len(self.presets), 0)
        roles = set()
        for preset in self.presets:
            with self.subTest(preset=preset['id']):
                roles.add(preset['role'])
                request = self.selection(preset['id'])
                envelope = p.resolve_config(request)
                self.assertEqual(p.validate_config_envelope(envelope), envelope)
                self.assertEqual(envelope['official'], False)
                self.assertEqual(envelope['complete'], False)
                self.assertLessEqual(set(preset['overrides']), p.SUPPORTED_FIELDS)
                self.assertEqual(set(preset['field_provenance']),set(preset['overrides']))
                for robot, record in zip(envelope['resolved_config']['robots'],envelope['provenance']['robots']):
                    self.assertEqual(robot['id'], record['id'])
                    self.assertEqual(set(record['fields']), set(n.PROFILE_BOUNDS))
                    self.assertEqual(record['conditions'],preset['conditions'])
                    for key in n.PROFILE_BOUNDS:
                        field = record['fields'][key]
                        self.assertEqual(field['value'],robot['profile'][key])
                        if key in preset['overrides']:
                            self.assertEqual(field['value'],preset['overrides'][key])
                            self.assertEqual(field['classification'],'sourced-parameter')
                            self.assertEqual(field['source'],preset['field_provenance'][key])
                        else:
                            self.assertEqual(field['classification'],'user-supplied-synthetic-assumption')
                            self.assertIsNone(field['source'])
                self.assertTrue(preset['excluded_fields'])
        self.assertNotIn('hero',roles)
        self.assertNotIn('engineer',roles)

    def test_all_static_infantry_levels_chassis_and_launcher_choices_covered(self):
        infantry = [item for item in self.presets if item['role']=='infantry']
        self.assertEqual(len(infantry),40)
        seen = {(r['conditions']['chassis'],r['conditions']['launcher'],r['conditions']['level']) for r in infantry}
        self.assertEqual(len(seen),40)
        self.assertEqual({x[2] for x in seen},set(range(1,11)))
        self.assertEqual(len({x[0] for x in seen}),2)
        self.assertEqual(len({x[1] for x in seen}),2)
        for row in infantry:
            self.assertEqual(set(row['overrides']),p.SUPPORTED_FIELDS)
            source = row['field_provenance']['heat_limit_tenths']
            self.assertEqual(row['overrides']['heat_limit_tenths'],source['source_value']*10)
            self.assertEqual(row['overrides']['cooling_per_second'],row['field_provenance']['cooling_per_second']['source_value'])

    def test_every_residual_numeric_boundary_for_every_exposed_preset(self):
        for preset in self.presets:
            for field in set(n.PROFILE_BOUNDS)-set(preset['overrides']):
                low,high = n.PROFILE_BOUNDS[field]
                for value in (low,high):
                    with self.subTest(preset=preset['id'],field=field,value=value):
                        request = self.selection(preset['id'])
                        request['robots'][0]['synthetic'][field] = value
                        p.resolve_config(request)
                for value in (low-1,high+1,True,False,float(low),str(low),None):
                    with self.subTest(preset=preset['id'],field=field,bad=value):
                        request = self.selection(preset['id'])
                        request['robots'][0]['synthetic'][field] = value
                        with self.assertRaises(ValueError): p.resolve_config(request)

    def test_every_source_field_cannot_be_overridden_or_fabricated(self):
        for preset in self.presets:
            for field,value in preset['overrides'].items():
                request = self.selection(preset['id'])
                request['robots'][0]['synthetic'][field] = value
                with self.subTest(preset=preset['id'],field=field), self.assertRaises(ValueError):
                    p.resolve_config(request)

    def test_every_required_residual_field_must_be_supplied(self):
        for preset in self.presets:
            request = self.selection(preset['id'])
            for field in tuple(request['robots'][0]['synthetic']):
                bad = copy.deepcopy(request)
                del bad['robots'][0]['synthetic'][field]
                with self.subTest(preset=preset['id'],missing=field),self.assertRaises(ValueError):
                    p.resolve_config(bad)

    def test_exact_condition_types_and_all_unsupported_options_fail(self):
        for preset in self.presets:
            for key,value in preset['conditions'].items():
                bad = self.selection(preset['id'])
                bad['robots'][0]['conditions'][key] = '__unsupported__'
                with self.subTest(preset=preset['id'],condition=key),self.assertRaises(ValueError):
                    p.resolve_config(bad)
                if type(value) is int:
                    for fake in (float(value),True,0,11,-1):
                        if type(fake) is int and fake == value: continue
                        bad = self.selection(preset['id'])
                        bad['robots'][0]['conditions'][key] = fake
                        with self.assertRaises(ValueError): p.resolve_config(bad)
            bad = self.selection(preset['id'])
            bad['robots'][0]['conditions']['unsupported_buff'] = 'enabled'
            with self.assertRaises(ValueError): p.resolve_config(bad)
        for value in ('hero','engineer','aerial','dart','radar','infantry_l0','infantry_l11','',None,True,{},[]):
            bad = self.selection()
            bad['robots'][0]['preset_id'] = value
            with self.subTest(value=value),self.assertRaises(ValueError): p.resolve_config(bad)

    def test_all_strategy_options_remain_explicit_and_unchanged(self):
        choices = (n.BOUNDARIES,n.CREDITS,n.DAMAGE_STRATEGIES,n.HEAT_STRATEGIES,n.HEAT_THRESHOLDS)
        for key, values in zip(n.SELECTED_CONFIG, choices):
            for value in values:
                request = self.selection()
                request['strategies'][key] = value
                self.assertEqual(p.resolve_config(request)['resolved_config'][key],value)
            for bad_value in (True,0,None,'official'):
                request = self.selection()
                request['strategies'][key] = bad_value
                with self.assertRaises(ValueError): p.resolve_config(request)

    def test_no_unknown_fields_at_any_selection_layer(self):
        for path in ((),('catalog',),('strategies',),('robots',0),('robots',0,'synthetic'),('robots',0,'spawn'),('robots',0,'conditions')):
            bad = self.selection()
            target = bad
            for key in path: target = target[key]
            target['official'] = True
            with self.subTest(path=path),self.assertRaises(ValueError): p.resolve_config(bad)

    def test_no_silent_defaults_and_output_is_defensive_copy(self):
        request = self.selection()
        before = copy.deepcopy(request)
        result = p.resolve_config(request)
        self.assertEqual(request,before)
        result['provenance']['robots'][0]['fields']['max_hp']['value'] += 1
        self.assertNotEqual(result,p.resolve_config(request))
        self.assertEqual(request,before)
        request['robots'].reverse()
        canonical_result = p.resolve_config(request)
        self.assertEqual(canonical_result,p.resolve_config(before))
        with self.assertRaises(ValueError): p.resolve_config(n.DEFAULT_CONFIG)

    def test_fake_completeness_and_wrong_types_are_rejected(self):
        for key in ('official','complete'):
            for fake in (True,0,1,0.0,None,'false'):
                bad = self.envelope()
                bad[key] = fake
                with self.subTest(key=key,value=fake),self.assertRaises(ValueError): p.validate_config_envelope(bad)
        bad = self.envelope()
        bad['provenance']['robots'][0]['complete'] = True
        with self.assertRaises(ValueError): p.validate_config_envelope(bad)

    def test_source_and_derived_tamper_rejected_even_with_recalculated_digest(self):
        changes = [
            lambda x:x['resolved_config']['robots'][0]['profile'].__setitem__('max_hp',999),
            lambda x:x['provenance']['robots'][0]['fields']['max_hp']['source'].__setitem__('source_value',999),
            lambda x:x['provenance']['robots'][0]['fields']['max_hp'].__setitem__('classification','official'),
            lambda x:x['provenance']['robots'][0]['fields']['ammo'].__setitem__('source',{'url':'https://fake.example'}),
            lambda x:x['provenance']['documents'].__setitem__(0,{'url':'https://fake.example'}),
            lambda x:x['provenance'].__setitem__('limitations',[]),
            lambda x:x.__setitem__('scope','official-complete'),
        ]
        for change in changes:
            bad = self.envelope()
            change(bad)
            bad['configuration_sha256'] = p._digest({k:v for k,v in bad.items() if k!='configuration_sha256'})
            with self.subTest(change=change),self.assertRaises(ValueError): p.validate_config_envelope(bad)

    def test_catalog_version_source_and_local_file_tampering_fail_closed(self):
        for key in ('id','version','sha256','source_audit_sha256'):
            bad = self.selection()
            bad['catalog'][key] += '-fake'
            with self.subTest(key=key),self.assertRaises(ValueError): p.resolve_config(bad)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name in ('catalog.json','source_audit.json'):
                (root/name).write_bytes((p.ROOT/name).read_bytes())
            for target in ('catalog.json','source_audit.json'):
                original = (root/target).read_bytes()
                (root/target).write_bytes(original+b' ')
                with patch.object(p,'ROOT',root),self.assertRaises(ValueError): p.catalog()
                (root/target).write_bytes(original)

    def test_replay_wrapper_does_not_mutate_or_relabel_raw_replay(self):
        envelope = self.envelope()
        raw = self.raw_fixture(envelope)
        before = copy.deepcopy(raw)
        wrapped = p.wrap_replay(envelope,raw)
        self.assertEqual(raw,before)
        self.assertEqual(wrapped['native_replay'],before)
        self.assertEqual(p.validate_replay_wrapper(wrapped),wrapped)
        with self.assertRaises(ValueError): p.validate_replay_wrapper(raw)
        wrapped['native_replay']['config']['robots'][0]['profile']['max_hp'] += 1
        wrapped['native_replay_sha256'] = p._digest(wrapped['native_replay'])
        with self.assertRaises(ValueError): p.validate_replay_wrapper(wrapped)

    def test_replay_structure_and_false_official_are_strict(self):
        envelope = self.envelope()
        for key,value in (('official',0),('official',True),('observation_version',True),('schema',p.REPLAY_SCHEMA),('build','untrusted'),('final_hash',True),('frames',{})):
            raw = self.raw_fixture(envelope)
            raw[key] = value
            with self.subTest(key=key,value=value),self.assertRaises(ValueError): p.wrap_replay(envelope,raw)
        wrapper = p.wrap_replay(envelope,self.raw_fixture(envelope))
        for key in ('official','complete','native_replay_sha256','extra'):
            bad = copy.deepcopy(wrapper)
            bad[key] = True
            with self.subTest(key=key),self.assertRaises(ValueError): p.validate_replay_wrapper(bad)

    def test_invalid_requests_never_touch_live_process(self):
        process = Mock()
        bad = self.envelope()
        bad['resolved_config']['robots'][0]['profile']['max_hp'] += 1
        with self.assertRaises(ValueError): p.reset_process(process,'red-1',bad)
        process.reset.assert_not_called()
        envelope = self.envelope()
        wrapper = p.wrap_replay(envelope,self.raw_fixture(envelope))
        wrapper['configuration']['provenance']['robots'][0]['fields']['max_hp']['value'] += 1
        with self.assertRaises(ValueError): p.import_process(process,'red-1',wrapper)
        process.import_replay.assert_not_called()
        with patch.object(n,'build_id',return_value='changed'),self.assertRaises(ValueError):
            p.reset_process(process,'red-1',envelope)
        process.reset.assert_not_called()

    def test_failed_native_transactions_propagate_without_success_metadata(self):
        process = Mock()
        process.reset.side_effect = ValueError('candidate failed')
        envelope = self.envelope()
        with self.assertRaisesRegex(ValueError,'candidate failed'): p.reset_process(process,'red-1',envelope)
        process.reset.assert_called_once_with({'observer':'red-1','config':envelope['resolved_config']})
        wrapper = p.wrap_replay(envelope,self.raw_fixture(envelope))
        process.import_replay.side_effect = ValueError('candidate failed')
        with self.assertRaisesRegex(ValueError,'candidate failed'): p.import_process(process,'red-1',wrapper)

    def test_duplicate_json_keys_and_nonfinite_are_rejected(self):
        for raw in ('{"x":1,"x":2}', '{"x":NaN}', '{"x":Infinity}'):
            with self.assertRaises(ValueError): p._json(raw,'test')


@unittest.skipUnless((n.ROOT/'build/rmuc_roster').is_file(),'build native roster binary first')
class SourcedNativeIntegration(unittest.TestCase):
    def test_sourced_reset_replay_native_roundtrip_and_failed_import_is_atomic(self):
        process = n.RosterProcess()
        try:
            legacy = process.replay()
            envelope = p.resolve_config(p.example_selection())
            state = p.reset_process(process,'red-1',envelope)
            self.assertFalse(state['official'])
            controllers = [{'id':robot['id'],'kind':'manual','action':{'sdk_version':1,'dx':0,'dy':0,'aim_x':6000,'aim_y':3800,'fire':0}}
                           for robot in state['config']['robots']]
            process.step({'count':3,'observer':'red-1','controllers':controllers})
            native = process.replay()
            wrapper = p.wrap_replay(envelope,native)
            result = p.import_process(process,'red-1',wrapper)
            self.assertEqual(result['verified_frames'],3)
            self.assertEqual(process.replay(),native)
            bad = copy.deepcopy(wrapper)
            bad['native_replay']['initial_hash'] ^= 1
            bad['native_replay_sha256'] = p._digest(bad['native_replay'])
            with self.assertRaises(ValueError): p.import_process(process,'red-1',bad)
            self.assertEqual(process.replay(),native)
            process.import_replay({'observer':'red-1','replay':legacy})
            self.assertEqual(process.replay(),legacy)
            self.assertEqual(process.replay()['schema'],n.REPLAY_SCHEMA)
        finally:
            process.close()


if __name__ == '__main__': unittest.main()
