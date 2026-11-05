#!/usr/bin/env python3
"""Independent adversarial review of roster bridge; not browser rendering QA."""
import copy
import os
import json
import subprocess
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from roster_mode import bridge


def idle(robot_id, **changes):
    return {'id': robot_id, 'kind': 'manual', 'action': {
        'sdk_version': 1, 'dx': 0, 'dy': 0, 'aim_x': 6000,
        'aim_y': 3800, 'fire': 0, **changes}}


def request(engine, count=1, observer='red-1', sources=None):
    robots = engine.state(observer)['config']['robots']
    return {'count': count, 'observer': observer,
            'controllers': sources if sources is not None else [idle(r['id']) for r in robots]}


class IndependentRosterBridgeReview(unittest.TestCase):
    def setUp(self):
        self.engine = bridge.RosterProcess(os.environ.get('ROSTER_REVIEW_BINARY'))
        self.addCleanup(self.engine.close)

    def assert_filtered(self, value):
        expected = {'build', 'schema', 'profile', 'observation_profile',
                    'observation_version', 'official', 'observer', 'tick',
                    'terminal', 'config', 'result', 'observation', 'last_sources'}
        self.assertEqual(set(value) - {'advanced', 'verified_frames'}, expected)
        obs = value['observation']
        self.assertEqual(set(obs), {'sdk_version', 'observation_version', 'profile',
            'hz', 'tick', 'self_id', 'self_team', 'sensor_range_mm', 'self', 'arena',
            'roster', 'contacts', 'projectiles', 'self_heat', 'zones', 'score', 'rate',
            'qualified_ms', 'duration_ms', 'credits_enabled'})
        self.assertEqual(obs['self_id'], value['observer'])
        self.assertEqual(obs['tick'], value['tick'])
        self.assertEqual(obs['roster'], value['config']['robots'])
        ids = [v['id'] for v in obs['contacts']]
        self.assertEqual(ids, sorted(set(ids)))
        self.assertNotIn(value['observer'], ids)
        for contact in obs['contacts']:
            self.assertEqual(set(contact), {'id', 'team', 'x', 'y', 'yaw'})
        points = []
        for projectile in obs['projectiles']:
            self.assertEqual(set(projectile), {'x', 'y', 'vx', 'vy'})
            points.append(tuple(projectile[k] for k in ('x', 'y', 'vx', 'vy')))
        self.assertEqual(points, sorted(points))
        for zone in obs['zones']:
            self.assertEqual(set(zone), {'id', 'rect', 'original_owner', 'controller', 'degree'})

    def test_default_roster_and_every_observer_are_filtered(self):
        original = self.engine.state('red-1')
        robots = original['config']['robots']
        self.assertEqual([r['id'] for r in robots], ['blue-1', 'blue-2', 'red-1', 'red-2'])
        self.assertEqual([sum(r['team'] == team for r in robots) for team in (0, 1)], [2, 2])
        for robot in robots:
            value = self.engine.state(robot['id'])
            self.assert_filtered(value)
            self.assertEqual(value['observation']['self_team'], robot['team'])
            self.assertEqual(value['observation']['self']['hp'], robot['profile']['max_hp'])
        value = self.engine.step(request(self.engine, 11))
        self.assert_filtered(value)
        saved = self.engine.replay()
        self.assert_filtered(self.engine.import_replay({'observer': 'blue-2', 'replay': saved}))
        self.assertEqual(self.engine.replay(), saved)

    def test_roster_and_input_permutation_do_not_change_identity(self):
        config = copy.deepcopy(bridge.DEFAULT_CONFIG)
        original = self.engine.reset({'observer': 'red-2', 'config': config})
        sources = [idle(r['id'], dx=1000 if r['team'] == 0 else -1000,
                        dy=300 if r['id'].endswith('1') else -300)
                   for r in original['config']['robots']]
        self.engine.step(request(self.engine, 9, 'red-2', sources))
        reference = self.engine.replay()
        config['robots'].reverse()
        self.engine.reset({'observer': 'red-2', 'config': config})
        self.engine.step(request(self.engine, 9, 'red-2', list(reversed(sources))))
        self.assertEqual(self.engine.replay(), reference)
        permuted = copy.deepcopy(reference)
        permuted['config']['robots'].reverse()
        for frame in permuted['frames']:
            frame['inputs'].reverse()
        self.engine.import_replay({'observer': 'blue-1', 'replay': permuted})
        self.assertEqual(self.engine.replay(), reference)

    def test_cardinality_limits_and_disabled_capture_profiles(self):
        template = copy.deepcopy(bridge.DEFAULT_CONFIG['robots'][0])
        for red in range(1, 5):
            for blue in range(1, 5):
                config = copy.deepcopy(bridge.DEFAULT_CONFIG)
                config['robots'] = []
                for team, count in ((0, red), (1, blue)):
                    for index in range(count):
                        robot = copy.deepcopy(template)
                        robot.update(id=f'{"red" if team == 0 else "blue"}-{index+1}',
                                     team=team, eligible=False,
                                     spawn={'x': 1600 if team == 0 else 10400,
                                            'y': (1000, 2500, 4000, 7000)[index],
                                            'yaw': 0 if team == 0 else 128})
                        robot['profile']['ammo'] = 0
                        config['robots'].append(robot)
                value = self.engine.reset({'observer': 'red-1', 'config': config})
                self.assertEqual(len(value['config']['robots']), red + blue)
                self.assert_filtered(self.engine.step(request(self.engine, 2)))
                self.assertEqual(self.engine.state('red-1')['observation']['score'], [0, 0])
        before = self.engine.replay()
        for robots in ([], [template], [template] * 9):
            with self.assertRaises((ValueError, RuntimeError)):
                self.engine.reset({'observer': 'red-1', 'config': {**config, 'robots': robots}})
            self.assertEqual(self.engine.replay(), before)

    def test_invalid_input_and_profile_boundaries_are_atomic(self):
        self.engine.step(request(self.engine, 3))
        before = self.engine.replay()
        data = request(self.engine)
        invalid = []
        for key, low, high in [('sdk_version', 1, 1), ('dx', -1000, 1000),
                ('dy', -1000, 1000), ('aim_x', 0, 12000), ('aim_y', 0, 7600), ('fire', 0, 1)]:
            for value in (True, 1.0, None, '1', low-1, high+1):
                changed = copy.deepcopy(data)
                changed['controllers'][0]['action'][key] = value
                invalid.append(changed)
        for controllers in ([], data['controllers'][:-1], data['controllers'] + [data['controllers'][0]]):
            invalid.append({**data, 'controllers': controllers})
        duplicate = copy.deepcopy(data)
        duplicate['controllers'][0]['id'] = duplicate['controllers'][1]['id']
        invalid.append(duplicate)
        for observer in ('unknown', '', 0, True, None):
            invalid.append({**data, 'observer': observer})
        for changed in invalid:
            with self.subTest(changed=changed):
                with self.assertRaises((ValueError, RuntimeError)):
                    self.engine.step(changed)
                self.assertEqual(self.engine.replay(), before)
        bounds = [('max_hp', 1, 10000), ('ammo', 0, 5000), ('radius_mm', 100, 500),
            ('speed_mm_tick', 0, 100), ('projectile_speed_mm_tick', 80, 1000),
            ('projectile_damage', 1, 1000), ('cooldown_ticks', 1, 120),
            ('sensor_range_mm', 0, 20000), ('heat_limit_tenths', 0, 20000),
            ('cooling_per_second', 0, 1000)]
        for key, low, high in bounds:
            for value in (True, 1.0, low-1, high+1, 2**63-1):
                config = copy.deepcopy(bridge.DEFAULT_CONFIG)
                config['robots'][0]['profile'][key] = value
                with self.subTest(key=key, value=value):
                    with self.assertRaises((ValueError, RuntimeError)):
                        self.engine.reset({'observer': 'red-1', 'config': config})
                    self.assertEqual(self.engine.replay(), before)
        config = copy.deepcopy(bridge.DEFAULT_CONFIG)
        # Same source/profile identity must not stand for conflicting assumptions.
        config['robots'][1]['profile'] = copy.deepcopy(config['robots'][0]['profile'])
        config['robots'][1]['profile']['max_hp'] += 1
        with self.assertRaises((ValueError, RuntimeError)):
            self.engine.reset({'observer': 'red-1', 'config': config})
        self.assertEqual(self.engine.replay(), before)

    def test_rejected_native_response_does_not_commit_candidate(self):
        self.engine.step(request(self.engine, 5))
        before = self.engine.replay()
        live = self.engine._native
        data = request(self.engine)
        command = bridge._Native.command
        changes = [
            lambda v: v.__setitem__('build', '0'*64),
            lambda v: v.__setitem__('private_debug', {'hp': 100}),
            lambda v: v['observation'].__setitem__('capture_progress', [1, 2]),
            lambda v: v['observation'].__setitem__('enemy_heat', 10),
            lambda v: v['observation']['self'].__setitem__('shots', 0),
            lambda v: v['observation']['self'].__setitem__('x', 0),
            lambda v: v['observation']['contacts'][0].update(x=7000, y=2900),
            lambda v: v['observation']['projectiles'].append({'x':7000,'y':2900,'vx':1,'vy':0}),
            lambda v: v['observation']['contacts'][0].__setitem__('hp', 100),
            lambda v: v['observation']['contacts'].append(v['observation']['contacts'][0]),
            lambda v: v['observation']['zones'][0].__setitem__('detected', True),
            lambda v: v['observation']['roster'][0]['spawn'].__setitem__('x', 1000),
            lambda v: v['observation'].__setitem__('self_id', 'blue-1'),
            lambda v: v['frames'][-1]['inputs'][0].__setitem__('id', 'unknown'),
            lambda v: v['frames'][-1]['inputs'][0].__setitem__('controller_id', 'wrong-source'),
            lambda v: v['last_sources'][0].__setitem__('id', 'unknown'),
            lambda v: v['heat_admin']['robots'][0].__setitem__('id', 'unknown'),
            lambda v: v['damage_admin'].__setitem__('remaining_robot_hp', [0, 0]),
            lambda v: v.__setitem__('tick', v['tick']-1),
            lambda v: v.__setitem__('terminal', True),
        ]
        for change in changes:
            candidates = []
            def corrupt(native, value):
                result = command(native, value)
                if value.startswith('STEP '):
                    candidates.append(native)
                    change(result)
                return result
            with self.subTest(change=change):
                with patch.object(bridge._Native, 'command', corrupt):
                    with self.assertRaises((ValueError, RuntimeError)):
                        self.engine.step(data)
                self.assertTrue(candidates)
                self.assertTrue(all(candidate is not live for candidate in candidates))
                self.assertIs(self.engine._native, live)
                self.assertEqual(self.engine.replay(), before)
        private = self.engine._native.command('STATE red-1')
        private['private_debug'] = {'hp': 100}
        self.assert_filtered(self.engine._public(private))

    def test_late_replay_tampering_and_source_changes_are_atomic(self):
        self.engine.step(request(self.engine, 600))
        self.engine.step(request(self.engine, 13))
        before = self.engine.replay()
        changes = [
            lambda v: v.__setitem__('build', '0'*64),
            lambda v: v.__setitem__('engine', 'integer-mm-roster-unknown'),
            lambda v: v.__setitem__('initial_hash', v['initial_hash'] ^ 1),
            lambda v: v.__setitem__('final_hash', v['final_hash'] ^ 1),
            lambda v: v['frames'][-1].__setitem__('hash', v['frames'][-1]['hash'] ^ 1),
            lambda v: v['frames'][-1]['inputs'][0].__setitem__('observation_hash', 0),
            lambda v: v['frames'][-1]['inputs'][0].__setitem__('id', 'red-2'),
            lambda v: v['frames'][-1]['inputs'][0]['action'].__setitem__(4, True),
            lambda v: v['frames'][-1]['inputs'].pop(),
            lambda v: v['config']['robots'][0]['profile'].__setitem__('max_hp', 101),
            lambda v: v['config']['robots'][0]['spawn'].__setitem__('x', 10000),
            lambda v: v['config']['robots'][0].__setitem__('team', 0),
            lambda v: v['config'].__setitem__('heat_strategy', 'disabled'),
            lambda v: v.__setitem__('damage_admin', {}),
        ]
        for change in changes:
            changed = copy.deepcopy(before)
            change(changed)
            with self.subTest(change=change):
                with self.assertRaises((ValueError, RuntimeError)):
                    self.engine.import_replay({'observer': 'red-1', 'replay': changed})
                self.assertEqual(self.engine.replay(), before)
        data = request(self.engine)
        with patch.object(bridge, 'build_id', lambda: '0'*64):
            with self.assertRaises((ValueError, RuntimeError)):
                self.engine.step(data)
        self.assertEqual(self.engine.replay(), before)

    def test_tick_zero_native_corruption_rejects_candidates_atomically(self):
        empty = self.engine.replay()
        self.engine.step(request(self.engine, 7))
        before = self.engine.replay()
        live = self.engine._native
        command = bridge._Native.command
        data = request(self.engine)
        def corrupt_initial_accounting(value):
            # Internally consistent bounded numbers still cannot be tick zero.
            value['observation']['self']['hp'] = 99
            value['damage_admin'].update(remaining_robot_hp=[179,180],
                actual_hp_loss=[1,0],attack_damage=[0,1],
                nominal_projectile_damage=[0,10],last_sequence=1)
        changes = [
            lambda value:value['observation']['self'].__setitem__('x',value['observation']['self']['x']+1),
            lambda value:value['observation']['self'].__setitem__('yaw',1),
            lambda value:value['observation'].__setitem__('contacts',[]),
            lambda value:value['observation']['contacts'][0].__setitem__('yaw',1),
            lambda value:value['observation']['projectiles'].append({'x':1600,'y':2900,'vx':0,'vy':1}),
            lambda value:value['observation']['zones'][0].__setitem__('degree','temporary'),
            corrupt_initial_accounting,
        ]
        operations = {
            'reset':lambda:self.engine.reset({'observer':'red-1','config':copy.deepcopy(bridge.DEFAULT_CONFIG)}),
            'step':lambda:self.engine.step(data),
            'empty_import':lambda:self.engine.import_replay({'observer':'red-1','replay':empty}),
            'history_import':lambda:self.engine.import_replay({'observer':'red-1','replay':before}),
        }
        for change in changes:
            for name, operation in operations.items():
                candidates = []
                def corrupt(native, value):
                    result = command(native, value)
                    if value.startswith('RESET '):
                        candidates.append(native)
                        change(result)
                    return result
                with self.subTest(change=change, operation=name):
                    with patch.object(bridge._Native, 'command', corrupt):
                        with self.assertRaises((ValueError, RuntimeError)):
                            operation()
                    self.assertTrue(candidates)
                    self.assertTrue(all(candidate is not live and candidate.proc.poll() is not None
                                        for candidate in candidates))
                    self.assertIs(self.engine._native, live)
                    self.assertEqual(self.engine.replay(), before)
        candidates = []
        def shift_pose(native, value):
            result = command(native, value)
            if result.get('tick') == 0 and value.startswith(('RESET ', 'STATE ')):
                candidates.append(native)
                result['observation']['self']['x'] += 1
            return result
        with patch.object(bridge._Native, 'command', shift_pose):
            with self.assertRaises((ValueError, RuntimeError)):
                bridge.RosterProcess(os.environ.get('ROSTER_REVIEW_BINARY'))
        self.assertTrue(candidates)
        self.assertTrue(all(candidate.proc.poll() is not None for candidate in candidates))
        initial = bridge.RosterProcess(os.environ.get('ROSTER_REVIEW_BINARY'))
        self.addCleanup(initial.close)
        with patch.object(bridge._Native, 'command', shift_pose):
            with self.assertRaises((ValueError, RuntimeError)):
                initial.state('red-1')
        self.assertEqual(initial.replay(), empty)
        self.assertEqual(self.engine.replay(), before)

    def test_empty_frame_native_state_still_binds_history_tail(self):
        self.engine.step(request(self.engine, 7))
        before = self.engine.replay()
        live = self.engine._native
        command = bridge._Native.command
        for target in ('live', 'restored'):
            def corrupt(native, value):
                result = command(native, value)
                applies = native is live if target == 'live' else native is not live
                if applies and value.startswith('STATE ') and result['tick'] == 7:
                    self.assertEqual(result['frames'], [])
                    result['hash'] ^= 1
                return result
            with self.subTest(target=target):
                with patch.object(bridge._Native, 'command', corrupt):
                    with self.assertRaises((ValueError, RuntimeError)):
                        if target == 'live':
                            self.engine.state('red-1')
                        else:
                            self.engine.import_replay({'observer':'red-1','replay':before})
                self.assertIs(self.engine._native, live)
                self.assertEqual(self.engine.replay(), before)

    def test_native_parser_rejects_malformed_commands_without_mutation(self):
        binary = os.environ.get('ROSTER_REVIEW_BINARY') or str(ROOT/'build/rmuc_roster')
        config = bridge.validate_config(bridge.DEFAULT_CONFIG)
        reset = bridge.reset_tokens('red-1', config)
        robot_ids = [robot['id'] for robot in config['robots']]
        sources = ' '.join(rid+' manual 1 0 0 6000 3800 0' for rid in robot_ids)
        valid_step = f'STEP 3 red-1 {len(robot_ids)} '+sources
        invalid = [
            'STATE unknown', 'STATE red-1 trailing', 'BUILD trailing',
            reset.replace('RESET red-1 ', 'RESET unknown ', 1),
            reset+' trailing', reset.replace(' 100 120 ', ' 9223372036854775808 120 ', 1),
            'STEP true red-1 4 '+sources, 'STEP 1.0 red-1 4 '+sources,
            'STEP +1 red-1 4 '+sources, 'STEP 601 red-1 4 '+sources,
            'STEP 1 unknown 4 '+sources, 'STEP 1 red-1 0',
            'STEP 1 red-1 4 '+sources.replace(robot_ids[0],robot_ids[1],1),
            'FRAMES 601 red-1 4', 'FRAMES 1 red-1 4 0 malformed',
        ]
        commands = [valid_step, 'STATE red-1']
        for command in invalid:
            commands += [command, 'STATE red-1']
        result = subprocess.run([binary],input='\n'.join(commands)+'\n',text=True,
                                capture_output=True,timeout=20,check=True)
        values = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(values),len(commands))
        baseline = values[1]
        self.assertEqual(baseline['tick'],3)
        for index, command in enumerate(invalid):
            with self.subTest(command=command):
                self.assertEqual(set(values[2+index*2]),{'error'})
                self.assertEqual(values[3+index*2],baseline)

    def test_import_is_callback_free_and_failed_restore_keeps_live_process(self):
        metadata = self.engine.meta()
        policy_id = metadata['policy']['controller_id']
        robots = self.engine.state('red-1')['config']['robots']
        sources = [{'id': r['id'], 'kind': 'policy', 'sdk_version': 1,
                    'controller_id': policy_id} if index % 2 else idle(r['id'])
                   for index, r in enumerate(robots)]
        self.engine.step(request(self.engine, 9, sources=sources))
        saved = self.engine.replay()
        command = bridge._Native.command
        seen = []
        def record(native, value):
            seen.append(value.split()[0])
            return command(native, value)
        with patch.object(bridge._Native, 'command', record):
            self.engine.import_replay({'observer': 'red-2', 'replay': saved})
        self.assertIn('FRAMES', seen)
        self.assertNotIn('STEP', seen)
        self.assertNotIn('POLICY_META', seen)
        live = self.engine._native
        for target in ('FRAMES ', 'STEP ', 'POLICY_META'):
            failed = []
            def fail(native, value):
                if value.startswith(target):
                    failed.append(native)
                    raise RuntimeError('Injected roster candidate failure')
                return command(native, value)
            with patch.object(bridge._Native, 'command', fail):
                with self.assertRaisesRegex(RuntimeError, 'candidate failure'):
                    self.engine.step(request(self.engine, 2, sources=sources))
            self.assertTrue(failed)
            self.assertTrue(all(candidate is not live for candidate in failed))
            self.assertIs(self.engine._native, live)
            self.assertEqual(self.engine.replay(), saved)
        self.assert_filtered(self.engine.step(request(self.engine)))


if __name__ == '__main__':
    unittest.main()
