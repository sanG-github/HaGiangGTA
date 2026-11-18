#!/usr/bin/env python3
"""Procedural 4 km x 4 km Ha Giang heightmap (4033 x 4033, 16-bit).

Deterministic (fixed seed): the same files come out every run, so the
generated data does not need to be committed.

Route (one continuous road, ~11 km):
  VP1  Doc Tham Ma      - 9 hairpins (10 legs) climbing 800 m -> 1050 m
  link                   - 1050 m -> 1200 m up to the canyon rim
  VP2  Ma Pi Leng        - rim road at ~1200 m along the Tu San canyon,
                           with the "Mom Da Tu Than" rock spur over the void
  VP3  Ta Lang descent   - 11 hairpins down 500 m to the boat dock on the
                           Nho Que river (water surface 700 m, canyon ~800 m deep
                           measured from the east-bank peaks)

Outputs (Generated/Heightmaps/):
  HaGiang_4033.png    16-bit grayscale PNG (UE "Import from file")
  HaGiang_4033.r16    raw little-endian uint16 (read by UHGLandscapeBuilder)
  HaGiang_layout.json landscape transform, road / river polylines, viewpoints, spawns
  HaGiang_preview.png hillshade preview with road + river overlay

Usage:
  .venv/bin/python Scripts/Tools/gen_heightmap.py          (needs numpy + pillow)
"""
from __future__ import annotations

import json
import math
import time
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT / "Generated" / "Heightmaps"

# ---------------------------------------------------------------------------
# Landscape constants
# ---------------------------------------------------------------------------
N = 4033                      # vertices per side (32 x 32 components of 2x2 63-quad sections)
CELL_M = 1.0                  # 100 uu per quad
HALF_M = (N - 1) * CELL_M / 2.0   # 2016 m
BASE_ASL_M = 300.0            # height value 0 == 300 m above sea level
Z_SCALE = 256.0               # landscape Z scale -> 2 cm per height step, 1310.72 m range
M_PER_UNIT = Z_SCALE / 128.0 / 100.0   # 0.02 m
MAX_ASL_M = BASE_ASL_M + 65535 * M_PER_UNIT

WATER_ASL_M = 700.0
RIVER_BED_ASL_M = 692.0
RIVER_HALF_WIDTH_M = 25.0
ROAD_HALF_WIDTH_M = 3.5
ROAD_SHOULDER_M = 1.0
SEED = 20261118

# Landscape actor transform (UE cm). World Z (cm) == altitude ASL (m) * 100.
LANDSCAPE_LOCATION_CM = (-HALF_M * 100.0, -HALF_M * 100.0, BASE_ASL_M * 100.0 + 32768 * Z_SCALE / 128.0)
LANDSCAPE_SCALE = (100.0, 100.0, Z_SCALE)


# ---------------------------------------------------------------------------
# Polyline helpers (all coordinates in metres, UE world X/Y)
# ---------------------------------------------------------------------------

def catmull_rom(points, step=2.0):
    """Centripetal-ish uniform Catmull-Rom through all points, resampled later."""
    pts = np.asarray(points, dtype=np.float64)
    ext = np.vstack([2 * pts[0] - pts[1], pts, 2 * pts[-1] - pts[-2]])
    out = []
    for i in range(1, len(ext) - 2):
        p0, p1, p2, p3 = ext[i - 1], ext[i], ext[i + 1], ext[i + 2]
        seg_len = np.linalg.norm(p2 - p1)
        n = max(2, int(seg_len / step))
        for t in np.linspace(0.0, 1.0, n, endpoint=False):
            t2, t3 = t * t, t * t * t
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2
                              + (-p0 + 3 * p1 - 3 * p2 + p3) * t3))
    out.append(pts[-1])
    return np.asarray(out)


