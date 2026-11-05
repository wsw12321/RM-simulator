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
        kinds = {e['kind'] for e in first['events']}
        for k in ('match-start', 'death', 'revived', 'level-up', 'assembly', 'temporary-control', 'match-end'):
            self.assertIn(k, kinds)
        times = [e['t'] for e in first['events']]
        self.assertEqual(times, sorted(times))
        self.assertGreater(len(first['timeline']), 1)

    def test_frames_and_summary(self):
        config = copy.deepcopy(cli.DEFAULT)
        config['duration_s'] = 30
        result = cli.run(config, frames=30)
        self.assertEqual(result['result']['outcome'], 'ongoing')
        self.assertEqual(len(result['frames']), 60)  # 30 s * 60 Hz / 30
        self.assertEqual(len(result['frames'][0]['r']), 8)
        self.assertEqual(len(result['field']['zones']), 4)
        text = cli.summary(result)
        self.assertIn('非官方', text)
        self.assertIn('red-heavy', text)
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
        out = subprocess.run([str(cli.NATIVE), 'RUN', '0', '0', '1', 'x', 'y'], capture_output=True, text=True)
        self.assertEqual(out.returncode, 2)
        self.assertIn('error', json.loads(out.stdout))


if __name__ == '__main__':
    unittest.main(verbosity=1)
