#!/usr/bin/env python3
"""Independent policy integration review; no rendered-browser claim.

Build native binaries first. To include the old/new manual-physics differential,
set LAYERED_BASELINE_BINARY to a clean pre-policy layered executable.
"""
import copy
import os
from pathlib import Path
import random
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from layered_mode import bridge

POLICY_REQUEST = {'count': 1, 'sdk_version': 1,
                  'controller_id': 'synthetic-ramp-deck-v1'}


def physics_response(value):
    """Source build legitimately changes native hashes, not movement evidence."""
    if isinstance(value, dict):
        return {key: ([frame[:3] for frame in item] if key == 'frames'
                      else physics_response(item))
                for key, item in value.items() if key not in ('hash', 'build')}
    if isinstance(value, list):
        return [physics_response(item) for item in value]
    return value


class IndependentPolicyReview(unittest.TestCase):
    def setUp(self):
        self.engine = bridge.LayeredProcess(os.environ.get('LAYERED_POLICY_BINARY'))
        self.addCleanup(self.engine.close)

    def policy(self, count, engine=None):
        return (engine or self.engine).policy_step({**POLICY_REQUEST, 'count': count})

    def test_wrong_version_id_bounds_types_and_extra_fields_are_atomic(self):
        self.engine.step({'count': 3, 'action': [1000, 0]})
        before = self.engine.replay()
        bad = []
        for key, values in [('sdk_version', [0, 2, True, 1.0, '1']),
                            ('controller_id', ['', 'unknown', None, 1]),
                            ('count', [0, -1, 601, True, 1.0])]:
            bad.extend({**POLICY_REQUEST, key: value} for value in values)
        bad.append({**POLICY_REQUEST, 'action': [0, 0]})
        for request in bad:
            with self.subTest(request=request):
                with self.assertRaises((ValueError, RuntimeError)):
                    self.engine.policy_step(request)
                self.assertEqual(self.engine.replay(), before)

    def test_replay_never_invokes_policy_and_stateless_continuation_matches(self):
        self.policy(43)
        saved = self.engine.replay()
        native_command = bridge._Native.command
        commands = []
        def record(instance, command):
            commands.append(command.split())
            return native_command(instance, command)
        with patch.object(bridge._Native, 'command', record):
            restored = self.engine.import_replay(saved)
        self.assertEqual(restored['verified_frames'], 43)
        self.assertNotIn('POLICY', [fields[0] for fields in commands])
        self.assertNotIn('POLICY_META', [fields[0] for fields in commands])
        self.assertEqual(sum(int(fields[1]) if fields[0] == 'ADVANCE' else 1
                             for fields in commands if fields[0] in ('ADVANCE', 'FRAME')), 43)
        resumed = self.policy(119)
        reference = bridge.LayeredProcess(os.environ.get('LAYERED_POLICY_BINARY'))
        self.addCleanup(reference.close)
        self.policy(162, reference)
        self.assertEqual(resumed['hash'], reference.state()['hash'])
        self.assertEqual(self.engine.replay()['frames'], reference.replay()['frames'])

    def test_policy_failure_after_restoration_preserves_live_process_and_journal(self):
        self.engine.step({'count': 8, 'action': [1000, 0]})
        before = self.engine.replay()
        original_process = self.engine._native
        native_command = bridge._Native.command
        restored_frames = []
        candidates = []
        def injected_failure(instance, command):
            if command.startswith(('FRAME ', 'ADVANCE ')):
                fields = command.split()
                restored_frames.extend([None] * (int(fields[1]) if fields[0] == 'ADVANCE' else 1))
            if command.startswith('POLICY '):
                candidates.append(instance)
                raise RuntimeError('Synthetic candidate failure after restored prefix.')
            return native_command(instance, command)
        with patch.object(bridge._Native, 'command', injected_failure):
            with self.assertRaisesRegex(RuntimeError, 'candidate failure'):
                self.policy(7)
        self.assertEqual(len(restored_frames), 8)
        self.assertEqual(len(candidates), 1)
        self.assertIs(self.engine._native, original_process)
        self.assertIsNot(candidates[0], original_process)
        self.assertIsNotNone(candidates[0].proc.poll())
        self.assertEqual(self.engine.replay(), before)
        self.assertEqual(self.engine.step({'count': 1, 'action': [0, 0]})['tick'], 9)

    def test_descriptor_failure_isolated_from_live_world(self):
        self.engine.step({'count': 4, 'action': [1000, 0]})
        before = self.engine.replay()
        native_command = bridge._Native.command
        live = self.engine._native
        metadata_processes = []
        def fail_descriptor(instance, command):
            if command == 'POLICY_META':
                metadata_processes.append(instance)
                raise RuntimeError('Synthetic descriptor timeout.')
            return native_command(instance, command)
        with patch.object(bridge._Native, 'command', fail_descriptor):
            meta = self.engine.meta()
            self.assertIsNone(meta['policy'])
            self.assertIn('descriptor timeout', meta['policy_error'])
            with self.assertRaisesRegex(RuntimeError, 'descriptor timeout'):
                self.policy(1)
        self.assertTrue(metadata_processes)
        self.assertTrue(all(process is not live and process.proc.poll() is not None
                            for process in metadata_processes))
        self.assertIs(self.engine._native, live)
        self.assertEqual(self.engine.replay(), before)
        self.assertEqual(self.engine.step({'count': 1, 'action': [0, 0]})['tick'], 5)

    def test_replay_source_segments_reject_wrong_version_and_gaps(self):
        self.policy(5)
        self.engine.step({'count': 2, 'action': [0, 0]})
        before = self.engine.replay()
        changes = [
            lambda data: data['action_sources'][0].__setitem__('sdk_version', 2),
            lambda data: data['action_sources'][0].__setitem__('sdk_version', True),
            lambda data: data['action_sources'][0].__setitem__('count', 0),
            lambda data: data['action_sources'][1].__setitem__('start_tick', 6),
            lambda data: data['action_sources'][1].__setitem__('controller_id', 'manual-is-not-policy'),
            lambda data: data.__setitem__('action_sources', []),
            lambda data: data.__setitem__('build', '0' * 64),
            lambda data: data.__setitem__('schema', 'rmuc-layered-replay.v1'),
        ]
        for change in changes:
            modified = copy.deepcopy(before)
            change(modified)
            with self.assertRaises((ValueError, RuntimeError)):
                self.engine.import_replay(modified)
            self.assertEqual(self.engine.replay(), before)

    def test_historical_source_label_is_declarative_not_controller_authentication(self):
        self.policy(5)
        saved = self.engine.replay()
        saved['action_sources'][0]['controller_id'] = 'declared-historical-controller'
        native_command = bridge._Native.command
        commands = []
        def record(instance, command):
            commands.append(command.split()[0])
            return native_command(instance, command)
        with patch.object(bridge._Native, 'command', record):
            restored = self.engine.import_replay(saved)
        self.assertNotIn('POLICY', commands)
        self.assertNotIn('POLICY_META', commands)
        self.assertEqual(restored['action_source']['controller_id'],
                         'declared-historical-controller')
        self.assertEqual(self.engine.replay(), saved)
        current = self.policy(1)
        self.assertEqual(current['action_source']['controller_id'], POLICY_REQUEST['controller_id'])
        self.assertEqual(len(self.engine.replay()['action_sources']), 2)

    def test_source_freshness_fails_closed_before_policy(self):
        before = self.engine.replay()
        with patch.object(bridge, 'build_id', return_value='0' * 64):
            with self.assertRaises((ValueError, RuntimeError)):
                self.policy(1)
        self.assertEqual(self.engine.replay(), before)

    def test_shipped_route_and_batch_partitioning_are_deterministic(self):
        self.policy(600)
        self.policy(300)
        result = self.engine.state()
        self.assertEqual(result['tick'], 900)
        self.assertEqual(result['robot']['surface_id'], 'upper_deck')
        self.assertEqual(result['robot']['z_q1048576'], 1000 * 1048576)
        self.assertIn('deck_annotation', result['active_trigger_ids'])
        reference = bridge.LayeredProcess(os.environ.get('LAYERED_POLICY_BINARY'))
        self.addCleanup(reference.close)
        for _ in range(30):
            self.policy(30, reference)
        self.assertEqual(result['hash'], reference.state()['hash'])
        self.assertEqual(self.engine.replay()['frames'], reference.replay()['frames'])
        self.assertEqual(self.engine.replay()['frames'][-1][1:3], [0, 0])

    @unittest.skipUnless(os.environ.get('LAYERED_BASELINE_BINARY'),
                         'Set LAYERED_BASELINE_BINARY for pre-policy differential.')
    def test_manual_physics_matches_pre_policy_binary(self):
        baseline = bridge._Native(os.environ['LAYERED_BASELINE_BINARY'])
        current = bridge._Native(self.engine.binary)
        self.addCleanup(baseline.close)
        self.addCleanup(current.close)
        rng = random.Random(771904)
        fixture = bridge.sample()
        cases = [fixture]
        tall = copy.deepcopy(fixture)
        tall['config']['height_mm'] = 800
        cases.append(tall)
        upper = copy.deepcopy(fixture)
        upper['config']['start'] = {'x_mm': 3500, 'y_mm': 1400,
                                    'surface_id': 'upper_deck'}
        cases.append(upper)
        slope = copy.deepcopy(fixture)
        slope['config']['max_slope_q1024'] = 128
        cases.append(slope)
        route = [(155, 0, 1000), (310, 1000, 0),
                 (150, 0, -1000), (175, -1000, 0)]
        for index, data in enumerate(cases):
            with self.subTest(fixture=index):
                command = bridge.load_command(data['map'], data['config'])
                self.assertEqual(physics_response(baseline.command(command)),
                                 physics_response(current.command(command)))
                actions = route + [(rng.randint(1, 20), rng.randint(-1000, 1000),
                                    rng.randint(-1000, 1000)) for _ in range(120)]
                for count, dx, dy in actions:
                    command = f'ADVANCE {count} {dx} {dy}'
                    self.assertEqual(physics_response(baseline.command(command)),
                                     physics_response(current.command(command)))


if __name__ == '__main__':
    unittest.main()