def hairpin_stack(start_u, v0, leg_len, spacing, n_legs, first_dir, stack_dir, axis, step=2.0):
    """Zig-zag of straight legs joined by semicircular hairpins.

    Legs run along ``axis`` ('x' or 'y'); successive legs are offset by
    ``spacing`` along the other axis in ``stack_dir``.
    """
    pts = []
    radius = spacing / 2.0
    u = start_u
    for i in range(n_legs):
        d = first_dir * (1 if i % 2 == 0 else -1)
        v = v0 + stack_dir * i * spacing
        u_end = u + d * leg_len
        n = max(2, int(leg_len / step))
        for t in np.linspace(0.0, 1.0, n, endpoint=False):
            pts.append((u + (u_end - u) * t, v))
        if i < n_legs - 1:
            cu, cv = u_end, v + stack_dir * radius
            a_start = math.atan2(-stack_dir * radius, 0.0)
            # Choose sweep direction so the hairpin bulges beyond the leg end.
            sweep = math.pi if math.cos(a_start + math.pi / 2) * d > 0 else -math.pi
            n_arc = max(8, int(math.pi * radius / step))
            for t in np.linspace(0.0, 1.0, n_arc, endpoint=False):
                a = a_start + sweep * t
                pts.append((cu + radius * math.cos(a), cv + radius * math.sin(a)))
        u = u_end
    pts.append((u, v0 + stack_dir * (n_legs - 1) * spacing))
    arr = np.asarray(pts)
    return arr if axis == "x" else arr[:, ::-1].copy()


def resample(poly, step):
    seg = np.linalg.norm(np.diff(poly, axis=0), axis=1)
    keep = np.concatenate([[True], seg > 1e-6])
    poly = poly[keep]
    s = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(poly, axis=0), axis=1))])
    targets = np.arange(0.0, s[-1], step)
    targets = np.append(targets, s[-1])
    x = np.interp(targets, s, poly[:, 0])
    y = np.interp(targets, s, poly[:, 1])
    return np.stack([x, y], axis=1), targets


def smooth_1d(values, window):
    if window <= 1:
        return values
    pad = window // 2
    padded = np.pad(values, pad, mode="edge")
    kernel = np.ones(window) / window
    return np.convolve(padded, kernel, mode="valid")


# ---------------------------------------------------------------------------
# Route definition
# ---------------------------------------------------------------------------

def build_route():
    pieces = []      # (name, polyline)

    approach = catmull_rom([(-1050, -1830), (-1250, -1770), (-1420, -1690), (-1490, -1655), (-1520, -1650)])
    pieces.append(("approach", approach))

    # VP1 Doc Tham Ma: 10 legs of 180 m, 36 m apart -> 9 hairpins, climbing north.
    tham_ma = hairpin_stack(start_u=-1520, v0=-1650, leg_len=180, spacing=36, n_legs=10,
                            first_dir=-1, stack_dir=+1, axis="x")
    pieces.append(("tham_ma", tham_ma))

    # Link + VP2 Ma Pi Leng rim road.
    rim = catmull_rom([(-1520, -1326), (-1470, -1318), (-1300, -1280), (-1000, -1160), (-600, -1010),
                       (-250, -900), (100, -800), (350, -660), (560, -400), (675, -150), (705, 0),
                       (690, 250), (645, 550), (628, 800), (642, 940), (660, 1000)])
    pieces.append(("rim", rim))

    # VP3 Ta Lang descent: 12 legs of 230 m along +Y/-Y, 34 m apart, stacked east.
    descent = hairpin_stack(start_u=1000, v0=660, leg_len=230, spacing=34, n_legs=12,
                            first_dir=+1, stack_dir=+1, axis="y")
    pieces.append(("descent", descent))

    end = descent[-1]
    dock_spur = catmull_rom([tuple(end), (end[0] + 2, end[1] - 14), (end[0] + 20, end[1] - 26),
                             (end[0] + 45, end[1] - 24)])
    pieces.append(("dock_spur", dock_spur))

    poly = np.vstack([p for _, p in pieces])
    poly, s = resample(poly, 2.0)

    # Elevation keyframes by arclength.
    lengths = {}
    acc = 0.0
    for name, p in pieces:
        acc += float(np.sum(np.linalg.norm(np.diff(p, axis=0), axis=1)))
        lengths[name] = acc
    key_s = [0.0, lengths["approach"], lengths["tham_ma"],
             lengths["tham_ma"] + 0.55 * (lengths["rim"] - lengths["tham_ma"]),
             lengths["rim"], lengths["descent"], lengths["dock_spur"]]
    key_z = [790.0, 800.0, 1050.0, 1200.0, 1202.0, 702.0, 701.8]
    z = np.interp(s, key_s, key_z)
    z = smooth_1d(z, 41)   # ~80 m window: no grade kinks
    return poly, z, s, lengths


