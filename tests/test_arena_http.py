#!/usr/bin/env python3
"""Live-loopback tests for the stateless map workbench, not a browser render test."""
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
    request = urllib.request.Request(BASE + path, data=data, headers={
        'X-Sandbox-Client': '1', 'Content-Type': 'application/json', **(headers or {})})
    try:
        with urllib.request.urlopen(request, timeout=15) as response:
            return response.status, json.load(response)
    except urllib.error.HTTPError as error:
        return error.code, json.load(error)


class ArenaHTTP(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        status, cls.sample = call('/api/arena/sample')
        assert status == 200, cls.sample
        cls.text = json.dumps(cls.sample)

    def test_sample_and_validated_copy(self):
        self.assertEqual(self.sample['schema'], 'rmuc-layered-arena.v1')
        status, result = call('/api/arena/validate', {'text': self.text})
        self.assertEqual(status, 200, result)
        self.assertIs(result['valid'], True)
        self.assertEqual(result['document'], self.sample)

    def test_query_is_read_only_and_layer_explicit(self):
        _, sandbox = call('/api/state')
        _, objective = call('/api/objective/state')
        status, result = call('/api/arena/query', {
            'text': self.text, 'probe': {'x_mm': 0, 'y_mm': 0, 'radius_mm': 100, 'height_mm': 100}})
        self.assertEqual(status, 200, result)
        self.assertEqual(result['schema'], 'rmuc-layered-query.v1')
        self.assertIsNone(result['selected_surface_id'])
        self.assertEqual(call('/api/state')[1], sandbox)
        self.assertEqual(call('/api/objective/state')[1], objective)
        self.assertEqual(call('/api/arena/sample')[1], self.sample)

    def test_structured_errors_and_duplicate_keys(self):
        malformed = copy.deepcopy(self.sample)
        malformed['schema'] = 'official-map-claimed'
        for text in (json.dumps(malformed), '{"schema":1,"schema":2}', '{"n":NaN}', '[' * 1500 + ']' * 1500):
            status, result = call('/api/arena/validate', {'text': text})
            self.assertEqual(status, 400, result)
            self.assertIn('error', result)
        status, result = call('/api/arena/validate', {'text': json.dumps(malformed)})
        self.assertTrue(result['errors'])
        self.assertTrue({'path', 'code', 'message'} <= set(result['errors'][0]))
        self.assertEqual(call('/api/arena/validate', raw=b'{"text":"{}","text":"{}"}')[0], 400)

    def test_exact_envelopes_and_probe_fields(self):
        invalid = [None, {}, {'text': 17}, {'text': self.text, 'extra': 1}]
        for body in invalid:
            self.assertEqual(call('/api/arena/validate', raw=json.dumps(body).encode())[0], 400)
        for probe in (None, {}, {'x_mm': 0}, {'x_mm': False, 'y_mm': 0}, {'x_mm': 10**400, 'y_mm': 0},
                      {'x_mm': 0, 'y_mm': 0, 'z_mm': 0}, {'x_mm': 0, 'y_mm': 0, 'surface_id': 'missing'}):
            status, result = call('/api/arena/query', {'text': self.text, 'probe': probe})
            self.assertEqual(status, 400, result)

    def test_bounds_and_unicode_fail_safely(self):
        for text in (' ' * (256 * 1024 + 1), '\ud800', '{"metadata":"\ud800"}'):
            status, result = call('/api/arena/validate', {'text': text})
            self.assertEqual(status, 400, result)
        # Reject an over-limit declared length before reading it. Sending the
        # entire rejected body can legitimately race the server's early close.
        connection = http.client.HTTPConnection(BASE.removeprefix('http://'), timeout=10)
        try:
            connection.request('POST', '/api/arena/validate', headers={
                'X-Sandbox-Client': '1', 'Content-Length': str(2 * 1024 * 1024 + 1)})
            self.assertEqual(connection.getresponse().status, 400)
        finally:
            connection.close()

    def test_origin_host_and_unknown_routes(self):
        body = {'text': self.text}
        self.assertEqual(call('/api/arena/validate', body, headers={'Origin': 'https://example.org'})[0], 403)
        self.assertEqual(call('/api/arena/validate', body, headers={'X-Sandbox-Client': '0'})[0], 403)
        self.assertEqual(call('/api/arena/sample', headers={'Host': 'attacker.invalid'})[0], 403)
        self.assertEqual(call('/api/arena/missing', body)[0], 404)
        self.assertEqual(call('/api/arena/missing')[0], 404)

    def test_workbench_assets_have_local_csp(self):
        for path, content_type in (('/arena.html', 'text/html'), ('/arena.js', 'javascript'), ('/arena.css', 'text/css')):
            with urllib.request.urlopen(BASE + path, timeout=10) as response:
                self.assertEqual(response.status, 200)
                self.assertIn(content_type, response.headers['Content-Type'])
                self.assertIn("default-src 'self'", response.headers['Content-Security-Policy'])
                self.assertTrue(response.read())


if __name__ == '__main__':
    unittest.main()
