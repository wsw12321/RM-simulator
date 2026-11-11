#!/usr/bin/env python3
"""Step 3 of the field extraction: zones, structures and checks -> field.json.

    python3 tools/field_extract/build_field_json.py cache_dir field_sim/field/rmuc2027_v1

Zone outlines are the red-side buff-point areas of manual figure 5-23 (printed
p102), traced from the figure image at 25 mm per pixel with trace_zones.py and
entered below; blue zones are their point reflection (manual §4.1: the field is
centrally symmetric). Tunnel-interior zones are the roofed strips between the
two end cards, read from the extracted overhang layer. Structure positions are
measured on the STEP model. Spawn poses are an ENGINEERING choice inside the
start zone; the manual only says robots start there.
"""
import hashlib
import json
import pathlib
import struct
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from build_terrain import CELL_MM, LENGTH_MM, NX, NY, WIDTH_MM, MAGIC, fit_frame, floor_z, to_field  # noqa: E402

FIG = 'RMUC 2027 manual V1.0.0 figure 5-23 (printed p102)'
# kind, team ('red' | 'neutral'), layer, polygon (mm). layer 'under' = only while below an overhang.
RED_SIDE_ZONES = [
    ('supply', 'supply', 'red', 'any', [[0, 3162], [0, 2362], [725, 2362], [912, 2162], [912, 0], [3509, 0], [3509, 2675], [2672, 3162]]),
    ('high_point', 'high_point', 'red', 'any',
     [[787, 13162], [412, 12788], [412, 11550], [812, 11162], [2036, 11162], [2410, 11538], [2410, 12788], [2036, 13162]]),
    ('base', 'base', 'red', 'any', [[2224, 9438], [850, 8638], [850, 6312], [2248, 5538], [4209, 6662], [4209, 8288]]),
    ('fortress', 'fortress', 'red', 'any', [[5957, 8538], [5458, 7525], [5957, 6462], [7194, 6450], [7706, 7500], [7206, 8525]]),
    ('outpost', 'outpost', 'red', 'any',
     [[10503, 2712], [11252, 2712], [11777, 3238], [11777, 4038], [11477, 4688], [10603, 4662], [10828, 4062], [11028, 4038],
      [11252, 3812], [11252, 3488], [11003, 3238], [10853, 3238]]),
    ('assembly', 'assembly', 'red', 'any', [[14325, 8588], [13176, 8912], [12127, 8038], [12127, 7688], [12851, 7112]]),
    # Neutral cards on the red half of the field.
    ('central_highland_west', 'central_highland', 'neutral', 'any',
     [[10603, 4662], [10353, 5188], [10353, 9362], [12376, 12262], [12551, 12338], [12576, 11212], [11128, 9138], [11128, 5462], [11477, 4688]]),
    ('highland_step_low_west', 'crossing_highland_low', 'neutral', 'any',
     [[8580, 8888], [8355, 8662], [8355, 6288], [8555, 6088], [9179, 6088], [9179, 8888]]),
    ('road_step_a_west', 'crossing_road', 'neutral', 'any', [[7756, 2738], [7756, 2188], [8730, 2188], [8730, 2738]]),
    ('road_step_b_west', 'crossing_road', 'neutral', 'any', [[7756, 1938], [7756, 1362], [8730, 1362], [8730, 1938]]),
    ('road_tunnel_end_a_west', 'tunnel_end', 'neutral', 'any', [[8905, 3262], [8905, 2488], [9729, 2488], [9729, 3262]]),
    ('road_tunnel_inside_west', 'tunnel_inside', 'neutral', 'under', [[8905, 2488], [8905, 1938], [9729, 1938], [9729, 2488]]),
    ('road_tunnel_end_b_west', 'tunnel_end', 'neutral', 'any', [[8905, 1938], [8905, 1262], [9729, 1262], [9729, 1938]]),
    ('far_tunnel_end_a_west', 'tunnel_end', 'neutral', 'any', [[12976, 938], [12976, 238], [13800, 238], [13800, 938]]),
    ('far_tunnel_inside_west', 'tunnel_inside', 'neutral', 'under', [[13800, 938], [13800, 238], [14150, 238], [14150, 938]]),
    ('far_tunnel_end_b_west', 'tunnel_end', 'neutral', 'any', [[14150, 938], [14150, 238], [14974, 238], [14974, 938]]),
    ('near_tunnel_end_a_west', 'tunnel_end', 'neutral', 'any', [[13925, 1888], [13925, 1188], [14749, 1188], [14749, 1888]]),
    ('near_tunnel_inside_west', 'tunnel_inside', 'neutral', 'under', [[14749, 1888], [14749, 1188], [15099, 1188], [15099, 1888]]),
    ('near_tunnel_end_b_west', 'tunnel_end', 'neutral', 'any', [[15099, 1888], [15099, 1188], [15923, 1188], [15923, 1888]]),
]
# STEP leaves measured for structure positions: (kind, path fragment).
STRUCTURES = [('base', '基地_26_0825'), ('outpost', '新前哨战'), ('energy_mechanism', '能量机关'), ('dart_station', '飞镖发射井/')]
# Manual dimensions checked against the model: (label, source, expected mm, measurement key).
SPAWN_RING_MM = 1250  # ENGINEERING: distance of the four start poses from the base centre


