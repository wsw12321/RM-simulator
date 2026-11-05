#!/usr/bin/env python3
"""Live native/loopback integration; does not claim rendered-browser QA."""
import copy
import json
import sys
import unittest
import urllib.error
import urllib.request

BASE = 'http://127.0.0.1:' + str(sys.argv.pop(1) if len(sys.argv) > 1 else 8765)
POLICY = {'kind': 'policy', 'sdk_version': 1, 'controller_id': 'filtered-cover-v1'}


def manual(dx=0, dy=0, aim_x=6000, aim_y=3800, fire=0):
    return {'kind': 'manual', 'action': {'sdk_version': 1, 'dx': dx, 'dy': dy,
            'aim_x': aim_x, 'aim_y': aim_y, 'fire': fire}}


def call(path, data=None, headers=None, raw=None):
    body = raw if raw is not None else None if data is None else json.dumps(data).encode()
    request = urllib.request.Request(BASE + path, data=body,
        headers={'X-Sandbox-Client': '1', 'Content-Type': 'application/json', **(headers or {})})
    try:
        with urllib.request.urlopen(request, timeout=45) as response:
            return response.status, json.load(response)
    except urllib.error.HTTPError as error:
        return error.code, json.load(error)


class CombatHTTP(unittest.TestCase):
    def setUp(self):
        status, self.initial = call('/api/combat/reset', {'observer': 0})
        self.assertEqual(status, 200, self.initial)

    def step(self, count=1, observer=0, controllers=None):
        status, state = call('/api/combat/step', {'count': count, 'observer': observer,
            'controllers': controllers or [POLICY, POLICY]})
        self.assertEqual(status, 200, state)
        self.assertEqual(state['observer'], observer)
        self.assertEqual(state['observation']['self_index'], observer)
        self.assertNotIn('frames', state)
        self.assertNotIn('hash', state)
        return state

    def test_public_filter_and_metadata(self):
        status, meta = call('/api/combat/meta')
        self.assertEqual(status, 200, meta)
        self.assertFalse(meta['official'])
        self.assertEqual(meta['policy']['execution'], 'trusted-local-native')
        self.assertEqual(meta['policy']['observation'], 'experimental-range-los-v1')
        self.assertEqual(meta['policy']['controller_id'], POLICY['controller_id'])
        for observer in (0, 1):
            status, state = call(f'/api/combat/state?observer={observer}')
            self.assertEqual(status, 200, state)
            self.assertEqual(set(state), {'build', 'schema', 'profile', 'observation_profile', 'official',
                 'observer', 'tick', 'terminal', 'observation', 'last_sources'})
            obs = state['observation']
            self.assertEqual(obs['self_index'], observer)
            self.assertIsNone(obs['enemy'])
            self.assertEqual(obs['projectiles'], [])
            self.assertEqual(set(obs['self']), {'x', 'y', 'yaw', 'hp', 'ammo', 'cooldown'})
            self.assertEqual(set(obs), {'sdk_version', 'profile', 'hz', 'tick', 'self_index',
                 'sensor_range_mm', 'self', 'arena', 'enemy', 'projectiles'})
        for query in ('observer=2', 'observer=00', 'observer=0&observer=1', 'other=0', 'observer='):
            self.assertEqual(call('/api/combat/state?' + query)[0], 400)

    def test_independent_manual_and_mixed_sources_replay(self):
        state = self.step(5, 1, [manual(-1000, 0, 2500, 500), manual(1000, 0, 8000, 500)])
        self.assertEqual(state['observation']['self']['x'], 10400 + 5 * 55)
        state = self.step(23, 0, [manual(0, -1000), POLICY])
        self.assertEqual(state['tick'], 28)
        _, replay = call('/api/combat/replay')
        self.assertEqual(replay['schema'], 'rmuc-filtered-combat-replay.v1')
        self.assertEqual(len(replay['frames']), 28)
        self.assertEqual(replay['frames'][0]['inputs'][0]['action'], [-1000, 0, 2500, 500, 0])
        self.assertEqual(replay['frames'][0]['inputs'][1]['action'], [1000, 0, 8000, 500, 0])
        self.assertEqual([s['kind'] for s in replay['frames'][-1]['inputs']], ['manual', 'policy'])
        status, restored = call('/api/combat/import', {'observer': 0, 'replay': replay})
        self.assertEqual(status, 200, restored)
        self.assertEqual(restored['observation'], state['observation'])
        self.assertEqual(restored['verified_frames'], 28)
        self.assertEqual(call('/api/combat/replay')[1], replay)

    def test_policy_combat_reproducible_and_damaging(self):
        state = self.initial
        saw_contact = False
        while not state['terminal']:
            state = self.step(120)
            saw_contact |= state['observation']['enemy'] is not None
        _, replay = call('/api/combat/replay')
        self.assertLessEqual(state['tick'], 7200)
        self.assertTrue(saw_contact)
        self.assertLess(state['observation']['self']['hp'], 100)
        self.assertTrue(any(inp['action'][4] for f in replay['frames'] for inp in f['inputs']))
        self.assertTrue(all(inp['kind'] == 'policy' for f in replay['frames'] for inp in f['inputs']))
        self.assertEqual(call('/api/combat/import', {'observer': 0, 'replay': replay})[0], 200)
        self.assertEqual(call('/api/combat/replay')[1], replay)
        before = copy.deepcopy(replay)
        self.assertEqual(call('/api/combat/step', {'count': 1, 'observer': 0,
            'controllers': [POLICY, POLICY]})[0], 400)
        self.assertEqual(call('/api/combat/replay')[1], before)

    def test_bad_requests_and_replays_preserve_live_run(self):
        self.step(9, controllers=[manual(400, 100), POLICY])
        _, before = call('/api/combat/replay')
        valid = {'count': 1, 'observer': 0, 'controllers': [manual(), POLICY]}
        invalid = [{}, {**valid, 'count': True}, {**valid, 'count': 121}, {**valid, 'count': 1.0},
            {**valid, 'observer': False}, {**valid, 'observer': 2}, {**valid, 'extra': 0},
            {**valid, 'controllers': [POLICY]},
            {**valid, 'controllers': [manual(), {**POLICY, 'controller_id': 'other'}]},
            {**valid, 'controllers': [manual(), {**POLICY, 'sdk_version': True}]}]
        for field, value in [('dx', 1001), ('dy', -1001), ('aim_x', -1), ('aim_y', 7601),
                             ('fire', 2), ('fire', True), ('dx', 0.5), ('sdk_version', 2)]:
            item = copy.deepcopy(valid)
            item['controllers'][0]['action'][field] = value
            invalid.append(item)
        for item in invalid:
            self.assertEqual(call('/api/combat/step', item)[0], 400, item)
            self.assertEqual(call('/api/combat/replay')[1], before)
        for field in ('hash', 'observation_hashes', 'action'):
            bad = copy.deepcopy(before)
            if field == 'hash': bad['frames'][0]['hash'] ^= 1
            elif field == 'observation_hashes': bad['frames'][0]['observation_hashes'][1] ^= 1
            else: bad['frames'][0]['inputs'][0]['action'][0] = -1000
            self.assertEqual(call('/api/combat/import', {'observer': 0, 'replay': bad})[0], 400)
            self.assertEqual(call('/api/combat/replay')[1], before)
        self.assertEqual(call('/api/combat/reset', raw=b'{"observer":0,"observer":1}')[0], 400)
        self.assertEqual(call('/api/combat/replay')[1], before)

    def test_timeout_full_replay_and_mode_isolation(self):
        others = {p: call(p)[1] for p in ('/api/state', '/api/objective/state', '/api/layered/state')}
        state = self.initial
        while not state['terminal']:
            state = self.step(120, controllers=[manual(), manual()])
        self.assertEqual(state['tick'], 7200)
        _, replay = call('/api/combat/replay')
        self.assertEqual(len(replay['frames']), 7200)
        self.assertLess(len(json.dumps(replay).encode()), 12 * 1024 * 1024)
        status, restored = call('/api/combat/import', {'observer': 1, 'replay': replay})
        self.assertEqual(status, 200, restored)
        self.assertEqual(restored['tick'], 7200)
        self.assertEqual(restored['observation']['self']['hp'], 100)
        for path, before in others.items():
            self.assertEqual(call(path)[1], before, path)

    def test_origin_and_unknown_endpoints(self):
        self.assertEqual(call('/api/combat/reset', {'observer': 0}, {'Origin': 'https://example.org'})[0], 403)
        self.assertEqual(call('/api/combat/reset', {'observer': 0}, {'X-Sandbox-Client': '0'})[0], 403)
        self.assertEqual(call('/api/combat/no-such')[0], 404)
        self.assertEqual(call('/api/combat/no-such', {'observer': 0})[0], 404)


if __name__ == '__main__':
    unittest.main()
