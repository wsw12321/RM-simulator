#!/usr/bin/env python3
"""Independent behavioral review of the constrained native movement profile.

These synthetic fixtures test engineering invariants, not official geometry or
real robot dynamics. No browser rendering or network permissions are exercised.
Run after building the native layered binary: python3 tests/test_layered_review.py.
"""
import copy
import json
import os
import random
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
# The bridge import is deliberately separate from server.py: no HTTP listeners.
from layered_mode.bridge import LayeredProcess


def rectangle(x1, y1, x2, y2):
    return [[x1, y1], [x2, y1], [x2, y2], [x1, y2]]


def surface(name, polygon, z=0, dx=0, dy=0, ceiling=None, layer='ground'):
    return {'id': name, 'layer_id': layer, 'polygon_mm': polygon,
            'height': {'z_mm': z, 'dx': dx, 'dy': dy},
            'ceiling_z_mm': ceiling, 'traversable': True,
            'source_ids': ['review_source']}


def volume(name, polygon, z1=0, z2=500, scoped=None):
    result = {'id': name, 'polygon_mm': polygon, 'z_min_mm': z1,
              'z_max_mm': z2, 'source_ids': ['review_source']}
    if scoped is not None:
        result.update(surface_ids=scoped, rule_tag='synthetic_review_annotation')
    return result


def portal(name, source, target, x, y1=0, y2=4000, both=True):
    return {'id': name, 'from_surface': source, 'to_surface': target,
            'portal_mm': [[x, y1], [x, y2]], 'bidirectional': both,
            'max_step_mm': 0, 'source_ids': ['review_source']}


def arena():
    return {
        'schema': 'rmuc-layered-arena.v1',
        'metadata': {'id': 'independent_movement_review',
                     'name': 'Independent synthetic movement review',
                     'description': 'Synthetic test geometry only.', 'official_map': False},
        'frame': {'units': 'mm', 'handedness': 'right', 'up_axis': '+z',
                  'x_axis': 'Synthetic east', 'y_axis': 'Synthetic north',
                  'origin': 'Synthetic zero'},
        'calibration': {'status': 'synthetic', 'notes': ['No official geometry.']},
        'sources': [{'id': 'review_source', 'kind': 'synthetic',
                     'reference': 'Independent synthetic test fixture',
                     'sha256': None, 'notes': 'No official source.'}],
        'boundary_mm': rectangle(0, 0, 6000, 4000),
        'layers': [{'id': 'ground', 'name': 'Ground', 'source_ids': ['review_source']},
                   {'id': 'upper', 'name': 'Upper', 'source_ids': ['review_source']}],
        'surfaces': [surface('floor', rectangle(0, 0, 6000, 4000))],
        'collision_volumes': [], 'trigger_volumes': [], 'transitions': [],
    }


def config(x=1000, y=2000, sid='floor', radius=10, height=100, speed=50, ticks=36000):
    return {'radius_mm': radius, 'height_mm': height, 'speed_mm_per_tick': speed,
            'max_slope_q1024': 512, 'max_ticks': ticks,
            'start': {'x_mm': x, 'y_mm': y, 'surface_id': sid}}


def split_arena(both=True, connected=True):
    result = arena()
    result['surfaces'] = [surface('west', rectangle(0, 0, 3000, 4000)),
                          surface('east', rectangle(3000, 0, 6000, 4000))]
    if connected:
        result['transitions'] = [portal('west_east', 'west', 'east', 3000, both=both)]
    return result


