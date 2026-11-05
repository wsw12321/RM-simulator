#!/usr/bin/env python3
"""Independent 17mm heat review; native/bridge only, no browser QA.

The ordinary firing test begins at reset and makes only accepted API requests.
Permanent-lock coverage is in the separate C++ boundary fixtures, not a claimed
naturally reachable playthrough under the immediate firing gate.
"""
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

ENABLED = 'EXPERIMENTAL-fixed-level1-cooling-priority-17mm-v1'
THRESHOLDS = ('strict-reject-unadjudicated', 'ENGINEERING-prose-greater-temporary-at-equality-v1',
              'ENGINEERING-flowchart-greater-equal-v1')
PUBLIC_KEYS = {'build', 'schema', 'profile', 'observation_profile', 'official',
               'observer', 'tick', 'terminal', 'observation', 'last_sources', 'config', 'result', 'observation_version'}
HEAT_KEYS = {'enabled', 'heat_tenths', 'limit_tenths', 'cooling_per_second',
             'overheat_locked', 'permanent_latched', 'firing_locked'}


def request(n=1, fire=0, observer=0):
    action = {'sdk_version': 1, 'dx': 0, 'dy': 0, 'aim_x': 6000, 'aim_y': 3800, 'fire': 0}
    controllers = [{'kind': 'manual', 'action': copy.deepcopy(action)} for _ in range(2)]
    controllers[0]['action']['fire'] = fire
    return {'count': n, 'observer': observer, 'controllers': controllers}


