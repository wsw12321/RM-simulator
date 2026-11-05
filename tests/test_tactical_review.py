#!/usr/bin/env python3
"""Independent bridge/transaction review. This does not claim browser rendering."""
import copy
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tactical_mode import bridge
from tactical_mode import build_id as identity

IDLE = {'kind': 'manual', 'action': {'sdk_version': 1, 'dx': 0, 'dy': 0,
    'aim_x': 6000, 'aim_y': 3800, 'fire': 0}}
POLICY = {'kind': 'policy', 'sdk_version': 1, 'controller_id': 'public-capture-combat-v1'}
PUBLIC_KEYS = {'build', 'schema', 'profile', 'observation_profile', 'observation_version', 'official',
    'observer', 'tick', 'terminal', 'observation', 'last_sources', 'config', 'result'}
OBS_KEYS = {'sdk_version', 'observation_version', 'profile', 'combat', 'self_heat', 'zones', 'score', 'rate',
    'qualified_ms', 'duration_ms', 'credits_enabled'}


def request(count=1, observer=0, sources=None):
    return {'count': count, 'observer': observer,
            'controllers': copy.deepcopy(sources or [IDLE, POLICY])}


class IndependentTacticalReview(unittest.TestCase):
    def setUp(self):
        self.engine = bridge.TacticalProcess(os.environ.get('TACTICAL_REVIEW_BINARY'))
        self.addCleanup(self.engine.close)

    def assert_filtered(self, state):
        self.assertEqual(set(state) - {'advanced', 'verified_frames'}, PUBLIC_KEYS)
        self.assertEqual(set(state['observation']), OBS_KEYS)
        self.assertEqual(state['observation']['combat']['self_index'], state['observer'])
        self.assertEqual(state['observation']['combat']['tick'], state['tick'])
        self.assertEqual(state['observation']['qualified_ms'], (state['tick']*1000+59)//60)
        for zone in state['observation']['zones']:
            self.assertEqual(set(zone), {'id', 'rect', 'original_owner', 'controller', 'degree'})
        combat = state['observation']['combat']
        if combat['enemy'] is not None:
            self.assertEqual(set(combat['enemy']), {'x', 'y', 'yaw'})
        for bullet in combat['projectiles']:
            self.assertEqual(set(bullet), {'x', 'y', 'vx', 'vy'})
        self.assertNotIn('hash', state)
        self.assertNotIn('frames', state)
        self.assertNotIn('damage_admin', state)
        self.assertNotIn('heat_admin', state)
        self.assertNotIn('attack_damage', state['observation'])

    def test_all_public_responses_and_configuration_combinations(self):
        for boundary in bridge.BOUNDARIES:
            for credit in bridge.CREDITS:
                for damage in bridge.DAMAGE_STRATEGIES:
                    config = {'boundary_policy': boundary, 'credit_strategy': credit,
                              'attack_damage_strategy': damage, 'heat_strategy': bridge.HEAT_STRATEGIES[0],
                              'heat_threshold_policy': bridge.HEAT_THRESHOLDS[0]}
                    state = self.engine.reset({'observer': 1, 'config': config})
                    self.assert_filtered(state)
                    self.assertEqual(state['config'], bridge.expanded_config(config))
                    self.assert_filtered(self.engine.step(request(61, 1, [IDLE, IDLE])))
                    saved = self.engine.replay()
                    self.assert_filtered(self.engine.import_replay({'observer': 0, 'replay': saved}))
                    self.assertEqual(self.engine.replay(), saved)
                    self.assertEqual(self.engine.state(0)['observation']['score'],
                                     None if credit == 'disabled' else [0, 0])

    def test_strict_manual_policy_scalars_and_configuration_inputs_are_atomic(self):
        self.engine.step(request(4))
        before = self.engine.replay()
        invalid = []
        for field, low, high in [('sdk_version', 1, 1), ('dx', -1000, 1000),
                ('dy', -1000, 1000), ('aim_x', 0, 12000), ('aim_y', 0, 7600), ('fire', 0, 1)]:
            for value in (True, False, 1.0, float('nan'), float('inf'), '1', None, low-1, high+1):
                data = request(); data['controllers'][0]['action'][field] = value
                invalid.append(data)
        for key, values in [('count', [0, -1, 601, True, 1.0]),
                ('observer', [-1, 2, True, 1.0]), ('controllers', [None, [], [IDLE], [IDLE]*3])]:
            invalid += [{**request(), key: value} for value in values]
        for key, values in [('sdk_version', [0, 2, True, 1.0]),
                ('controller_id', ['', 'unknown', 'bad id', 1, None])]:
            for value in values:
                data = request(); data['controllers'][1][key] = value; invalid.append(data)
        for data in invalid:
            with self.subTest(data=data):
                with self.assertRaises((ValueError, RuntimeError)):
                    self.engine.step(data)
                self.assertEqual(self.engine.replay(), before)
        for config in [None, {}, bridge.expanded_config(bridge.DEFAULT_CONFIG),
                       {**bridge.DEFAULT_CONFIG, 'death_strategy': 'ordinary-grace'},
                       {**bridge.DEFAULT_CONFIG, 'credit_strategy': True},
                       {**bridge.DEFAULT_CONFIG, 'boundary_policy': 'unknown'},
                       {k: v for k, v in bridge.DEFAULT_CONFIG.items() if k != 'attack_damage_strategy'},
                       {**bridge.DEFAULT_CONFIG, 'attack_damage_strategy': None},
                       {**bridge.DEFAULT_CONFIG, 'attack_damage_strategy': True},
                       {**bridge.DEFAULT_CONFIG, 'attack_damage_strategy': 'nominal-hit-damage'}]:
            with self.subTest(config=config):
                with self.assertRaises((ValueError, RuntimeError)):
                    self.engine.reset({'observer': 0, 'config': config})
                self.assertEqual(self.engine.replay(), before)
        self.assertEqual(self.engine.step(request())['tick'], 5)

    def test_mixed_sources_multibatch_replay_is_callback_free_and_continues(self):
        self.engine.step(request(600, sources=[IDLE, IDLE]))
        self.engine.step(request(31))
        self.engine.step(request(17, sources=[POLICY, POLICY]))
        self.engine.step(request(9, observer=1, sources=[POLICY, IDLE]))
        saved = self.engine.replay()
        command = bridge._Native.command
        commands = []
        def record(instance, value):
            commands.append(value.split()[0])
            return command(instance, value)
        with patch.object(bridge._Native, 'command', record):
            state = self.engine.import_replay({'observer': 1, 'replay': saved})
        self.assertEqual(state['verified_frames'], 657)
        self.assertEqual(commands.count('FRAMES'), 2)
        self.assertNotIn('POLICY_META', commands)
        self.assertNotIn('STEP', commands)
        self.assertEqual(self.engine.replay(), saved)
        reference = bridge.TacticalProcess(os.environ.get('TACTICAL_REVIEW_BINARY'))
        self.addCleanup(reference.close)
        reference.import_replay({'observer': 1, 'replay': saved})
        self.engine.step(request(23, sources=[POLICY, POLICY]))
        for count in (3, 7, 13):
            reference.step(request(count, sources=[POLICY, POLICY]))
        self.assertEqual(self.engine.replay(), reference.replay())

    def test_candidate_failure_and_descriptor_failure_preserve_live_world(self):
        self.engine.step(request(23))
        before = self.engine.replay()
        live = self.engine._native
        command = bridge._Native.command
        for target in ('STEP ', 'POLICY_META'):
            candidates = []
            def fail(instance, value):
                if value.startswith(target):
                    candidates.append(instance)
                    raise RuntimeError('Injected tactical candidate failure')
                return command(instance, value)
            with patch.object(bridge._Native, 'command', fail):
                with self.assertRaisesRegex(RuntimeError, 'candidate failure'):
                    self.engine.step(request(20))
            self.assertTrue(candidates)
            self.assertTrue(all(candidate is not live and candidate.proc.poll() is not None for candidate in candidates))
            self.assertIs(self.engine._native, live)
            self.assertEqual(self.engine.replay(), before)
        self.assertEqual(self.engine.step(request())['tick'], 24)

    def test_hidden_fields_and_native_corruption_fail_closed_without_committing(self):
        self.engine.step(request(4))
        before = self.engine.replay()
        command = bridge._Native.command
        mutations = [
            lambda result: result['observation'].__setitem__('capture_progress', 1),
            lambda result: result['observation']['zones'][0].__setitem__('progress_ms', 1),
            lambda result: result['observation']['zones'][0].__setitem__('detected', [True, False]),
            lambda result: result['observation']['zones'][0].__setitem__('active', [True, False]),
            lambda result: result['observation']['zones'][0].__setitem__('deactivation_at_ms', [2000, None]),
            lambda result: result['observation']['zones'][0].__setitem__('completed_at_ms', 5000),
            lambda result: result['observation']['combat'].__setitem__('enemy_hp', 100),
            lambda result: result['observation']['combat'].__setitem__('events', []),
            lambda result: result['observation']['zones'][0]['rect'].__setitem__('x1', 1),
            lambda result: result['observation']['rate'].__setitem__(0, 4),
            lambda result: result['observation']['score'].__setitem__(0, True),
            lambda result: result.__setitem__('terminal', 1),
            lambda result: result['frames'][-1]['inputs'][1].__setitem__('controller_id', 'wrong-source'),
            lambda result: result['damage_admin'].__setitem__('events', []),
            lambda result: result['damage_admin'].__setitem__('complete', 1),
            lambda result: result['damage_admin'].__setitem__('attack_damage', [100, 0]),
            lambda result: result['damage_admin'].__setitem__('remaining_robot_hp', [99, 100]),
            lambda result: result['damage_admin'].__setitem__('actual_hp_loss', [0, True]),
            lambda result: result['damage_admin'].__setitem__('strategy', bridge.DAMAGE_STRATEGIES[0]),
            lambda result: result['damage_admin'].__setitem__('last_sequence', -1),
            lambda result: result['damage_admin'].__setitem__('last_sequence', 1),
            lambda result: result['damage_admin'].__setitem__('actual_hp_loss', [101, 0]),
            lambda result: result['damage_admin'].__setitem__('nominal_projectile_damage', [1201, 0]),
            lambda result: result['damage_admin'].__setitem__('nominal_projectile_damage', [1, 0]),
            lambda result: result['damage_admin'].__setitem__('nominal_projectile_damage', [10, 0]),
            lambda result: result['damage_admin'].__setitem__('reason', 'unadjudicated-native-damage'),
            lambda result: result.__setitem__('private_debug', {'enemy_hp': 100}),
        ]
        for mutate in mutations:
            def corrupt(instance, value):
                result = command(instance, value)
                if value.startswith('STEP '):
                    mutate(result)
                return result
            with patch.object(bridge._Native, 'command', corrupt):
                with self.assertRaises((ValueError, RuntimeError)):
                    self.engine.step(request())
            self.assertEqual(self.engine.replay(), before)
        # Even when a caller bypasses validation, the explicit public allowlist
        # cannot accidentally include new top-level administrative diagnostics.
        private = self.engine._native.command('STATE 0')
        private['private_debug'] = {'enemy_hp': 100}
        self.assert_filtered(self.engine._public(private))

    def test_replay_late_divergence_metadata_and_assumptions_are_atomic(self):
        self.engine.step(request(600, sources=[IDLE, IDLE]))
        self.engine.step(request(13, sources=[IDLE, IDLE]))
        before = self.engine.replay()
        changes = [
            lambda replay: replay.__setitem__('build', '0'*64),
            lambda replay: replay.__setitem__('initial_hash', replay['initial_hash']^1),
            lambda replay: replay.__setitem__('final_hash', replay['final_hash']^1),
            lambda replay: replay['frames'][-1].__setitem__('hash', replay['frames'][-1]['hash']^1),
            lambda replay: replay['frames'][-1]['observation_hashes'].__setitem__(1, 0),
            lambda replay: replay['frames'][-1]['inputs'][1].__setitem__('sdk_version', True),
            lambda replay: replay['frames'][-1]['inputs'][1].__setitem__('controller_id', 'manual-is-null'),
            lambda replay: replay['frames'][-1]['inputs'][1]['action'].__setitem__(4, 2),
            lambda replay: replay['frames'][-1].__setitem__('tick', 1),
            lambda replay: replay['config'].__setitem__('death_strategy', 'official'),
            lambda replay: replay['config'].__setitem__('respawn', True),
            lambda replay: replay['config'].__setitem__('base_hp_sentinel', True),
            lambda replay: replay['config'].__setitem__('phase_order', 'credit-before-capture'),
            lambda replay: replay['config'].__setitem__('boundary_policy', bridge.BOUNDARIES[0]),
            lambda replay: replay['config'].__setitem__('attack_damage_strategy', bridge.DAMAGE_STRATEGIES[0]),
            lambda replay: replay['config'].__setitem__('remaining_hp_strategy', 'include-disabled'),
            lambda replay: replay.__setitem__('damage_admin', {}),
        ]
        for change in changes:
            modified = copy.deepcopy(before); change(modified)
            with self.assertRaises((ValueError, RuntimeError)):
                self.engine.import_replay({'observer': 1, 'replay': modified})
            self.assertEqual(self.engine.replay(), before)

    def test_legal_but_wrong_candidate_configuration_cannot_replace_run(self):
        self.engine.step(request(7))
        before = self.engine.replay()
        live = self.engine._native
        command = bridge._Native.command
        alternate = bridge.expanded_config({**bridge.DEFAULT_CONFIG, 'boundary_policy': bridge.BOUNDARIES[0]})
        for target in ('FRAMES ', 'STATE '):
            def corrupt(instance, value):
                result = command(instance, value)
                if instance is not live and value.startswith(target):
                    result['config'] = copy.deepcopy(alternate)
                return result
            with patch.object(bridge._Native, 'command', corrupt):
                with self.assertRaises((ValueError, RuntimeError)):
                    self.engine.import_replay({'observer': 1, 'replay': before})
            self.assertIs(self.engine._native, live)
            self.assertEqual(self.engine.replay(), before)

    def test_timeout_comparison_order_and_missing_ledger(self):
        damage = {'complete': True, 'attack_damage': [50, 10], 'remaining_robot_hp': [50, 90]}
        cases = [([2, 1], damage, 'red-wins', 'timeout-control'),
                 ([1, 2], damage, 'blue-wins', 'timeout-control'),
                 ([2, 2], damage, 'red-wins', 'timeout-attack-damage'),
                 ([2, 2], {**damage, 'attack_damage': [10, 50]}, 'blue-wins', 'timeout-attack-damage'),
                 ([2, 2], {**damage, 'attack_damage': [10, 10]}, 'blue-wins', 'timeout-surviving-robot-hp'),
                 ([2, 2], {**damage, 'attack_damage': [10, 10], 'remaining_robot_hp': [50, 50]}, 'draw', 'timeout-equal')]
        all_results = [(outcome, reason) for _, _, outcome, reason in cases]
        for score, values, outcome, reason in cases:
            bridge.validate_result({'outcome': outcome, 'reason': reason}, 1,
                                   bridge.DEFAULT_CONFIG, score, values)
            for wrong, why in all_results:
                if (wrong, why) == (outcome, reason):
                    continue
                with self.assertRaises(ValueError):
                    bridge.validate_result({'outcome': wrong, 'reason': why}, 1,
                                           bridge.DEFAULT_CONFIG, score, values)
        missing = {**bridge.DEFAULT_CONFIG, 'attack_damage_strategy': bridge.DAMAGE_STRATEGIES[0]}
        bridge.validate_result({'outcome': 'unresolved-timeout-tie', 'reason': bridge.DAMAGE_STRATEGIES[0]},
                               1, missing, [0, 0], {'complete': False})
        with self.assertRaises(ValueError):
            bridge.validate_result({'outcome': 'draw', 'reason': 'timeout-equal'},
                                   1, missing, [0, 0], {'complete': False})
        # Unavailable later stages do not override a decided earlier stage.
        bridge.validate_result({'outcome': 'red-wins', 'reason': 'timeout-control'},
                               1, bridge.DEFAULT_CONFIG, [1, 0], {'complete': False})
        bridge.validate_result({'outcome': 'red-wins', 'reason': 'timeout-attack-damage'},
                               1, bridge.DEFAULT_CONFIG, [0, 0],
                               {'complete': True, 'attack_damage': [1, 0], 'remaining_robot_hp': None})
        bridge.validate_result({'outcome': 'unresolved-timeout-tie', 'reason': 'missing-adjudicated-surviving-robot-hp'},
                               1, bridge.DEFAULT_CONFIG, [0, 0],
                               {'complete': True, 'attack_damage': [0, 0], 'remaining_robot_hp': None})

    def test_incomplete_private_ledger_is_unavailable_and_never_public(self):
        native = self.engine._native.command('STATE 0')
        native['damage_admin'].update(complete=False, reason='unadjudicated-native-damage', attack_damage=None)
        self.engine._check_state(native, 0, 0)
        self.assert_filtered(self.engine._public(native))
        for value in (0, True, [], [100], [0, 0, 0], [0, False], [-1, 0]):
            corrupt = copy.deepcopy(native)
            corrupt['damage_admin']['attack_damage'] = value
            with self.assertRaises(ValueError):
                self.engine._check_state(corrupt, 0, 0)


    def test_source_guard_and_declarative_historical_labels(self):
        self.engine.step(request(5, sources=[POLICY, POLICY]))
        saved = self.engine.replay()
        with patch.object(bridge, 'build_id', return_value='0'*64):
            with self.assertRaises(ValueError):
                self.engine.step(request())
        self.assertEqual(self.engine.replay(), saved)
        # A reset cannot swap a candidate if source changes during construction.
        with patch.object(bridge, 'build_id', side_effect=[self.engine._build, '0'*64]):
            with self.assertRaises(ValueError):
                self.engine.reset({'observer': 1, 'config': dict(bridge.DEFAULT_CONFIG)})
        self.assertEqual(self.engine.replay(), saved)
        for frame in saved['frames']:
            frame['inputs'][1]['controller_id'] = 'declared-historical-controller'
        state = self.engine.import_replay({'observer': 1, 'replay': saved})
        self.assertEqual(state['last_sources'][1]['controller_id'], 'declared-historical-controller')
        self.assertEqual(self.engine.replay(), saved)
        required = {'src/sim.hpp','src/sim.cpp','src/directions.inc',
                    'combat_mode/core.hpp','combat_mode/core.cpp','combat_mode/policy_sdk.hpp',
                    'objective_mode/objective.hpp', 'tactical_mode/core.hpp','tactical_mode/core.cpp',
                    'tactical_mode/policy_sdk.hpp','tactical_mode/example_policy.cpp',
                    'tactical_mode/bridge.py','tactical_mode/build_id.py',
                    'rules/rmuc2027_capture_v1/capture_rules.hpp','rules/rmuc2027_capture_v1/capture_rules.cpp',
                    'rules/rmuc2027_v1/match_rules.hpp','rules/rmuc2027_v1/match_rules.cpp'}
        self.assertTrue(required.issubset(set(identity.SOURCES)))


if __name__ == '__main__':
    unittest.main()
