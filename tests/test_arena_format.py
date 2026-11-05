"""Strict schema and conservative static geometry checks. No external packages."""
import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from arena_format import ArenaValidationError, MAX_TEXT_BYTES, parse_document, query, validate_document

SAMPLE = ROOT / 'arena_format/examples/synthetic_bridge_tunnel.json'


def fixture():
    return parse_document(SAMPLE.read_text())


def codes(result):
    return [r['code'] for r in result['candidates'][0]['reasons']]


class ArenaValidationTests(unittest.TestCase):
    def reject(self, document, code=None, path=None):
        with self.assertRaises(ArenaValidationError) as caught:
            validate_document(document)
        e = caught.exception.errors[0]
        self.assertEqual(set(e), {'path', 'code', 'message'})
        if code:
            self.assertEqual(e['code'], code)
        if path:
            self.assertEqual(e['path'], path)
        return e

    def test_valid_sample_and_read_only_validation(self):
        document = fixture()
        before = copy.deepcopy(document)
        self.assertIs(validate_document(document), document)
        self.assertEqual(document, before)
        self.assertFalse(document['metadata']['official_map'])
        self.assertEqual(document['calibration']['status'], 'synthetic')

    def test_json_roundtrip_and_machine_readable_duplicate_key_path(self):
        document = fixture()
        self.assertEqual(parse_document(json.dumps(document)), document)
        text = SAMPLE.read_text().replace('"units": "mm"', '"units": "mm", "units": "m"')
        with self.assertRaises(ArenaValidationError) as caught:
            parse_document(text)
        self.assertEqual(caught.exception.errors[0]['path'], '$.frame.units')
        self.assertEqual(caught.exception.errors[0]['code'], 'duplicate_key')

    def test_duplicate_nested_unknown_key_never_silently_overwrites(self):
        with self.assertRaises(ArenaValidationError) as caught:
            parse_document('{"x":[{"odd.key":1,"odd.key":2}]}')
        self.assertEqual(caught.exception.errors[0]['path'], '$.x[0]["odd.key"]')

    def test_json_syntax_constants_overflow_and_encoding(self):
        for value in ('{', '{} trailing', '{"x":NaN}', '{"x":Infinity}', '{"x":-Infinity}', '{"x":1e999}', '"\ud800"', '{"x":"\\ud800"}', '1' * 5000):
            with self.subTest(value=value[:50]), self.assertRaises(ArenaValidationError):
                parse_document(value)
        with self.assertRaises(ArenaValidationError):
            parse_document(b'{}')

    def test_unknown_fields_rejected_at_every_object_kind(self):
        paths = [(), ('metadata',), ('frame',), ('calibration',), ('sources', 0),
                 ('layers', 0), ('surfaces', 0), ('surfaces', 0, 'height'),
                 ('collision_volumes', 0), ('trigger_volumes', 0), ('transitions', 0)]
        for path in paths:
            with self.subTest(path=path):
                d = fixture()
                target = d
                for step in path:
                    target = target[step]
                target['unexpected'] = 1
                self.reject(d, 'unknown_field')

    def test_all_required_root_fields(self):
        for key in fixture():
            with self.subTest(key=key):
                d = fixture()
                del d[key]
                self.reject(d, 'required', '$.' + key)

    def test_boolean_numbers_and_nonfinite_rejected(self):
        for value in (True, False, float('nan'), float('inf'), -float('inf'), '12', None, 10**400):
            with self.subTest(value=repr(value)[:50]):
                d = fixture()
                d['surfaces'][0]['height']['z_mm'] = value
                self.reject(d, 'number')

    def test_fixed_units_frame_and_scope(self):
        for field, value in (('units', 'm'), ('handedness', 'left'), ('up_axis', '+y')):
            d = fixture(); d['frame'][field] = value
            self.reject(d, 'enum')
        for v in (True, 0, 'false', None):
            d = fixture(); d['metadata']['official_map'] = v
            self.reject(d, 'scope')
        d = fixture(); d['schema'] = 'future-version'
        self.reject(d, 'enum')

    def test_empty_bad_and_unknown_ids(self):
        for value in ('', '../file', 'space here', '0bad', 'x' * 65):
            d = fixture(); d['layers'][0]['id'] = value
            self.reject(d, 'id')
        d = fixture(); d['surfaces'][0]['layer_id'] = 'missing'
        self.reject(d, 'reference')
        d = fixture(); d['surfaces'][0]['source_ids'] = ['missing']
        self.reject(d, 'reference')
        d = fixture(); d['trigger_volumes'][0]['surface_ids'] = ['missing']
        self.reject(d, 'reference')
        d = fixture(); d['transitions'][0]['to_surface'] = 'missing'
        self.reject(d, 'reference')

    def test_global_duplicate_identifiers_and_references(self):
        d = fixture(); d['collision_volumes'][0]['id'] = d['surfaces'][0]['id']
        self.reject(d, 'duplicate_id')
        d = fixture(); d['surfaces'][0]['source_ids'] *= 2
        self.reject(d, 'duplicate_id')
        d = fixture(); d['sources'][0]['sha256'] = 'G' * 64
        self.reject(d, 'digest')

    def test_invalid_polygons(self):
        rings = [
            [[0, 0], [0, 100], [100, 100], [100, 0]],  # clockwise
            [[0, 0], [100, 100], [0, 100], [100, 0]],  # crossing
            [[0, 0], [100, 0], [40, 40], [100, 100], [0, 100]],  # concave
            [[0, 0], [50, 0], [100, 0]],  # degenerate
            [[0, 0], [100, 0], [100, 100], [0, 100], [0, 0]],  # repeated closure
            [[0, 0], [100, 0], [100, 0], [0, 100]],
            [[0, 0], [50, 0], [100, 0], [100, 100], [0, 100]],  # redundant collinear
            [[0, 0], [1e-9, 0], [0, 1e-9]],
        ]
        for ring in rings:
            with self.subTest(ring=ring):
                d = fixture(); d['collision_volumes'][0]['polygon_mm'] = ring
                self.reject(d, 'polygon')

    def test_polygon_vertex_types_limits_and_surface_containment(self):
        d = fixture(); d['boundary_mm'][0][0] = True
        self.reject(d, 'number')
        d = fixture(); d['boundary_mm'] = [[0, 0], [10, 0]]
        self.reject(d, 'limit')
        d = fixture(); d['boundary_mm'] = [[i, i] for i in range(65)]
        self.reject(d, 'limit')
        d = fixture(); d['surfaces'][0]['polygon_mm'][0][0] = -10
        self.reject(d, 'boundary')

    def test_vertical_ranges_and_slope(self):
        for field in ('collision_volumes', 'trigger_volumes'):
            for delta in (0, -1):
                d = fixture(); d[field][0]['z_max_mm'] = d[field][0]['z_min_mm'] + delta
                self.reject(d, 'vertical_range')
        d = fixture(); d['surfaces'][3]['ceiling_z_mm'] = 0
        self.reject(d, 'vertical_range')
        d = fixture(); d['surfaces'][1]['ceiling_z_mm'] = 119
        self.reject(d, 'vertical_range')
        d = fixture(); d['surfaces'][1]['height']['dy'] = 11
        self.reject(d, 'number')
        d = fixture(); d['surfaces'][0]['height']['z_mm'] = 1000000
        d['surfaces'][0]['height']['dx'] = 1
        self.reject(d, 'number')

    def test_strict_types_and_nonempty_references(self):
        d = fixture(); d['surfaces'][0]['traversable'] = 1
        self.reject(d, 'type')
        d = fixture(); d['surfaces'][0]['source_ids'] = []
        self.reject(d, 'limit')
        d = fixture(); d['surfaces'] = []
        self.reject(d, 'limit')
        d = fixture(); d['calibration']['notes'] = ['   ']
        self.reject(d, 'string')

    def test_portals_have_shared_edges_distinct_endpoints_and_consistent_steps(self):
        for a, b in (([2000, 1500], [2000, 1500]), ([2100, 1200], [2100, 2000]), ([2000, 1100], [2000, 2800])):
            d = fixture(); d['transitions'][0]['portal_mm'] = [a, b]
            self.reject(d, 'portal')
        d = fixture(); d['transitions'][0]['to_surface'] = 'west_floor'
        self.reject(d, 'reference')
        d = fixture(); d['surfaces'][3]['height']['z_mm'] = 10
        self.reject(d, 'portal')
        d['transitions'][0]['max_step_mm'] = 10
        d['transitions'][1]['max_step_mm'] = 10
        validate_document(d)

    def test_bounds_cyclic_shared_direct_objects(self):
        d = fixture(); d['metadata']['name'] = 'x' * 4097
        self.reject(d, 'limit')
        d = fixture(); d['extra'] = d
        self.reject(d, 'cycle')
        d = fixture(); d['layers'][1]['source_ids'] = d['layers'][0]['source_ids']
        self.reject(d, 'cycle')
        d = fixture(); d['extra'] = object()
        self.reject(d, 'type')
        d = fixture(); d['x' * 129] = 1
        self.reject(d, 'limit')
        d = fixture(); d['extra'] = [0] * 20001
        self.reject(d, 'limit')
        d = fixture(); d['collision_volumes'] = [copy.deepcopy(d['collision_volumes'][0]) for _ in range(257)]
        self.reject(d, 'limit')
        d = fixture(); deep = 0
        for _ in range(20): deep = [deep]
        d['extra'] = deep
        self.reject(d, 'limit')
        for text in (' ' * (MAX_TEXT_BYTES + 1), '[' * 2000 + '0' + ']' * 2000):
            with self.assertRaises(ArenaValidationError): parse_document(text)


