#!/usr/bin/env python3
"""Step 2 of the field extraction: triangle cache -> layered height field.

    python3 tools/field_extract/build_terrain.py cache_dir field_sim/field/rmuc2027_v1

Development-time tool (numpy; Pillow only for the preview image).

Battlefield frame used by the simulator (millimetres):
  x  0..28000 along the long side, from the red baseline towards blue;
  y  0..15000 across, so that red is on the left when y points up (the layout
     of the manual's top views, e.g. figure 4-5);
  z  up, 0 on the central ridge of the floor. The official floor is two planes
     falling 1.5 degrees from the long centre line towards both long sides
     (manual figure 4-6), so the floor is about 196 mm lower at the fences.
The frame is found from the model itself: the two floor planes are fitted, their
intersection gives the ridge (y = 7500, z = 0), and x is centred on the model.

Every 20 mm cell stores the height of the floor-connected surface ("ground")
and, where something hangs above free space (tunnel roofs, bridges, the energy
mechanism), the underside and top of the lowest such body ("overhang").
"""
import json
import pathlib
import struct
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import raster  # noqa: E402

CELL_MM = 20
LENGTH_MM, WIDTH_MM = 28000, 15000
NX, NY = LENGTH_MM // CELL_MM, WIDTH_MM // CELL_MM
MERGE_GAP_MM = 60        # solid runs closer than this are fused (panels, decals, mesh tolerance)
MIN_OVERHANG_GAP_MM = 100  # free space below an overhang must be at least this high to be kept
GROUND_LEAF = '战场大地面'
MAGIC = b'RMTERRN1'
NO_OVERHANG = -32768


def fit_frame(leaves, arrays):
    """Ridge position and tilt from the two floor planes."""
    k = next(l['leaf'] for l in leaves if any(GROUND_LEAF in p for p in l['path']))
    t = arrays[f't{k}'].astype(np.float64)
    n = np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0])
    area = np.linalg.norm(n, axis=1)
    ok = area > 1e-6
    t, n, area = t[ok], n[ok] / area[ok, None], area[ok]
    planes = []
    for side in (-1, 1):
        sel = (n[:, 2] > 0.99) & (np.abs(n[:, 0]) < 1e-3) & (np.sign(n[:, 1]) == side) & (np.abs(n[:, 1]) > 0.01)
        # keep the dominant offset: the floor itself, not small recessed faces
        d = (n[sel] * t[sel].mean(1)).sum(1)
        centre = np.median(np.repeat(d, np.maximum(1, (area[sel] / area[sel].max() * 50).astype(int))))
        sel_idx = np.nonzero(sel)[0][np.abs(d - centre) < 2.0]
        pts = t[sel_idx].reshape(-1, 3)
        a = np.c_[pts[:, 1], np.ones(len(pts))]
        slope, offset = np.linalg.lstsq(a, pts[:, 2], rcond=None)[0]   # z = slope*y + offset
        planes.append((slope, offset, float(np.abs(a @ [slope, offset] - pts[:, 2]).max())))
    (s1, o1, r1), (s2, o2, r2) = planes
    yc = (o2 - o1) / (s1 - s2)
    zr = s1 * yc + o1
    xs = arrays[f't{k}'][:, :, 0]
    xc = (float(xs.min()) + float(xs.max())) / 2
    return {'cad_ridge_y': float(yc), 'cad_ridge_z': float(zr), 'cad_centre_x': float(xc),
            'tilt_deg': [float(np.degrees(np.arctan(abs(s1)))), float(np.degrees(np.arctan(abs(s2))))],
            'plane_fit_residual_mm': [r1, r2]}


def to_field(t, frame):
    """CAD frame -> battlefield frame: a half turn about the vertical axis plus a shift."""
    m = np.empty(t.shape, dtype=np.float64)
    m[..., 0] = LENGTH_MM / 2 - (t[..., 0] - frame['cad_centre_x'])
    m[..., 1] = WIDTH_MM / 2 - (t[..., 1] - frame['cad_ridge_y'])
    m[..., 2] = t[..., 2] - frame['cad_ridge_z']
    return m


def floor_z(y_mm, tilt_deg=1.5):
    return -np.abs(np.asarray(y_mm, dtype=np.float64) - WIDTH_MM / 2) * np.tan(np.radians(tilt_deg))


