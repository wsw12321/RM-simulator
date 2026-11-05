#!/usr/bin/env python3
"""Independent filtered-combat bridge review. No rendered-browser claim."""
import copy
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from combat_mode import bridge

POLICY = {'kind': 'policy', 'sdk_version': 1, 'controller_id': 'filtered-cover-v1'}
IDLE = {'kind': 'manual', 'action': {'sdk_version': 1, 'dx': 0, 'dy': 0,
                                   'aim_x': 6000, 'aim_y': 3800, 'fire': 0}}
PUBLIC_KEYS = {'build', 'schema', 'profile', 'observation_profile', 'official',
               'observer', 'tick', 'terminal', 'observation', 'last_sources'}
OBS_KEYS = {'sdk_version', 'profile', 'hz', 'tick', 'self_index', 'sensor_range_mm',
            'self', 'arena', 'enemy', 'projectiles'}


def request(count=1, observer=0, sources=None):
    return {'count': count, 'observer': observer,
            'controllers': copy.deepcopy(sources or [IDLE, POLICY])}


class IndependentCombatReview(unittest.TestCase):
    def setUp(self):
        self.engine = bridge.CombatProcess(os.environ.get('COMBAT_REVIEW_BINARY'))
        self.addCleanup(self.engine.close)

    def assert_filtered(self, state):
        self.assertEqual(set(state) - {'advanced', 'verified_frames'}, PUBLIC_KEYS)
        observation = state['observation']
        self.assertEqual(set(observation), OBS_KEYS)
        self.assertEqual(observation['self_index'], state['observer'])
        self.assertEqual(observation['tick'], state['tick'])
        self.assertEqual(set(observation['self']), {'x', 'y', 'yaw', 'hp', 'ammo', 'cooldown'})
        if observation['enemy'] is not None:
            self.assertEqual(set(observation['enemy']), {'x', 'y', 'yaw'})
        for projectile in observation['projectiles']:
            self.assertEqual(set(projectile), {'x', 'y', 'vx', 'vy'})
        self.assertEqual(observation['projectiles'], sorted(observation['projectiles'],
                         key=lambda p: (p['x'], p['y'], p['vx'], p['vy'])))

    def test_public_state_and_every_mutation_response_allowlist(self):
        for observer in (0, 1):
            self.assert_filtered(self.engine.state(observer))
            self.assertIsNone(self.engine.state(observer)['observation']['enemy'])
        self.assert_filtered(self.engine.step(request(90)))
        self.assert_filtered(self.engine.state(1))
        saved = self.engine.replay()
        self.assert_filtered(self.engine.import_replay({'observer': 1, 'replay': saved}))
        self.assert_filtered(self.engine.reset({'observer': 1}))
        self.assertEqual(self.engine.state(0)['tick'], 0)

    def test_strict_fields_versions_scalar_types_and_bounds_preserve_live_world(self):
        self.engine.step(request(3))
        before = self.engine.replay()
        invalid = []
        for field, low, high in [('sdk_version', 1, 1), ('dx', -1000, 1000),
                                 ('dy', -1000, 1000), ('aim_x', 0, 12000),
                                 ('aim_y', 0, 7600), ('fire', 0, 1)]:
            for value in (True, False, 1.0, float('nan'), float('inf'), '1', None, low - 1, high + 1):
                data = request(); data['controllers'][0]['action'][field] = value
                invalid.append(data)
        for key, values in [('count', [0, -1, 121, True, 1.0]),
                            ('observer', [-1, 2, True, 1.0]),
                            ('controllers', [None, [], [IDLE], [IDLE, POLICY, IDLE]])]:
            invalid.extend({**request(), key: value} for value in values)
        for key, values in [('sdk_version', [0, 2, True, 1.0]),
                            ('controller_id', ['', 'unknown', 'bad id', 1, None])]:
            for value in values:
                data = request(); data['controllers'][1][key] = value; invalid.append(data)
        extra = request(); extra['controllers'][0]['action']['hidden_hp'] = 100; invalid.append(extra)
        extra = request(); extra['controllers'][1]['action'] = {}; invalid.append(extra)
        for data in invalid:
            with self.subTest(data=data):
                with self.assertRaises((ValueError, RuntimeError)):
                    self.engine.step(data)
                self.assertEqual(self.engine.replay(), before)
        self.assertEqual(self.engine.step(request())['tick'], 4)

    def test_mixed_sources_replay_uses_actions_only_and_continues_statelessly(self):
        self.engine.step(request(31))
        self.engine.step(request(41, sources=[POLICY, POLICY]))
        self.engine.step(request(19, observer=1, sources=[POLICY, IDLE]))
        saved = self.engine.replay()
        self.assertEqual([frame['inputs'][0]['kind'] for frame in saved['frames'][:31]], ['manual'] * 31)
        commands = []
        command = bridge._Native.command
        def record(instance, value):
            commands.append(value.split()[0])
            return command(instance, value)
        with patch.object(bridge._Native, 'command', record):
            restored = self.engine.import_replay({'observer': 1, 'replay': saved})
        self.assertEqual(restored['verified_frames'], 91)
        self.assertTrue({'RESET', 'FRAMES', 'STATE'}.issubset(commands))
        self.assertNotIn('POLICY_META', commands)
        self.assertNotIn('STEP', commands)
        self.assertEqual(self.engine.replay(), saved)
        reference = bridge.CombatProcess(os.environ.get('COMBAT_REVIEW_BINARY'))
        self.addCleanup(reference.close)
        reference.import_replay({'observer': 0, 'replay': saved})
        self.engine.step(request(23, sources=[POLICY, POLICY]))
        for count in (3, 7, 13):
            reference.step(request(count, sources=[POLICY, POLICY]))
        self.assertEqual(self.engine.replay(), reference.replay())

    def test_policy_failure_after_prefix_restoration_is_transactional(self):
        self.engine.step(request(23))
        before = self.engine.replay()
        live = self.engine._native
        commands = []
        candidates = []
        command = bridge._Native.command
        def fail(instance, value):
            commands.append(value.split()[0])
            if value.startswith('STEP '):
                candidates.append(instance)
                raise RuntimeError('Injected candidate output failure')
            return command(instance, value)
        with patch.object(bridge._Native, 'command', fail):
            with self.assertRaisesRegex(RuntimeError, 'candidate output failure'):
                self.engine.step(request(20))
        self.assertIn('FRAMES', commands)
        self.assertEqual(len(candidates), 1)
        self.assertIs(self.engine._native, live)
        self.assertIsNot(candidates[0], live)
        self.assertIsNotNone(candidates[0].proc.poll())
        self.assertEqual(self.engine.replay(), before)
        self.assertEqual(self.engine.step(request())['tick'], 24)

    def test_bad_second_robot_attribution_and_observation_fields_fail_closed(self):
        self.engine.step(request(4))
        before = self.engine.replay()
        command = bridge._Native.command
        for corruption in ('source', 'observation', 'observation_order'):
            def corrupt(instance, value):
                result = command(instance, value)
                if value.startswith('STEP '):
                    if corruption == 'source':
                        result['frames'][-1]['inputs'][1]['controller_id'] = 'unrequested-controller'
                    elif corruption == 'observation':
                        result['observation']['enemy_hp'] = 100
                    else:
                        result['observation']['projectiles'] = [
                            {'x': 3000, 'y': 1000, 'vx': 0, 'vy': 0},
                            {'x': 2000, 'y': 1000, 'vx': 0, 'vy': 0}]
                return result
            with patch.object(bridge._Native, 'command', corrupt):
                with self.assertRaises((ValueError, RuntimeError)):
                    self.engine.step(request())
            self.assertEqual(self.engine.replay(), before)

    def test_metadata_failure_cannot_kill_or_mutate_live_simulation(self):
        self.engine.step(request(7))
        before = self.engine.replay()
        live = self.engine._native
        candidates = []
        command = bridge._Native.command
        def fail(instance, value):
            if value == 'POLICY_META':
                candidates.append(instance)
                raise RuntimeError('Injected descriptor timeout')
            return command(instance, value)
        with patch.object(bridge._Native, 'command', fail):
            self.assertIsNone(self.engine.meta()['policy'])
            with self.assertRaisesRegex(RuntimeError, 'descriptor timeout'):
                self.engine.step(request())
        self.assertTrue(candidates)
        self.assertTrue(all(candidate is not live and candidate.proc.poll() is not None for candidate in candidates))
        self.assertIs(self.engine._native, live)
        self.assertEqual(self.engine.replay(), before)
        self.assertEqual(self.engine.step(request(sources=[IDLE, IDLE]))['tick'], 8)

    def test_late_replay_divergence_invalid_metadata_and_source_type_rollback(self):
        self.engine.step(request(120, sources=[IDLE, IDLE]))
        self.engine.step(request(13, sources=[IDLE, IDLE]))
        before = self.engine.replay()
        changes = [
            lambda data: data.__setitem__('schema', 'wrong-schema'),
            lambda data: data.__setitem__('build', '0' * 64),
            lambda data: data.__setitem__('official', True),
            lambda data: data.__setitem__('initial_hash', data['initial_hash'] ^ 1),
            lambda data: data.__setitem__('final_hash', data['final_hash'] ^ 1),
            lambda data: data['frames'][-1].__setitem__('hash', data['frames'][-1]['hash'] ^ 1),
            lambda data: data['frames'][-1]['observation_hashes'].__setitem__(1, 0),
            lambda data: data['frames'][-1]['inputs'][1].__setitem__('sdk_version', True),
            lambda data: data['frames'][-1]['inputs'][1].__setitem__('controller_id', 'manual-must-be-null'),
            lambda data: data['frames'][-1]['inputs'][1]['action'].__setitem__(0, 1001),
            lambda data: data['frames'][-1].__setitem__('tick', 1),
            lambda data: data['frames'][-1].__setitem__('hidden_hp', 100),
        ]
        for change in changes:
            modified = copy.deepcopy(before)
            change(modified)
            with self.assertRaises((ValueError, RuntimeError)):
                self.engine.import_replay({'observer': 1, 'replay': modified})
            self.assertEqual(self.engine.replay(), before)

    def test_historical_controller_labels_are_declarative_not_authentication(self):
        self.engine.step(request(5, sources=[POLICY, POLICY]))
        saved = self.engine.replay()
        for frame in saved['frames']:
            frame['inputs'][1]['controller_id'] = 'declared-historical-controller'
        restored = self.engine.import_replay({'observer': 1, 'replay': saved})
        self.assertEqual(restored['last_sources'][1]['controller_id'], 'declared-historical-controller')
        self.assertEqual(self.engine.replay(), saved)
        self.engine.step(request())
        self.assertEqual(self.engine.replay()['frames'][-1]['inputs'][1]['controller_id'], POLICY['controller_id'])

    def test_source_change_guard_and_private_envelope_filter(self):
        before = self.engine.replay()
        with patch.object(bridge, 'build_id', return_value='0' * 64):
            with self.assertRaises(ValueError):
                self.engine.step(request())
        self.assertEqual(self.engine.replay(), before)
        command = bridge._Native.command
        def extra_debug(instance, value):
            result = command(instance, value)
            result['private_debug'] = {'hidden_robot_hp': 100, 'hidden_events': [1, 2, 3]}
            return result
        with patch.object(bridge._Native, 'command', extra_debug):
            with self.assertRaises((ValueError, RuntimeError)):
                self.engine.state(0)
        self.assertEqual(self.engine.replay(), before)
        private_state = self.engine._native.command('STATE 0')
        private_state['private_debug'] = {'hidden_robot_hp': 100}
        public = self.engine._public(private_state)
        self.assert_filtered(public)
        self.assertNotIn('private_debug', public)


if __name__ == '__main__':
    unittest.main()
