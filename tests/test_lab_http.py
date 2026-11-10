#!/usr/bin/env python3
"""Local bridge contract for the stateless rules lab endpoints."""
import copy
import json
import sys
import unittest
import urllib.error
import urllib.request

PORT = int(sys.argv.pop(1)) if len(sys.argv) > 1 else 18765
BASE = f'http://127.0.0.1:{PORT}'


def call(path, body=None, headers=None):
    data = None if body is None else (body if isinstance(body, bytes) else json.dumps(body).encode())
    req = urllib.request.Request(BASE + path, data=data, headers={'X-Sandbox-Client': '1', 'Content-Type': 'application/json', **(headers or {})})
    try:
        with urllib.request.urlopen(req, timeout=120) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read())


class LabHttp(unittest.TestCase):
    def test_example_and_run(self):
        status, example = call('/api/lab/example')
        self.assertEqual(status, 200)
        config = copy.deepcopy(example)
        config['duration_s'] = 60
        status, result = call('/api/lab/run', {'config': config, 'frame_every': 12})
        self.assertEqual(status, 200, result)
        self.assertEqual(result['schema'], 'rmuc2027-rules-lab-result.v2')
        self.assertEqual(result['seed'], config['seed'])
        self.assertEqual(len(result['field']['energy']), 2)
        self.assertIs(result['official'], False)
        self.assertEqual(result['ticks'], 3600)
        self.assertEqual(len(result['frames']), 300)
        self.assertEqual(result['config'], config)
        status, again = call('/api/lab/run', {'config': config, 'frame_every': 12})
        self.assertEqual(again['hash'], result['hash'])  # stateless and deterministic

    def test_sweep(self):
        _, example = call('/api/lab/example')
        config = copy.deepcopy(example)
        config['duration_s'] = 30
        status, result = call('/api/lab/sweep', {'config': config, 'seeds': 3})
        self.assertEqual(status, 200, result)
        self.assertEqual(result['schema'], 'rmuc2027-rules-lab-sweep.v1')
        self.assertIs(result['official'], False)
        self.assertEqual(result['seeds'], 3)
        self.assertEqual(len(result['matches']), 3)
        self.assertEqual(sum(result['outcomes'].values()), 3)
        self.assertNotIn('frames', result)
        status, again = call('/api/lab/sweep', {'config': config, 'seeds': 3})
        self.assertEqual(again, result)
        bad = copy.deepcopy(config)
        bad['blue']['dart_target'] = 'radar'
        for body in ({'config': config, 'seeds': 1}, {'config': config, 'seeds': 51}, {'config': config, 'seeds': True},
                     {'config': config}, {'config': config, 'seeds': 3, 'frame_every': 0}, {'config': bad, 'seeds': 3}):
            status, result = call('/api/lab/sweep', body)
            self.assertEqual(status, 400, body)
            self.assertIn('error', result)

    def test_rejections(self):
        _, example = call('/api/lab/example')
        bad = copy.deepcopy(example)
        bad['red']['sentry'] = 'remote'
        for body, code in ((
                {'config': bad, 'frame_every': 0}, 400), ({'config': example}, 400),
                ({'config': example, 'frame_every': 601}, 400), ({'config': example, 'frame_every': True}, 400)):
            status, result = call('/api/lab/run', body)
            self.assertEqual(status, code, body)
            self.assertIn('error', result)
        status, _ = call('/api/lab/run', {'config': example, 'frame_every': 0}, {'X-Sandbox-Client': '0'})
        self.assertEqual(status, 403)
        status, _ = call('/api/lab/other', {'a': 1})
        self.assertEqual(status, 404)
        status, result = call('/api/lab/run', b'{"config":{},"config":{}}')
        self.assertEqual(status, 400)
        status, _ = call('/api/lab/run', b'x' * 20001)
        self.assertEqual(status, 400)


if __name__ == '__main__':
    unittest.main(verbosity=1)
