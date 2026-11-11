"""Vertical-ray sampling of triangle meshes on a regular grid (numpy only).

For every grid cell centre the mesh is intersected with a vertical line. Each
crossing is recorded as (cell, z, sign) where sign is +1 for an upward-facing
triangle (the line leaves the solid) and -1 for a downward-facing one (the line
enters it). Closed volumes then become z-intervals by a running winding count,
which also forms the union of overlapping solids.
"""
import numpy as np

# Cell centres are shifted by a fraction of a millimetre so that model edges on
# round coordinates never run exactly through a sample point.
JITTER = (0.137, 0.291)


def crossings(tris, cell, nx, ny):
    """tris: (N,3,3) float64 in grid coordinates (mm). Returns cell index, z, sign, triangle index."""
    tris = np.asarray(tris, dtype=np.float64)
    out_c, out_z, out_s, out_t = [], [], [], []
    empty = (np.zeros(0, np.int64), np.zeros(0), np.zeros(0, np.int8), np.zeros(0, np.int64))
    if not len(tris):
        return empty
    x, y, z = tris[:, :, 0] - JITTER[0], tris[:, :, 1] - JITTER[1], tris[:, :, 2]
    area2 = (x[:, 1] - x[:, 0]) * (y[:, 2] - y[:, 0]) - (x[:, 2] - x[:, 0]) * (y[:, 1] - y[:, 0])
    i0 = np.ceil(x.min(1) / cell - 0.5).astype(np.int64)
    i1 = np.floor(x.max(1) / cell - 0.5).astype(np.int64)
    j0 = np.ceil(y.min(1) / cell - 0.5).astype(np.int64)
    j1 = np.floor(y.max(1) / cell - 0.5).astype(np.int64)
    i0c, i1c, j0c, j1c = np.maximum(i0, 0), np.minimum(i1, nx - 1), np.maximum(j0, 0), np.minimum(j1, ny - 1)
    w, h = i1c - i0c + 1, j1c - j0c + 1
    keep = (np.abs(area2) > 1e-9) & (w > 0) & (h > 0)
    size = np.maximum(w, h)

    def emit(sel, k):
        """All triangles in sel have a clipped bounding box of at most k x k cells."""
        for start in range(0, len(sel), max(1, 4_000_000 // (k * k))):
            s = sel[start:start + max(1, 4_000_000 // (k * k))]
            gi = i0c[s, None] + np.arange(k)[None, :]
            gj = j0c[s, None] + np.arange(k)[None, :]
            px = ((gi + 0.5) * cell)[:, :, None]            # (n,k,1)
            py = ((gj + 0.5) * cell)[:, None, :]            # (n,1,k)
            sg = np.sign(area2[s])[:, None, None]
            def edge(a, b):
                return ((x[s, b] - x[s, a])[:, None, None] * (py - y[s, a][:, None, None])
                        - (y[s, b] - y[s, a])[:, None, None] * (px - x[s, a][:, None, None])) * sg
            w0, w1, w2 = edge(1, 2), edge(2, 0), edge(0, 1)
            inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            inside &= (gi <= i1c[s, None])[:, :, None] & (gj <= j1c[s, None])[:, None, :]
            n, a, b = np.nonzero(inside)
            if not len(n):
                continue
            total = np.abs(area2[s])[n]
            zz = (w0[n, a, b] * z[s, 0][n] + w1[n, a, b] * z[s, 1][n] + w2[n, a, b] * z[s, 2][n]) / total
            out_c.append(gj[n, b] * nx + gi[n, a])
            out_z.append(zz)
            out_s.append(np.sign(area2[s])[n].astype(np.int8))
            out_t.append(s[n])

    for k in (2, 4, 8, 16, 32, 64):
        sel = np.nonzero(keep & (size <= k) & (size > k // 2 if k > 2 else True))[0]
        if len(sel):
            emit(sel, k)
    for t in np.nonzero(keep & (size > 64))[0]:   # the few large triangles, one by one
        gi = np.arange(i0c[t], i1c[t] + 1)
        gj = np.arange(j0c[t], j1c[t] + 1)
        px = ((gi + 0.5) * cell)[:, None]
        py = ((gj + 0.5) * cell)[None, :]
        sg = np.sign(area2[t])
        def edge(a, b):
            return ((x[t, b] - x[t, a]) * (py - y[t, a]) - (y[t, b] - y[t, a]) * (px - x[t, a])) * sg
        w0, w1, w2 = edge(1, 2), edge(2, 0), edge(0, 1)
        a, b = np.nonzero((w0 >= 0) & (w1 >= 0) & (w2 >= 0))
        if not len(a):
            continue
        zz = (w0[a, b] * z[t, 0] + w1[a, b] * z[t, 1] + w2[a, b] * z[t, 2]) / abs(area2[t])
        out_c.append(gj[b] * nx + gi[a])
        out_z.append(zz)
        out_s.append(np.full(len(a), sg, dtype=np.int8))
        out_t.append(np.full(len(a), t, dtype=np.int64))
    if not out_c:
        return empty
    return np.concatenate(out_c), np.concatenate(out_z), np.concatenate(out_s), np.concatenate(out_t)


def solid_intervals(cells, z, sign):
    """Winding-count intervals. Returns (cell, bottom, top) for every solid run."""
    if not len(cells):
        return np.zeros(0, np.int64), np.zeros(0), np.zeros(0)
    # At equal z an entry sorts before an exit so touching solids fuse.
    order = np.lexsort((sign, z, cells))
    c, zz, s = cells[order], z[order], -sign[order].astype(np.int64)   # +1 entering, -1 leaving
    run = np.cumsum(s)
    first = np.r_[True, c[1:] != c[:-1]]
    base = np.maximum.accumulate(np.where(first, np.arange(len(c)), 0))
    count = run - (run[base] - s[base])            # winding count after each crossing, per cell
    before = count - s
    start = (before <= 0) & (count > 0)
    end = (before > 0) & (count <= 0)
    # Pair every start with the next end in the same cell.
    si, ei = np.nonzero(start)[0], np.nonzero(end)[0]
    pos = np.searchsorted(ei, si)
    ok = pos < len(ei)
    si, pos = si[ok], pos[ok]
    ok = c[ei[pos]] == c[si]
    si, pos = si[ok], pos[ok]
    return c[si], zz[si], zz[ei[pos]]


def hull_intervals(cells, z):
    """Per-cell [min z, max z] of an open sheet: its vertical hull."""
    if not len(cells):
        return np.zeros(0, np.int64), np.zeros(0), np.zeros(0)
    order = np.lexsort((z, cells))
    c, zz = cells[order], z[order]
    first = np.r_[True, c[1:] != c[:-1]]
    last = np.r_[first[1:], True]
    return c[first], zz[first], zz[last]
