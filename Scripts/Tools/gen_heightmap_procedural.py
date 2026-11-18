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
# Researched altitudes (see CHECKPOINT_LOG.md, "Research"): Tham Ma top ~1,490 m, Ma Pi Leng road
# ~1,230 m, Nho Que 1 reservoir ~420 m (Tu San ~800 m deep), peaks up to ~1,900 m.
BASE_ASL_M = 380.0            # height value 0 == 380 m above sea level
Z_SCALE = 300.0               # landscape Z scale -> 2.34 cm per height step, 1536 m range (380..1916 m)
M_PER_UNIT = Z_SCALE / 128.0 / 100.0   # 0.02 m
MAX_ASL_M = BASE_ASL_M + 65535 * M_PER_UNIT

WATER_ASL_M = 420.0           # Nho Que 1 hydropower reservoir (Ta Lang dock, Tu San gorge)
RIVER_BED_ASL_M = 404.0
DAM_Y_M = 1720.0              # Nho Que 1 dam: ~400 m downstream of the dock (dock ~1.4 km below the gorge)
DOWNSTREAM_WATER_ASL_M = 392.0
DOWNSTREAM_BED_ASL_M = 384.0
RIVER_HALF_WIDTH_M = 42.0   # Nho Que is a reservoir here: ~80-100 m wide
ROAD_HALF_WIDTH_M = 3.0        # QL4C: 6-7 m asphalt
CONCRETE_HALF_WIDTH_M = 2.0    # Ta Lang road: concrete, "two motorbikes wide" 
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

# Doc Tham Ma as on the map / photos: the QL4C climbs a narrow SW->NE valley (steep green mountain
# on its NW side), then folds into a tight cluster of ~4 irregular hairpins on the valley head wall;
# the check-in point / coffee stop sits at the top of the cluster.
THAMMA_A = np.array([-1960.0, -1960.0])          # valley mouth (map SW corner)
THAMMA_B = np.array([-1180.0, -1230.0])          # valley head / pass
THAMMA_D = (THAMMA_B - THAMMA_A) / np.linalg.norm(THAMMA_B - THAMMA_A)   # up-valley
THAMMA_N = np.array([-THAMMA_D[1], THAMMA_D[0]])                         # towards the NW mountain


def organic_switchbacks(origin, U, V, legs, rng, entry=None, exit_dir=None):
    """Irregular zig-zag of curved legs joined by hairpins.

    legs: list of (u_from, u_to, v) in metres in the (U, V) frame - legs need not be parallel or equal.
    Each leg gets a gentle wobble; each hairpin bulges past the leg end with its own radius.
    Returns the control points (world XY) for a Catmull-Rom curve."""
    O, U, V = np.asarray(origin, float), np.asarray(U, float), np.asarray(V, float)
    W = lambda u, v: O + U * u + V * v
    pts = [] if entry is None else [np.asarray(e, float) for e in entry]
    # A leg is (u0, u1, v) or (u0, u1, v_start, v_end): tilted legs make the fan-shaped,
    # non-parallel cluster seen on the satellite image of Doc Tham Ma.
    legs = [tuple(l) if len(l) == 4 else (l[0], l[1], l[2], l[2]) for l in legs]
    for i, (u0, u1, va, vb) in enumerate(legs):
        nxt = legs[i + 1] if i + 1 < len(legs) else None
        wob = rng.uniform(-9.0, 9.0)
        pts.append(W(u0, va))
        pts.append(W(u0 + (u1 - u0) * 0.35, va + (vb - va) * 0.35 + wob))
        pts.append(W(u0 + (u1 - u0) * 0.7, va + (vb - va) * 0.7 - wob * 0.6 + rng.uniform(-4, 4)))
        pts.append(W(u1, vb))
        if nxt is not None:
            r = (nxt[2] - vb) * 0.5
            bulge = np.sign(u1 - u0) * abs(r) * rng.uniform(0.9, 1.25)
            mid_u = (u1 + nxt[0]) * 0.5
            pts.append(W(u1 + bulge * 0.75, vb + r * 0.35))
            pts.append(W(mid_u + bulge, vb + r))
            pts.append(W(nxt[0] + bulge * 0.75, vb + r * 1.65))
    if exit_dir is not None:
        last = pts[-1]
        pts.append(last + np.asarray(exit_dir, float) * 40.0)
    return pts