def merge_runs(cell, bottom, top, gap):
    """Union of intervals per cell; runs closer than gap fuse. Sorted by (cell, bottom)."""
    order = np.lexsort((bottom, cell))
    c, b, t = cell[order], bottom[order], top[order]
    big = 1.0e6
    reach = np.maximum.accumulate(t + c * big) - c * big      # highest top so far within the cell
    new = np.r_[True, (c[1:] != c[:-1]) | (b[1:] > reach[:-1] + gap)]
    group = np.cumsum(new) - 1
    gc = c[new]
    gb = b[new]
    gt = np.full(len(gc), -np.inf)
    np.maximum.at(gt, group, t)
    return gc, gb, gt


def build(cache):
    meta = json.loads((cache / 'mesh.json').read_text(encoding='utf-8'))
    arrays = np.load(cache / 'mesh.npz')
    frame = fit_frame(meta['leaves'], arrays)
    cells, bottoms, tops = [], [], []
    stats = []
    for leaf in meta['leaves']:
        k = leaf['leaf']
        tri = to_field(arrays[f't{k}'], frame)
        part = arrays[f'p{k}']
        closed = np.array([p == 'solid' for p in leaf['parts']], dtype=bool)
        n_cells = NX * NY
        got = 0
        if len(tri):
            is_closed = closed[part]
            for flag in (True, False):
                sel = is_closed == flag
                if not sel.any():
                    continue
                c, z, s, t_index = raster.crossings(tri[sel], CELL_MM, NX, NY)
                key = part[sel][t_index].astype(np.int64) * n_cells + c
                if flag:
                    kc, kb, kt = raster.solid_intervals(key, z, s)
                else:
                    kc, kb, kt = raster.hull_intervals(key, z)
                cells.append(kc % n_cells)
                bottoms.append(kb)
                tops.append(kt)
                got += len(kc)
        stats.append({'leaf': k, 'name': '/'.join(leaf['path']), 'triangles': leaf['triangles'], 'runs': got})
    cell = np.concatenate(cells)
    bottom = np.concatenate(bottoms)
    top = np.concatenate(tops)
    gc, gb, gt = merge_runs(cell, bottom, top, MERGE_GAP_MM)
    first = np.r_[True, gc[1:] != gc[:-1]]
    rank = np.arange(len(gc)) - np.maximum.accumulate(np.where(first, np.arange(len(gc)), 0))
    ground = np.full(NX * NY, np.nan)
    ground[gc[rank == 0]] = gt[rank == 0]
    over_b = np.full(NX * NY, np.nan)
    over_t = np.full(NX * NY, np.nan)
    second = rank == 1
    over_b[gc[second]] = gb[second]
    over_t[gc[second]] = gt[second]
    return frame, stats, ground.reshape(NY, NX), over_b.reshape(NY, NX), over_t.reshape(NY, NX)


def clean(ground, over_b, over_t):
    """Fill cells without data, drop low overhang gaps, and store overhangs sparsely."""
    yy = (np.arange(NY) + 0.5) * CELL_MM
    floor = floor_z(yy)[:, None] * np.ones((1, NX))
    missing = int(np.isnan(ground).sum())
    ground = np.where(np.isnan(ground), floor, ground)
    low = ~np.isnan(over_b) & (over_b - ground < MIN_OVERHANG_GAP_MM)
    ground = np.where(low, over_t, ground)
    over_b = np.where(low, np.nan, over_b)
    over_t = np.where(low, np.nan, over_t)
    return ground, over_b, over_t, missing


def write_terrain(path, ground, over_b, over_t):
    g = np.rint(ground).astype(np.int16)
    has = ~np.isnan(over_b)
    idx = np.nonzero(has.reshape(-1))[0].astype(np.uint32)
    ob = np.rint(over_b.reshape(-1)[idx]).astype(np.int16)
    ot = np.rint(over_t.reshape(-1)[idx]).astype(np.int16)
    with open(path, 'wb') as f:
        f.write(MAGIC)
        f.write(struct.pack('<5i', NX, NY, CELL_MM, len(idx), 0))
        f.write(g.astype('<i2').tobytes())
        f.write(idx.astype('<u4').tobytes())
        f.write(ob.astype('<i2').tobytes())
        f.write(ot.astype('<i2').tobytes())
    return int(len(idx))


def main():
    cache, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    frame, stats, ground, over_b, over_t = build(cache)
    np.savez_compressed(cache / 'layers_raw.npz', ground=ground, over_b=over_b, over_t=over_t)
    ground, over_b, over_t, missing = clean(ground, over_b, over_t)
    count = write_terrain(out / 'terrain.bin', ground, over_b, over_t)
    report = {'frame': frame, 'cells_without_data': missing, 'overhang_cells': count,
              'ground_min_mm': float(ground.min()), 'ground_max_mm': float(ground.max()), 'leaves': stats}
    (cache / 'build_report.json').write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k != 'leaves'}, indent=1))


if __name__ == '__main__':
    main()
