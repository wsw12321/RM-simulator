#!/usr/bin/env python3
"""Independent adversarial/oracle checks, not official geometry certification.

The oracles cover this format's conservative static cylinder contract, not real
robot dynamics, safe routes, source authenticity, or competition dimensions.
Run from any directory: python3 tests/test_arena_review.py.
"""
import copy
import json
import math
from pathlib import Path
import random
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from arena_format import ArenaValidationError, MAX_TEXT_BYTES, parse_document, query, validate_document


def fixture():
    return parse_document((ROOT / 'arena_format/examples/synthetic_bridge_tunnel.json').read_text(encoding='utf-8'))


def paths(value, prefix=()):
    yield prefix, value
    if type(value) is dict:
        for key, item in value.items():
            yield from paths(item, prefix + (key,))
    elif type(value) is list:
        for index, item in enumerate(value):
            yield from paths(item, prefix + (index,))


def replace(document, path, value):
    result = copy.deepcopy(document)
    if not path:
        return copy.deepcopy(value)
    container = result
    for key in path[:-1]:
        container = container[key]
    container[path[-1]] = copy.deepcopy(value)
    return result


def plane_fixture():
    document = fixture()
    document['boundary_mm'] = [[0, 0], [6000, 0], [6000, 4000], [0, 4000]]
    document['transitions'] = []
    document['trigger_volumes'] = []
    document['collision_volumes'] = []
    surface = copy.deepcopy(document['surfaces'][0])
    surface['polygon_mm'] = copy.deepcopy(document['boundary_mm'])
    surface['height'] = {'z_mm': 0, 'dx': 0, 'dy': 0}
    surface['ceiling_z_mm'] = None
    document['surfaces'] = [surface]
    return document


