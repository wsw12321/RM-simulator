#!/usr/bin/env python3
"""Python CLI contract for the integrated rules lab (strict config + determinism)."""
import copy
import json
import pathlib
import subprocess
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from match_lab import cli  # noqa: E402

CLI = [sys.executable, str(ROOT / 'match_lab' / 'cli.py')]


class MatchLabCli(unittest.TestCase):
    def test_example_round_trips_and_validates(self):
        out = subprocess.run(CLI + ['example'], capture_output=True, text=True, check=True).stdout
        self.assertEqual(json.loads(out), cli.DEFAULT)
        cli.validate(json.loads(out))

    def test_strict_validation(self):
        bad = []
        c = copy.deepcopy(cli.DEFAULT); c['extra'] = 1; bad.append(c)
        c = copy.deepcopy(cli.DEFAULT); c['duration_s'] = True; bad.append(c)
        c = copy.deepcopy(cli.DEFAULT); c['duration_s'] = 421; bad.append(c)
        c = copy.deepcopy(cli.DEFAULT); c['credits'] = 1; bad.append(c)
        c = copy.deepcopy(cli.DEFAULT); c['red']['infantry'].pop(); bad.append(c)
        c = copy.deepcopy(cli.DEFAULT); c['red']['infantry'][0]['chassis'] = 'speed'; bad.append(c)
        c = copy.deepcopy(cli.DEFAULT); c['blue']['sentry'] = 'manual'; bad.append(c)
        c = copy.deepcopy(cli.DEFAULT); c['blue']['max_assembly'] = 4; bad.append(c)
        c = copy.deepcopy(cli.DEFAULT); c['blue']['gold_reserve'] = 2.5; bad.append(c)
        c = copy.deepcopy(cli.DEFAULT); del c['seed']; bad.append(c)
        c = copy.deepcopy(cli.DEFAULT); c['seed'] = -1; bad.append(c)
        c = copy.deepcopy(cli.DEFAULT); c['seed'] = cli.MAX_SEED + 1; bad.append(c)
        c = copy.deepcopy(cli.DEFAULT); c['red']['energy_accuracy'] = 101; bad.append(c)
        c = copy.deepcopy(cli.DEFAULT); c['red']['dart_target'] = 'base'; bad.append(c)
        c = copy.deepcopy(cli.DEFAULT); c['blue']['dart_accuracy'] = '50'; bad.append(c)
        c = copy.deepcopy(cli.DEFAULT); c['blue']['radar_accuracy'] = False; bad.append(c)
        c = copy.deepcopy(cli.DEFAULT); del c['blue']['radar_accuracy']; bad.append(c)
        for config in bad:
            with self.assertRaises(cli.LabError):
                cli.validate(config)
        with self.assertRaises(cli.LabError):
            json.loads('{"a":1,"a":2}', object_pairs_hook=cli._no_duplicates)

    def test_full_match_is_deterministic_and_complete(self):
        first = cli.run(cli.DEFAULT)
        second = cli.run(copy.deepcopy(cli.DEFAULT))
        self.assertEqual(first['hash'], second['hash'])
        self.assertEqual(first, second)
        self.assertEqual(first['build'], cli.build_id())
        self.assertIs(first['official'], False)
        self.assertIn(first['result']['outcome'], ('red-wins', 'blue-wins', 'draw'))
        self.assertEqual(len(first['robots']), 8)
        roles = sorted(r['role'] for r in first['robots'])
        self.assertEqual(roles, sorted(['heavy', 'infantry', 'infantry', 'sentry'] * 2))
        for t in first['teams']:
            self.assertEqual(t['gold'], t['gold_income'] - t['gold_spent'])
            self.assertLessEqual(t['purchased_17mm'], 1000)
        self.assertEqual(first['schema'], 'rmuc2027-rules-lab-result.v2')
        self.assertEqual(first['seed'], cli.DEFAULT['seed'])
        self.assertEqual(len(first['modules']), 12)
        kinds = {e['kind'] for e in first['events']}
        for k in ('match-start', 'death', 'revived', 'level-up', 'assembly', 'temporary-control', 'match-end',
                  'energy-trigger', 'energy-activated', 'dart-launch', 'dart-hit', 'radar-marked', 'radar-double'):
            self.assertIn(k, kinds)
        for t in first['teams']:
            self.assertLessEqual(t['dart']['hits'], t['dart']['launched'])
            self.assertLessEqual(t['dart']['launched'], 4)
            self.assertLessEqual(t['radar']['double_triggers'], 2)
            self.assertGreaterEqual(t['energy']['small_activations'] + t['energy']['big_activations'], 1)
        for line in first['timeline']:
            self.assertEqual(len(line['energy']), 2)
            self.assertEqual(len(line['dart']), 2)
            self.assertTrue(all(len(row) == 5 and row[4] in (0, 15, 20, 30, 40) for row in line['robots']))
        times = [e['t'] for e in first['events']]
        self.assertEqual(times, sorted(times))
        self.assertGreater(len(first['timeline']), 1)

    def test_seed_and_switched_off_systems(self):
        config = copy.deepcopy(cli.DEFAULT)
        config['duration_s'] = 120
        base = cli.run(config)
        other = cli.run({**config, 'seed': config['seed'] + 1})
        self.assertNotEqual(base['hash'], other['hash'])
        self.assertEqual(other['seed'], config['seed'] + 1)
        for side in ('red', 'blue'):
            config[side].update(energy_accuracy=0, dart_target='none', dart_accuracy=0, radar_accuracy=0)
        off = cli.run(config)
        kinds = {e['kind'] for e in off['events']}
        self.assertFalse(kinds & {'energy-trigger', 'energy-activated', 'dart-gate', 'dart-launch', 'dart-hit', 'radar-marked', 'radar-double'})
        for t in off['teams']:
            self.assertEqual(t['dart'], {'target': 'none', 'openings': 0, 'launched': 0, 'hits': 0, 'damage': 0})
            self.assertEqual(t['energy']['attempts'], 0)
        self.assertTrue(all(r['radar_progress'] == 0 for r in off['robots']))

    def test_sweep(self):
        config = copy.deepcopy(cli.DEFAULT)
        config['duration_s'] = 60
        result = cli.sweep(config, 3)
        self.assertEqual(result['schema'], 'rmuc2027-rules-lab-sweep.v1')
        self.assertIs(result['official'], False)
        self.assertEqual([m['seed'] for m in result['matches']], [2027, 2028, 2029])
        self.assertEqual(sum(result['outcomes'].values()), 3)
        self.assertEqual(sum(result['reasons'].values()), 3)
        self.assertEqual(result['matches'][0]['hash'], cli.run(config)['hash'])  # the first match is the plain run
        self.assertEqual(result, cli.sweep(copy.deepcopy(config), 3))
        self.assertIn('不代表真实比赛胜率', cli.sweep_summary(result))
        for seeds in (1, cli.MAX_SWEEP + 1, True, 2.0):
            with self.assertRaises(cli.LabError):
                cli.sweep(config, seeds)
        with self.assertRaises(cli.LabError):
            cli.sweep({**config, 'seed': cli.MAX_SEED}, 2)
        out = subprocess.run(CLI + ['sweep', '--seeds', '2', '--seed', '5'], capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stderr)
        self.assertIn('seed 5–6', out.stdout)
        out = subprocess.run(CLI + ['sweep', '--seeds', '1'], capture_output=True, text=True)
        self.assertEqual(out.returncode, 2)

    def test_frames_and_summary(self):
        config = copy.deepcopy(cli.DEFAULT)
        config['duration_s'] = 30
        result = cli.run(config, frames=30)
        self.assertEqual(result['result']['outcome'], 'ongoing')
        self.assertEqual(len(result['frames']), 60)  # 30 s * 60 Hz / 30
        self.assertEqual(len(result['frames'][0]['r']), 8)
        self.assertEqual(len(result['field']['zones']), 4)
        self.assertEqual(len(result['field']['energy']), 2)
        text = cli.summary(result)
        self.assertIn('非官方', text)
        self.assertIn('red-heavy', text)
        self.assertIn('能量机关', text)
        self.assertIn(f"seed {config['seed']}", text)
        path = ROOT / 'build' / 'match_lab_test_config.json'
        path.write_text(json.dumps(config), encoding='utf-8')
        out = subprocess.run(CLI + ['run', str(path)], capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stderr)
        self.assertIn('结果', out.stdout)
        path.write_text('{"duration_s": 30, "duration_s": 31}', encoding='utf-8')
        out = subprocess.run(CLI + ['run', str(path)], capture_output=True, text=True)
        self.assertEqual(out.returncode, 2)
        self.assertIn('duplicate key', out.stderr)

    def test_native_rejects_bad_arguments(self):
        good = cli.spec(cli.DEFAULT['red'])
        for args in (['RUN', '0', '0', '1', '1', 'x', 'y'], ['RUN', '60', '0', '1', good, good],  # missing seed
                     ['RUN', '60', '0', '1', '1', good, good.replace('outpost', 'moon')],
                     ['RUN', '60', '0', '1', '1', good, good.rsplit(',', 1)[0] + ',101'],
                     ['RUN', '60', '0', '1', '1000000000', good, good]):
            out = subprocess.run([str(cli.NATIVE), *args], capture_output=True, text=True)
            self.assertEqual(out.returncode, 2, args)
            self.assertIn('error', json.loads(out.stdout))


if __name__ == '__main__':
    unittest.main(verbosity=1)