def build_river():
    pts = catmull_rom([(1150, -2150), (1060, -1500), (1040, -900), (1020, -400), (990, 0), (1040, 450),
                       (1100, 900), (1180, 1250), (1300, 1700), (1450, 2150)], step=5.0)
    pts, _ = resample(pts, 10.0)
    return pts


# ---------------------------------------------------------------------------
# Raster helpers
# ---------------------------------------------------------------------------

def grid_coords(n, stride=1):
    idx = np.arange(0, n, stride, dtype=np.float64)
    coords = idx * CELL_M - HALF_M
    return np.meshgrid(coords, coords, indexing="xy")   # X[row, col], Y[row, col]


def value_noise(n, cells, rng, stride=1):
    """Smooth value noise on an n x n grid (sampled every ``stride`` vertices)."""
    grid = rng.random((cells + 2, cells + 2)).astype(np.float32)
    coords = np.arange(0, n, stride, dtype=np.float32) * (cells / (n - 1))
    i0 = np.floor(coords).astype(np.int32)
    f = coords - i0
    f = f * f * (3 - 2 * f)
    a = grid[i0][:, i0]
    b = grid[i0][:, i0 + 1]
    c = grid[i0 + 1][:, i0]
    d = grid[i0 + 1][:, i0 + 1]
    fx = f[None, :]
    fy = f[:, None]
    top = a + (b - a) * fx
    bot = c + (d - c) * fx
    return top + (bot - top) * fy


def fbm(n, base_cells, octaves, rng, ridged=False):
    total = np.zeros((n, n), dtype=np.float32)
    amp, norm = 1.0, 0.0
    cells = base_cells
    for _ in range(octaves):
        v = value_noise(n, cells, rng)
        if ridged:
            v = 1.0 - np.abs(2.0 * v - 1.0)
            v = v * v
        total += amp * v
        norm += amp
        amp *= 0.5
        cells *= 2
    return total / norm


def upsample(coarse, n):
    """Bilinear upsample a (m x m) array sampled every k vertices to n x n."""
    m = coarse.shape[0]
    pos = np.linspace(0, m - 1, n, dtype=np.float32)
    i0 = np.clip(np.floor(pos).astype(np.int32), 0, m - 2)
    f = pos - i0
    a = coarse[i0][:, i0]
    b = coarse[i0][:, i0 + 1]
    c = coarse[i0 + 1][:, i0]
    d = coarse[i0 + 1][:, i0 + 1]
    top = a + (b - a) * f[None, :]
    bot = c + (d - c) * f[None, :]
    return (top + (bot - top) * f[:, None]).astype(np.float32)