def build_route():
    pieces = []      # (name, polyline)
    rng = np.random.default_rng(SEED + 99)
    D, N_ = THAMMA_D, THAMMA_N

    # Valley road along the floor, keeping slightly to the SE bank.
    valley_pts = [THAMMA_A + D * s_ + N_ * off for s_, off in
                  ((40, -25), (180, -18), (330, -28), (480, -15), (600, -22), (690, -10))]
    approach = catmull_rom(valley_pts)
    pieces.append(("approach", approach))

    # Head-wall cluster: U = across the valley (towards NW), V = up-valley. 5 legs = 4 hairpins.
    O = THAMMA_A + D * 720.0
    # Compact, irregular: legs 110-190 m, tilted -25..+30 deg, unequal hairpin radii.
    legs = [(-30.0, 120.0, 0.0, 30.0), (95.0, -40.0, 72.0, 40.0), (-65.0, 85.0, 78.0, 118.0),
            (70.0, -55.0, 160.0, 140.0), (-40.0, 150.0, 182.0, 205.0)]
    tm_pts = organic_switchbacks(O, N_, D, legs, rng, entry=[valley_pts[-1]], exit_dir=D)
    tham_ma = catmull_rom(tm_pts)
    pieces.append(("tham_ma", tham_ma))

    # Link + VP2 Ma Pi Leng rim road.
    E = tham_ma[-1]
    rim = catmull_rom([tuple(E), tuple(E + D * 90 + N_ * -10), (-1000, -1140), (-600, -1010),
                       (-250, -900), (100, -800), (350, -660), (560, -400), (675, -150), (705, 0),
                       (690, 250), (650, 520), (600, 720), (530, 830), (470, 880)])
    pieces.append(("rim", rim))

    # VP3 Ta Lang descent: the real concrete road has ~45 tiers / 47-50 bends over ~8 km and ~800 m.
    # In 4 km we fit 16 irregular tiers (~5 km, ~16 % average) stacked down the west wall.
    # Irregular like the photos: legs 120-400 m, each hairpin where the previous leg ended (so the
    # stack wanders along the contour), tilted legs, uneven tier spacing; ~5 km in total.
    legs = []
    v, u, total, i = 0.0, 10.0, 0.0, 0
    while total < 4700.0 and i < 30:
        lo, hi = (-70.0, 430.0)
        span = rng.uniform(120.0, 400.0)
        end = u + span if i % 2 == 0 else u - span
        end = float(np.clip(end, lo, hi))
        if abs(end - u) < 110.0:                      # bounced on the clamp: short leg the other way round
            end = u + 140.0 if i % 2 == 0 else u - 140.0
        dv = rng.uniform(-10.0, 16.0)
        legs.append((u, end, v, v + dv))
        total += abs(end - u) + 60.0
        v += dv + rng.uniform(24.0, 44.0)
        u = end + rng.uniform(-15.0, 15.0)
        i += 1
    scale = 600.0 / max(v, 1.0)                      # keep the stack within the 600 m wide west wall
    legs = [(a_, b_, c_ * scale, d_ * scale) for a_, b_, c_, d_ in legs]
    print(f"descent legs {len(legs)}, ~{total:.0f} m, tier spacing x{scale:.2f}")
    descent = catmull_rom(organic_switchbacks((470.0, 880.0), (0.0, 1.0), (1.0, 0.0), legs, rng))
    pieces.append(("descent", descent))

    end = descent[-1]
    dock_spur = catmull_rom([tuple(end), (end[0] + 6, end[1] - 14), (end[0] + 22, end[1] - 26),
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
    key_z = [1250.0, 1330.0, 1482.0, 1236.0, 1228.0, WATER_ASL_M + 2.0, WATER_ASL_M + 1.8]
    z = np.interp(s, key_s, key_z)
    z = smooth_1d(z, 41)   # ~80 m window: no grade kinks
    return poly, z, s, lengths


def build_river():
    pts = catmull_rom([(1150, -2150), (1010, -1720), (1120, -1280), (1010, -860), (1060, -430), (990, 0),
                       (1080, 400), (1130, 780), (1215, 1100), (1270, 1430), (1330, 1790), (1460, 2150)], step=5.0)
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
    (-1950, -1950, 1240), (-1700, -1850, 1260), (-1100, -1950, 1350), (-400, -1900, 1380), (400, -1900, 1350),
    (-1950, -1300, 1450), (-1900, -600, 1500), (-1200, -500, 1480), (-500, -300, 1420), (200, -200, 1350),
    (-1800, 400, 1520), (-1000, 600, 1480), (-200, 700, 1400), (300, 500, 1320),
    (-1700, 1500, 1560), (-900, 1600, 1520), (-100, 1700, 1450), (400, 1700, 1380), (300, 1300, 1320),
    (1600, -1900, 1450), (1700, -1000, 1500), (1750, 0, 1540), (1700, 900, 1520), (1800, 1900, 1450),
    (1500, -400, 1480), (1450, 500, 1500),
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
    ys = np.array([-2200, -1300, -850, 650, 950, 2200], dtype=np.float32)

    def smooth_interp(values):
        # Cosine interpolation between keys -> no visible derivative kinks on the walls.
        values = np.asarray(values, dtype=np.float32)
        i = np.clip(np.searchsorted(ys, y) - 1, 0, len(ys) - 2)
        t = np.clip((y - ys[i]) / (ys[i + 1] - ys[i]), 0.0, 1.0)
        t = 0.5 - 0.5 * np.cos(np.pi * t)
        return values[i] + (values[i + 1] - values[i]) * t

    # Tu San narrows (the 1.7 km gorge under Ma Pi Leng, y ~ -600..600) have near-vertical walls;
    # elsewhere the canyon is a steep V of vegetated slopes (~35-45 deg), as in the photos.
    W = smooth_interp([600, 560, 330, 330, 640, 640])     # Tu San narrows: near-vertical; V elsewhere
    k = smooth_interp([1.0, 1.0, 0.45, 0.45, 1.0, 1.0])
    rim = smooth_interp([1300, 1290, 1270, 1270, 1290, 1300])
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


# ---------------------------------------------------------------------------
# v2 terrain: fengcong karst (Worley), hillside, droplet erosion, strata
# ---------------------------------------------------------------------------

COARSE = 4   # erosion / karst work grid: every 4th vertex (4 m cells, 1009 x 1009)


def worley_karst(rng, cell_m=150.0, max_h=290.0, stride=COARSE):
    """Fengcong karst: one steep cone per jittered cell, cones meet in saddles and closed
    depressions (dolines) - the 'tai meo' stone forest look of Dong Van / Ma Pi Leng."""
    X, Y = grid_coords(N, stride)
    warp_x = (value_noise(N, 9, rng, stride) - 0.5) * 120.0
    warp_y = (value_noise(N, 9, rng, stride) - 0.5) * 120.0
    xw, yw = X + warp_x, Y + warp_y
    table = 64
    jit = rng.random((table, table, 2)).astype(np.float32) * 0.8 + 0.1
    amp = rng.random((table, table)).astype(np.float32)
    amp = np.where(amp < 0.18, amp * 0.3, 0.45 + 0.55 * amp)          # a few low cells = basins
    sharp = rng.uniform(1.1, 1.8, (table, table)).astype(np.float32)
    cx = np.floor(xw / cell_m).astype(np.int32)
    cy = np.floor(yw / cell_m).astype(np.int32)
    # Each cell owns one cone; the terrain is the MAX of all overlapping cones (continuous everywhere ->
    # no straight Voronoi seams), so neighbouring cones meet in natural saddles and dolines.
    h = np.zeros(X.shape, np.float32)
    radius = rng.uniform(0.55, 0.85, (table, table)).astype(np.float32) * cell_m
    for dj in (-2, -1, 0, 1, 2):
        for di in (-2, -1, 0, 1, 2):
            ix, iy = cx + di, cy + dj
            tx, ty = ix % table, iy % table
            px = (ix + jit[ty, tx, 0]) * cell_m
            py = (iy + jit[ty, tx, 1]) * cell_m
            d = np.hypot(xw - px, yw - py).astype(np.float32)
            cone = np.clip(1.0 - d / radius[ty, tx], 0.0, 1.0)
            # Steep flanks, rounded summit (smoothstep on top of the power curve).
            prof = cone ** sharp[ty, tx]
            prof = prof * prof * (3.0 - 2.0 * prof) * 0.35 + prof * 0.65
            np.maximum(h, max_h * amp[ty, tx] * prof, out=h)
    return h.astype(np.float32)


def thamma_hillside():
    """Doc Tham Ma valley: narrow V floor rising SW->NE (830 m -> 1055 m at the head wall), a big steep
    green mountain on the NW side (~ +480 m), a lower ridge on the SE side, plateau beyond the head."""
    X, Y = grid_coords(N, COARSE)
    rel_x, rel_y = X - THAMMA_A[0], Y - THAMMA_A[1]
    along = rel_x * THAMMA_D[0] + rel_y * THAMMA_D[1]
    side = rel_x * THAMMA_N[0] + rel_y * THAMMA_N[1]          # > 0 towards the NW mountain
    floor = np.interp(along, [-400, 0, 700, 990, 1150, 3000], [1225, 1250, 1330, 1486, 1505, 1510]).astype(np.float32)
    nw = 480.0 * smoothstep(20.0, 520.0, side) ** 0.85
    se = 260.0 * smoothstep(20.0, 420.0, -side) ** 0.9
    head = np.clip((along - 950.0) / 300.0, 0, 1)                # walls fade out above the pass
    hill = floor + (nw + se) * (1 - head)
    mask = (1 - smoothstep(1050.0, 1350.0, along)) * (1 - smoothstep(650.0, 900.0, np.abs(side)))
    return hill.astype(np.float32), mask.astype(np.float32)


def droplet_erosion(h, rng, drops=450_000, batch=150_000, steps=60):
    """Vectorised particle hydraulic erosion on the coarse grid (heights in metres)."""
    # Work in scaled units (metres / 20) so slopes look like Lague's 0..1 heightfields: stable rates.
    scale = 20.0
    H = h.astype(np.float64) / scale
    n = H.shape[0]
    inertia, capacity, min_slope = 0.08, 6.0, 0.02
    erode_rate, deposit_rate, evaporate, gravity = 0.35, 0.25, 0.02, 4.0
    flat = H.ravel()
    for b0 in range(0, drops, batch):
        m = min(batch, drops - b0)
        px = rng.uniform(1, n - 2, m); py = rng.uniform(1, n - 2, m)
        dx = np.zeros(m); dy = np.zeros(m)
        speed = np.ones(m); water = np.ones(m); sed = np.zeros(m)
        alive = np.ones(m, bool)
        for _ in range(steps):
            ix = px.astype(np.int64); iy = py.astype(np.int64)
            fx = px - ix; fy = py - iy
            i00 = iy * n + ix
            h00 = flat[i00]; h10 = flat[i00 + 1]; h01 = flat[i00 + n]; h11 = flat[i00 + n + 1]
            gx = (h10 - h00) * (1 - fy) + (h11 - h01) * fy
            gy = (h01 - h00) * (1 - fx) + (h11 - h10) * fx
            hc = (h00 * (1 - fx) + h10 * fx) * (1 - fy) + (h01 * (1 - fx) + h11 * fx) * fy
            dx = dx * inertia - gx * (1 - inertia)
            dy = dy * inertia - gy * (1 - inertia)
            ln = np.hypot(dx, dy)
            stuck = ln < 1e-9
            ln[stuck] = 1.0
            dx /= ln; dy /= ln
            nx = px + dx; ny = py + dy
            alive &= ~stuck & (nx > 1) & (nx < n - 2) & (ny > 1) & (ny < n - 2)
            nxc = np.clip(nx, 1, n - 2.001); nyc = np.clip(ny, 1, n - 2.001)
            jx = nxc.astype(np.int64); jy = nyc.astype(np.int64)
            gx2 = nxc - jx; gy2 = nyc - jy
            j00 = jy * n + jx
            hn = ((flat[j00] * (1 - gx2) + flat[j00 + 1] * gx2) * (1 - gy2)
                  + (flat[j00 + n] * (1 - gx2) + flat[j00 + n + 1] * gx2) * gy2)
            dh = hn - hc
            cap = np.maximum(-dh, min_slope) * speed * water * capacity
            deposit = np.where(dh > 0, np.minimum(dh, sed), np.where(sed > cap, (sed - cap) * deposit_rate, 0.0))
            erode = np.where((dh <= 0) & (sed <= cap), np.minimum((cap - sed) * erode_rate, -dh * 0.5), 0.0)
            erode = np.minimum(erode, 0.05) * alive
            deposit = deposit * alive
            sed += erode - deposit
            # Deposit on the 4 bilinear corners, erode with a 3x3 brush (no pits / spikes).
            w00 = (1 - fx) * (1 - fy); w10 = fx * (1 - fy); w01 = (1 - fx) * fy; w11 = fx * fy
            idx = [i00, i00 + 1, i00 + n, i00 + n + 1]
            wts = [w00 * deposit, w10 * deposit, w01 * deposit, w11 * deposit]
            for oy, ox, wb in ((-1, -1, 1), (-1, 0, 2), (-1, 1, 1), (0, -1, 2), (0, 0, 4), (0, 1, 2), (1, -1, 1), (1, 0, 2), (1, 1, 1)):
                idx.append(np.clip(i00 + oy * n + ox, 0, n * n - 1))
                wts.append(-erode * (wb / 16.0))
            flat += np.bincount(np.concatenate(idx), weights=np.concatenate(wts), minlength=n * n)
            speed = np.minimum(np.sqrt(np.maximum(speed * speed - dh * gravity, 0.0)), 6.0)
            water *= (1 - evaporate)
            px, py = nxc, nyc  # dead droplets stay clamped (they no longer change the map)
            if not alive.any():
                break
    return (H.reshape(n, n) * scale).astype(np.float32)


def blur3(a):
    p = np.pad(a, 1, mode="edge")
    return (p[:-2, :-2] + 2 * p[:-2, 1:-1] + p[:-2, 2:] + 2 * p[1:-1, :-2] + 4 * p[1:-1, 1:-1] + 2 * p[1:-1, 2:]
            + p[2:, :-2] + 2 * p[2:, 1:-1] + p[2:, 2:]) / 16.0


def terrace(z, base, step, warp):
    """Limestone strata: flat ledges + steep risers."""
    q = (z - base) / step + warp
    f = np.floor(q)
    fr = q - f
    return base + (f + smoothstep(0.5, 1.0, fr) - warp) * step


def generate():
    t0 = time.time()
    rng = np.random.default_rng(SEED)

    road_xy, road_z, road_s, piece_len = build_route()
    river_xy = build_river()
    print(f"road {road_s[-1] / 1000:.2f} km, {len(road_xy)} samples; river {len(river_xy)} pts")

    # --- distance fields ---
    road_d, road_val = polyline_distance(road_xy, road_z.astype(np.float32), margin=130.0)
    concrete_from = piece_len["rim"]
    road_hw = np.where(road_s >= concrete_from, CONCRETE_HALF_WIDTH_M, ROAD_HALF_WIDTH_M).astype(np.float32)
    _, road_hw_val = polyline_distance(road_xy, road_hw, margin=130.0)
    river_d, _ = polyline_distance(river_xy, np.zeros(len(river_xy), np.float32), margin=700.0)
    print(f"distance fields {time.time() - t0:.1f}s")

    # --- base: regional shape + road-following anchors + Tham Ma hillside (coarse 4 m grid) ---
    base = rbf_base(road_xy, road_z)[::COARSE, ::COARSE].copy()
    hill, hill_mask = thamma_hillside()
    base = base * (1 - 0.9 * hill_mask) + hill * (0.9 * hill_mask)

    # --- fengcong karst cones, kept away from the road corridor and the Tham Ma slope ---
    coarse_road_d, _ = polyline_distance(road_xy[::10], np.zeros(len(road_xy[::10]), np.float32), margin=400.0)
    road_c = np.minimum(coarse_road_d[::COARSE, ::COARSE], 1e6)
    del coarse_road_d
    karst = np.maximum(worley_karst(rng, cell_m=150.0, max_h=230.0),
                       worley_karst(rng, cell_m=380.0, max_h=300.0))
    ridges = (fbm(N, 7, 4, rng, ridged=True)[::COARSE, ::COARSE] - 0.25) * 120.0
    karst = karst + np.maximum(ridges, 0.0)
    karst_mask = smoothstep(25.0, 210.0, road_c) * (1 - 0.75 * hill_mask)
    relief = ((fbm(N, 10, 5, rng)[::COARSE, ::COARSE] - 0.5) * 36.0
              + (fbm(N, 28, 4, rng, ridged=True)[::COARSE, ::COARSE] - 0.3) * 20.0)
    coarse = base + karst * karst_mask + relief * (0.3 + 0.7 * smoothstep(8.0, 60.0, road_c))
    print(f"karst {time.time() - t0:.1f}s")

    # --- hydraulic erosion: gullies, debris fans, softened valley floors ---
    eroded = droplet_erosion(coarse, rng)
    delta = blur3(blur3(eroded - coarse)) * 0.8         # softer: gullies, not melted blobs
    print(f"erosion {time.time() - t0:.1f}s (mean |dz| {np.abs(delta).mean():.2f} m, max cut {delta.min():.1f} m)")
    height = upsample(coarse + delta, N)
    del coarse, eroded, delta, base, karst, relief, hill, hill_mask, karst_mask, road_c

    fine = (fbm(N, 400, 3, rng) - 0.5) * 1.4 + (fbm(N, 120, 3, rng, ridged=True) - 0.3) * 3.0
    near_road = smoothstep(8.0, 45.0, np.minimum(road_d, 1e6))
    height += fine * (0.2 + 0.8 * near_road)

    # --- Nho Que canyon (cutter, min with terrain) ---
    _, Yg = grid_coords(N)
    W, k, rim = canyon_params(Yg.astype(np.float32))
    del Yg
    rd = np.minimum(river_d, 5000.0)
    _, Yd = grid_coords(N)
    below = (Yd > DAM_Y_M).astype(np.float32)
    del Yd
    bed = RIVER_BED_ASL_M * (1 - below) + DOWNSTREAM_BED_ASL_M * below
    surf = WATER_ASL_M * (1 - below) + DOWNSTREAM_WATER_ASL_M * below
    chan = bed + (np.clip(rd / RIVER_HALF_WIDTH_M, 0, 1) ** 2) * (surf - 2.0 - bed)
    t = np.clip((rd - RIVER_HALF_WIDTH_M) / (W - RIVER_HALF_WIDTH_M), 0.0, 1.0)
    wall_base = surf - 2.0
    # Beyond the wall the cutter climbs fast, so the natural terrain takes over.
    wall = wall_base + (rim - wall_base) * np.power(t, k) + np.maximum(rd - W, 0.0) * 8.0
    cutter = np.where(rd < RIVER_HALF_WIDTH_M, chan, wall).astype(np.float32)
    del bed, surf, below
    # Rough-up the canyon walls a little (strata).
    strata = (fbm(N, 60, 4, rng) - 0.5) * 30.0 + (fbm(N, 140, 3, rng, ridged=True) - 0.3) * 35.0
    cutter += (strata * np.clip(t * (1 - t) * 4.0, 0.0, 1.0)).astype(np.float32)
    del strata
    # Limestone strata: ledges + near-vertical risers, levels warped so they are not perfectly flat.
    wall_mask = np.clip(t * 6.0, 0, 1) * (rd >= RIVER_HALF_WIDTH_M)
    level_warp = (value_noise(N, 14, rng) - 0.5) * 0.6
    _, Yg2 = grid_coords(N)
    gorge = (smoothstep(-950, -700, Yg2) * (1 - smoothstep(700, 950, Yg2))).astype(np.float32)
    del Yg2
    strata_amt = (0.1 + 0.25 * value_noise(N, 6, rng)) * gorge   # strata only in the Tu San narrows
    tz = terrace(cutter, WATER_ASL_M, 60.0, level_warp)          # a few big strata; fine steps read as ripples
    cutter = (cutter * (1 - strata_amt * wall_mask) + tz * (strata_amt * wall_mask)).astype(np.float32)
    del tz, strata_amt
    del wall_mask, level_warp
    # Smooth minimum (k = 35 m): the canyon rim rolls over instead of leaving a hard crease.
    kk = 35.0
    hdiff = np.clip(0.5 + 0.5 * (cutter - height) / kk, 0.0, 1.0)
    height = (cutter * (1 - hdiff) + height * hdiff - kk * hdiff * (1 - hdiff)).astype(np.float32)
    del hdiff
    del cutter, chan, wall, t, W, k, rim
    print(f"canyon {time.time() - t0:.1f}s")

    # --- road carving (cut & fill, width-dependent banks) ---
    core = road_hw_val + ROAD_SHOULDER_M
    # Between stacked hairpin legs the nearest-leg road height jumps (Voronoi seam -> a cliff step).
    # Banks blend towards a smoothed road-height field (4 m grid, ~18 m blur, bilinear back up);
    # only the road core uses the exact centreline height.
    from PIL import Image as _Image
    rv = np.where(np.isfinite(road_val), road_val, height).astype(np.float32)
    c4 = rv[:N - 1, :N - 1].reshape((N - 1) // 4, 4, (N - 1) // 4, 4).mean(axis=(1, 3))
    for _ in range(25):
        c4 = blur3(c4)
    rv_s = np.asarray(_Image.fromarray(c4.astype(np.float32), mode="F").resize((N, N), _Image.BILINEAR), dtype=np.float32)
    wcore = 1.0 - smoothstep(core, core + 10.0, np.minimum(road_d, 1e6))
    target = np.where(np.isfinite(road_val), road_val * wcore + rv_s * (1.0 - wcore), height).astype(np.float32)
    del rv, c4, rv_s, wcore
    diff = np.abs(height - target)
    cut = height > target
    bank = np.where(cut, np.clip(6.0 + 1.5 * diff, 6.0, 110.0), np.clip(5.0 + 0.6 * diff, 5.0, 30.0))
    w = 1.0 - smoothstep(core, core + bank, np.minimum(road_d, 1e6))
    w = np.where(np.isfinite(road_d), w, 0.0).astype(np.float32)
    # Road surface sits on a 0.15 m cut so the road mesh is never buried.
    height = (height * (1.0 - w) + (target - 0.15) * w).astype(np.float32)
    del diff, bank, cut, target

    # --- viewpoint stamps ---
    layout_vp = {}
    # VP1 Doc Tham Ma check-in pad (top of the hairpins).
    top_i = int(np.searchsorted(road_s, piece_len["tham_ma"]))
    vp1_road = road_xy[min(top_i + 20, len(road_xy) - 1)]
    vp1_xy = vp1_road - THAMMA_D * 24.0 + THAMMA_N * 4.0     # terrace on the valley side of the pass
    vp1_z = float(road_z[min(top_i + 20, len(road_z) - 1)])
    m = capsule_mask(vp1_road - THAMMA_D * 6.0, vp1_xy, 11.0, 7.0)
    height = height * (1 - m) + (vp1_z - 0.15) * m
    layout_vp["doc_tham_ma"] = {"xy": vp1_xy.tolist(), "z": vp1_z}

    # VP2 Mom Da Tu Than: rock spur jutting east from the rim road over the canyon.
    rim_i = int(np.argmin(np.abs(road_xy[:, 1]) + 1000.0 * (road_s < piece_len["tham_ma"])
                          + 1000.0 * (road_s > piece_len["rim"])))
    rim_xy = road_xy[rim_i]
    spur_a = rim_xy + np.array([ROAD_HALF_WIDTH_M + 1.0, -2.0])
    spur_b = rim_xy + np.array([74.0, 9.0])
    spur_z = float(road_z[rim_i]) - 3.0
    m = capsule_mask(spur_a, spur_b, 5.0, 1.0)
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

    # Landmark pads (ART_BIBLE section 3), cut into the uphill side of the road like the real
    # roadside cafes: Ma Pi Leng summit plaza (monument + name stone + Panorama cafe), Doc Tham Ma
    # coffee shop, Ta Lang dock yard (ticket booth + parking).
    def road_frame(i):
        j = min(i + 4, len(road_xy) - 1); k0 = max(i - 4, 0)
        t_ = road_xy[j] - road_xy[k0]; t_ = t_ / (np.linalg.norm(t_) + 1e-6)
        n_ = np.array([-t_[1], t_[0]])
        probe = lambda sgn: height[int(np.clip(round(road_xy[i, 1] + n_[1] * sgn * 25 + HALF_M), 0, N - 1)),
                                   int(np.clip(round(road_xy[i, 0] + n_[0] * sgn * 25 + HALF_M), 0, N - 1))]
        up = 1.0 if probe(1.0) > probe(-1.0) else -1.0                # uphill side
        return t_, n_ * up

    landmarks = {}
    def stamp_pad(key, i, depth, length, offset):
        t_, up = road_frame(i)
        z_ = float(road_z[i])
        c_ = road_xy[i] + up * (road_hw_at(i) + offset + depth * 0.5)
        global_mask = capsule_mask(c_ - t_ * length * 0.5, c_ + t_ * length * 0.5, depth * 0.5, 6.0)
        nonlocal_height[0] = nonlocal_height[0] * (1 - global_mask) + (z_ - 0.15) * global_mask
        landmarks[key] = {"xy": c_.tolist(), "z": z_, "yaw": math.degrees(math.atan2(t_[1], t_[0])),
                          "up": up.tolist(), "road_xy": road_xy[i].tolist()}

    road_hw_at = lambda i: CONCRETE_HALF_WIDTH_M if road_s[i] >= piece_len["rim"] else ROAD_HALF_WIDTH_M
    nonlocal_height = [height]
    mpl_i = int(np.searchsorted(road_s, road_s[rim_i] - 140.0))
    stamp_pad("ma_pi_leng_plaza", mpl_i, 18.0, 46.0, 1.0)
    stamp_pad("tham_ma_cafe", min(top_i + 48, len(road_xy) - 1), 14.0, 24.0, 1.0)
    height = nonlocal_height[0]
    # Dock yard: flat apron behind the slipway for the ticket booth, cafe and parked bikes.
    yard = end_xy - to_river * 16.0
    m = capsule_mask(end_xy - to_river * 4.0, yard, 11.0, 6.0)
    height = height * (1 - m) + (dock_z + 0.6) * m
    landmarks["dock_yard"] = {"xy": yard.tolist(), "z": dock_z + 0.6,
                              "yaw": math.degrees(math.atan2(to_river[1], to_river[0])), "up": (-to_river).tolist()}
    print(f"stamps {time.time() - t0:.1f}s")

    # --- quantise ---
    top = MAX_ASL_M - 60.0
    height = np.where(height > top, top + 55.0 * np.tanh((height - top) / 55.0), height)
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

    # Nho Que 1 dam across the river at DAM_Y_M.
    di = int(np.argmin(np.abs(river_xy[:, 1] - DAM_Y_M)))
    dd = river_xy[min(di + 1, len(river_xy) - 1)] - river_xy[max(di - 1, 0)]
    dam_info = {"location_cm": cm3(river_xy[di, 0], river_xy[di, 1], DOWNSTREAM_BED_ASL_M),
                "yaw": math.degrees(math.atan2(dd[1], dd[0])), "crest_z_cm": (WATER_ASL_M + 4.0) * 100.0,
                "width_cm": (RIVER_HALF_WIDTH_M * 2 + 40.0) * 100.0}

    start_i = 15
    layout = {
        "heightmap": {"size": N, "png": "Generated/Heightmaps/HaGiang_4033.png",
                      "r16": "Generated/Heightmaps/HaGiang_4033.r16",
                      "location_cm": list(LANDSCAPE_LOCATION_CM), "scale": list(LANDSCAPE_SCALE),
                      "section_size_quads": 63, "sections_per_component": 2, "components": [32, 32],
                      "base_asl_m": BASE_ASL_M, "m_per_unit": M_PER_UNIT},
        "water": {"surface_z_cm": WATER_ASL_M * 100.0, "river_cm": [cm3(p[0], p[1], WATER_ASL_M) for p in river_xy],
                  "dam_y_cm": DAM_Y_M * 100.0, "downstream_surface_z_cm": DOWNSTREAM_WATER_ASL_M * 100.0,
                  "dam": dam_info},
        "road": {"half_width_cm": ROAD_HALF_WIDTH_M * 100.0, "length_m": float(road_s[-1]),
                 "concrete_half_width_cm": CONCRETE_HALF_WIDTH_M * 100.0, "concrete_from_m": float(piece_len["rim"]),
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
            "player_start": {"location_cm": cm3(road_xy[start_i, 0], road_xy[start_i, 1] + 2.0, road_z[start_i] + 1.6),
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
        "landmarks": {k: {"location_cm": cm3(*v["xy"], v["z"]), "yaw": v["yaw"], "up": v["up"]}
                      for k, v in landmarks.items()},
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


if __name__ == "__main__":  # legacy procedural map; the game now uses gen_heightmap.py (real DEM + OSM)
    generate()
