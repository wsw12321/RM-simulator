#!/usr/bin/env python3
"""Strict native-profile and transactional replay adapter checks."""
import copy
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from layered_mode import bridge


class ProfileTests(unittest.TestCase):
    def setUp(self):
        self.data = bridge.sample()

    def test_sample_and_no_mutation(self):
        original = copy.deepcopy(self.data)
        bridge.validate_map(self.data['map'])
        bridge.validate_config(self.data['config'], self.data['map'])
        self.assertEqual(original, self.data)
        self.assertNotEqual(bridge.digest(self.data['map']), bridge.digest(self.data['config']))

    def test_unsupported_geometry_is_not_rounded(self):
        for mutate in (
            lambda d: d['boundary_mm'][0].__setitem__(0, .1),
            lambda d: d['surfaces'][3]['height'].__setitem__('dx', .25001),
            lambda d: d['transitions'][0].__setitem__('max_step_mm', 1),
        ):
            doc = copy.deepcopy(self.data['map']); mutate(doc)
            with self.assertRaises(ValueError): bridge.validate_map(doc)

    def test_config_types_and_fields(self):
        for key, value in [('radius_mm', True), ('height_mm', 0), ('speed_mm_per_tick', 51), ('max_ticks', 36001), ('max_slope_q1024', 1.0)]:
            cfg = copy.deepcopy(self.data['config']); cfg[key] = value
            with self.assertRaises(ValueError): bridge.validate_config(cfg, self.data['map'])
        cfg = copy.deepcopy(self.data['config']); cfg['extra'] = 0
        with self.assertRaises(ValueError): bridge.validate_config(cfg, self.data['map'])
        cfg = copy.deepcopy(self.data['config']); cfg['start']['x_mm'] = 800.0
        with self.assertRaises(ValueError): bridge.validate_config(cfg, self.data['map'])

    def test_full_authored_map_hash(self):
        doc = copy.deepcopy(self.data['map']); doc['calibration']['notes'].append('Different evidence.')
        self.assertNotEqual(bridge.digest(doc), bridge.digest(self.data['map']))
        self.assertEqual(bridge.digest({'a': 1, 'b': 2}), bridge.digest({'b': 2, 'a': 1}))

    def test_same_side_portals_rejected(self):
        doc = copy.deepcopy(self.data['map'])
        doc['surfaces'][1]['polygon_mm'] = [[0,0],[2000,0],[2000,3000],[0,3000]]
        doc['transitions'] = [doc['transitions'][0]]
        with self.assertRaisesRegex(ValueError, 'opposite sides'): bridge.validate_map(doc)


class FakeNative:
    """Deterministic protocol double for transaction behavior, not motion tests."""
    instances = []
    def __init__(self, binary):
        self.closed = False; self.tick = 0
        self.instances.append(self)
    def command(self, text):
        fields = text.split()
        if fields[0] == 'LOAD':
            self.map_hash, self.config_hash = fields[1:3]
            if fields[8] == '-99999': raise ValueError('invalid native start')
        elif fields[0] in ('FRAME', 'ADVANCE'):
            count = int(fields[1]) if fields[0] == 'ADVANCE' else 1
            dx, dy = map(int, fields[2:4]); frames = []
            for _ in range(count):
                tick = self.tick; self.tick += 1
                frames.append([tick, dx, dy, self.state()['hash']])
            return {'state': self.state(), 'frames': frames, 'batch_events': []}
        return self.state()
    def state(self):
        return {'build': 'a'*64, 'map_hash': self.map_hash, 'config_hash': self.config_hash,
                'tick': self.tick, 'hash': f'{self.tick:016x}'}
    def close(self): self.closed = True