def polyline_distance(poly, values, margin, n=N):
    """Distance (m) from every vertex to the polyline, plus the interpolated
    per-vertex ``values`` at the closest point. Only evaluated within
    ``margin`` metres of the line (inf elsewhere)."""
    dist = np.full((n, n), np.inf, dtype=np.float32)
    val = np.zeros((n, n), dtype=np.float32)
    for i in range(len(poly) - 1):
        a, b = poly[i], poly[i + 1]
        lo = np.minimum(a, b) - margin
        hi = np.maximum(a, b) + margin
        c0 = max(0, int(math.floor(lo[0] + HALF_M)))
        c1 = min(n - 1, int(math.ceil(hi[0] + HALF_M)))
        r0 = max(0, int(math.floor(lo[1] + HALF_M)))
        r1 = min(n - 1, int(math.ceil(hi[1] + HALF_M)))
        if c0 > c1 or r0 > r1:
            continue
        xs = np.arange(c0, c1 + 1, dtype=np.float32) - HALF_M
        ys = np.arange(r0, r1 + 1, dtype=np.float32) - HALF_M
        px = xs[None, :] - a[0]
        py = ys[:, None] - a[1]
        ab = b - a
        denom = float(ab @ ab) or 1e-9
        t = np.clip((px * ab[0] + py * ab[1]) / denom, 0.0, 1.0)
        dx = px - t * ab[0]
        dy = py - t * ab[1]
        d = np.sqrt(dx * dx + dy * dy)
        sub_d = dist[r0:r1 + 1, c0:c1 + 1]
        better = d < sub_d
        if not better.any():
            continue
        sub_d[better] = d[better]
        v = values[i] + (values[i + 1] - values[i]) * t
        val[r0:r1 + 1, c0:c1 + 1][better] = v[better]
    return dist, val


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


# ---------------------------------------------------------------------------
# Terrain synthesis
# ---------------------------------------------------------------------------

REGIONAL_ANCHORS = [
    # (x, y, altitude m) - broad shape of the region
    (-1950, -1950, 760), (-1700, -1850, 780), (-1100, -1950, 820), (-400, -1900, 900), (400, -1900, 1000),
    (-1950, -1300, 1050), (-1900, -600, 1180), (-1200, -500, 1230), (-500, -300, 1240), (200, -200, 1230),
    (-1800, 400, 1280), (-1000, 600, 1300), (-200, 700, 1280), (300, 500, 1250),
    (-1700, 1500, 1350), (-900, 1600, 1330), (-100, 1700, 1300), (400, 1700, 1280), (300, 1300, 1260),
    (1600, -1900, 1300), (1700, -1000, 1380), (1750, 0, 1420), (1700, 900, 1400), (1800, 1900, 1330),
    (1500, -400, 1360), (1450, 500, 1380),
]


def rbf_base(road_xy, road_z, stride=8):
    """Smooth base altitude from regional anchors + road samples (Gaussian RBF)."""
    X, Y = grid_coords(N, stride)
    num = np.full(X.shape, 1150.0 * 1e-6)
    den = np.full(X.shape, 1e-6)

    def add(points, sigma, weight):
        nonlocal num, den
        for (x, y, z) in points:
            w = weight * np.exp(-((X - x) ** 2 + (Y - y) ** 2) / (2.0 * sigma * sigma))
            num += w * z
            den += w

    add(REGIONAL_ANCHORS, 650.0, 1.0)
    idx = np.arange(0, len(road_xy), 30)   # every 60 m
    add([(road_xy[i, 0], road_xy[i, 1], road_z[i]) for i in idx], 110.0, 4.0)
    return upsample((num / den).astype(np.float32), N)


def canyon_params(y):
    """Wall half-width W (m), profile exponent k and rim altitude along the river (by Y)."""
    ys = np.array([-2200, -800, -500, 600, 900, 2200], dtype=np.float32)

    def smooth_interp(values):
        # Cosine interpolation between keys -> no visible derivative kinks on the walls.
        values = np.asarray(values, dtype=np.float32)
        i = np.clip(np.searchsorted(ys, y) - 1, 0, len(ys) - 2)
        t = np.clip((y - ys[i]) / (ys[i + 1] - ys[i]), 0.0, 1.0)
        t = 0.5 - 0.5 * np.cos(np.pi * t)
        return values[i] + (values[i + 1] - values[i]) * t

    W = smooth_interp([350, 330, 300, 300, 425, 425])
    k = smooth_interp([0.70, 0.65, 0.55, 0.55, 1.00, 1.00])
    rim = smooth_interp([1250, 1245, 1235, 1235, 1260, 1260])
    return W.astype(np.float32), k.astype(np.float32), rim.astype(np.float32)


