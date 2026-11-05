#!/usr/bin/env python3
"""Programmable local controller contract, action replay, and process rollback."""
import copy
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from layered_mode import bridge

BINARY = Path(os.environ.get('LAYERED_POLICY_BINARY', ROOT / 'build/rmuc_layered'))
REQUEST = {'count': 1, 'sdk_version': 1, 'controller_id': 'synthetic-ramp-deck-v1'}


@unittest.skipUnless(BINARY.exists(), 'Native layered policy binary not built')
class PolicyBridgeTests(unittest.TestCase):
    def setUp(self):
        self.engine = bridge.LayeredProcess(BINARY)

    def tearDown(self):
        self.engine.close()

    def policy(self, count):
        return self.engine.policy_step({**REQUEST, 'count': count})

    def test_metadata_and_default_route_stops(self):
        meta = self.engine.meta()
        self.assertEqual(meta['policy']['sdk_version'], 1)
        self.assertEqual(meta['policy']['observation'], 'full-authored-geometry')
        self.assertEqual(meta['policy']['execution'], 'trusted-local-native')
        self.assertIsNone(self.engine.state()['action_source'])
        self.policy(600)
        state = self.policy(165)
        robot = state['robot']
        self.assertEqual((robot['x_q1024'], robot['y_q1024'], robot['z_q1048576']),
                         (4000*1024, 1500*1024, 1000*1048576))
        self.assertEqual(robot['surface_id'], 'upper_deck')
        self.assertIn('deck_annotation', state['active_trigger_ids'])
        stopped = self.policy(20)
        self.assertEqual(stopped['robot'], robot)
        self.assertTrue(all(f[1:3] == [0, 0] for f in self.engine.replay()['frames'][-20:]))

    def test_policy_batch_partition_and_manual_resolution(self):
        self.policy(170)
        whole = self.engine.replay()
        self.engine.reset(bridge.sample())
        for count in [1, 60, 39, 70]: self.policy(count)
        self.assertEqual(whole, self.engine.replay())
        self.engine.reset(bridge.sample())
        for _, dx, dy, _ in whole['frames']:
            self.engine.step({'count': 1, 'action': [dx, dy]})
        manual = self.engine.replay()
        self.assertEqual(manual['frames'], whole['frames'])
        self.assertEqual(manual['final_hash'], whole['final_hash'])
        self.assertNotEqual(manual['action_sources'], whole['action_sources'])

    def test_nondivisor_speed_and_off_lattice_resume(self):
        data = bridge.sample()
        data['config']['speed_mm_per_tick'] = 17
        self.engine.reset(data)
        self.engine.step({'count': 1, 'action': [13, 77]})
        for _ in range(3): state = self.policy(600)
        self.assertEqual(state['robot']['surface_id'], 'upper_deck')
        self.assertLessEqual(abs(state['robot']['x_q1024'] - 4000*1024), 1024)
        self.assertLessEqual(abs(state['robot']['y_q1024'] - 1500*1024), 1024)
        robot = state['robot']
        self.assertEqual(self.policy(10)['robot'], robot)

    def test_request_types_versions_and_source_guard_preserve(self):
        self.engine.step({'count': 2, 'action': [0, 0]})
        before = self.engine.replay()
        invalid = [
            {**REQUEST, 'sdk_version': True}, {**REQUEST, 'sdk_version': 1.0},
            {**REQUEST, 'sdk_version': 2}, {**REQUEST, 'controller_id': 'other'},
            {**REQUEST, 'controller_id': 'x'*65}, {**REQUEST, 'count': True},
            {**REQUEST, 'count': 0}, {**REQUEST, 'action': [0, 0]},
        ]
        for request in invalid:
            with self.assertRaises(ValueError): self.engine.policy_step(request)
            self.assertEqual(before, self.engine.replay())
        for action in [[True, 0], [1.0, 0], [1001, 0]]:
            with self.assertRaises(ValueError): self.engine.step({'count': 1, 'action': action})
            self.assertEqual(before, self.engine.replay())
        with patch.object(bridge, 'build_id', return_value='f'*64):
            for request in (lambda: self.policy(1), lambda: self.engine.step({'count': 1, 'action': [0, 0]})):
                with self.assertRaises(ValueError): request()
        self.assertEqual(before, self.engine.replay())

    def test_import_replays_only_resolved_actions_and_checks_attribution(self):
        self.policy(20)
        self.engine.step({'count': 2, 'action': [1000, 0]})
        self.policy(3)
        saved = self.engine.replay()
        self.assertEqual(saved['schema'], 'rmuc-layered-replay.v2')
        self.assertEqual([s['kind'] for s in saved['action_sources']], ['policy', 'manual', 'policy'])
        commands = []
        original = bridge._Native.command
        def record(process, command):
            commands.append(command)
            if command.startswith('POLICY ' ) or command == 'POLICY_META':
                raise AssertionError('Replay must not execute controller decisions or descriptors')
            return original(process, command)
        with patch.object(bridge._Native, 'command', record):
            state = self.engine.import_replay(saved)
        self.assertEqual(state['verified_frames'], 25)
        self.assertEqual(saved, self.engine.replay())
        self.assertTrue(any(c.startswith('ADVANCE ') for c in commands))
        self.assertFalse(any(c.startswith('POLICY ') for c in commands))
        self.assertNotIn('POLICY_META', commands)
        labelled = copy.deepcopy(saved)
        labelled['action_sources'][0]['controller_id'] = 'declared-label-only'
        self.assertEqual(self.engine.import_replay(labelled)['hash'], saved['final_hash'])
        self.assertEqual(self.engine.replay()['action_sources'][0]['controller_id'], 'declared-label-only')
        self.engine.import_replay(saved)
        for mutate in (
            lambda d: d['frames'][10].__setitem__(1, 1001),
            lambda d: d['frames'][10].__setitem__(3, '0'*16),
            lambda d: d['action_sources'][0].__setitem__('controller_id', 'not a valid id'),
            lambda d: d['action_sources'][0].__setitem__('sdk_version', True),
            lambda d: d['action_sources'][0].__setitem__('start_tick', 1),
            lambda d: d['action_sources'][0].__setitem__('count', 19),
            lambda d: d.__setitem__('action_sources', []),
            lambda d: d.__setitem__('schema', 'rmuc-layered-replay.v1'),
        ):
            bad = copy.deepcopy(saved); mutate(bad)
            with self.assertRaises(ValueError): self.engine.import_replay(bad)
            self.assertEqual(saved, self.engine.replay())

    def test_candidate_failure_timeout_and_bad_response_preserve_live_process(self):
        self.policy(10)
        before = self.engine.replay()
        live = self.engine._native
        original = bridge._Native.command
        for failure in ('exception', 'timeout', 'malformed'):
            candidates = []
            def faulty(process, command):
                if command.startswith('POLICY '):
                    candidates.append(process)
                    # The candidate advances before the injected failure. The
                    # real live process and its journal must still be untouched.
                    result = original(process, command)
                    if failure == 'exception': raise ValueError('Policy exception; experiment invalid')
                    if failure == 'timeout':
                        process.close()
                        raise RuntimeError('Native layered engine timed out; run invalid.')
                    result['frames'][0][1] = True
                    return result
                return original(process, command)
            with patch.object(bridge._Native, 'command', faulty):
                with self.assertRaises((ValueError, RuntimeError)): self.policy(5)
            self.assertIs(self.engine._native, live)
            self.assertEqual(before, self.engine.replay())
            self.assertEqual(len(candidates), 1)
            self.assertIsNotNone(candidates[0].proc.poll())
        self.assertEqual(self.policy(1)['tick'], 11)

    def test_duration_and_metadata_failure_are_atomic(self):
        data = bridge.sample(); data['config']['max_ticks'] = 3
        self.engine.reset(data)
        self.policy(2)
        before = self.engine.replay()
        with self.assertRaises(ValueError): self.policy(2)
        self.assertEqual(before, self.engine.replay())
        original = bridge._Native.command
        def fail_descriptor(process, command):
            if command == 'POLICY_META': raise ValueError('Invalid descriptor')
            return original(process, command)
        with patch.object(bridge._Native, 'command', fail_descriptor):
            with self.assertRaises(ValueError): self.policy(1)
        self.assertEqual(before, self.engine.replay())
        self.assertEqual(self.policy(1)['status'], 'limit_reached')


if __name__ == '__main__': unittest.main()