class TransactionTests(unittest.TestCase):
    def setUp(self):
        self.p1 = patch.object(bridge, '_Native', FakeNative); self.p1.start()
        self.p2 = patch.object(bridge, 'build_id', return_value='a'*64); self.p2.start()
        self.engine = bridge.LayeredProcess()
    def tearDown(self):
        self.engine.close(); self.p1.stop(); self.p2.stop()
    def test_reset_failure_preserves_run(self):
        self.engine.step({'count': 3, 'action': [1, 0]})
        before = self.engine.replay(); data = bridge.sample(); data['config']['start']['x_mm'] = -99999
        with self.assertRaises(ValueError): self.engine.reset(data)
        self.assertEqual(before, self.engine.replay())
        self.assertTrue(FakeNative.instances[-1].closed)
    def test_import_verified_and_atomic(self):
        self.engine.step({'count': 3, 'action': [1, 0]}); replay = self.engine.replay()
        bad = copy.deepcopy(replay); bad['frames'][1][3] = 'f'*16
        with self.assertRaises(ValueError): self.engine.import_replay(bad)
        self.assertEqual(replay, self.engine.replay())
        self.assertTrue(FakeNative.instances[-1].closed)
        result = self.engine.import_replay(replay)
        self.assertEqual(result['verified_frames'], 3)
        self.assertEqual(replay, self.engine.replay())
    def test_replay_strict_fields_types_and_identity(self):
        self.engine.step({'count': 1, 'action': [0, 0]}); good = self.engine.replay()
        changes = [lambda d: d.__setitem__('extra', 1), lambda d: d.__setitem__('official', 0),
                   lambda d: d.__setitem__('map_hash', 'b'*64), lambda d: d.__setitem__('final_hash', 0),
                   lambda d: d['frames'][0].__setitem__(0, False), lambda d: d['frames'][0].__setitem__(1, 1.0),
                   lambda d: d['frames'][0].__setitem__(2, 1001)]
        for mutate in changes:
            bad = copy.deepcopy(good); mutate(bad)
            with self.assertRaises(ValueError): self.engine.import_replay(bad)
            self.assertEqual(good, self.engine.replay())
    def test_source_build_change_rejects_replay_and_reset(self):
        before = self.engine.replay()
        with patch.object(bridge, 'build_id', return_value='b'*64):
            with self.assertRaises(ValueError): self.engine.import_replay(before)
            with self.assertRaises(ValueError): self.engine.reset(bridge.sample())
        self.assertEqual(before, self.engine.replay())

    def test_step_limit_atomic_and_metadata_copies(self):
        data = bridge.sample(); data['config']['max_ticks'] = 2; self.engine.reset(data)
        before = self.engine.replay()
        for action in ({'count': 3, 'action': [0, 0]}, {'count': True, 'action': [0, 0]}, {'count': 1, 'action': [0, 0], 'speed': 1}):
            with self.assertRaises(ValueError): self.engine.step(action)
            self.assertEqual(before, self.engine.replay())
        self.engine.meta()['config']['max_ticks'] = 999
        self.assertEqual(self.engine.meta()['config']['max_ticks'], 2)
        self.engine.step({'count': 2, 'action': [0, 0]})
        with self.assertRaises(ValueError): self.engine.step({'count': 1, 'action': [0, 0]})


@unittest.skipUnless((ROOT/'build/rmuc_layered').exists(), 'Native layered binary not built')
class NativeBridgeTests(unittest.TestCase):
    def test_tunnel_and_ramp_routes(self):
        engine = bridge.LayeredProcess()
        try:
            state = engine.step({'count': 135, 'action': [1000, 0]})
            self.assertEqual(state['robot']['surface_id'], 'tunnel_floor')
            self.assertEqual(state['robot']['z_mm'], 0)
            self.assertIn('tunnel_annotation', state['active_trigger_ids'])
            self.assertTrue(any(e['type'] == 'trigger_enter' and e['id'] == 'tunnel_annotation' for e in state['batch_events']))
            self.assertNotIn('batch_events', engine.state())
            self.assertEqual(engine.state()['hash'], state['hash'])
            state = engine.step({'count': 165, 'action': [1000, 0]})
            self.assertEqual(state['robot']['surface_id'], 'east_floor')
            engine.reset(bridge.sample())
            for count, action in [(155, [0,1000]), (300, [1000,0]), (155, [0,-1000]), (150, [-1000,0])]:
                state = engine.step({'count': count, 'action': action})
            self.assertEqual(state['robot']['surface_id'], 'upper_deck')
            self.assertEqual(state['robot']['z_mm'], 1000)
            self.assertIn('deck_annotation', state['active_trigger_ids'])
            self.assertNotIn('tunnel_annotation', state['active_trigger_ids'])
            self.assertEqual(engine.import_replay(engine.replay())['hash'], state['hash'])
        finally:
            engine.close()

    def test_round_trip_and_failed_reset(self):
        engine = bridge.LayeredProcess()
        try:
            state = engine.step({'count': 10, 'action': [1000, 0]})
            replay = engine.replay()
            self.assertEqual(state['tick'], 10)
            self.assertEqual(engine.import_replay(replay)['hash'], state['hash'])
            bad = bridge.sample(); bad['config']['start']['x_mm'] = -99999
            with self.assertRaises(ValueError): engine.reset(bad)
            self.assertEqual(replay, engine.replay())
        finally:
            engine.close()


if __name__ == '__main__': unittest.main()