def place_cones(rng, road_dist_coarse, stride, count=420):
    cones = []
    m = road_dist_coarse.shape[0]
    tries = 0
    while len(cones) < count and tries < count * 40:
        tries += 1
        x = rng.uniform(-HALF_M, HALF_M)
        y = rng.uniform(-HALF_M, HALF_M)
        r = rng.uniform(55, 170)
        h = rng.uniform(90, 260) * (r / 170) ** 0.6
        ci = min(m - 1, int((x + HALF_M) / stride))
        ri = min(m - 1, int((y + HALF_M) / stride))
        if road_dist_coarse[ri, ci] < r + 35:
            continue
        if any((x - cx) ** 2 + (y - cy) ** 2 < (0.55 * (r + cr)) ** 2 for cx, cy, cr, _, _, _ in cones):
            continue
        ang = rng.uniform(0, math.pi)
        aspect = rng.uniform(1.0, 1.7)
        cones.append((x, y, r, h, ang, aspect))
    return cones


def stamp_cones(height, cones, rng):
    for (x, y, r, h, ang, aspect) in cones:
        reach = r * aspect + 2
        c0 = max(0, int(x - reach + HALF_M)); c1 = min(N - 1, int(x + reach + HALF_M))
        r0 = max(0, int(y - reach + HALF_M)); r1 = min(N - 1, int(y + reach + HALF_M))
        if c0 >= c1 or r0 >= r1:
            continue
        xs = np.arange(c0, c1 + 1, dtype=np.float32) - HALF_M - x
        ys = np.arange(r0, r1 + 1, dtype=np.float32) - HALF_M - y
        ca, sa = math.cos(ang), math.sin(ang)
        u = xs[None, :] * ca + ys[:, None] * sa
        v = -xs[None, :] * sa + ys[:, None] * ca
        d = np.sqrt((u / aspect) ** 2 + v ** 2) / r
        # Steep "tai meo" haystack karst: bell-shaped dome with a slightly pointed top.
        bell = np.clip(1.0 - d * d, 0.0, 1.0) ** 2.2
        tip = np.clip(1.0 - d, 0.0, 1.0) ** 3.0
        prof = 0.8 * bell + 0.2 * tip
        height[r0:r1 + 1, c0:c1 + 1] += (h * prof).astype(np.float32)


def capsule_mask(p0, p1, radius, feather):
    pts = np.array([p0, p1], dtype=np.float64)
    d, _ = polyline_distance(pts, np.zeros(2), radius + feather + 2)
    return 1.0 - smoothstep(radius, radius + feather, d)