def mirror(poly):
    return [[LENGTH_MM - x, WIDTH_MM - y] for x, y in poly]


def load_terrain(path):
    raw = path.read_bytes()
    assert raw[:8] == MAGIC
    nx, ny, cell, count, _ = struct.unpack_from('<5i', raw, 8)
    ground = np.frombuffer(raw, '<i2', nx * ny, 28).reshape(ny, nx).astype(np.float64)
    off = 28 + nx * ny * 2
    idx = np.frombuffer(raw, '<u4', count, off)
    ob = np.frombuffer(raw, '<i2', count, off + 4 * count)
    ot = np.frombuffer(raw, '<i2', count, off + 6 * count)
    return ground, idx, ob, ot


def inside(poly, x, y):
    poly = np.asarray(poly, dtype=np.float64)
    x0, y0 = poly[:, 0], poly[:, 1]
    x1, y1 = np.roll(x0, -1), np.roll(y0, -1)
    hit = np.zeros(np.shape(x), dtype=bool)
    for a, b, c, d in zip(x0, y0, x1, y1):
        cond = ((b > y) != (d > y)) & (x < (c - a) * (y - b) / (d - b + 1e-30) + a)
        hit ^= cond
    return hit


def main():
    cache, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    meta = json.loads((cache / 'mesh.json').read_text(encoding='utf-8'))
    arrays = np.load(cache / 'mesh.npz')
    frame = fit_frame(meta['leaves'], arrays)
    ground, over_idx, over_b, over_t = load_terrain(out / 'terrain.bin')
    rel = ground - floor_z((np.arange(NY) + 0.5) * CELL_MM)[:, None]
    gx, gy = np.meshgrid((np.arange(NX) + 0.5) * CELL_MM, (np.arange(NY) + 0.5) * CELL_MM)

    # ---- structures, measured on the model
    boxes = {}
    for leaf in meta['leaves']:
        name = '/'.join(leaf['path'])
        for kind, fragment in STRUCTURES:
            if fragment in name and leaf['triangles']:
                p = to_field(arrays[f"t{leaf['leaf']}"], frame).reshape(-1, 3)
                team = 'red' if p[:, 0].mean() < LENGTH_MM / 2 else 'blue'
                key = (kind, team) if kind != 'energy_mechanism' else (kind, 'neutral')
                lo, hi = p.min(0), p.max(0)
                if key in boxes:
                    lo, hi = np.minimum(lo, boxes[key][0]), np.maximum(hi, boxes[key][1])
                boxes[key] = (lo, hi)
    structures = []
    for (kind, team), (lo, hi) in sorted(boxes.items()):
        c = (lo + hi) / 2
        if kind in ('base', 'outpost'):   # the armour modules are arranged around the vertical axis
            c[:2] = np.rint(c[:2])
        structures.append({'id': f'{team}_{kind}' if team != 'neutral' else kind, 'kind': kind, 'team': team,
                           'center_mm': [int(round(c[0])), int(round(c[1]))],
                           'bbox_mm': [[int(round(v)) for v in lo], [int(round(v)) for v in hi]], 'source': 'STEP model'})
    # Bases and outposts: the manual dimensions the vertical axis (figure 4-5: 25589 / 17024 from the
    # red baseline, 7500 / 3600 from the long side). The model agrees within the stated tolerance;
    # the measured axis (centroid of the tallest tenth of the body) is kept for the report.
    top = np.full(NX * NY, np.nan)
    top[over_idx] = over_t
    top = top.reshape(NY, NX) - floor_z((np.arange(NY) + 0.5) * CELL_MM)[:, None]
    manual_axis = {'base': [LENGTH_MM - 25589, 7500], 'outpost': [LENGTH_MM - 17024, 3600]}
    axis_report = {}
    for kind, (ax, ay) in manual_axis.items():
        near = (np.hypot(gx - ax, gy - ay) < 700) & ~np.isnan(top)
        tall = near & (top > np.nanpercentile(top[near], 90))
        measured = [round(float(gx[tall].mean())), round(float(gy[tall].mean()))]
        axis_report[kind] = {'manual_mm': [ax, ay], 'model_tallest_part_centroid_mm': measured}
        for s_ in structures:
            if s_['kind'] == kind:
                s_['center_mm'] = [ax, ay] if s_['team'] == 'red' else [LENGTH_MM - ax, WIDTH_MM - ay]
                s_['source'] = 'axis: manual figure 4-5; bbox: STEP model'
    asymmetry = axis_report

    # ---- zones
    zones = []
    for name, kind, team, layer, poly in RED_SIDE_ZONES:
        for flip in (False, True):
            p = mirror(poly) if flip else poly
            if team == 'neutral':
                zid, zteam = (name.replace('_west', '_east') if flip else name), 'neutral'
            else:
                zteam = 'blue' if flip else 'red'
                zid = f'{zteam}_{name}'
            m = inside(p, gx, gy)
            zones.append({'id': zid, 'kind': kind, 'team': zteam, 'layer': layer, 'polygon_mm': p,
                          'mean_height_above_floor_mm': int(round(float(rel[m].mean()))), 'source': FIG})
    # Crossing cards come in a lower and a higher one (§5.6.4.5); name them by measured height.
    for side in ('west', 'east'):
        a = next(z for z in zones if z['id'] == f'road_step_a_{side}')
        b = next(z for z in zones if z['id'] == f'road_step_b_{side}')
        lo, hi = (a, b) if a['mean_height_above_floor_mm'] <= b['mean_height_above_floor_mm'] else (b, a)
        lo['id'], lo['kind'] = f'road_step_low_{side}', 'crossing_road_low'
        hi['id'], hi['kind'] = f'road_step_high_{side}', 'crossing_road_high'

    # ---- spawn poses: four points around the base, facing the opponent
    spawns = {}
    for team in ('red', 'blue'):
        base = next(s for s in structures if s['id'] == f'{team}_base')['center_mm']
        sign = 1 if team == 'red' else -1
        poses = []
        for dx, dy in ((0.55, 1.0), (0.55, -1.0), (1.25, 0.45), (1.25, -0.45)):
            x, y = base[0] + sign * dx * SPAWN_RING_MM, base[1] + sign * dy * SPAWN_RING_MM
            poses.append({'x_mm': int(round(x)), 'y_mm': int(round(y)), 'yaw_deg': 0 if team == 'red' else 180})
        spawns[team] = poses

    # ---- checks against manual dimensions
    def height_at(x, y):
        return float(rel[int(y // CELL_MM), int(x // CELL_MM)])
    def plateau(mask):
        v, c = np.unique(np.rint(rel[mask] / 5) * 5, return_counts=True)
        return [int(x) for x in v[np.argsort(-c)[:6]]]
    trap = rel[(gx > 3500) & (gx < 7000) & (gy > 11500) & (gy < 14000)]
    high = rel[inside(RED_SIDE_ZONES[1][4], gx, gy)]
    xs_central = gx[(rel > 250) & (gy > 7400) & (gy < 7600) & (gx > 8000) & (gx < 20000)]
    fort = rel[(gy > 7400) & (gy < 7600) & (gx > 5000) & (gx < 8200)]
    fort_x = gx[(gy > 7400) & (gy < 7600) & (gx > 5000) & (gx < 8200)][fort > 60]
    stairs = sorted({int(round(float(rel[int(12200 // CELL_MM), int(x // CELL_MM)]) / 10) * 10) for x in range(2300, 3800, 40)})
    bumps = np.array([rel[50, int(x // CELL_MM)] for x in range(5300, 7600, CELL_MM)])
    central_leaf = next(l for l in meta['leaves'] if any('中央高地_地面' in q for q in l['path']))
    cy = to_field(arrays[f"t{central_leaf['leaf']}"], frame)[:, :, 1]
    tunnels = {}
    for z in zones:
        if z['kind'] == 'tunnel_inside' and z['id'].endswith('west'):
            cells = (inside(z['polygon_mm'], gx, gy)).reshape(-1)
            sel = cells[over_idx]
            clear = (over_b[sel].astype(float) - ground.reshape(-1)[over_idx[sel]])
            tunnels[z['id']] = {'roofed_cells': int(sel.sum()), 'zone_cells': int(cells.sum()),
                                'clearance_mm_median': float(np.median(clear)) if len(clear) else None,
                                'clearance_mm_min': float(clear.min()) if len(clear) else None}
    checks = [
        {'what': 'field length', 'source': '§4.1, figure 4-5', 'manual': 28000, 'model': round(float(arrays['t0'][:, :, 0].max() - arrays['t0'][:, :, 0].min()), 1)},
        {'what': 'floor tilt, degrees (both halves)', 'source': 'figure 4-6: 1°~2°', 'manual': '1..2', 'model': [round(v, 4) for v in frame['tilt_deg']]},
        {'what': 'red fortress centre x', 'source': 'figure 4-5 (6571)', 'manual': 6571, 'model': round(float((fort_x.min() + fort_x.max()) / 2))},
        {'what': 'base axis (x, y)', 'source': 'figure 4-5 (25589 mirrored, 7500)', 'manual': manual_axis['base'], 'model': axis_report['base']['model_tallest_part_centroid_mm']},
        {'what': 'outpost axis (x, y)', 'source': 'figure 4-5 (17024 mirrored, 3600)', 'manual': manual_axis['outpost'], 'model': axis_report['outpost']['model_tallest_part_centroid_mm']},
        {'what': 'trapezoid highland height', 'source': '§4.3.1 (200)', 'manual': 200, 'model': round(float(np.median(trap)), 1)},
        {'what': 'central highland levels', 'source': 'figure 4-27 section A-A (200, 300, 350, 360, 400)', 'manual': [200, 300, 350, 360, 400],
         'model': plateau((gx > 9000) & (gx < 19000) & (gy > 2000) & (gy < 13000) & (rel > 100))},
        {'what': 'central highland extent along y', 'source': 'figure 4-27 (10820)', 'manual': 10820, 'model': round(float(cy.max() - cy.min()), 1)},
        {'what': 'central highland width incl. two 150 mm edges', 'source': 'figure 4-27 (7740 + 2 x 150)', 'manual': 8040, 'model': round(float(xs_central.max() - xs_central.min() + CELL_MM))},
        {'what': 'steps from trapezoid highland to high point', 'source': '§4.2.4: three steps', 'manual': 'three steps', 'model': stairs},
        {'what': 'undulating road: bump height above the 200 mm road', 'source': 'figure 4-36/4-37 (no number readable)', 'manual': None, 'model': round(float(bumps.max() - bumps.min()), 1)},
        {'what': 'high point deck height', 'source': 'model only', 'manual': None, 'model': round(float(np.median(high)), 1)},
        {'what': 'tunnel clearances', 'source': 'figure 4-35 (road tunnel 200/260), figure 4-38', 'manual': '200..260', 'model': {k: v['clearance_mm_median'] for k, v in tunnels.items()}},
    ]
    terrain_sha = hashlib.sha256((out / 'terrain.bin').read_bytes()).hexdigest()
    field = {
        'format': 'rmuc-field.v1', 'id': 'rmuc2027_v1',
        'title': 'RMUC 2027 battlefield, extracted from the official STEP model V1.0.0',
        'size_mm': [LENGTH_MM, WIDTH_MM], 'cell_mm': CELL_MM, 'grid': [NX, NY],
        'terrain': {'file': 'terrain.bin', 'sha256': terrain_sha, 'overhang_cells': int(len(over_idx))},
        'frame': 'x: red baseline -> blue, y: across (red on the left when y is up), z: up from the floor ridge',
        'floor': {'ridge_y_mm': WIDTH_MM // 2, 'tilt_deg': 1.5, 'fence_height_mm': 2400,
                  'note': 'two planes falling 1.5 deg from the long centre line (manual figure 4-6); fence height from §4.1'},
        'structures': structures, 'zones': zones, 'spawns': spawns,
    }
    (out / 'field.json').write_text(json.dumps(field, ensure_ascii=False, indent=1) + '\n', encoding='utf-8')
    report = {'frame': frame, 'structure_asymmetry_mm': asymmetry, 'tunnels': tunnels, 'checks': checks}
    (cache / 'field_report.json').write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False, indent=1))


if __name__ == '__main__':
    main()