class ArenaQueryTests(unittest.TestCase):
    def probe(self, **kwargs):
        args = dict(x_mm=3000, y_mm=2000, radius_mm=200, height_mm=600)
        args.update(kwargs)
        return query(fixture(), **args)

    def test_stacked_support_is_not_flattened_or_implicitly_selected(self):
        result = self.probe()
        self.assertTrue(result['feasible'])
        self.assertIsNone(result['selected_surface_id'])
        self.assertEqual([c['surface_id'] for c in result['candidates']], ['tunnel_floor', 'bridge_deck'])
        self.assertEqual([c['support_z_mm'] for c in result['candidates']], [0, 800])
        self.assertTrue(all(c['feasible'] for c in result['candidates']))
        self.assertEqual(result['candidates'][0]['trigger_ids'], ['tunnel_test_trigger'])
        self.assertEqual(result['candidates'][1]['trigger_ids'], ['deck_test_trigger'])
        self.assertEqual(result['candidates'][1]['collision_ids'], [])

    def test_tall_robot_blocks_under_bridge_but_not_on_deck(self):
        result = self.probe(height_mm=700)
        self.assertTrue(result['feasible'])  # existential only
        self.assertFalse(result['candidates'][0]['feasible'])
        self.assertTrue(result['candidates'][1]['feasible'])
        self.assertEqual(result['candidates'][0]['collision_ids'], ['bridge_slab'])
        self.assertEqual(codes(result), ['ceiling_clearance', 'collision'])
        self.assertFalse(self.probe(height_mm=700, surface_id='tunnel_floor')['feasible'])
        self.assertTrue(self.probe(height_mm=699.99, surface_id='tunnel_floor')['feasible'])

    def test_clearance_uses_highest_support_under_cylinder(self):
        d = fixture(); d['surfaces'][1]['ceiling_z_mm'] = 500
        result = query(d, 1000, 3400, radius_mm=200, height_mm=419, surface_id='slope_patch')
        c = result['candidates'][0]
        self.assertEqual(c['support_z_mm'], 60)
        self.assertEqual(c['support_range_mm'], [40, 80])
        self.assertEqual(c['clearance_mm'], 420)
        self.assertAlmostEqual(c['slope_deg'], math.degrees(math.atan(.1)))
        self.assertTrue(c['feasible'])
        self.assertFalse(query(d, 1000, 3400, radius_mm=200, height_mm=420, surface_id='slope_patch')['feasible'])

    def test_slope_limits_do_not_claim_robot_mobility(self):
        result = self.probe(x_mm=1000, y_mm=3400, max_slope_deg=5)
        self.assertIn('slope_exceeded', codes(result))
        self.assertFalse(result['feasible'])
        self.assertTrue(self.probe(x_mm=1000, y_mm=3400, max_slope_deg=6)['feasible'])

    def test_full_footprint_single_surface_and_boundary(self):
        result = self.probe(x_mm=50, y_mm=500)
        self.assertIn('footprint_outside_surface', codes(result))
        self.assertIn('footprint_outside_boundary', codes(result))
        self.assertFalse(result['feasible'])
        self.assertTrue(self.probe(x_mm=200, y_mm=500)['feasible'])  # boundary tangency allowed
        self.assertFalse(self.probe(x_mm=-1, y_mm=500)['feasible'])
        self.assertEqual(self.probe(x_mm=-1, y_mm=500)['candidates'], [])
        self.assertEqual(self.probe(surface_id='west_floor')['candidates'], [])

    def test_side_collision_includes_disk_corners_and_tangency(self):
        self.assertFalse(self.probe(x_mm=600, y_mm=900)['feasible'])
        self.assertTrue(self.probe(x_mm=599, y_mm=900)['feasible'])
        self.assertFalse(self.probe(x_mm=700, y_mm=700)['feasible'])
        self.assertTrue(self.probe(x_mm=650, y_mm=650)['feasible'])
        self.assertIn('west_test_column', self.probe(x_mm=900, y_mm=900)['candidates'][0]['collision_ids'])

    def test_collision_is_world_space_not_filtered_by_layer(self):
        d = fixture(); d['collision_volumes'][1]['polygon_mm'] = [[2700,1700],[3300,1700],[3300,2300],[2700,2300]]
        result = query(d, 3000, 2000, radius_mm=200, height_mm=100)
        self.assertFalse(result['feasible'])
        self.assertTrue(all('west_test_column' in c['collision_ids'] for c in result['candidates']))

    def test_trigger_annotations_never_block(self):
        d = fixture(); d['trigger_volumes'][0]['rule_tag'] = 'synthetic_forbidden_example'
        result = query(d, 3000, 2000, radius_mm=100, height_mm=100, surface_id='tunnel_floor')
        self.assertTrue(result['feasible'])
        self.assertEqual(result['candidates'][0]['trigger_ids'], ['tunnel_test_trigger'])

    def test_zero_radius_zero_height_is_a_point_query(self):
        result = query(fixture(), 3000, 2000, surface_id='tunnel_floor')
        self.assertTrue(result['feasible'])
        self.assertEqual(result['robot'], {'radius_mm': 0, 'height_mm': 0, 'max_slope_deg': 90})
        self.assertTrue(result['candidates'][0]['trigger_ids'])

    def test_portal_annotations_do_not_certify_union_footprint_or_reachability(self):
        result = self.probe(x_mm=2000, y_mm=2000)
        self.assertFalse(result['feasible'])
        self.assertIn('west_to_tunnel', result['candidates'][0]['transition_ids'])
        self.assertIn('west_to_tunnel', result['candidates'][1]['transition_ids'])
        self.assertEqual(result['candidates'][2]['transition_ids'], [])  # deck XY overlap not a transition
        d = fixture(); d['transitions'][0]['bidirectional'] = False
        result = query(d, 2000, 2000, surface_id='tunnel_floor')
        self.assertNotIn('west_to_tunnel', result['candidates'][0]['transition_ids'])

    def test_not_traversable(self):
        d = fixture(); d['surfaces'][3]['traversable'] = False
        result = query(d, 3000, 2000, surface_id='tunnel_floor')
        self.assertEqual(codes(result), ['not_traversable'])

    def test_query_input_errors_are_structured_including_huge_integers(self):
        for key in ('x_mm', 'y_mm', 'radius_mm', 'height_mm', 'max_slope_deg'):
            for value in (True, False, None, '0', float('nan'), float('inf'), 10**400, -10**400):
                with self.subTest(key=key, value=repr(value)[:40]), self.assertRaises(ArenaValidationError) as caught:
                    self.probe(**{key:value})
                self.assertEqual(caught.exception.errors[0]['path'], '$.probe.' + key)
        for key in ('radius_mm', 'height_mm', 'max_slope_deg'):
            with self.assertRaises(ArenaValidationError): self.probe(**{key:-1})
        for surface in ('missing', True, ''):
            with self.assertRaises(ArenaValidationError): self.probe(surface_id=surface)

    def test_document_revalidated_and_queries_deterministic_read_only(self):
        d = fixture(); before = copy.deepcopy(d)
        first = query(d, 3000, 2000, radius_mm=200, height_mm=600)
        for _ in range(5): self.assertEqual(query(d, 3000, 2000, radius_mm=200, height_mm=600), first)
        self.assertEqual(d, before)
        json.dumps(first, allow_nan=False)
        d['surfaces'][0]['height']['dx'] = True
        with self.assertRaises(ArenaValidationError): query(d, 3000, 2000)