def generate():
    t0 = time.time()
    rng = np.random.default_rng(SEED)

    road_xy, road_z, road_s, piece_len = build_route()
    river_xy = build_river()
    print(f"road {road_s[-1] / 1000:.2f} km, {len(road_xy)} samples; river {len(river_xy)} pts")

    # --- distance fields ---
    road_d, road_val = polyline_distance(road_xy, road_z.astype(np.float32), margin=60.0)
    river_d, _ = polyline_distance(river_xy, np.zeros(len(river_xy), np.float32), margin=700.0)
    print(f"distance fields {time.time() - t0:.1f}s")

    # --- base + relief ---
    height = rbf_base(road_xy, road_z)
    stride = 8
    coarse_road_d, _ = polyline_distance(road_xy[::10], np.zeros(len(road_xy[::10]), np.float32), margin=400.0)
    cones = place_cones(rng, coarse_road_d[::stride, ::stride], stride)
    stamp_cones(height, cones, rng)

    relief = (fbm(N, 10, 6, rng) - 0.5) * 40.0 + (fbm(N, 24, 5, rng, ridged=True) - 0.3) * 22.0
    fine = (fbm(N, 400, 3, rng) - 0.5) * 1.6
    near_road = smoothstep(8.0, 45.0, np.minimum(road_d, 1e6))
    height += relief * (0.25 + 0.75 * near_road) + fine
    print(f"relief {time.time() - t0:.1f}s, {len(cones)} karst cones")

    # --- Nho Que canyon (cutter, min with terrain) ---
    _, Yg = grid_coords(N)
    W, k, rim = canyon_params(Yg.astype(np.float32))
    del Yg
    rd = np.minimum(river_d, 5000.0)
    chan = RIVER_BED_ASL_M + (np.clip(rd / RIVER_HALF_WIDTH_M, 0, 1) ** 2) * (WATER_ASL_M - 2.0 - RIVER_BED_ASL_M)
    t = np.clip((rd - RIVER_HALF_WIDTH_M) / (W - RIVER_HALF_WIDTH_M), 0.0, 1.0)
    wall_base = WATER_ASL_M - 2.0
    # Beyond the wall the cutter climbs fast, so the natural terrain takes over.
    wall = wall_base + (rim - wall_base) * np.power(t, k) + np.maximum(rd - W, 0.0) * 8.0
    cutter = np.where(rd < RIVER_HALF_WIDTH_M, chan, wall).astype(np.float32)
    # Rough-up the canyon walls a little (strata).
    strata = (fbm(N, 60, 4, rng) - 0.5) * 30.0 + (fbm(N, 140, 3, rng, ridged=True) - 0.3) * 35.0
    cutter += (strata * np.clip(t * (1 - t) * 4.0, 0.0, 1.0)).astype(np.float32)
    del strata
    height = np.minimum(height, cutter)
    del cutter, chan, wall, t, W, k, rim
    print(f"canyon {time.time() - t0:.1f}s")

    # --- road carving (cut & fill, width-dependent banks) ---
    core = ROAD_HALF_WIDTH_M + ROAD_SHOULDER_M
    diff = np.abs(height - road_val)
    bank = np.clip(5.0 + 0.6 * diff, 5.0, 30.0)
    w = 1.0 - smoothstep(core, core + bank, np.minimum(road_d, 1e6))
    w = np.where(np.isfinite(road_d), w, 0.0).astype(np.float32)
    # Road surface sits on a 0.15 m cut so the road mesh is never buried.
    height = height * (1.0 - w) + (road_val - 0.15) * w
    del diff, bank

    # --- viewpoint stamps ---
    layout_vp = {}
    # VP1 Doc Tham Ma check-in pad (top of the hairpins).
    top_i = int(np.searchsorted(road_s, piece_len["tham_ma"]))
    vp1_xy = road_xy[min(top_i + 25, len(road_xy) - 1)] + np.array([0.0, 22.0])
    vp1_z = float(road_z[min(top_i + 25, len(road_z) - 1)])
    m = capsule_mask(vp1_xy, vp1_xy + [30.0, 0.0], 16.0, 10.0)
    height = height * (1 - m) + (vp1_z - 0.15) * m
    layout_vp["doc_tham_ma"] = {"xy": vp1_xy.tolist(), "z": vp1_z}

    # VP2 Mom Da Tu Than: rock spur jutting east from the rim road over the canyon.
    rim_i = int(np.argmin(np.abs(road_xy[:, 1]) + 1000.0 * (road_s < piece_len["tham_ma"])
                          + 1000.0 * (road_s > piece_len["rim"])))
    rim_xy = road_xy[rim_i]
    spur_a = rim_xy + np.array([ROAD_HALF_WIDTH_M + 1.0, -2.0])
    spur_b = rim_xy + np.array([82.0, 6.0])
    spur_z = float(road_z[rim_i]) - 4.0
    m = capsule_mask(spur_a, spur_b, 7.0, 1.5)
    height = np.maximum(height * (1 - m) + spur_z * m, height * (1 - m))
    layout_vp["mom_da_tu_than"] = {"xy": spur_b.tolist(), "z": spur_z, "road_xy": rim_xy.tolist(),
                                   "road_z": float(road_z[rim_i])}

    # VP3 Ta Lang dock pad at the end of the road, reaching the river edge.
    end_xy = road_xy[-1]
    nearest = river_xy[np.argmin(np.linalg.norm(river_xy - end_xy, axis=1))]
    to_river = (nearest - end_xy) / (np.linalg.norm(nearest - end_xy) + 1e-6)
    pad_end = nearest - to_river * (RIVER_HALF_WIDTH_M - 4.0)
    dock_z = WATER_ASL_M + 1.6
    m = capsule_mask(end_xy, pad_end, 9.0, 8.0)
    height = height * (1 - m) + dock_z * m
    boat_center = nearest - to_river * 6.0
    layout_vp["ta_lang_dock"] = {"xy": pad_end.tolist(), "z": dock_z, "river_xy": nearest.tolist(),
                                 "to_river": to_river.tolist(), "boat_xy": boat_center.tolist()}
    print(f"stamps {time.time() - t0:.1f}s")

    # --- quantise ---
    height = np.clip(height, BASE_ASL_M + 1.0, MAX_ASL_M - 1.0)
    h16 = np.round((height - BASE_ASL_M) / M_PER_UNIT).astype(np.uint16)

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    write_png16(OUT_DIR / "HaGiang_4033.png", h16)
    h16.astype("<u2").tofile(OUT_DIR / "HaGiang_4033.r16")

    # --- layout json (UE cm) ---
    def cm3(x, y, z):
        return [round(float(x) * 100.0, 1), round(float(y) * 100.0, 1), round(float(z) * 100.0, 1)]

    road_out = [cm3(road_xy[i, 0], road_xy[i, 1], road_z[i]) for i in range(0, len(road_xy), 5)]
    if (len(road_xy) - 1) % 5:
        road_out.append(cm3(road_xy[-1, 0], road_xy[-1, 1], road_z[-1]))

    def ground_z(xy):
        c = int(round(xy[0] + HALF_M)); r = int(round(xy[1] + HALF_M))
        return float(height[r, c])

    def road_heading(i):
        j = min(i + 3, len(road_xy) - 1); k0 = max(i - 3, 0)
        d = road_xy[j] - road_xy[k0]
        return math.degrees(math.atan2(d[1], d[0]))

    start_i = 15
    layout = {
        "heightmap": {"size": N, "png": "Generated/Heightmaps/HaGiang_4033.png",
                      "r16": "Generated/Heightmaps/HaGiang_4033.r16",
                      "location_cm": list(LANDSCAPE_LOCATION_CM), "scale": list(LANDSCAPE_SCALE),
                      "section_size_quads": 63, "sections_per_component": 2, "components": [32, 32],
                      "base_asl_m": BASE_ASL_M, "m_per_unit": M_PER_UNIT},
        "water": {"surface_z_cm": WATER_ASL_M * 100.0, "river_cm": [cm3(p[0], p[1], WATER_ASL_M) for p in river_xy]},
        "road": {"half_width_cm": ROAD_HALF_WIDTH_M * 100.0, "length_m": float(road_s[-1]),
                 "points_cm": road_out,
                 "sections_m": {k: float(v) for k, v in piece_len.items()}},
        "viewpoints": {
            "doc_tham_ma": {"name": "DOC THAM MA", "altitude_m": round(vp1_z),
                             "location_cm": cm3(*layout_vp["doc_tham_ma"]["xy"], vp1_z)},
            "ma_pi_leng": {"name": "DEO MA PI LENG - MOM DA TU THAN", "altitude_m": round(spur_z),
                            "location_cm": cm3(*spur_b, spur_z),
                            "road_cm": cm3(*rim_xy, road_z[rim_i])},
            "tu_san": {"name": "HEM VUC TU SAN - SONG NHO QUE", "altitude_m": round(WATER_ASL_M),
                        "location_cm": cm3(*pad_end, dock_z)},
        },
        "spawns": {
            "player_start": {"location_cm": cm3(road_xy[start_i, 0], road_xy[start_i, 1] + 6.0, road_z[start_i] + 1.2),
                             "yaw": road_heading(start_i)},
            "bikes": [
                {"location_cm": cm3(road_xy[start_i + 4, 0], road_xy[start_i + 4, 1], road_z[start_i + 4] + 0.6),
                 "yaw": road_heading(start_i + 4)},
                {"location_cm": cm3(vp1_xy[0] + 8, vp1_xy[1], vp1_z + 0.6), "yaw": 0.0},
                {"location_cm": cm3(rim_xy[0] - 1.5, rim_xy[1] - 12, road_z[rim_i] + 0.6), "yaw": 90.0},
            ],
            "boats": [
                {"type": "Cano", "location_cm": cm3(boat_center[0], boat_center[1] - 8, WATER_ASL_M)},
                {"type": "Kayak", "location_cm": cm3(boat_center[0], boat_center[1] + 8, WATER_ASL_M)},
            ],
            "dock": {"location_cm": cm3(*pad_end, dock_z), "yaw": math.degrees(math.atan2(to_river[1], to_river[0]))},
        },
        "seed": SEED,
    }
    (OUT_DIR / "HaGiang_layout.json").write_text(json.dumps(layout, indent=1), encoding="utf-8")

    write_preview(height, road_xy, river_xy, layout)
    print(f"done in {time.time() - t0:.1f}s -> {OUT_DIR}")
    print(f"altitude range {height.min():.0f}..{height.max():.0f} m")
    return layout