class IndependentLayeredMovementReview(unittest.TestCase):
    def setUp(self):
        self.engine = LayeredProcess(os.environ.get('LAYERED_REVIEW_BINARY'))
        self.addCleanup(self.engine.close)

    def load(self, document=None, settings=None):
        return self.engine.reset({'map': document or arena(), 'config': settings or config()})

    def move(self, count, dx=1000, dy=0):
        return self.engine.step({'count': count, 'action': [dx, dy]})

    def assert_unchanged_failure(self, operation):
        before = self.engine.state()
        with self.assertRaises((ValueError, RuntimeError)):
            operation()
        self.assertEqual(self.engine.state(), before)

    def test_flat_distance_fixed_tick_and_replay_repeatability(self):
        self.load()
        state = self.move(20)
        self.assertEqual(state['tick'], 20)
        self.assertEqual(state['robot']['x_q1024'], 2000 * 1024)
        self.assertEqual(state['robot']['y_q1024'], 2000 * 1024)
        self.assertEqual(state['robot']['z_q1048576'], 0)
        journal = self.engine.replay()
        verified = self.engine.import_replay(journal)
        self.assertEqual(verified['hash'], state['hash'])
        self.assertEqual(verified['robot'], state['robot'])
        self.assertEqual(self.engine.replay(), journal)
        self.load()
        for _ in range(20):
            state2 = self.move(1)
        self.assertEqual(state2['hash'], state['hash'], 'Batch size cannot alter deterministic state.')
        self.assertEqual(self.engine.replay(), journal)

    def test_no_unconnected_seam_or_stacked_xy_transfer(self):
        document = split_arena(connected=False)
        document['surfaces'].append(surface('stacked_upper', rectangle(0, 0, 6000, 4000), z=1000, layer='upper'))
        self.load(document, config(x=2500, sid='west', radius=100))
        state = self.move(40)
        self.assertEqual(state['robot']['surface_id'], 'west')
        self.assertEqual(state['robot']['layer_id'], 'ground')
        self.assertEqual(state['robot']['z_q1048576'], 0)
        self.assertLessEqual(state['robot']['x_q1024'], 2900 * 1024)
        self.assertFalse(any(item['type'] == 'transition' for item in state['events']))

    def test_bidirectional_and_one_way_portals(self):
        self.load(split_arena(), config(x=2500, sid='west', radius=100))
        forward = self.move(20)
        self.assertEqual(forward['robot']['surface_id'], 'east')
        self.assertEqual(forward['robot']['x_q1024'], 3500 * 1024)
        backward = self.move(20, -1000)
        self.assertEqual(backward['robot']['surface_id'], 'west')
        self.assertEqual(backward['robot']['x_q1024'], 2500 * 1024)
        self.load(split_arena(both=False), config(x=2500, sid='west', radius=100))
        self.assertEqual(self.move(20)['robot']['surface_id'], 'east')
        blocked = self.move(30, -1000)
        self.assertEqual(blocked['robot']['surface_id'], 'east')
        self.assertGreaterEqual(blocked['robot']['x_q1024'], 3000 * 1024)

    def test_portal_endpoint_radius_and_ceiling_before_center_crossing(self):
        document = split_arena()
        document['transitions'][0]['portal_mm'] = [[3000, 1500], [3000, 2500]]
        self.load(document, config(x=2500, y=1450, sid='west', radius=100))
        blocked = self.move(30)
        self.assertEqual(blocked['robot']['surface_id'], 'west')
        self.assertLessEqual(blocked['robot']['x_q1024'], 2900 * 1024)
        document = split_arena()
        document['surfaces'][1]['ceiling_z_mm'] = 100
        self.load(document, config(x=2500, sid='west', radius=100, height=100))
        blocked = self.move(30)
        self.assertEqual(blocked['robot']['surface_id'], 'west')
        self.assertLessEqual(blocked['robot']['x_q1024'], 2900 * 1024)
        self.assertIn({'kind': 'ceiling', 'id': 'east'}, blocked['contacts'])
        self.assertNotIn({'kind': 'ceiling', 'id': 'west'}, blocked['contacts'])
        document['surfaces'][1]['ceiling_z_mm'] = 101
        self.load(document, config(x=2500, sid='west', radius=100, height=100))
        self.assertEqual(self.move(20)['robot']['surface_id'], 'east')

    def test_ramp_height_and_slope_limit(self):
        document = arena()
        document['surfaces'] = [surface('west', rectangle(0, 0, 2000, 4000)),
                                surface('ramp', rectangle(2000, 0, 4000, 4000), z=-250, dx=.125),
                                surface('deck', rectangle(4000, 0, 6000, 4000), z=250, layer='upper')]
        document['transitions'] = [portal('west_ramp', 'west', 'ramp', 2000),
                                   portal('ramp_deck', 'ramp', 'deck', 4000)]
        self.load(document, config(x=1500, sid='west', radius=100))
        slope = self.move(30)
        self.assertEqual(slope['robot']['surface_id'], 'ramp')
        self.assertEqual(slope['robot']['z_q1048576'], 125 * 1048576)
        top = self.move(40)
        self.assertEqual(top['robot']['surface_id'], 'deck')
        self.assertEqual(top['robot']['layer_id'], 'upper')
        self.assertEqual(top['robot']['z_q1048576'], 250 * 1048576)
        settings = config(x=1500, sid='west', radius=100)
        settings['max_slope_q1024'] = 127
        self.load(document, settings)
        rejected = self.move(30)
        self.assertEqual(rejected['robot']['surface_id'], 'west')
        self.assertLessEqual(rejected['robot']['x_q1024'], 1900 * 1024)

    def test_global_prisms_are_height_aware_and_top_rest_is_allowed(self):
        document = arena()
        document['surfaces'].append(surface('upper_floor', rectangle(0, 0, 6000, 4000), z=500, layer='upper'))
        document['collision_volumes'] = [volume('slab', rectangle(0, 0, 6000, 4000), 400, 500)]
        self.load(document, config(sid='upper_floor'))
        self.assertEqual(self.move(10)['robot']['x_q1024'], 1500 * 1024)
        self.load(document, config(height=399))
        self.assertEqual(self.move(10)['robot']['x_q1024'], 1500 * 1024)
        self.assert_unchanged_failure(lambda: self.load(document, config(height=400)))
        document['collision_volumes'] = [volume('tall_global_pillar', rectangle(1800, 1800, 1900, 2200), 0, 1000)]
        self.load(document, config(sid='upper_floor', x=1500, radius=100))
        state = self.move(20)
        self.assertLess(state['robot']['x_q1024'], 1700 * 1024)
        self.assertEqual(state['robot']['surface_id'], 'upper_floor')

    def test_thin_solid_blocks_and_thin_trigger_gets_transient_events(self):
        document = arena()
        document['collision_volumes'] = [volume('thin_wall', rectangle(2020, 1000, 2021, 3000))]
        self.load(document, config(x=2000, radius=1, speed=50))
        stopped = self.move(1)
        self.assertLess(stopped['robot']['x_q1024'], 2019 * 1024)
        document['collision_volumes'] = []
        document['trigger_volumes'] = [volume('thin_trigger', rectangle(2020, 1000, 2021, 3000), scoped=['floor']),
                                       volume('other_floor_trigger', rectangle(2020, 1000, 2021, 3000), scoped=['stacked'])]
        document['surfaces'].append(surface('stacked', rectangle(0, 0, 6000, 4000), z=500, layer='upper'))
        self.load(document, config(x=2000, radius=1, speed=50))
        passed = self.move(1)
        self.assertEqual(passed['robot']['x_q1024'], 2050 * 1024)
        relevant = [(item['type'], item['id']) for item in passed['events']]
        self.assertIn(('trigger_enter', 'thin_trigger'), relevant)
        self.assertIn(('trigger_exit', 'thin_trigger'), relevant)
        self.assertLess(relevant.index(('trigger_enter', 'thin_trigger')), relevant.index(('trigger_exit', 'thin_trigger')))
        self.assertFalse(any(item[1] == 'other_floor_trigger' for item in relevant))
        self.assertEqual(passed['active_trigger_ids'], [])

    def test_stationary_circle_rectangle_oracle_and_tangent_boundary_motion(self):
        document = arena()
        document['collision_volumes'] = [volume('oracle_box', rectangle(2500, 1500, 3500, 2500), 100, 300)]
        rng = random.Random(6105)
        for index in range(100):
            x, y = rng.randint(2200, 3800), rng.randint(1200, 2800)
            radius, height = rng.randint(1, 250), rng.choice([99, 100, 101])
            delta_x, delta_y = max(2500-x, 0, x-3500), max(1500-y, 0, y-2500)
            intersects = delta_x*delta_x + delta_y*delta_y <= radius*radius and height >= 100
            settings = config(x=x, y=y, radius=radius, height=height)
            with self.subTest(index=index):
                if intersects:
                    self.assert_unchanged_failure(lambda: self.load(document, settings))
                else:
                    state = self.load(document, settings)
                    self.assertEqual(state['robot']['x_q1024'], x * 1024)
                    still = self.move(1, 0, 0)
                    self.assertEqual(still['robot'], state['robot'])
                    self.assertEqual(still['contacts'], [])
        self.load(arena(), config(x=1000, y=100, radius=100))
        slide = self.move(10)
        self.assertEqual(slide['robot']['x_q1024'], 1500 * 1024)
        self.assertEqual(slide['robot']['y_q1024'], 100 * 1024)

    def test_exact_corner_contact_and_authored_trigger_event_order(self):
        document = arena()
        document['collision_volumes'] = [volume('corner', rectangle(2020, 2020, 2100, 2100))]
        # A 3-4-5 contact must block; moving one millimeter farther is clear.
        self.load()
        self.assert_unchanged_failure(lambda: self.load(document, config(x=2017, y=2016, radius=5)))
        self.load(document, config(x=2016, y=2016, radius=5))
        document['collision_volumes'] = []
        document['trigger_volumes'] = [volume(name, rectangle(2020, 1000, 2021, 3000), scoped=['floor'])
                                      for name in ('zeta_first', 'alpha_second')]
        self.load(document, config(x=2000, radius=1))
        events = self.move(1)['events']
        self.assertEqual([(item['type'], item['id']) for item in events],
                         [('trigger_enter', 'zeta_first'), ('trigger_enter', 'alpha_second'),
                          ('trigger_exit', 'zeta_first'), ('trigger_exit', 'alpha_second')])
        journal = self.engine.replay()
        self.assertEqual(self.engine.import_replay(journal)['events'], events)

    def test_unsupported_numeric_maps_fail_without_mutating_current_run(self):
        self.load()
        self.move(3)
        for value in (.1, 1e-100, -.1):
            document = arena()
            document['surfaces'][0]['height']['dx'] = value
            with self.subTest(slope=value):
                self.assert_unchanged_failure(lambda: self.load(document))
        for value in (True, .5, 100001, 10**100):
            document = arena()
            document['boundary_mm'][0][0] = value
            with self.subTest(coordinate=value):
                self.assert_unchanged_failure(lambda: self.load(document))
        for key, value in [('radius_mm', True), ('height_mm', 0), ('speed_mm_per_tick', 51),
                           ('max_slope_q1024', 1025), ('max_ticks', 36001)]:
            settings = config()
            settings[key] = value
            with self.subTest(config=key):
                self.assert_unchanged_failure(lambda: self.load(arena(), settings))
        for action in ([True, 0], [1.5, 0], [1001, 0], [0, -1001]):
            with self.subTest(action=action):
                self.assert_unchanged_failure(lambda: self.engine.step({'count': 1, 'action': action}))

    def test_coordinate_extremes_negative_slope_and_exact_limit(self):
        document = arena()
        document['boundary_mm'] = rectangle(-100000, -100000, 100000, 100000)
        document['surfaces'] = [surface('floor', copy.deepcopy(document['boundary_mm']), dx=-.5, dy=.5)]
        settings = config(x=-99000, y=-99000, radius=1000, height=3000, speed=50, ticks=3)
        settings['max_slope_q1024'] = 1024
        self.load(document, settings)
        state = self.move(3)
        self.assertEqual(state['tick'], 3)
        self.assertEqual(state['status'], 'limit_reached')
        self.assertEqual(state['robot']['z_q1048576'], -75 * 1048576)
        final = self.engine.state()
        # A terminal advance may return unchanged state or reject. Neither may append frames.
        try:
            self.move(1)
        except ValueError:
            pass
        self.assertEqual(self.engine.state(), final)

    def test_replay_tampering_is_atomic_and_identity_includes_provenance(self):
        self.load()
        self.move(5)
        replay = self.engine.replay()
        for key in ('build', 'map_hash', 'config_hash', 'final_hash'):
            if key not in replay:
                self.fail(f'Replay must bind identity/checksum field {key}.')
            mutated = copy.deepcopy(replay)
            mutated[key] = '0' * len(str(replay[key]))
            with self.subTest(identity=key):
                self.assert_unchanged_failure(lambda: self.engine.import_replay(mutated))
        mutated = copy.deepcopy(replay)
        mutated['frames'][0][-1] = 'not-a-state-hash'
        self.assert_unchanged_failure(lambda: self.engine.import_replay(mutated))
        previous_map = self.engine.state()['map_hash']
        document = arena()
        document['sources'][0]['notes'] += ' Changed provenance.'
        self.load(document)
        self.assertNotEqual(self.engine.state()['map_hash'], previous_map)


if __name__ == '__main__':
    unittest.main()