class ArenaCLITests(unittest.TestCase):
    def run_cli(self, *args):
        return subprocess.run([sys.executable, '-m', 'arena_format', *args], cwd=ROOT, capture_output=True, text=True, timeout=10)

    def test_valid_cli(self):
        run = self.run_cli('validate', str(SAMPLE))
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertTrue(json.loads(run.stdout)['valid'])
        run = self.run_cli('query', str(SAMPLE), '--x-mm', '3000', '--y-mm', '2000', '--height-mm', '700', '--surface-id', 'tunnel_floor')
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertFalse(json.loads(run.stdout)['feasible'])

    def test_invalid_cli_json_file_encoding_and_limit(self):
        with tempfile.TemporaryDirectory() as temp:
            p = Path(temp) / 'bad.json'
            for content in (b'{"a":1,"a":2}', b'\xff', b' ' * (MAX_TEXT_BYTES+1)):
                p.write_bytes(content)
                run = self.run_cli('validate', str(p))
                self.assertEqual(run.returncode, 2, run.stderr)
                self.assertFalse(json.loads(run.stdout)['valid'])
        run = self.run_cli('validate', '/this/path/does/not/exist.json')
        self.assertEqual(run.returncode, 2)
        self.assertEqual(json.loads(run.stdout)['errors'][0]['code'], 'file')
        run = self.run_cli('query', str(SAMPLE), '--x-mm', 'nan', '--y-mm', '2000')
        self.assertEqual(run.returncode, 2)
        self.assertEqual(json.loads(run.stdout)['errors'][0]['code'], 'number')

    def test_checked_in_invalid_examples(self):
        paths = sorted((SAMPLE.parent / 'invalid').glob('*.json'))
        self.assertEqual(len(paths), 3)
        for p in paths:
            with self.subTest(path=p.name), self.assertRaises(ArenaValidationError):
                parse_document(p.read_text())


if __name__ == '__main__':
    unittest.main()