def write_png16(path, data):
    """Write a 16-bit grayscale PNG (big-endian samples) without relying on Pillow modes."""
    import struct
    import zlib

    h, w = data.shape
    rows = np.empty((h, 1 + w * 2), dtype=np.uint8)
    rows[:, 0] = 0  # filter: none
    rows[:, 1:] = data.astype(">u2").view(np.uint8).reshape(h, w * 2)

    def chunk(tag, payload):
        c = struct.pack(">I", len(payload)) + tag + payload
        return c + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 16, 0, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", zlib.compress(rows.tobytes(), 6)))
        f.write(chunk(b"IEND", b""))


def write_preview(height, road_xy, river_xy, layout, size=1344):
    step = (N - 1) // (size - 1) if (N - 1) % (size - 1) == 0 else 3
    h = height[::step, ::step]
    gy, gx = np.gradient(h, step * CELL_M)
    light = np.array([-0.6, -0.6, 0.55])
    light /= np.linalg.norm(light)
    nz = 1.0 / np.sqrt(gx * gx + gy * gy + 1.0)
    shade = np.clip((-gx * light[0] - gy * light[1] + light[2]) * nz, 0, 1)
    t = (h - 650.0) / (1600.0 - 650.0)
    t = np.clip(t, 0, 1)
    base = np.stack([0.35 + 0.45 * t, 0.45 + 0.35 * t, 0.30 + 0.40 * t], axis=-1)
    rgb = base * (0.35 + 0.65 * shade[..., None])
    water = h < WATER_ASL_M
    rgb[water] = [0.05, 0.55, 0.50]
    img = (np.clip(rgb, 0, 1) * 255).astype(np.uint8)

    def px(p):
        return int((p[0] + HALF_M) / step), int((p[1] + HALF_M) / step)

    for p in road_xy[::2]:
        c, r = px(p)
        img[max(r - 1, 0):r + 1, max(c - 1, 0):c + 1] = [235, 60, 40]
    for vp in layout["viewpoints"].values():
        x, y = vp["location_cm"][0] / 100.0, vp["location_cm"][1] / 100.0
        c, r = px((x, y))
        img[max(r - 6, 0):r + 6, max(c - 6, 0):c + 6] = [255, 220, 0]
    Image.fromarray(img).save(OUT_DIR / "HaGiang_preview.png")


if __name__ == "__main__":
    generate()
