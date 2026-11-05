#!/usr/bin/env python3
"""Bounded native-profile stress and raw protocol rejection regression tests."""
import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import time
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from layered_mode.bridge import LayeredProcess, load_command, sample
from test_layered_review import arena, config, rectangle, surface, volume, portal


class ProfileLimits(unittest.TestCase):
    def test_supported_complexity_and_maximum_step_batch(self):
        document = arena()
        document['boundary_mm'] = rectangle(0, 0, 90000, 90000)
        document['surfaces'] = [surface('floor', rectangle(0, 0, 45000, 90000)),
                                surface('east', rectangle(45000, 0, 90000, 90000))]
        document['surfaces'] += [surface('stack' + str(i), copy.deepcopy(document['boundary_mm']), z=1000*i)
                                 for i in range(1, 31)]
        document['transitions'] = [portal('portal' + str(i), 'floor', 'east', 45000, 0, 90000) for i in range(64)]
        def polygon(cx, cy):
            return [[round(cx + 1000*math.cos(i*math.pi/8)),
                     round(cy + 1000*math.sin(i*math.pi/8))] for i in range(16)]
        document['collision_volumes'] = [volume('solid' + str(i), polygon(40000+i*100, 80000)) for i in range(64)]
        document['trigger_volumes'] = [volume('trigger' + str(i), polygon(40000+i*100, 80000), scoped=['floor']) for i in range(32)]
        engine = LayeredProcess()
        self.addCleanup(engine.close)
        engine.reset({'map': document, 'config': config(x=1000, y=1000)})
        started = time.monotonic()
        result = engine.step({'count': 600, 'action': [1000, 0]})
        elapsed = time.monotonic() - started
        self.assertEqual(result['tick'], 600)
        self.assertEqual(result['robot']['x_q1024'], 31000*1024)
        self.assertEqual(result['robot']['surface_id'], 'floor')
        self.assertEqual(len(engine.replay()['frames']), 600)
        print(f'\nNative maximum-count/shape stress: {elapsed:.3f}s for 600 ticks (informational, not a timing assertion).')
        before = engine.state()
        bad = copy.deepcopy(document)
        bad['collision_volumes'].append(volume('extra_solid', rectangle(80000, 0, 81000, 1000)))
        with self.assertRaises(ValueError):
            engine.reset({'map': bad, 'config': config()})
        self.assertEqual(engine.state(), before)

    def test_raw_cli_rejects_oversize_trailing_and_invalid_frames(self):
        builtin = sample()
        request = load_command(builtin['map'], builtin['config'])
        protocol = '\n'.join([request, 'STATE', 'x'*300001, 'STATE', 'ADVANCE 1 1001 0',
                              'STATE', 'FRAME 1 0 0', 'STATE', request+' trailing', 'STATE',
                              'ADVANCE 1 0 0', 'STATE']) + '\n'
        result = subprocess.run([str(ROOT/'build/rmuc_layered')], input=protocol, text=True,
                                capture_output=True, timeout=20, check=True)
        rows = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(rows), 12)
        initial = rows[0]
        for index in (1, 3, 5, 7, 9):
            self.assertEqual(rows[index], initial)
        for index in (2, 4, 6, 8):
            self.assertIn('error', rows[index])
        self.assertEqual(rows[10]['state'], rows[11])
        self.assertEqual(rows[11]['tick'], 1)


if __name__ == '__main__':
    unittest.main()
