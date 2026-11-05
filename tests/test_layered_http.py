#!/usr/bin/env python3
"""Live loopback integration checks for isolated native layered movement."""
import copy
import http.client
import json
import sys
import unittest
import urllib.error
import urllib.request

BASE = 'http://127.0.0.1:' + str(sys.argv.pop(1) if len(sys.argv) > 1 else 8765)


def call(path, body=None, *, raw=None, headers=None):
    data = raw if raw is not None else (None if body is None else json.dumps(body).encode())
    request = urllib.request.Request(BASE + '/api/' + path, data=data, headers={
        'X-Sandbox-Client': '1', 'Content-Type': 'application/json', **(headers or {})})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return response.status, json.load(response)
    except urllib.error.HTTPError as error:
        return error.code, json.load(error)


class LayeredHTTP(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        status, cls.sample = call('layered/sample')
        assert status == 200, cls.sample

    def setUp(self):
        status, state = call('layered/reset', self.sample)
        self.assertEqual(status, 200, state)

    def test_movement_replay_and_mode_isolation(self):
        _, sandbox = call('state')
        _, objective = call('objective/state')
        _, before = call('layered/state')
        status, state = call('layered/step', {'count': 60, 'action': [1000, 0]})
        self.assertEqual(status, 200, state)
        self.assertEqual(state['tick'], 60)
        self.assertNotEqual(state['hash'], before['hash'])
        self.assertTrue({'x_mm', 'y_mm', 'z_mm', 'surface_id', 'layer_id'} <= set(state['robot']))
        _, replay = call('layered/replay')
        status, restored = call('layered/import', replay)
        self.assertEqual(status, 200, restored)
        self.assertEqual(restored['hash'], state['hash'])
        self.assertEqual(call('state')[1], sandbox)
        self.assertEqual(call('objective/state')[1], objective)

    def test_rejected_config_preserves_run(self):
        call('layered/step', {'count': 5, 'action': [1000, 0]})
        _, before = call('layered/state')
        for field, bad in [('radius_mm', True), ('height_mm', 10**400),
                           ('max_ticks', 0), ('speed_mm_per_tick', 51)]:
            value = copy.deepcopy(self.sample)
            value['config'][field] = bad
            status, response = call('layered/reset', value)
            self.assertEqual(status, 400, response)
            self.assertEqual(call('layered/state')[1], before)
        value = copy.deepcopy(self.sample)
        value['map']['metadata']['official_map'] = True
        self.assertEqual(call('layered/reset', value)[0], 400)
        self.assertEqual(call('layered/state')[1], before)

    def test_invalid_steps_are_atomic(self):
        _, before = call('layered/state')
        for value in ({}, {'count': True, 'action': [0, 0]}, {'count': 601, 'action': [0, 0]},
                      {'count': 1, 'action': [1001, 0]}, {'count': 1, 'action': [False, 0]},
                      {'count': 1, 'action': [10**400, 0]}, {'count': 1, 'action': [0]},
                      {'count': 1, 'action': [0, 0], 'mode': 1}):
            status, response = call('layered/step', value)
            self.assertEqual(status, 400, response)
            self.assertEqual(call('layered/state')[1], before)

    def test_cross_profile_replay_rejected(self):
        _, before = call('layered/state')
        for source in ('replay', 'objective/replay'):
            _, other = call(source)
            self.assertEqual(call('layered/import', other)[0], 400)
            self.assertEqual(call('layered/state')[1], before)
        _, replay = call('layered/replay')
        self.assertEqual(call('import', replay)[0], 400)
        self.assertEqual(call('objective/import', replay)[0], 400)
        replay['schema'] = 'not-the-layered-schema'
        self.assertEqual(call('layered/import', replay)[0], 400)
        self.assertEqual(call('layered/state')[1], before)

    def test_duplicate_keys_unicode_and_nesting(self):
        _, before = call('layered/state')
        for raw in (b'{"count":1,"count":2,"action":[0,0]}',
                    b'{"\\ud800":1,"\\ud800":2}', b'{"count":NaN,"action":[0,0]}', b'{"count":1.000000000000000001,"action":[0,0]}',
                    b'{"count":' + b'[' * 2000 + b']' * 2000 + b'}', b'null', b'[]'):
            status, response = call('layered/step', raw=raw)
            self.assertEqual(status, 400, response)
            self.assertEqual(call('layered/state')[1], before)
        # Raw file bodies preserve duplicate nested map keys, unlike a JS parse.
        text = json.dumps(self.sample).replace('"units": "mm"', '"units":"mm","units":"mm"')
        self.assertNotEqual(text, json.dumps(self.sample))
        self.assertEqual(call('layered/reset', raw=text.encode())[0], 400)
        self.assertEqual(call('layered/state')[1], before)
        for literal in ('0.250000000000000001', '1e-400'):
            text = json.dumps(self.sample).replace('\"dx\": 0.25', '\"dx\": ' + literal)
            self.assertNotEqual(text, json.dumps(self.sample))
            self.assertEqual(call('layered/reset', raw=text.encode())[0], 400)
            self.assertEqual(call('layered/state')[1], before)

    def test_bounds_origin_and_unknown_routes(self):
        self.assertEqual(call('layered/reset', self.sample, headers={'Origin': 'https://example.org'})[0], 403)
        self.assertEqual(call('layered/reset', self.sample, headers={'X-Sandbox-Client': '0'})[0], 403)
        self.assertEqual(call('layered/state', headers={'Host': 'attacker.invalid'})[0], 403)
        self.assertEqual(call('layered/missing')[0], 404)
        self.assertEqual(call('layered/missing', {})[0], 404)
        connection = http.client.HTTPConnection(BASE.removeprefix('http://'), timeout=10)
        try:
            connection.request('POST', '/api/layered/import', headers={
                'X-Sandbox-Client': '1', 'Content-Length': str(12 * 1024 * 1024 + 1)})
            self.assertEqual(connection.getresponse().status, 400)
        finally:
            connection.close()

    def test_assets_and_current_map_metadata(self):
        status, meta = call('layered/meta')
        self.assertEqual(status, 200, meta)
        self.assertEqual(meta['map'], self.sample['map'])
        self.assertEqual(meta['config'], self.sample['config'])
        for path in ('layered.html', 'layered.js', 'layered.css'):
            with urllib.request.urlopen(BASE + '/' + path, timeout=10) as response:
                self.assertEqual(response.status, 200)
                self.assertIn("default-src 'self'", response.headers['Content-Security-Policy'])
                self.assertTrue(response.read())


if __name__ == '__main__':
    unittest.main()
