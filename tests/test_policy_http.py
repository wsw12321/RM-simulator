#!/usr/bin/env python3
"""Live-loopback tests for explicit trusted-local native policy requests."""
import copy
import json
import sys
import unittest
import urllib.error
import urllib.request

BASE = 'http://127.0.0.1:' + str(sys.argv.pop(1) if len(sys.argv) > 1 else 8765)
POLICY = {'sdk_version': 1, 'controller_id': 'synthetic-ramp-deck-v1'}


def call(path, data=None, headers=None):
    request = urllib.request.Request(BASE + '/api/' + path,
        data=None if data is None else json.dumps(data).encode(),
        headers={'X-Sandbox-Client': '1', 'Content-Type': 'application/json', **(headers or {})})
    try:
        with urllib.request.urlopen(request, timeout=45) as response:
            return response.status, json.load(response)
    except urllib.error.HTTPError as error:
        return error.code, json.load(error)


class PolicyHTTP(unittest.TestCase):
    def setUp(self):
        status, self.sample = call('layered/sample')
        self.assertEqual(status, 200)
        self.assertEqual(call('layered/reset', self.sample)[0], 200)

    def test_metadata_is_explicit_and_trusted_local(self):
        status, meta = call('layered/meta')
        self.assertEqual(status, 200)
        policy = meta['policy']
        self.assertEqual(policy['sdk_version'], 1)
        self.assertEqual(policy['controller_id'], POLICY['controller_id'])
        self.assertEqual(policy['execution'], 'trusted-local-native')
        self.assertEqual(policy['state_mode'], 'stateless')
        self.assertEqual(policy['decision_hz'], 60)

    def test_native_example_goal_action_replay_and_other_modes(self):
        _, sandbox = call('state')
        _, objective = call('objective/state')
        status, state = call('layered/policy-step', {'count': 600, **POLICY})
        self.assertEqual(status, 200, state)
        status, state = call('layered/policy-step', {'count': 600, **POLICY})
        self.assertEqual(status, 200, state)
        self.assertEqual(state['tick'], 1200)
        self.assertEqual(state['robot']['surface_id'], 'upper_deck')
        self.assertEqual(state['robot']['z_q1048576'], 1000 * 1048576)
        _, replay = call('layered/replay')
        self.assertEqual(replay['schema'], 'rmuc-layered-replay.v2')
        self.assertEqual(len(replay['frames']), 1200)
        self.assertTrue(replay['action_sources'])
        self.assertTrue(any(frame[1] or frame[2] for frame in replay['frames']))
        status, restored = call('layered/import', replay)
        self.assertEqual(status, 200, restored)
        self.assertEqual(restored['hash'], state['hash'])
        self.assertEqual(call('state')[1], sandbox)
        self.assertEqual(call('objective/state')[1], objective)

    def test_bad_policy_request_is_atomic(self):
        _, before = call('layered/state')
        invalid = [{}, {'count': 1, 'sdk_version': 1}, {'count': 0, **POLICY},
                   {'count': 601, **POLICY}, {'count': True, **POLICY},
                   {'count': 1, **POLICY, 'sdk_version': True},
                   {'count': 1, **POLICY, 'sdk_version': 2},
                   {'count': 1, **POLICY, 'controller_id': 'unknown'},
                   {'count': 1, **POLICY, 'action': [1000, 0]}]
        for data in invalid:
            status, response = call('layered/policy-step', data)
            self.assertEqual(status, 400, response)
            self.assertEqual(call('layered/state')[1], before)

    def test_manual_endpoint_still_resolves_manual_input(self):
        status, state = call('layered/step', {'count': 1, 'action': [-1000, 0]})
        self.assertEqual(status, 200, state)
        self.assertEqual(state['robot']['x_q1024'], 780 * 1024)
        _, replay = call('layered/replay')
        self.assertEqual(replay['frames'][0][1:3], [-1000, 0])
        before = call('layered/state')[1]
        self.assertEqual(call('layered/step', {'count': 1, 'action': [1001, 0]})[0], 400)
        self.assertEqual(call('layered/state')[1], before)

    def test_policy_request_limit_and_origin(self):
        sample = copy.deepcopy(self.sample)
        sample['config']['max_ticks'] = 1
        self.assertEqual(call('layered/reset', sample)[0], 200)
        before = call('layered/state')[1]
        self.assertEqual(call('layered/policy-step', {'count': 2, **POLICY})[0], 400)
        self.assertEqual(call('layered/state')[1], before)
        status, state = call('layered/policy-step', {'count': 1, **POLICY})
        self.assertEqual(status, 200, state)
        self.assertEqual(state['status'], 'limit_reached')
        self.assertEqual(call('layered/policy-step', {'count': 1, **POLICY})[0], 400)
        self.assertEqual(call('layered/policy-step', {'count': 1, **POLICY}, {'Origin': 'https://example.org'})[0], 403)


if __name__ == '__main__':
    unittest.main()
