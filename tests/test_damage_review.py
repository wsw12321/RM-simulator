#!/usr/bin/env python3
"""Independent damage integration review. No browser rendering is exercised."""
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

STRATEGIES = ('missing-adjudicated-attack-damage',
              'EXPERIMENTAL-reconciled-projectile-hp-loss-v1')
IDLE = {'kind': 'manual', 'action': {'sdk_version': 1, 'dx': 0, 'dy': 0,
        'aim_x': 6000, 'aim_y': 3800, 'fire': 0}}
PUBLIC = {'build', 'schema', 'profile', 'observation_profile', 'observation_version', 'official',
          'observer', 'tick', 'terminal', 'observation', 'last_sources', 'config', 'result'}
OBS = {'sdk_version', 'observation_version', 'profile', 'combat', 'self_heat', 'zones', 'score', 'rate',
       'qualified_ms', 'duration_ms', 'credits_enabled'}
PRIVATE = {'damage_admin', 'attack_damage', 'remaining_robot_hp', 'actual_hp_loss',
           'excluded_hp_loss', 'penalty_attack_damage', 'nominal_projectile_damage'}


def request(n=1, observer=0):
    return {'count': n, 'observer': observer, 'controllers': copy.deepcopy([IDLE, IDLE])}


class DamageIntegrationReview(unittest.TestCase):
    def setUp(self):
        self.engine = bridge.TacticalProcess(os.environ.get('TACTICAL_REVIEW_BINARY'))
        self.addCleanup(self.engine.close)

    def assert_public(self, state):
        self.assertEqual(set(state) - {'advanced', 'verified_frames'}, PUBLIC)
        self.assertEqual(set(state['observation']), OBS)
        def walk(value):
            if isinstance(value, dict):
                self.assertFalse(PRIVATE & set(value))
                for child in value.values():
                    walk(child)
            elif isinstance(value, list):
                for child in value:
                    walk(child)
        walk(state)
        self.assertEqual(set(state['result']), {'outcome', 'reason'})

    def test_all_eight_configurations_roundtrip_and_private_admin_stripping(self):
        initial_hashes = set()
        for boundary in bridge.BOUNDARIES:
            for credits in bridge.CREDITS:
                for damage in STRATEGIES:
                    selection = {'boundary_policy': boundary, 'credit_strategy': credits,
                                 'attack_damage_strategy': damage, 'heat_strategy': bridge.HEAT_STRATEGIES[0],
                                 'heat_threshold_policy': bridge.HEAT_THRESHOLDS[0]}
                    with self.subTest(selection=selection):
                        self.assert_public(self.engine.reset({'observer': 1, 'config': selection}))
                        admin = self.engine._native.command('STATE 1')['damage_admin']
                        self.assertEqual(admin['strategy'], damage)
                        self.assertEqual(admin['complete'], damage == STRATEGIES[1])
                        initial_hashes.add(self.engine.replay()['initial_hash'])
                        self.assert_public(self.engine.step(request(61, 1)))
                        saved = self.engine.replay()
                        self.assertEqual(saved['config'], bridge.expanded_config(selection))
                        self.assert_public(self.engine.import_replay({'observer': 0, 'replay': saved}))
                        self.assertEqual(self.engine.replay(), saved)
                        # Explicit publication allowlist remains safe even if called
                        # on administrative state without another validation pass.
                        raw = self.engine._native.command('STATE 0')
                        raw['private_extension'] = {'attack_damage': [10, 20]}
                        self.assert_public(self.engine._public(raw))
        self.assertEqual(len(initial_hashes), 8, 'config selection must bind the initial hash')
        self.assertEqual(bridge.DEFAULT_CONFIG['attack_damage_strategy'], STRATEGIES[1])

    def test_damage_selection_is_required_typed_and_atomic(self):
        self.engine.step(request(3))
        saved = self.engine.replay()
        values = (None, False, True, 0, 1, 1.0, [], {}, '', 'official', STRATEGIES[1]+' ')
        bad_configs = [{k:v for k,v in bridge.DEFAULT_CONFIG.items() if k != 'attack_damage_strategy'}]
        bad_configs += [{**bridge.DEFAULT_CONFIG, 'attack_damage_strategy': value} for value in values]
        bad_configs += [{**bridge.DEFAULT_CONFIG, 'remaining_hp_strategy': 'official'}]
        for config in bad_configs:
            with self.subTest(config=config):
                with self.assertRaises((ValueError, RuntimeError, TypeError)):
                    self.engine.reset({'observer': 0, 'config': config})
                self.assertEqual(self.engine.replay(), saved)
        for tokens in ('RESET 0 1 1', 'RESET 0 1 1 -1', 'RESET 0 1 1 2',
                       'RESET 0 1 1 true', 'RESET 0 1 1 1.0', 'RESET 0 1 1 +1'):
            with self.assertRaises((ValueError, RuntimeError)):
                self.engine._native.command(tokens)
            self.assertEqual(self.engine.replay(), saved)

    def test_new_private_fields_fail_closed_and_candidate_does_not_commit(self):
        self.engine.step(request(4))
        saved = self.engine.replay()
        live = self.engine._native
        original = bridge._Native.command
        mutations = [
            lambda s: s['observation'].__setitem__('attack_damage', [0, 0]),
            lambda s: s['observation']['combat'].__setitem__('damage_admin', {}),
            lambda s: s['result'].__setitem__('attack_damage', [0, 0]),
            lambda s: s['damage_admin'].__setitem__('enemy_hp', 100),
            lambda s: s['damage_admin'].__setitem__('schema', True),
            lambda s: s['damage_admin'].__setitem__('module', 'unverified'),
            lambda s: s['damage_admin'].__setitem__('strategy', STRATEGIES[0]),
            lambda s: s['damage_admin'].__setitem__('complete', 1),
            lambda s: s['damage_admin'].__setitem__('attack_damage', [True, 0]),
            lambda s: s['damage_admin'].__setitem__('actual_hp_loss', [-1, 0]),
            lambda s: s['damage_admin'].__setitem__('excluded_hp_loss', [0, 0.0]),
            lambda s: s['damage_admin'].__setitem__('penalty_attack_damage', [0]),
            lambda s: s['damage_admin'].__setitem__('nominal_projectile_damage', [0, '0']),
            lambda s: s['damage_admin'].__setitem__('remaining_robot_hp', [100, -1]),
            lambda s: s['damage_admin'].__setitem__('last_sequence', True),
            lambda s: s['damage_admin'].__setitem__('last_sequence', 1),
            lambda s: s['damage_admin'].__setitem__('nominal_projectile_damage', [1, 0]),
            lambda s: s['damage_admin'].__setitem__('nominal_projectile_damage', [10, 0]),
            lambda s: s['damage_admin'].__setitem__('nominal_projectile_damage', [1210, 0]),
        ]
        for mutate in mutations:
            candidates = []
            def corrupt(instance, value):
                result = original(instance, value)
                if value.startswith('STEP '):
                    candidates.append(instance)
                    mutate(result)
                return result
            with patch.object(bridge._Native, 'command', corrupt):
                with self.assertRaises((ValueError, RuntimeError, TypeError)):
                    self.engine.step(request())
            self.assertIs(self.engine._native, live)
            self.assertTrue(all(c.proc.poll() is not None for c in candidates))
            self.assertEqual(self.engine.replay(), saved)

    def test_strategy_replay_prefix_and_late_hash_mismatch_are_atomic(self):
        self.engine.step(request(600))
        self.engine.step(request(17))
        saved = self.engine.replay()
        changes = [
            lambda r: r['config'].__setitem__('attack_damage_strategy', STRATEGIES[0]),
            lambda r: r['config'].__setitem__('remaining_hp_strategy', 'unknown'),
            lambda r: r.__setitem__('engine', 'integer-mm-tactical-v1'),
            lambda r: r.__setitem__('damage_admin', {}),
            lambda r: r['frames'][-1].__setitem__('hash', r['frames'][-1]['hash'] ^ 1),
            lambda r: r['frames'][-1]['observation_hashes'].__setitem__(0, 0),
        ]
        for change in changes:
            modified = copy.deepcopy(saved); change(modified)
            with self.assertRaises((ValueError, RuntimeError)):
                self.engine.import_replay({'observer': 1, 'replay': modified})
            self.assertEqual(self.engine.replay(), saved)
        self.engine.import_replay({'observer': 1, 'replay': saved})
        original = bridge._Native.command
        calls = []
        def record(instance, value):
            calls.append(value.split()[0])
            return original(instance, value)
        with patch.object(bridge._Native, 'command', record):
            self.engine.import_replay({'observer': 1, 'replay': saved})
        self.assertEqual(calls.count('FRAMES'), 2)
        self.assertNotIn('STEP', calls)
        self.assertNotIn('POLICY_META', calls)
        self.assert_public(self.engine.step(request(13)))

    def test_damage_module_and_provenance_are_source_pinned(self):
        required = {'rules/rmuc2027_damage_v1/damage_rules.hpp',
                    'rules/rmuc2027_damage_v1/damage_rules.cpp',
                    'rules/rmuc2027_damage_v1/provenance.json'}
        self.assertTrue(required <= set(identity.SOURCES))
        original = Path.read_bytes
        baseline = identity.build_id()
        for relative in required:
            target = ROOT / relative
            def changed(path):
                value = original(path)
                return value+b'\nINDEPENDENT-REVIEW-SOURCE-CHANGE' if path == target else value
            with patch.object(Path, 'read_bytes', changed):
                self.assertNotEqual(identity.build_id(), baseline)
        self.engine.step(request(2))
        saved = self.engine.replay()
        with patch.object(bridge, 'build_id', return_value='f'*64):
            for operation in (lambda: self.engine.step(request()),
                              lambda: self.engine.reset({'observer': 0, 'config': bridge.DEFAULT_CONFIG}),
                              lambda: self.engine.import_replay({'observer': 0, 'replay': saved})):
                with self.assertRaisesRegex(ValueError, 'source changed'):
                    operation()
        self.assertEqual(self.engine.replay(), saved)


if __name__ == '__main__':
    unittest.main(verbosity=2)
