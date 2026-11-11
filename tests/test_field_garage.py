#!/usr/bin/env python3
"""Vehicle configuration and garage: built-in vehicles, validation by the engine,
saving, persistence across restarts, selection for a run and behaviour changes."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from field_sim import bridge, garage  # noqa: E402


class GarageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.sim = self.open()

    def open(self):
        sim = bridge.FieldSim(garage_dir=self.tmp.name)
        self.addCleanup(sim.close)
        return sim

    def edited(self, base='builtin:infantry_mecanum', **chassis):
        vehicle = self.sim.garage.get(base)['vehicle']
        vehicle['chassis'].update(chassis)
        return vehicle

    def test_builtin_vehicles(self):
        listing = self.sim.garage.list()
        self.assertEqual(listing['default'], 'builtin:infantry_mecanum')
        ids = [v['id'] for v in listing['vehicles']]
        self.assertEqual(ids, ['builtin:heavy_default', 'builtin:infantry_balance', 'builtin:infantry_climber', 'builtin:infantry_low',
                               'builtin:infantry_mecanum', 'builtin:infantry_omni', 'builtin:sentry_auto'])
        self.assertTrue(all(v['readonly'] for v in listing['vehicles']))
        self.assertEqual({v['class'] for v in listing['vehicles']}, {'infantry', 'heavy', 'sentry'})
        names = [v['name'] for v in listing['vehicles']]
        self.assertEqual(len(set(names)), len(names))
        for vid in ids:
            entry = self.sim.garage.get(vid)
            on_disk = json.loads((ROOT / 'field_sim/vehicles' / f'{vid.split(":")[1]}.json').read_text(encoding='utf-8'))
            self.assertEqual(entry['vehicle'], on_disk, vid)            # checked-in files are already normalised
            self.assertEqual(entry['hash'], garage.vehicle_hash(on_disk))
        # rule attributes come from the rule tables (3-6, 5-16..5-18), not from the files
        attrs = {v['id']: v['attributes'] for v in listing['vehicles']}
        self.assertEqual((attrs['builtin:infantry_mecanum']['max_hp'], attrs['builtin:infantry_mecanum']['chassis_power_limit_w']), (200, 45))
        self.assertEqual((attrs['builtin:sentry_auto']['max_hp'], attrs['builtin:sentry_auto']['initial_allowance']), (400, 300))
        self.assertEqual((attrs['builtin:heavy_default']['max_hp'], attrs['builtin:heavy_default']['muzzle_speed_limit_mps']), (600, 16.5))

    def test_schema_and_labels(self):
        meta = self.sim.vehicle_schema()
        fields = meta['schema']['fields']
        self.assertEqual(len(fields), 60)
        paths = [f"{f['section']}.{f['key']}" if f['section'] else f['key'] for f in fields]
        self.assertEqual(sorted(paths), sorted(meta['labels']['fields']))                  # every parameter has a label, none is stale
        self.assertTrue({f['section'] for f in fields} | {'armor'} <= set(meta['labels']['sections']))
        for f in fields:
            if f['type'] == 'enum':
                self.assertTrue(set(f['choices']) <= set(meta['labels']['choices']), f)
        self.assertEqual(meta['schema']['default']['format'], 'rmuc-vehicle.v1')

    def test_save_get_overwrite_delete(self):
        vehicle = self.edited(max_speed_mps=2.0, max_lateral_speed_mps=2.0)
        vehicle['name'] = '试验车'
        saved = self.sim.garage.save(vehicle, None)
        self.assertRegex(saved['id'], r'^garage:[0-9a-f]{12}$')
        self.assertFalse(saved['readonly'])
        self.assertEqual(saved['vehicle'], vehicle)
        self.assertEqual(self.sim.garage.get(saved['id'])['vehicle'], vehicle)
        self.assertIn(saved['id'], [v['id'] for v in self.sim.garage.list()['vehicles']])
        vehicle['chassis']['mass_kg'] = 25
        again = self.sim.garage.save(vehicle, saved['id'])
        self.assertEqual(again['id'], saved['id'])
        self.assertNotEqual(again['hash'], saved['hash'])
        self.assertEqual(self.sim.garage.get(saved['id'])['vehicle']['chassis']['mass_kg'], 25)
        self.assertEqual(len(list(Path(self.tmp.name).glob('*.json'))), 1)
        self.assertEqual(self.sim.garage.delete(saved['id']), {'deleted': saved['id']})
        with self.assertRaises(garage.GarageError):
            self.sim.garage.get(saved['id'])
        self.assertEqual(list(Path(self.tmp.name).glob('*')), [])

    def test_saved_vehicle_survives_a_restart_and_can_be_selected(self):
        vehicle = self.edited(max_speed_mps=1.0, max_lateral_speed_mps=1.0)
        vehicle['name'] = '慢车'
        vehicle['rules']['chassis_type'] = 'power_first'
        saved = self.sim.garage.save(vehicle, None)
        self.sim.close()
        sim = self.open()                                          # a new server process would do the same
        self.assertEqual(sim.garage.get(saved['id'])['vehicle'], vehicle)
        state = sim.reset({'robots': [{'id': 'slow', 'team': 'red', 'vehicle': saved['id']}, {'id': 'std', 'team': 'red'}]})
        session = sim.meta()['session']['robots']
        self.assertEqual(session[0], {'id': 'slow', 'team': 'red', 'vehicle': saved['id'], 'vehicle_name': '慢车', 'vehicle_hash': saved['hash']})
        self.assertEqual(session[1]['vehicle'], 'builtin:infantry_mecanum')
        self.assertEqual(state['robots'][0]['vehicle'], '慢车')
        self.assertEqual(state['robots'][0]['attributes']['chassis_power_limit_w'], 60)
        self.assertEqual(state['robots'][1]['attributes']['chassis_power_limit_w'], 45)
        drive = {'chassis': {'vx': 5.0}}
        state = sim.step(200, {'slow': drive, 'std': drive})
        slow, std = state['robots']
        self.assertAlmostEqual(slow['vx'], 1.0, places=6)          # the saved top speed
        self.assertGreater(std['vx'], 2.0)
        self.assertGreater(std['odometer'], slow['odometer'] + 1.0)

    def test_parameters_change_behaviour(self):
        climber, low, tall = 'builtin:infantry_climber', 'builtin:infantry_low', 'builtin:infantry_mecanum'
        self.assertEqual(self.sim.garage.get(climber)['vehicle']['chassis']['max_step_mm'], 220)
        self.assertEqual(self.sim.garage.get(low)['vehicle']['chassis']['height_mm'], 240)
        # (step height and body height against real terrain are covered by the native tests)
        # gimbal travel: the low vehicle's pitch stop is 25 degrees, the standard one's 35
        self.sim.reset({'robots': [{'id': 'R1', 'team': 'red', 'vehicle': low}]})
        state = self.sim.step(150, {'R1': {'gimbal': {'mode': 'angle', 'yaw': 0.0, 'pitch': 1.0}}})
        self.assertAlmostEqual(state['robots'][0]['gimbal']['pitch'], 25 * 3.141592653589793 / 180, places=3)   # its pitch_max_deg is 25
        self.sim.reset({'robots': [{'id': 'R1', 'team': 'red', 'vehicle': tall}]})
        state = self.sim.step(150, {'R1': {'gimbal': {'mode': 'angle', 'yaw': 0.0, 'pitch': 1.0}}})
        self.assertAlmostEqual(state['robots'][0]['gimbal']['pitch'], 35 * 3.141592653589793 / 180, places=3)
        # differential drive ignores a sideways command
        self.sim.reset({'robots': [{'id': 'R1', 'team': 'red', 'vehicle': 'builtin:infantry_balance'}, {'id': 'R2', 'team': 'red', 'vehicle': 'builtin:infantry_omni'}]})
        state = self.sim.step(100, {'R1': {'chassis': {'vy': 2.0}}, 'R2': {'chassis': {'vy': 2.0}}})
        self.assertEqual(state['robots'][0]['odometer'], 0)
        self.assertGreater(state['robots'][1]['odometer'], 0.5)

    def test_rejections(self):
        g = self.sim.garage
        good = self.edited()
        good['name'] = '可用'
        for change, fragment in [({'max_speed_mps': 99}, 'chassis.max_speed_mps'), ({'wheelbase_mm': 900}, 'wheelbase'),
                                 ({'drive': 'hover'}, 'chassis.drive'), ({'colour': 'red'}, 'unknown key')]:
            bad = self.edited(**change)
            with self.assertRaises(garage.GarageError) as ctx:
                g.save(bad, None)
            self.assertIn(fragment, str(ctx.exception))
        for bad in (None, [], 'x', {}, {'format': 'rmuc-vehicle.v1'}, {**good, 'format': 'other'}, {**good, 'name': ''}, {**good, 'extra': 1}):
            with self.assertRaises(garage.GarageError):
                g.save(bad, None)
        for vid in ('builtin:infantry_mecanum', 'garage:zzz', 'garage:000000000000', '../x', 5):
            with self.assertRaises(garage.GarageError):
                g.save(good, vid)                                   # built-ins are read-only; unknown ids are refused
            with self.assertRaises(garage.GarageError):
                g.delete(vid)
        for vid in ('builtin:nope', 'garage:000000000000', '', None, 'builtin:../../server'):
            with self.assertRaises(garage.GarageError):
                g.get(vid)
        self.assertEqual(list(Path(self.tmp.name).glob('*')), [])  # nothing was written by any of the above
        first = g.save(good, None)
        with self.assertRaises(garage.GarageError):
            g.save(good, None)                                      # same name again
        builtin_name = self.edited()
        with self.assertRaises(garage.GarageError):
            g.save(builtin_name, None)                              # the name of a built-in vehicle
        with self.assertRaises(bridge.FieldError):
            self.sim.reset({'robots': [{'id': 'a', 'team': 'red', 'vehicle': 'garage:000000000000'}]})
        self.assertEqual([r['id'] for r in self.sim.state()['robots']], ['R1', 'R2', 'B1', 'B2'])   # run unchanged
        g.delete(first['id'])

    def test_damaged_files_are_reported_not_loaded(self):
        g = self.sim.garage
        good = self.edited()
        good['name'] = '完好'
        saved = g.save(good, None)
        directory = Path(self.tmp.name)
        (directory / 'aaaaaaaaaaaa.json').write_text('{not json', encoding='utf-8')
        (directory / 'bbbbbbbbbbbb.json').write_text(json.dumps({'entry_format': 'rmuc-garage-entry.v1', 'vehicle': {**good, 'name': '坏', 'chassis': {'mass_kg': 9999}}}), encoding='utf-8')
        (directory / 'cccccccccccc.json').write_text(json.dumps(good), encoding='utf-8')          # a bare vehicle, not an entry
        (directory / 'notes.txt').write_text('ignored', encoding='utf-8')
        (directory / 'UPPER.json').write_text('{}', encoding='utf-8')
        listing = {v['id']: v for v in g.list()['vehicles']}
        self.assertIn('name', listing[saved['id']])
        for broken in ('garage:aaaaaaaaaaaa', 'garage:bbbbbbbbbbbb', 'garage:cccccccccccc'):
            self.assertIn('invalid', listing[broken])
            with self.assertRaises(garage.GarageError):
                g.get(broken)
        self.assertIn('chassis.mass_kg', listing['garage:bbbbbbbbbbbb']['invalid'])
        self.assertNotIn('garage:UPPER', listing)
        with self.assertRaises(bridge.FieldError):
            self.sim.reset({'robots': [{'id': 'a', 'team': 'red', 'vehicle': 'garage:bbbbbbbbbbbb'}]})
        self.assertEqual(g.delete('garage:aaaaaaaaaaaa'), {'deleted': 'garage:aaaaaaaaaaaa'})       # broken files can be removed

    def test_check_normalises_and_fills_defaults(self):
        out = self.sim.garage.check({'format': 'rmuc-vehicle.v1', 'name': '只有名字'})
        default = self.sim.vehicle_schema()['schema']['default']
        self.assertEqual({**out['vehicle'], 'name': default['name']}, default)
        self.assertEqual([row['level'] for row in out['attributes']], [1, 5, 10])
        sentry = self.sim.garage.check({'format': 'rmuc-vehicle.v1', 'name': 's', 'class': 'sentry', 'rules': {'sentry_mode': 'semi'}})
        self.assertEqual([(r['level'], r['max_hp'], r['chassis_power_limit_w']) for r in sentry['attributes']], [(1, 200, 60)])
        self.assertEqual(out['hash'], garage.vehicle_hash(out['vehicle']))
        self.assertNotEqual(out['hash'], sentry['hash'])


if __name__ == '__main__':
    unittest.main()
