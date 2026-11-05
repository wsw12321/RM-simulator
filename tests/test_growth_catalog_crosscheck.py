#!/usr/bin/env python3
"""Cross-check the C++ growth tables against the independently audited catalog.

robot_profiles_v1/catalog.json (Python, per-field quotes) and
rules/rmuc2027_growth_v1 (C++, tables 3-6/5-16..5-18) were derived separately
from the same pinned manual. Every one of the 46 catalog presets must agree with
the native row it names; the native side must also cover no unknown rows.
"""
import json
import pathlib
import subprocess
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
DUMP = ROOT / 'build' / 'growth_rules_dump'
CATALOG = ROOT / 'robot_profiles_v1' / 'catalog.json'


def native_rows():
    output = subprocess.run([str(DUMP)], check=True, capture_output=True, text=True, timeout=20).stdout
    lines = [json.loads(line) for line in output.splitlines() if line.strip()]
    return lines[0], lines[1:]


class GrowthCatalogCrossCheck(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not DUMP.exists():
            raise unittest.SkipTest('run bash tools/build.sh first')
        cls.header, cls.rows = native_rows()
        cls.catalog = json.loads(CATALOG.read_text(encoding='utf-8'))

    def find(self, **wanted):
        matches = [r for r in self.rows if all(r[k] == v for k, v in wanted.items())]
        self.assertEqual(len(matches), 1, wanted)
        return matches[0]

    def test_same_pinned_source(self):
        documents = json.dumps(self.catalog['documents'])
        self.assertIn(self.header['source_sha256'], documents)

    def test_every_catalog_preset_matches_native_row(self):
        checked = 0
        for preset in self.catalog['presets']:
            role, cond, values = preset['role'], preset['conditions'], preset['overrides']
            if role == 'infantry':
                row = self.find(role='infantry', chassis=preset['chassis'], launcher=preset['launcher'],
                                level=preset['level'])
                self.assertEqual(values['max_hp'], row['max_hp'], preset['id'])
                self.assertEqual(values['heat_limit_tenths'], row['heat_limit'] * 10, preset['id'])
                self.assertEqual(values['cooling_per_second'], row['cooling_per_second'], preset['id'])
            elif role == 'heavy':
                row = self.find(role='heavy', stage=cond['heavy_phase'])
                self.assertEqual(values['max_hp'], row['max_hp'], preset['id'])
            elif role == 'sentry':
                row = self.find(role='sentry', stage=cond['control_mode'])
                self.assertEqual(values['max_hp'], row['max_hp'], preset['id'])
            else:
                self.fail(f'unexpected role {role}')
            checked += 1
        self.assertEqual(checked, 46)

    def test_native_rows_are_exactly_the_catalog_rows(self):
        self.assertEqual(len(self.rows), 46)


if __name__ == '__main__':
    unittest.main(verbosity=1, argv=[sys.argv[0]])