class IndependentHeatReview(unittest.TestCase):
    def setUp(self):
        self.engine = bridge.TacticalProcess(os.environ.get('TACTICAL_REVIEW_BINARY'))
        self.addCleanup(self.engine.close)

    def assert_public(self, state):
        self.assertEqual(set(state) - {'advanced', 'verified_frames'}, PUBLIC_KEYS)
        self.assertEqual(state['observation']['observation_version'], 2)
        self.assertEqual(set(state['observation']['self_heat']), HEAT_KEYS)
        self.assertNotIn('heat_admin', state)
        self.assertNotIn('damage_admin', state)
        self.assertNotIn('hash', state)
        combat = state['observation']['combat']
        if combat['enemy'] is not None:
            self.assertEqual(set(combat['enemy']), {'x', 'y', 'yaw'})
        for projectile in combat['projectiles']:
            self.assertEqual(set(projectile), {'x', 'y', 'vx', 'vy'})
        def walk(value):
            if isinstance(value, dict):
                self.assertFalse({'enemy_heat', 'enemy_lock', 'blocked_fire_ticks',
                                  'detected_shots', 'actual_hp_loss', 'attack_damage'} & set(value))
                for child in value.values(): walk(child)
            elif isinstance(value, list):
                for child in value: walk(child)
        walk(state)

    def test_explicit_configuration_variants_and_roundtrip(self):
        self.assertEqual(bridge.DEFAULT_CONFIG['heat_strategy'], ENABLED)
        self.assertEqual(bridge.DEFAULT_CONFIG['heat_threshold_policy'], THRESHOLDS[0])
        hashes = set()
        for heat in ('disabled', ENABLED):
            for threshold in THRESHOLDS:
                selected = {**bridge.DEFAULT_CONFIG, 'heat_strategy': heat,
                            'heat_threshold_policy': threshold}
                state = self.engine.reset({'observer': 0, 'config': selected})
                self.assert_public(state)
                self.assertEqual(state['observation']['self_heat']['enabled'], heat != 'disabled')
                hashes.add(self.engine.replay()['initial_hash'])
                self.assert_public(self.engine.step(request(61, fire=1)))
                saved = self.engine.replay()
                self.assert_public(self.engine.import_replay({'observer': 1, 'replay': saved}))
                self.assertEqual(saved, self.engine.replay())
        self.assertEqual(len(hashes), 6, 'each declared configuration must bind replay identity')

    def test_native_shots_not_fire_attempts_and_exact_tenths_oracle(self):
        # This independent oracle uses only ammo actually spent, not fire intent.
        heat = 0
        locked = False
        ammo = 120
        saw_lock = saw_below_limit_lock = saw_unlock = saw_cooldown_attempt = False
        max_heat = 0
        for tick in range(1, 271):
            fire = 1 if tick <= 65 or tick >= 260 else 0
            before_locked = locked
            if tick % 6 == 0:
                heat = max(0, heat - 12)
                if heat == 0: locked = False
            gate_locked = locked
            state = self.engine.step(request(fire=fire))
            self.assert_public(state)
            own = state['observation']['combat']['self']
            actual_shots = ammo - own['ammo']
            self.assertIn(actual_shots, (0, 1))
            if gate_locked:
                self.assertEqual(actual_shots, 0, 'heat gate must run before ammo/projectile emission')
            if fire and not gate_locked and actual_shots == 0:
                saw_cooldown_attempt = True
            heat += 100 * actual_shots
            if heat > 400: locked = True
            value = state['observation']['self_heat']
            self.assertEqual(value['heat_tenths'], heat)
            self.assertEqual(value['limit_tenths'], 400)
            self.assertEqual(value['cooling_per_second'], 12)
            self.assertEqual(value['overheat_locked'], locked)
            self.assertFalse(value['permanent_latched'])
            self.assertEqual(value['firing_locked'], locked)
            self.assertEqual(own['hp'], 100, 'overheat must not invent HP damage')
            saw_lock |= locked
            saw_below_limit_lock |= locked and 0 < heat <= 400
            saw_unlock |= before_locked and not locked
            max_heat = max(max_heat, heat)
            ammo = own['ammo']
        self.assertTrue(saw_lock and saw_below_limit_lock and saw_unlock and saw_cooldown_attempt)
        self.assertLessEqual(max_heat, 500)
        raw = self.engine._native.command('STATE 0')
        self.assertEqual(raw['damage_admin']['attack_damage'], [0, 0])
        self.assertEqual(raw['damage_admin']['actual_hp_loss'], [0, 0])
        other = self.engine.state(1)
        self.assert_public(other)
        self.assertEqual(other['observation']['self_heat']['heat_tenths'], 0)
        self.assertFalse(other['observation']['self_heat']['firing_locked'])
        saved = self.engine.replay()
        for frame in saved['frames']:
            expected_fire = int(frame['tick'] < 65 or frame['tick'] >= 259)
            self.assertEqual(frame['inputs'][0]['action'][-1], expected_fire,
                             'replay must preserve requested, not heat-gated actions')
        self.engine.import_replay({'observer': 0, 'replay': saved})
        self.assertEqual(saved, self.engine.replay())

    def test_strict_selection_and_full_replay_config_fail_atomically(self):
        self.engine.step(request(61, fire=1))
        saved = self.engine.replay()
        for key in ('heat_strategy', 'heat_threshold_policy'):
            configs = [{k:v for k,v in bridge.DEFAULT_CONFIG.items() if k != key}]
            configs += [{**bridge.DEFAULT_CONFIG, key: value}
                        for value in (None, False, True, 0, 1, 1.0, [], {}, '', 'official',
                                      bridge.DEFAULT_CONFIG[key]+' ')]
            for config in configs:
                with self.subTest(config=config):
                    with self.assertRaises((ValueError, RuntimeError, TypeError)):
                        self.engine.reset({'observer': 0, 'config': config})
                    self.assertEqual(saved, self.engine.replay())
        mutations = [
            lambda r: r['config'].__setitem__('heat_strategy', 'disabled'),
            lambda r: r['config'].__setitem__('heat_threshold_policy', THRESHOLDS[2]),
            lambda r: r['config'].__setitem__('heat_boundary_policy', 'official'),
            lambda r: r['config'].__setitem__('heat_limit_tenths', 999),
            lambda r: r.__setitem__('engine', 'integer-mm-tactical-v2'),
            lambda r: r.__setitem__('observation_profile', 'experimental-tactical-public-control-v1'),
            lambda r: r['frames'][-1].__setitem__('hash', r['frames'][-1]['hash'] ^ 1),
            lambda r: r['frames'][-1]['observation_hashes'].__setitem__(1, 0),
            lambda r: r.__setitem__('heat_admin', {}),
        ]
        for mutate in mutations:
            modified = copy.deepcopy(saved); mutate(modified)
            with self.assertRaises((ValueError, RuntimeError, TypeError)):
                self.engine.import_replay({'observer': 1, 'replay': modified})
            self.assertEqual(saved, self.engine.replay())

    def test_corrupted_heat_response_never_commits_candidate_or_leaks(self):
        self.engine.step(request(61, fire=1))
        saved = self.engine.replay()
        live = self.engine._native
        original = bridge._Native.command
        mutations = [
            lambda s: s['observation'].__setitem__('enemy_heat', 12),
            lambda s: s['observation']['self_heat'].__setitem__('heat_tenths', -1),
            lambda s: s['observation']['self_heat'].__setitem__('heat_tenths', True),
            lambda s: s['observation']['self_heat'].__setitem__('limit_tenths', 401),
            lambda s: s['observation']['self_heat'].__setitem__('cooling_per_second', 13),
            lambda s: s['observation']['self_heat'].__setitem__('overheat_locked', 1),
            lambda s: s['observation']['self_heat'].__setitem__('firing_locked', False),
            lambda s: s['heat_admin'].__setitem__('enemy_private_extra', 123),
            lambda s: s['heat_admin'].__setitem__('schema', True),
            lambda s: s['heat_admin'].__setitem__('module', 'unverified'),
            lambda s: s['heat_admin'].__setitem__('threshold_policy', THRESHOLDS[2]),
            lambda s: s['heat_admin']['robots'][0].__setitem__('heat_tenths', 368),
            lambda s: s['heat_admin']['robots'][0].__setitem__('detected_shots', 4),
            lambda s: s['heat_admin']['robots'][0].__setitem__('detected_shots', True),
            lambda s: s['heat_admin']['robots'][0].__setitem__('blocked_fire_ticks', 63),
            lambda s: s['observation']['self_heat'].__setitem__('enabled', False),
        ]
        for mutate in mutations:
            candidates = []
            def corrupt(instance, command):
                state = original(instance, command)
                if command.startswith('STEP '):
                    candidates.append(instance); mutate(state)
                return state
            with patch.object(bridge._Native, 'command', corrupt):
                with self.assertRaises((ValueError, RuntimeError, TypeError)):
                    self.engine.step(request(fire=1))
            self.assertIs(self.engine._native, live)
            self.assertTrue(all(c.proc.poll() is not None for c in candidates))
            self.assertEqual(saved, self.engine.replay())
        raw = self.engine._native.command('STATE 0')
        raw['new_private_admin'] = {'enemy_heat': 9000}
        self.assert_public(self.engine._public(raw))

    def test_heat_module_provenance_and_source_changes_are_bound(self):
        required = {'rules/rmuc2027_heat_v1/heat_rules.hpp',
                    'rules/rmuc2027_heat_v1/heat_rules.cpp',
                    'rules/rmuc2027_heat_v1/provenance.json'}
        self.assertTrue(required <= set(identity.SOURCES))
        original = Path.read_bytes
        baseline = identity.build_id()
        for relative in required:
            target = ROOT / relative
            def altered(path):
                data = original(path)
                return data+b'\nHEAT-INDEPENDENT-REVIEW' if path == target else data
            with patch.object(Path, 'read_bytes', altered):
                self.assertNotEqual(identity.build_id(), baseline)
        self.engine.step(request(3, fire=1))
        saved = self.engine.replay()
        with patch.object(bridge, 'build_id', return_value='f'*64):
            for operation in (lambda: self.engine.step(request()),
                              lambda: self.engine.reset({'observer':0, 'config':bridge.DEFAULT_CONFIG}),
                              lambda: self.engine.import_replay({'observer':0, 'replay':saved})):
                with self.assertRaisesRegex(ValueError, 'source changed'):
                    operation()
        self.assertEqual(saved, self.engine.replay())


if __name__ == '__main__':
    unittest.main(verbosity=2)
