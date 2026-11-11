#!/usr/bin/env python3
"""Local HTTP contract of the official-field endpoints (/api/field/*)."""
import hashlib
import json
import sys
import unittest
import urllib.error
import urllib.request

PORT = int(sys.argv.pop(1)) if len(sys.argv) > 1 else 18765
BASE = f'http://127.0.0.1:{PORT}'
HEADERS = {'X-Sandbox-Client': '1', 'Content-Type': 'application/json'}


def call(path, body=None, headers=None, raw=False):
    data = None if body is None else (body if isinstance(body, bytes) else json.dumps(body).encode())
    req = urllib.request.Request(BASE + path, data=data, headers=HEADERS if headers is None else headers)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            payload = r.read()
            return r.status, (payload if raw else json.loads(payload))
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read())


class FieldHttp(unittest.TestCase):
    def setUp(self):
        status, meta = call('/api/field/meta')
        self.assertEqual(status, 200, meta)
        self.meta = meta
        status, out = call('/api/field/reset', meta['default_session'])
        self.assertEqual(status, 200, out)

    def test_meta_terrain_and_pages(self):
        self.assertEqual(self.meta['profile'], 'rmuc2027-field-sim-v1')
        self.assertEqual(self.meta['field']['size_mm'], [28000, 15000])
        status, raw = call('/api/field/terrain', raw=True)
        self.assertEqual(status, 200)
        self.assertEqual(hashlib.sha256(raw).hexdigest(), self.meta['field']['terrain']['sha256'])
        for page in ('/field.html', '/field.js', '/field.css'):
            status, body = call(page, raw=True)
            self.assertEqual(status, 200)
            self.assertGreater(len(body), 500)
        status, index = call('/', raw=True)
        self.assertIn(b'field.html', index)

    def test_step_state_and_probe(self):
        status, out = call('/api/field/step', {'ticks': 100, 'commands': {'B1': {'chassis': {'vx': 2.0}}}})
        self.assertEqual(status, 200, out)
        b1 = next(r for r in out['state']['robots'] if r['id'] == 'B1')
        self.assertLess(b1['x'], 24.902 - 0.8)                   # blue faces red: forward is -x
        status, state = call('/api/field/state')
        self.assertEqual(state, out['state'])
        status, probe = call('/api/field/probe', {'x': 5.0, 'y': 13.0})
        self.assertEqual(status, 200)
        self.assertAlmostEqual(probe['ground'] - probe['floor'], 0.2, delta=0.003)   # trapezoid highland, §4.3.1

    def test_reset_with_custom_session(self):
        session = {'robots': [{'id': 'solo', 'team': 'blue'}]}
        status, out = call('/api/field/reset', session)
        self.assertEqual(status, 200, out)
        self.assertEqual(out['session'], session)
        self.assertEqual([r['id'] for r in out['state']['robots']], ['solo'])
        self.assertEqual(out['state']['tick'], 0)

    def test_rejections(self):
        before = call('/api/field/state')[1]
        for path, body in [('/api/field/step', {'ticks': 1}), ('/api/field/step', {'ticks': 0, 'commands': {}}),
                           ('/api/field/step', {'ticks': 1, 'commands': {'ghost': {}}}),
                           ('/api/field/step', {'ticks': 1, 'commands': {'R1': {'fire': True}}}),
                           ('/api/field/reset', {'robots': [{'id': 'x', 'team': 'green'}]}),
                           ('/api/field/probe', {'x': 1}), ('/api/field/probe', {'x': 'a', 'y': 1})]:
            status, out = call(path, body)
            self.assertEqual(status, 400, (path, body, out))
            self.assertIn('error', out)
        self.assertEqual(call('/api/field/state')[1], before)
        self.assertEqual(call('/api/field/unknown', {})[0], 404)
        self.assertEqual(call('/api/field/unknown')[0], 404)
        self.assertEqual(call('/api/field/step', b'{"ticks":1,"ticks":2,"commands":{}}')[0], 400)   # duplicate key
        self.assertEqual(call('/api/field/step', b'x' * 300000)[0], 400)                              # oversized body
        # mutating endpoints need the local client header and a loopback Host
        self.assertEqual(call('/api/field/step', {'ticks': 1, 'commands': {}}, headers={'Content-Type': 'application/json'})[0], 403)
        self.assertEqual(call('/api/field/meta', headers={'Host': 'example.com'})[0], 403)


if __name__ == '__main__':
    unittest.main()
