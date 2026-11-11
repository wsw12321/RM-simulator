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
        robot = out['session']['robots'][0]
        self.assertEqual((robot['id'], robot['team'], robot['vehicle'], robot['vehicle_name']), ('solo', 'blue', 'builtin:infantry_mecanum', '麦轮步兵（默认）'))
        self.assertRegex(robot['vehicle_hash'], r'^[0-9a-f]{64}$')
        self.assertEqual([r['id'] for r in out['state']['robots']], ['solo'])
        self.assertEqual(out['state']['tick'], 0)

    def test_vehicles_and_garage(self):
        status, listing = call('/api/field/vehicles')
        self.assertEqual(status, 200, listing)
        self.assertEqual(listing['default'], 'builtin:infantry_mecanum')
        self.assertGreaterEqual(len(listing['vehicles']), 7)
        status, meta = call('/api/field/vehicles/schema')
        self.assertEqual(status, 200)
        self.assertEqual(len(meta['schema']['fields']), 60)
        self.assertIn('chassis.max_speed_mps', meta['labels']['fields'])
        status, entry = call('/api/field/vehicles/get', {'id': 'builtin:infantry_omni'})
        self.assertEqual(status, 200, entry)
        self.assertTrue(entry['readonly'])
        vehicle = entry['vehicle']
        vehicle['name'] = 'http 测试车'
        vehicle['chassis']['max_speed_mps'] = 2.0
        vehicle['chassis']['max_lateral_speed_mps'] = 2.0
        status, checked = call('/api/field/vehicles/check', {'vehicle': vehicle})
        self.assertEqual(status, 200, checked)
        self.assertEqual(checked['vehicle'], vehicle)
        for leftover in [v for v in listing['vehicles'] if v.get('name') == vehicle['name']]:
            call('/api/field/vehicles/delete', {'id': leftover['id']})
        status, saved = call('/api/field/vehicles/save', {'vehicle': vehicle, 'id': None})
        self.assertEqual(status, 200, saved)
        self.addCleanup(call, '/api/field/vehicles/delete', {'id': saved['id']})
        self.assertEqual(saved['hash'], checked['hash'])
        status, out = call('/api/field/reset', {'robots': [{'id': 'R1', 'team': 'red', 'vehicle': saved['id']}]})
        self.assertEqual(status, 200, out)
        self.assertEqual(out['session']['robots'][0]['vehicle_hash'], saved['hash'])
        self.assertEqual(out['state']['robots'][0]['vehicle'], 'http 测试车')
        status, out = call('/api/field/step', {'ticks': 200, 'commands': {'R1': {'chassis': {'vx': 9}}}})
        self.assertAlmostEqual(out['state']['robots'][0]['vx'], 2.0, places=6)
        # rejections: bad parameter, read-only built-in, unknown id, wrong shapes
        vehicle['chassis']['mass_kg'] = 500
        status, out = call('/api/field/vehicles/check', {'vehicle': vehicle})
        self.assertEqual(status, 400)
        self.assertIn('chassis.mass_kg', out['error'])
        self.assertEqual(call('/api/field/vehicles/save', {'vehicle': entry['vehicle'], 'id': 'builtin:infantry_omni'})[0], 400)
        self.assertEqual(call('/api/field/vehicles/delete', {'id': 'builtin:infantry_omni'})[0], 400)
        self.assertEqual(call('/api/field/vehicles/get', {'id': 'garage:000000000000'})[0], 400)
        self.assertEqual(call('/api/field/vehicles/save', {'vehicle': vehicle})[0], 400)
        self.assertEqual(call('/api/field/vehicles/get', {})[0], 400)
        self.assertEqual(call('/api/field/vehicles/rename', {'id': 'x'})[0], 404)
        self.assertEqual(call('/api/field/reset', {'robots': [{'id': 'R1', 'team': 'red', 'vehicle': 'garage:000000000000'}]})[0], 400)
        self.assertEqual(call('/api/field/vehicles/delete', {'id': saved['id']})[0], 200)
        self.assertEqual(call('/api/field/vehicles/get', {'id': saved['id']})[0], 400)
        for page in ('/garage.html', '/garage.js', '/garage.css'):
            self.assertEqual(call(page, raw=True)[0], 200)

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
