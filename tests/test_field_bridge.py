#!/usr/bin/env python3
"""field_sim bridge: session validation, candidate reset, engine errors, field data."""
import hashlib
import json
import math
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from field_sim import bridge  # noqa: E402


class FieldData(unittest.TestCase):
    def test_field_document_matches_terrain(self):
        doc = bridge.field_document()
        raw = bridge.terrain_bytes()
        self.assertEqual(doc['format'], 'rmuc-field.v1')
        self.assertEqual(doc['size_mm'], [28000, 15000])
        self.assertEqual(hashlib.sha256(raw).hexdigest(), doc['terrain']['sha256'])
        self.assertEqual(raw[:8], b'RMTERRN1')
        nx, ny, cell, count, _ = struct.unpack_from('<5i', raw, 8)
        self.assertEqual([nx, ny, cell], [1400, 750, 20])
        self.assertEqual(count, doc['terrain']['overhang_cells'])
        self.assertEqual(len(raw), 28 + nx * ny * 2 + count * 8)

    def test_zones_are_point_symmetric_and_unique(self):
        doc = bridge.field_document()
        ids = [z['id'] for z in doc['zones']]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual(len(ids), 38)
        polys = {z['id']: z['polygon_mm'] for z in doc['zones']}
        for zid, poly in polys.items():
            partner = (zid.replace('red_', 'blue_') if zid.startswith('red_') else zid.replace('blue_', 'red_') if zid.startswith('blue_')
                       else zid.replace('_west', '_east') if zid.endswith('_west') else zid.replace('_east', '_west'))
            self.assertNotEqual(partner, zid)
            self.assertEqual(polys[partner], [[28000 - x, 15000 - y] for x, y in poly], zid)
        kinds = {z['kind'] for z in doc['zones']}
        self.assertEqual(kinds, {'supply', 'high_point', 'base', 'fortress', 'outpost', 'assembly', 'central_highland',
                                 'crossing_highland_low', 'crossing_road_low', 'crossing_road_high', 'tunnel_end', 'tunnel_inside'})
        # §5.6.4.5: nine tunnel crossing cards on each side (three tunnels x two ends and an interior)
        self.assertEqual(sum(z['kind'].startswith('tunnel') and z['id'].endswith('west') for z in doc['zones']), 9)

    def test_provenance_names_the_official_model(self):
        prov = json.loads((bridge.FIELD_DIR / 'provenance.json').read_text(encoding='utf-8'))
        self.assertEqual(prov['step_model']['md5'], '3d3d6a411f44e9a4983794261b3f0bf7')
        self.assertEqual(prov['step_model']['bytes'], 653443279)
        self.assertEqual(prov['terrain_sha256'], bridge.field_document()['terrain']['sha256'])
        self.assertEqual(prov['manual']['sha256'], '9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be')


class Session(unittest.TestCase):
    def setUp(self):
        self.sim = bridge.FieldSim()
        self.addCleanup(self.sim.close)

    def test_validate_session(self):
        ok = {'robots': [{'id': 'a', 'team': 'red'}]}
        self.assertEqual(bridge.validate_session(ok), ok)
        bad = [None, {}, {'robots': []}, {'robots': [{'id': 'a'}]}, {'robots': [{'id': 'a', 'team': 'green'}]},
               {'robots': [{'id': 'a b', 'team': 'red'}]}, {'robots': [{'id': 'a', 'team': 'red'}, {'id': 'a', 'team': 'blue'}]},
               {'robots': [{'id': f'r{i}', 'team': 'red'} for i in range(5)]}, {'robots': [{'id': 1, 'team': 'red'}]},
               {'robots': [{'id': 'a', 'team': 'red', 'x': 1}]}, {'robots': ok['robots'], 'extra': 1}]
        for case in bad:
            with self.assertRaises(bridge.FieldError, msg=case):
                bridge.validate_session(case)

    def test_meta_and_default_run(self):
        meta = self.sim.meta()
        self.assertEqual(meta['profile'], 'rmuc2027-field-sim-v1')
        self.assertEqual(meta['build'], bridge.build_id())
        self.assertEqual(meta['tick_hz'], 100)
        state = self.sim.state()
        self.assertEqual([r['id'] for r in state['robots']], ['R1', 'R2', 'B1', 'B2'])
        self.assertEqual(state['tick'], 0)
        for robot in state['robots']:
            self.assertEqual(robot['zones'], ['red_base' if robot['team'] == 'red' else 'blue_base'])

    def test_step_drives_and_is_deterministic(self):
        commands = {'R1': {'chassis': {'vx': 2.0, 'vy': 0.5, 'wz': 1.0}, 'gimbal': {'mode': 'angle', 'yaw': 1.0, 'pitch': 0.1}}}
        first = self.sim.step(150, commands)
        self.assertEqual(first['tick'], 150)
        r1 = first['robots'][0]
        self.assertGreater(math.hypot(r1['x'] - 3.098, r1['y'] - 8.75), 1.0)
        self.assertAlmostEqual(r1['gimbal']['yaw'], 1.0, places=3)
        self.assertEqual(first['robots'][2]['x'], 24.902)       # nobody else moved
        self.sim.reset(bridge.DEFAULT_SESSION)
        self.assertEqual(self.sim.step(150, commands), first)
        # the same motion in two requests gives the same state: commands persist
        self.sim.reset(bridge.DEFAULT_SESSION)
        self.sim.step(70, commands)
        self.assertEqual(self.sim.step(80, {}), first)

    def test_rejections_leave_the_run_unchanged(self):
        self.sim.step(10, {'R1': {'chassis': {'vx': 1.0}}})
        before = self.sim.state()
        cases = [(0, {}), (201, {}), (True, {}), (1.5, {}), (1, []), (1, {'nobody': {}}), (1, {'R1': {'chassis': {'speed': 1}}}),
                 (1, {'R1': {'chassis': {'vx': 1e9}}}), (1, {'R1': {'gimbal': {'yaw': 0.1}}}), (1, {'R1': {'gimbal': {'mode': 'spin'}}}),
                 (1, {'R1': {'chassis': {'vx': 'fast'}}}), (1, {'R1': 5})]
        for ticks, commands in cases:
            with self.assertRaises(bridge.FieldError, msg=(ticks, commands)):
                self.sim.step(ticks, commands)
        self.assertEqual(self.sim.state(), before)
        with self.assertRaises(bridge.FieldError):
            self.sim.reset({'robots': []})
        self.assertEqual(self.sim.state(), before)              # a rejected reset keeps the current run

    def test_probe(self):
        high = self.sim.probe(1.4, 12.2)
        self.assertTrue(high['inside'])
        self.assertAlmostEqual(high['ground'] - high['floor'], 0.8, delta=0.003)
        self.assertEqual(high['zones'], ['red_high_point'])
        tunnel = self.sim.probe(14.0, 0.6)
        self.assertAlmostEqual(tunnel['overhang_bottom'] - tunnel['ground'], 0.25, delta=0.003)
        self.assertFalse(self.sim.probe(30.0, 1.0)['inside'])
        for bad in [('a', 1), (float('nan'), 1), (True, 1), (1e6, 0)]:
            with self.assertRaises(bridge.FieldError):
                self.sim.probe(*bad)

    def test_stale_binary_is_refused(self):
        real = bridge.build_id
        bridge.build_id = lambda: 'not-the-binary'
        try:
            with self.assertRaises(RuntimeError):
                bridge.FieldSim()
        finally:
            bridge.build_id = real


if __name__ == '__main__':
    unittest.main()