class IndependentArenaReview(unittest.TestCase):
    def assert_structured_error(self, operation, expected_path=None):
        with self.assertRaises(ArenaValidationError) as caught:
            operation()
        errors = caught.exception.errors
        self.assertEqual(len(errors), 1)
        self.assertEqual(set(errors[0]), {'path', 'code', 'message'})
        self.assertTrue(all(type(item) is str and item for item in errors[0].values()))
        json.dumps(errors, allow_nan=False).encode('utf-8')
        if expected_path is not None:
            self.assertEqual(errors[0]['path'], expected_path)

    def test_type_directed_rejections_at_every_schema_path(self):
        """Every mutation here must fail, rather than merely avoiding a crash."""
        document = fixture()
        substitutions = {
            dict: [None, False, 0, 'bad', []],
            list: [None, False, 0, 'bad', {}],
            str: [None, False, 0, [], {}, '', '\ud800'],
            int: [None, False, True, '0', [], {}, 10**400, -10**400, math.nan, math.inf, -math.inf],
            float: [None, False, True, '0', [], {}, 10**400, -10**400, math.nan, math.inf, -math.inf],
            bool: [None, 0, 1, 'false', [], {}],
            # The fixture's nullable fields are ceilings and SHA-256 digests.
            type(None): [True, [], {}, math.nan, math.inf, 10**400, -10**400, 'invalid digest or height'],
        }
        count = 0
        for path, original in paths(document):
            for replacement in substitutions[type(original)]:
                if path and path[-1] == 'ceiling_z_mm' and replacement is None:
                    continue  # A ceiling may intentionally be unspecified.
                mutated = replace(document, path, replacement)
                before = repr(mutated)
                with self.subTest(path=path, replacement=repr(replacement)[:50]):
                    self.assert_structured_error(lambda: validate_document(mutated))
                    self.assertEqual(repr(mutated), before, 'Rejection must not change input.')
                count += 1
        self.assertGreater(count, 2000)

    def test_known_valid_roundtrips_and_no_mutation(self):
        for text in ['Reviewer synthetic fixture', '合成审查 😀', 'quotes " and \\ slashes', '<script>text only</script>']:
            document = fixture()
            document['metadata']['name'] = text
            document['metadata']['description'] = text
            document['calibration']['notes'] = [text]
            document['sources'][0]['notes'] = text
            before = copy.deepcopy(document)
            self.assertIs(validate_document(document), document)
            encoded = json.dumps(document, ensure_ascii=False)
            self.assertEqual(parse_document(encoded), before)
            result = query(document, 3000, 2000, radius_mm=100, height_mm=500)
            self.assertEqual(document, before)
            self.assertIsNone(result['selected_surface_id'])
            self.assertEqual({c['surface_id'] for c in result['candidates']}, {'tunnel_floor', 'bridge_deck'})
        document = fixture()
        for surface in document['surfaces']:
            surface['ceiling_z_mm'] = None
        self.assertIs(validate_document(document), document)

    def test_adversarial_numeric_probes_are_structured(self):
        document = fixture()
        for field in ('x_mm', 'y_mm', 'radius_mm', 'height_mm', 'max_slope_deg'):
            for value in (10**400, -10**400, math.nan, math.inf, -math.inf, True, False, None, '0', {}, []):
                probe = {'x_mm': 3000, 'y_mm': 2000, field: value}
                with self.subTest(field=field, value=repr(value)[:50]):
                    self.assert_structured_error(lambda: query(document, **probe), '$.probe.' + field)
        # These values pass the JSON integer parser and must not overflow a
        # float conversion inside the actual server integration function.
        from server import arena_post
        text = json.dumps(document)
        for field in ('x_mm', 'y_mm', 'radius_mm', 'height_mm', 'max_slope_deg'):
            envelope = json.loads(json.dumps({'text': text, 'probe': {'x_mm': 0, 'y_mm': 0, field: 10**400}}))
            self.assert_structured_error(lambda: arena_post('/api/arena/query', envelope), '$.probe.' + field)

    def test_flat_collision_matches_independent_rectangle_oracle(self):
        document = plane_fixture()
        document['collision_volumes'] = [{
            'id': 'review_box', 'polygon_mm': [[2000, 1500], [4000, 1500], [4000, 2500], [2000, 2500]],
            'z_min_mm': 100, 'z_max_mm': 500, 'source_ids': [document['sources'][0]['id']],
        }]
        random_source = random.Random(5105)
        for index in range(1000):
            x, y = random_source.uniform(200, 5800), random_source.uniform(200, 3800)
            radius, height = random_source.uniform(0, 199), random_source.uniform(0, 800)
            candidate = query(document, x, y, radius_mm=radius, height_mm=height)['candidates'][0]
            # Nearest point on the axis-aligned closed rectangle, independent
            # of the production edge/segment geometry implementation.
            distance = math.hypot(max(2000 - x, 0, x - 4000), max(1500 - y, 0, y - 2500))
            collision = distance <= radius + 1e-7 and height >= 100 - 1e-7
            with self.subTest(index=index):
                self.assertEqual(bool(candidate['collision_ids']), collision)
                self.assertEqual(candidate['feasible'], not collision)
        # Exactly touching a box side or underside blocks; resting atop a
        # solid is allowed. A tiny real gap beyond arithmetic EPS is clear.
        for x, height, expected in ((1900, 100, True), (1899.999, 100, False), (1900, 99.999, False)):
            result = query(document, x, 2000, radius_mm=100, height_mm=height)['candidates'][0]
            self.assertEqual(bool(result['collision_ids']), expected)
        document['surfaces'][0]['height']['z_mm'] = 500
        self.assertEqual(query(document, 3000, 2000, radius_mm=100, height_mm=100)['candidates'][0]['collision_ids'], [])

    def test_sloped_support_matches_independent_analytic_oracle(self):
        document = plane_fixture()
        surface = document['surfaces'][0]
        surface['height'] = {'z_mm': -400, 'dx': .1, 'dy': .2}
        surface['ceiling_z_mm'] = 1500
        random_source = random.Random(5106)
        for index in range(1000):
            x, y = random_source.uniform(200, 5800), random_source.uniform(200, 3800)
            radius, height = random_source.uniform(0, 199), random_source.uniform(0, 1200)
            candidate = query(document, x, y, radius_mm=radius, height_mm=height)['candidates'][0]
            z = -400 + .1 * x + .2 * y
            delta = radius * math.hypot(.1, .2)
            with self.subTest(index=index):
                self.assertEqual(candidate['support_range_mm'], [z - delta, z + delta])
                self.assertAlmostEqual(candidate['clearance_mm'], 1500 - z - delta, places=10)
                self.assertEqual(candidate['feasible'], height < 1500 - z - delta - 1e-7)
        slope = math.degrees(math.atan(math.hypot(.1, .2)))
        self.assertTrue(query(document, 3000, 2000, max_slope_deg=slope)['candidates'][0]['feasible'])
        self.assertFalse(query(document, 3000, 2000, max_slope_deg=slope - .001)['candidates'][0]['feasible'])

    def test_exact_canonical_byte_limit_is_inclusive(self):
        document = fixture()
        compact = lambda value: json.dumps(value, ensure_ascii=True, separators=(',', ':'))
        for index in range(200):
            document['sources'].append({'id': f'review_pad_{index}', 'kind': 'synthetic', 'reference': 'Synthetic test padding', 'sha256': None, 'notes': 'x' * 2048})
            if len(compact(document)) >= MAX_TEXT_BYTES:
                break
        excess = len(compact(document)) - MAX_TEXT_BYTES
        self.assertGreaterEqual(excess, 0)
        for source in reversed(document['sources']):
            take = min(excess, len(source['notes']) - 1)
            if take:
                source['notes'] = source['notes'][:-take]
            excess -= take
            if not excess:
                break
        text = compact(document)
        self.assertEqual(len(text.encode('utf-8')), MAX_TEXT_BYTES)
        self.assertIs(validate_document(document), document)
        self.assertEqual(parse_document(text), document)
        self.assert_structured_error(lambda: parse_document(text + '\n'))


if __name__ == '__main__':
    unittest.main()
