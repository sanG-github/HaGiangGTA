#!/usr/bin/env python3
"""Ha Giang heightmap from real data: Copernicus GLO-30 elevation + OpenStreetMap roads / water.

Frame: 5.89 km x 5.89 km centred on 23.2450 N 105.4040 E - Ta Lang road, Tu San gorge, Ma Pi Leng,
Nho Que 1 reservoir and dam - at true scale (4033 x 4033 vertices, 1.46 m). Doc Tham Ma lies ~25 km
further west, so its real terrain (1.5 km square around the viewpoint) is pasted into the SW part
of the frame and joined to QL4C by a short link road.

Axes: UE is left-handed, X = east, Y = south (north is -Y). World Z (cm) == altitude ASL (m) * 100.

Inputs (Data/Geo/, not committed - see Data/Geo/README.md):
  Copernicus_N23_E105.tif   Copernicus DEM GLO-30 tile (pixel-is-point, WGS84)
  osm.json                  Overpass export (roads, rivers, water, dams, POIs)
Outputs (Generated/Heightmaps/): HaGiang_4033.png/.r16, HaGiang_layout.json, HaGiang_preview.png

Usage: .venv/bin/python Scripts/Tools/gen_heightmap.py   (numpy, pillow, tifffile, imagecodecs)
"""
from __future__ import annotations

import json
import math
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_heightmap_procedural as proc  # noqa: E402  shared helpers (splines, noise, erosion, PNG16)

ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT / "Generated" / "Heightmaps"
GEO = ROOT / "Data" / "Geo"

# ---------------------------------------------------------------------------
# Grid / landscape
# ---------------------------------------------------------------------------
N = 4033
CELL_M = 1.46
HALF_M = (N - 1) * CELL_M / 2.0          # 2943 m: all of QL4C Ta Lang junction -> south entry + the dam
LAT0, LON0 = 23.2450, 105.40396
M_PER_DEG_LAT = 110747.5                 # at 23.2 deg N


def m_per_deg_lon(lat):
    return 111412.84 * math.cos(math.radians(lat)) - 93.5 * math.cos(math.radians(3 * lat))


BASE_ASL_M = 380.0
Z_SCALE = 300.0
M_PER_UNIT = Z_SCALE / 128.0 / 100.0
MAX_ASL_M = BASE_ASL_M + 65535 * M_PER_UNIT
LANDSCAPE_LOCATION_CM = (-HALF_M * 100.0, -HALF_M * 100.0, BASE_ASL_M * 100.0 + 32768 * Z_SCALE / 128.0)
LANDSCAPE_SCALE = (CELL_M * 100.0, CELL_M * 100.0, Z_SCALE)

ROAD_HALF_WIDTH_M = 3.0          # QL4C asphalt
CONCRETE_HALF_WIDTH_M = 2.0      # Ta Lang concrete road
ROAD_SHOULDER_M = 1.0
RIVER_HALF_WIDTH_M = 26.0        # Tu San narrows (the wide reservoir comes from the OSM polygon)
DOWNSTREAM_DROP_M = 18.0         # water level below the Nho Que 1 dam
SEED = 20261118

# Real places (OSM ids / nodes; checked against the Google Maps screenshots in docs/ART_BIBLE.md)
THAMMA_VIEW = (23.16892, 105.19415)          # "Thẩm Mã" viewpoint node
THAMMA_CAFE = (23.16944, 105.19467)          # Coffee Dốc Thẩm Mã
PANORAMA = (23.22705, 105.41789)             # Mã Pí Lèng Panorama restaurant / viewpoint
YOUTH_MONUMENT = (23.24972, 105.39370)       # Youth Volunteers monument
TA_LANG_WAY = 855373895                      # Ta Lang road: QL4C -> Nho Que (35 hairpins)
DAM_WAY = 1320784007                         # Đập thủy điện Nho Quế 1
RESERVOIR_WAY = 555462463                    # natural=water reservoir polygon
PATCH_CENTER = np.array([0.0, 2150.0])    # Tham Ma patch position in the frame
PATCH_CORE, PATCH_BAND = 560.0, 220.0        # real terrain inside CORE, blended over BAND


def to_xy(lat, lon):
    return np.array([(lon - LON0) * m_per_deg_lon(LAT0), -(lat - LAT0) * M_PER_DEG_LAT])


def to_patch_xy(lat, lon):
    return PATCH_CENTER + np.array([(lon - THAMMA_VIEW[1]) * m_per_deg_lon(THAMMA_VIEW[0]),
                                    -(lat - THAMMA_VIEW[0]) * M_PER_DEG_LAT])


# ---------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------

def load_dem():
    import tifffile
    return tifffile.imread(GEO / "Copernicus_N23_E105.tif").astype(np.float64)


def dem_point(dem, lat, lon):
    r, c = (24.0 - lat) * 3600.0, (lon - 105.0) * 3600.0
    r0, c0 = int(r), int(c)
    fr, fc = r - r0, c - c0
    return float((dem[r0, c0] * (1 - fc) + dem[r0, c0 + 1] * fc) * (1 - fr)
                 + (dem[r0 + 1, c0] * (1 - fc) + dem[r0 + 1, c0 + 1] * fc) * fr)


def cubic_matrix(f, smin, size):
    """Catmull-Rom interpolation weights from source samples [smin, smin+size) to positions f."""
    W = np.zeros((len(f), size))
    i = np.floor(f).astype(int)
    t = f - i
    w = (0.5 * (-t + 2 * t * t - t ** 3), 0.5 * (2 - 5 * t * t + 3 * t ** 3),
         0.5 * (t + 4 * t * t - 3 * t ** 3), 0.5 * (-t * t + t ** 3))
    rows = np.arange(len(f))
    for k in range(4):
        np.add.at(W, (rows, i - 1 + k - smin), w[k])
    return W


def sample_dem(dem, lats, lons):
    """Bicubic DEM on the separable grid lats (rows) x lons (cols)."""
    rf, cf = (24.0 - lats) * 3600.0, (lons - 105.0) * 3600.0
    r0, r1 = int(math.floor(rf.min())) - 2, int(math.ceil(rf.max())) + 3
    c0, c1 = int(math.floor(cf.min())) - 2, int(math.ceil(cf.max())) + 3
    sub = dem[r0:r1, c0:c1]
    return (cubic_matrix(rf, r0, r1 - r0) @ sub @ cubic_matrix(cf, c0, c1 - c0).T).astype(np.float32)


def load_osm():
    d = json.loads((GEO / "osm.json").read_text(encoding="utf-8"))
    ways = {e["id"]: e for e in d["elements"] if e["type"] == "way" and "geometry" in e}
    return ways


def latlon(way):
    return np.array([(g["lat"], g["lon"]) for g in way["geometry"]])


def chain_ways(ways, seed_id, tol_deg=2e-5):
    """Join ways sharing end nodes into one polyline, growing both ends from seed_id."""
    pool = {k: latlon(v) for k, v in ways.items() if k != seed_id}
    line = latlon(ways[seed_id])
    grown = True
    while grown:
        grown = False
        for k, pts in list(pool.items()):
            for end in (0, -1):
                for other in (0, -1):
                    if np.abs(line[end] - pts[other]).max() < tol_deg:
                        seg = pts if other == 0 else pts[::-1]          # seg starts at the shared node
                        line = np.vstack([seg[::-1][:-1], line]) if end == 0 else np.vstack([line, seg[1:]])
                        del pool[k]
                        grown = True
                        break
                if grown:
                    break
            if grown:
                break
    return line


def dense(xy, step=4.0):
    """Linear resample of a polyline (metres) - keeps OSM corners, avoids spline overshoot."""
    xy, _ = proc.resample(np.asarray(xy, float), step)
    return xy


def nearest_index(poly, p):
    return int(np.argmin(np.linalg.norm(poly - np.asarray(p)[None, :2], axis=1)))


# ---------------------------------------------------------------------------
# Grid helpers (1.46 m cells)
# ---------------------------------------------------------------------------

def to_px(xy):
    return (np.asarray(xy, float) + HALF_M) / CELL_M


def coords():
    c = np.arange(N, dtype=np.float64) * CELL_M - HALF_M     # float64: lon ~105.4 needs it (float32 -> 0.8 m steps)
    return c, c


def polyline_distance(poly, values, margin):
    """Distance (m) from every vertex to the polyline + interpolated per-vertex values (inf outside margin)."""
    dist = np.full((N, N), np.inf, dtype=np.float32)
    val = np.zeros((N, N), dtype=np.float32)
    for i in range(len(poly) - 1):
        a, b = poly[i], poly[i + 1]
        lo = (np.minimum(a, b) - margin + HALF_M) / CELL_M
        hi = (np.maximum(a, b) + margin + HALF_M) / CELL_M
        c0, c1 = max(0, int(lo[0])), min(N - 1, int(math.ceil(hi[0])))
        r0, r1 = max(0, int(lo[1])), min(N - 1, int(math.ceil(hi[1])))
        if c0 > c1 or r0 > r1:
            continue
        xs = np.arange(c0, c1 + 1, dtype=np.float32) * CELL_M - HALF_M
        ys = np.arange(r0, r1 + 1, dtype=np.float32) * CELL_M - HALF_M
        px, py = xs[None, :] - a[0], ys[:, None] - a[1]
        ab = b - a
        t = np.clip((px * ab[0] + py * ab[1]) / max(float(ab @ ab), 1e-9), 0.0, 1.0)
        d = np.sqrt((px - t * ab[0]) ** 2 + (py - t * ab[1]) ** 2)
        sub = dist[r0:r1 + 1, c0:c1 + 1]
        better = d < sub
        if better.any():
            sub[better] = d[better]
            val[r0:r1 + 1, c0:c1 + 1][better] = (values[i] + (values[i + 1] - values[i]) * t)[better]
    return dist, val


def capsule(p0, p1, radius, feather):
    d, _ = polyline_distance(np.array([p0, p1], float), np.zeros(2), radius + feather + 2)
    return 1.0 - proc.smoothstep(radius, radius + feather, d)


def blur_coarse(a, k, iters):
    """Box-downsample by k, blur, bilinear back up (cheap wide blur)."""
    m = (N - 1) // k
    c = a[:m * k, :m * k].reshape(m, k, m, k).mean(axis=(1, 3)).astype(np.float32)
    for _ in range(iters):
        c = proc.blur3(c)
    return np.asarray(Image.fromarray(c, mode="F").resize((N, N), Image.BICUBIC), dtype=np.float32)


def bilinear(h, xy):
    f = np.clip(to_px(xy), 0, N - 1.001)
    c0, r0 = f[..., 0].astype(int), f[..., 1].astype(int)
    tx, ty = f[..., 0] - c0, f[..., 1] - r0
    return ((h[r0, c0] * (1 - tx) + h[r0, c0 + 1] * tx) * (1 - ty)
            + (h[r0 + 1, c0] * (1 - tx) + h[r0 + 1, c0 + 1] * tx) * ty)


def least_cost_road(height, a, b, spacing=8.0, margin=450.0, ideal_grade=0.07, avoid=None, avoid_m=45.0):
    """Dijkstra on a coarse grid: the path a road engineer would take - follows the contours,
    grade penalised above ~7 %, no water. Returns a smoothed polyline (m)."""
    import heapq
    lo = np.maximum(np.minimum(a, b) - margin, -HALF_M + 20)
    hi = np.minimum(np.maximum(a, b) + margin, HALF_M - 20)
    gx_ = np.arange(lo[0], hi[0], spacing)
    gy_ = np.arange(lo[1], hi[1], spacing)
    XX, YY = np.meshgrid(gx_, gy_)
    H = bilinear(height, np.stack([XX, YY], -1)).astype(np.float64)
    ny_, nx_ = H.shape
    # Keep clear of an existing road (no doubled roads) except within 90 m of the end point.
    pen = np.ones(H.shape)
    if avoid is not None:
        pts_ = np.stack([XX.ravel(), YY.ravel()], 1)
        dmin = np.full(len(pts_), np.inf)
        for q in avoid[::2]:
            np.minimum(dmin, np.hypot(pts_[:, 0] - q[0], pts_[:, 1] - q[1]), out=dmin)
        dmin = dmin.reshape(H.shape)
        near_end = np.hypot(XX - b[0], YY - b[1]) < 90.0
        pen = np.where(near_end, 1.0, 1.0 + 12.0 * np.exp(-(dmin / avoid_m) ** 2))
    idx = lambda p_: (int(round((p_[1] - lo[1]) / spacing)), int(round((p_[0] - lo[0]) / spacing)))
    sa, sb = idx(a), idx(b)
    dist = np.full(H.shape, np.inf)
    prev = np.full(H.shape + (2,), -1, int)
    dist[sa] = 0.0
    heap = [(0.0, sa)]
    nbr = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1), (-2, -1), (-2, 1), (2, -1), (2, 1),
           (-1, -2), (1, -2), (-1, 2), (1, 2)]
    while heap:
        d0, (r, c) = heapq.heappop(heap)
        if (r, c) == sb:
            break
        if d0 > dist[r, c]:
            continue
        for dr_, dc_ in nbr:
            r2, c2 = r + dr_, c + dc_
            if not (0 <= r2 < ny_ and 0 <= c2 < nx_):
                continue
            L = spacing * math.hypot(dr_, dc_)
            g = abs(H[r2, c2] - H[r, c]) / L
            cost = L * (1.0 + (max(g - ideal_grade, 0.0) / 0.03) ** 2) * 0.5 * (pen[r, c] + pen[r2, c2])
            nd = d0 + cost
            if nd < dist[r2, c2]:
                dist[r2, c2] = nd
                prev[r2, c2] = (r, c)
                heapq.heappush(heap, (nd, (r2, c2)))
    path = [sb]
    while path[-1] != sa and prev[path[-1]][0] >= 0:
        path.append(tuple(prev[path[-1]]))
    pts = np.array([[gx_[c], gy_[r]] for r, c in path[::-1]], float)
    pts[0], pts[-1] = a, b
    for _ in range(3):                                   # relax the grid zig-zag
        pts[1:-1] = (pts[:-2] + 2 * pts[1:-1] + pts[2:]) / 4.0
    return pts


# ---------------------------------------------------------------------------
# Generation
# ---------------------------------------------------------------------------

def generate():
    t0 = time.time()
    rng = np.random.default_rng(SEED)
    dem = load_dem()
    ways = load_osm()
    xs, ys = coords()

    # --- 1. real terrain of the frame (bicubic from 30 m) ---
    lats = LAT0 - ys / M_PER_DEG_LAT
    lons = LON0 + xs / m_per_deg_lon(LAT0)
    height = sample_dem(dem, lats, lons)
    print(f"dem {time.time() - t0:.1f}s  {height.min():.0f}..{height.max():.0f} m")

    # --- 2. Doc Tham Ma patch (real terrain around the viewpoint), level-matched to the frame ---
    p_lats = THAMMA_VIEW[0] - (ys - PATCH_CENTER[1]) / M_PER_DEG_LAT
    p_lons = THAMMA_VIEW[1] + (xs - PATCH_CENTER[0]) / m_per_deg_lon(THAMMA_VIEW[0])
    patch = sample_dem(dem, p_lats, p_lons)
    X, Y = np.meshgrid(xs, ys)
    cheb = np.maximum(np.abs(X - PATCH_CENTER[0]), np.abs(Y - PATCH_CENTER[1]))
    ring = (cheb > PATCH_CORE) & (cheb < PATCH_CORE + PATCH_BAND)
    # Real altitude (no offset): the HUD location card and the altimeter then agree (986 m at the viewpoint);
    # the blend band absorbs the difference to the surrounding frame terrain.
    patch_offset = 0.0
    wpatch = 1.0 - proc.smoothstep(PATCH_CORE, PATCH_CORE + PATCH_BAND, cheb)
    height = (height * (1 - wpatch) + (patch + patch_offset) * wpatch).astype(np.float32)
    del patch, ring, X, Y
    print(f"tham ma patch offset {patch_offset:+.0f} m")

    # --- 3. sub-DEM detail: multi-octave relief scaled by slope (30 m data is too smooth at 1.3 m) ---
    gy, gx = np.gradient(height, CELL_M)
    slope = np.degrees(np.arctan(np.hypot(gx, gy)))
    del gx, gy
    amp = np.clip(0.35 + slope / 45.0, 0.35, 1.6).astype(np.float32)
    detail = np.zeros((N, N), np.float32)
    for wave_m, a in ((160.0, 5.0), (80.0, 2.6), (40.0, 1.4), (20.0, 0.7), (10.0, 0.35)):
        cells = max(2, int(2 * HALF_M / wave_m))
        detail += (proc.value_noise(N, cells, rng) - 0.5) * 2.0 * a
    height += detail * amp
    del detail, amp
    # Light hydraulic erosion (5.2 m work grid): gullies and debris fans on the slopes.
    k = 4
    m = (N - 1) // k + 1
    coarse = height[::k, ::k].copy()
    eroded = proc.droplet_erosion(coarse, rng, drops=260_000, batch=130_000, steps=50)
    delta = proc.blur3(eroded - coarse).astype(np.float32)          # bicubic: no 5.8 m facet stripes
    height += np.asarray(Image.fromarray(delta, mode="F").resize((N, N), Image.BICUBIC), dtype=np.float32) * 0.8
    del coarse, eroded
    print(f"detail+erosion {time.time() - t0:.1f}s")

    # --- 4. water: Nho Que 1 reservoir (OSM polygon + river line), dam, lower river ---
    river_ways = {k_: w for k_, w in ways.items() if w.get("tags", {}).get("waterway") == "river"}
    seed = max((k_ for k_, w in river_ways.items() if w["tags"].get("name") == "Sông Nho Quế"),
               key=lambda k_: len(river_ways[k_]["geometry"]))
    river_ll = chain_ways(river_ways, seed)
    river_xy = dense(np.array([to_xy(*p) for p in river_ll]), 5.0)
    if river_xy[0, 1] > river_xy[-1, 1]:
        river_xy = river_xy[::-1]                                          # flow: north (-Y) first
    inside = (np.abs(river_xy) < HALF_M + 50).all(axis=1)
    river_xy = river_xy[np.argmax(inside):len(inside) - np.argmax(inside[::-1])]
    dam_ll = latlon(ways[DAM_WAY])
    dam_pts = np.array([to_xy(*p) for p in dam_ll])
    dam_c = dam_pts.mean(axis=0)
    # The dam is mapped as a closed outline: its long axis (PCA) is the crest line.
    _, _, vt = np.linalg.svd(dam_pts - dam_c)
    dam_dir = vt[0]
    proj = (dam_pts - dam_c) @ dam_dir
    dam_len = float(proj.max() - proj.min())
    di = nearest_index(river_xy, dam_c)
    flow = river_xy[min(di + 4, len(river_xy) - 1)] - river_xy[max(di - 4, 0)]
    flow /= np.linalg.norm(flow)

    img = Image.new("L", (N, N), 0)
    dr = ImageDraw.Draw(img)
    res = latlon(ways[RESERVOIR_WAY])
    dr.polygon([tuple(to_px(to_xy(*p))) for p in res], fill=255)
    dr.line([tuple(p) for p in to_px(river_xy)], fill=255, width=int(2 * RIVER_HALF_WIDTH_M / CELL_M))
    water = np.asarray(img, np.float32) / 255.0 > 0.5
    del img, dr
    # The DSM's river surface is wider than the mapped line: low flat ground within ~70 m is water too
    # (otherwise it shows as a sandy beach - the Tu San walls drop straight into the water).
    near = blur_coarse(water.astype(np.float32), 4, 12) > 0.01
    low = height < float(np.median(height[water][::50])) + 2.5
    water = water | (near & low)
    del near, low
    gx_, gy_ = np.meshgrid(xs, ys)
    downstream = ((gx_ - dam_c[0]) * flow[0] + (gy_ - dam_c[1]) * flow[1]) > 0
    del gx_, gy_
    up_mask = water & ~downstream
    WATER_ASL_M = round(float(np.median(height[up_mask][::50])) - 2.0, 1)  # DSM water is noisy: a bit below
    DOWN_ASL_M = WATER_ASL_M - DOWNSTREAM_DROP_M
    surf = np.where(downstream, DOWN_ASL_M, WATER_ASL_M).astype(np.float32)
    depth = blur_coarse(water.astype(np.float32), 4, 8)                     # 0.5 at the shore, 1 inside
    bed = surf - 1.5 - 32.0 * np.clip((depth - 0.5) * 2.0, 0, 1)
    height = np.where(water, np.minimum(height, bed), height)
    bank = (depth > 0.02) & ~water
    # Steep banks: +0.6 m at the waterline rising ~15 m across the ~12 m shore ring.
    height = np.where(bank, np.maximum(height, surf + 0.6 + np.clip(0.5 - depth, 0, 0.5) * 10.0), height).astype(np.float32)
    del depth, bed, bank, surf
    print(f"water {WATER_ASL_M} m / below dam {DOWN_ASL_M} m  {time.time() - t0:.1f}s")

    # --- 5. route: Tham Ma (patch) -> link -> QL4C north past Ma Pi Leng -> Ta Lang road -> dock ---
    trunk = {k_: w for k_, w in ways.items() if w.get("tags", {}).get("highway") == "trunk"
             and "4C" in (w["tags"].get("ref", "") + w["tags"].get("name", ""))}
    pan_xy = to_xy(*PANORAMA)

    def closest_way(cands, latlon_pt):
        return min(cands, key=lambda k_: np.abs(latlon(cands[k_]) - np.array(latlon_pt)).sum(axis=1).min())

    ql_ll = chain_ways(trunk, closest_way(trunk, PANORAMA))
    ql = dense(np.array([to_xy(*p) for p in ql_ll]), 4.0)
    tl_ll = latlon(ways[TA_LANG_WAY])
    tl = dense(np.array([to_xy(*p) for p in tl_ll]), 4.0)
    if np.linalg.norm(tl[0] - ql[nearest_index(ql, tl[0])]) > np.linalg.norm(tl[-1] - ql[nearest_index(ql, tl[-1])]):
        tl = tl[::-1]                                     # start at the QL4C junction
    j_ql = nearest_index(ql, tl[0])                       # Ta Lang junction on QL4C
    p_ql = nearest_index(ql, pan_xy)
    step = 1 if j_ql > p_ql else -1                       # walking from Ma Pi Leng to the junction
    # South: walk away from the junction past Ma Pi Leng to the frame edge.
    i = p_ql
    while 0 < i < len(ql) - 1 and np.abs(ql[i]).max() < HALF_M - 8.0:
        i -= step
    join_i = i
    edge_i = join_i                                        # where QL4C leaves the frame (south)
    south = list(range(p_ql - step * 40, edge_i, -step))   # >= 160 m south of the Panorama
    beyond = ql[j_ql:] if step > 0 else ql[:j_ql + 1][::-1]
    stub_north = beyond[(np.abs(beyond) < HALF_M - 5).all(axis=1)]

    # Tham Ma: the QL4C run through the patch, bottom first.
    tm_ll = chain_ways(trunk, closest_way(trunk, THAMMA_VIEW))
    tm = dense(np.array([to_patch_xy(*p) for p in tm_ll]), 4.0)
    vi = nearest_index(tm, to_patch_xy(*THAMMA_VIEW))
    inside_p = np.abs(tm - PATCH_CENTER).max(axis=1) < PATCH_CORE + 60.0
    a_, b_ = vi, vi
    while a_ > 0 and inside_p[a_ - 1]:
        a_ -= 1
    while b_ < len(tm) - 1 and inside_p[b_ + 1]:
        b_ += 1
    s_all = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(tm, axis=0), axis=1))])
    a_ = max(a_, int(np.searchsorted(s_all, s_all[vi] - 520.0)))     # climb + bends, short exit
    b_ = min(b_, int(np.searchsorted(s_all, s_all[vi] + 260.0)))
    tm = tm[a_:b_ + 1]
    tm_alt = [dem_point(dem, *tm_ll[nearest_index(np.array([to_patch_xy(*p) for p in tm_ll]), q)]) for q in (tm[0], tm[-1])]
    if tm_alt[0] > tm_alt[-1]:
        tm = tm[::-1]
    vi = nearest_index(tm, to_patch_xy(*THAMMA_VIEW))
    s_tm = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(tm, axis=0), axis=1))])
    approach_end = int(np.searchsorted(s_tm, max(s_tm[vi] - 330.0, s_tm[-1] * 0.25)))
    approach, tham_ma = tm[:approach_end + 1], tm[approach_end:]

    # Link road: from the patch exit to the nearest QL4C point south of Ma Pi Leng.
    exit_p, exit_d = tham_ma[-1], tham_ma[-1] - tham_ma[-6]
    exit_d /= np.linalg.norm(exit_d)
    join_i = p_ql - step * 12                              # ~50 m south of the Panorama: a T-junction
    rim_ql = ql[join_i:j_ql + 1] if step > 0 else ql[j_ql:join_i + 1][::-1]
    stub_south = ql[edge_i:join_i + 1] if step > 0 else ql[join_i:edge_i + 1][::-1]
    join_p, join_d = rim_ql[0], rim_ql[5] - rim_ql[0]
    join_d /= np.linalg.norm(join_d)
    lcp = least_cost_road(height, exit_p + exit_d * 60.0, join_p, avoid=ql[np.abs(ql).max(axis=1) < HALF_M])
    link = proc.catmull_rom(np.vstack([[exit_p], lcp[::3], [join_p]]), step=4.0)

    # Dock spur: from the end of the Ta Lang road down to the water's edge.
    end = tl[-1]
    wpx = np.argwhere(up_mask[::4, ::4]) * 4
    wxy = np.stack([wpx[:, 1], wpx[:, 0]], axis=1) * CELL_M - HALF_M
    shore = wxy[np.argmin(np.linalg.norm(wxy - end, axis=1))]
    to_w = (shore - end) / (np.linalg.norm(shore - end) + 1e-6)
    pad_end = shore + to_w * 3.0                                          # slipway runs 3 m into the water
    dock_spur = proc.catmull_rom([end, end + (pad_end - end) * 0.5, pad_end], step=4.0)

    pieces = [("approach", approach), ("tham_ma", tham_ma), ("link", link), ("rim", rim_ql),
              ("descent", tl), ("dock_spur", dock_spur)]
    poly = np.vstack([p for _, p in pieces])
    poly = proc.catmull_rom(poly[np.r_[True, np.linalg.norm(np.diff(poly, axis=0), axis=1) > 0.5]], step=2.0)
    road_xy, road_s = proc.resample(poly, 2.0)
    # OSM keeps hairpins as sharp polyline corners: round them to a drivable radius (~8 m) with a
    # light Gaussian along the line (endpoints pinned), then resample evenly.
    k_ = np.exp(-0.5 * (np.arange(-6, 7) / 2.5) ** 2); k_ /= k_.sum()
    for _ in range(2):
        pad_ = np.vstack([np.repeat(road_xy[:1], 6, 0), road_xy, np.repeat(road_xy[-1:], 6, 0)])
        road_xy = np.stack([np.convolve(pad_[:, 0], k_, "valid"), np.convolve(pad_[:, 1], k_, "valid")], 1)
    road_xy, road_s = proc.resample(road_xy, 2.0)
    piece_len, acc = {}, 0.0
    for name, p in pieces:
        acc += float(np.sum(np.linalg.norm(np.diff(p, axis=0), axis=1)))
        piece_len[name] = acc
    scale_s = road_s[-1] / acc
    piece_len = {k_: v * scale_s for k_, v in piece_len.items()}
    concrete_from = piece_len["rim"]
    print(f"route {road_s[-1] / 1000:.2f} km: " + ", ".join(f"{k_} {v:.0f}" for k_, v in piece_len.items()))

    # Road profile: real terrain under the centreline, smoothed (~80 m), dock ramp to the water.
    z = bilinear(height, road_xy)
    z = proc.smooth_1d(proc.smooth_1d(z, 41), 21)
    s_dock = piece_len["descent"]
    tdock = np.clip((road_s - s_dock) / max(road_s[-1] - s_dock, 1.0), 0, 1)
    road_z = (z * (1 - tdock) + (WATER_ASL_M + 1.6) * tdock).astype(np.float32)
    road_z[-1] = WATER_ASL_M + 1.6

    # --- 6. carve roads (+ QL4C stubs to the frame edge), smoothed bank targets ---
    def stub_z(p):
        return proc.smooth_1d(proc.smooth_1d(bilinear(height, p), 41), 21).astype(np.float32)

    carve = []
    stubs = []
    for st in (stub_south, stub_north):
        if len(st) > 3:
            st = proc.resample(st, 2.0)[0]
            sz = stub_z(st)
            stubs.append((st, sz))
            carve.append((st, sz, np.full(len(st), ROAD_HALF_WIDTH_M)))
    # Main route carved last: where a QL4C stub crosses it, the route's levels win.
    carve.append((road_xy, road_z, np.where(road_s >= concrete_from, CONCRETE_HALF_WIDTH_M, ROAD_HALF_WIDTH_M)))
    for poly_, zz, hw in carve:
        d, v = polyline_distance(poly_, zz, 120.0)
        _, hwv = polyline_distance(poly_, hw.astype(np.float32), 120.0)
        core = hwv + ROAD_SHOULDER_M
        rv = np.where(np.isfinite(d), v, height).astype(np.float32)
        rv_s = blur_coarse(rv, 4, 25)
        wcore = 1.0 - proc.smoothstep(core, core + 10.0, np.minimum(d, 1e6))
        target = np.where(np.isfinite(d), v * wcore + rv_s * (1.0 - wcore), height).astype(np.float32)
        diff = np.abs(height - target)
        bankw = np.where(height > target, np.clip(6.0 + 1.5 * diff, 6.0, 50.0), np.clip(5.0 + 0.6 * diff, 5.0, 30.0))
        w = np.where(np.isfinite(d), 1.0 - proc.smoothstep(core, core + bankw, np.minimum(d, 1e6)), 0.0)
        height = (height * (1.0 - w) + (target - 0.15) * w).astype(np.float32)
        del d, v, hwv, core, rv, rv_s, wcore, target, diff, bankw, w
    print(f"roads {time.time() - t0:.1f}s")

    # --- 7. pads: viewpoints and landmarks at their real positions ---
    def road_heading(i):
        j, k0 = min(i + 3, len(road_xy) - 1), max(i - 3, 0)
        d = road_xy[j] - road_xy[k0]
        return math.degrees(math.atan2(d[1], d[0]))

    landmarks = {}

    def pad(key, target_xy, radius, feather=6.0, lift=0.0):
        nonlocal height
        i = nearest_index(road_xy, target_xy)
        rp = road_xy[i]
        off = np.asarray(target_xy) - rp
        dist = float(np.linalg.norm(off))
        if dist < 4.0:                                     # on the road: put the pad on the uphill side
            tng = road_xy[min(i + 3, len(road_xy) - 1)] - road_xy[max(i - 3, 0)]
            nrm = np.array([-tng[1], tng[0]]) / (np.linalg.norm(tng) + 1e-6)
            if bilinear(height, rp + nrm * 20) < bilinear(height, rp - nrm * 20):
                nrm = -nrm
            off, dist = nrm * (radius + 3.0), radius + 3.0
        up = off / dist
        c = rp + up * max(dist, radius * 0.6 + 3.0)
        zz = float(road_z[i]) + lift
        m_ = capsule(rp + up * 2.0, c, radius, feather)
        height = (height * (1 - m_) + (zz - 0.15) * m_).astype(np.float32)
        if key:
            landmarks[key] = {"xy": c, "z": zz, "yaw": road_heading(i), "up": up}
        return c, zz, i

    vp1_xy, vp1_z, vp1_i = pad(None, to_patch_xy(*THAMMA_VIEW), 9.0)
    pad("tham_ma_cafe", to_patch_xy(*THAMMA_CAFE), 9.0)
    pad("ma_pi_leng_plaza", pan_xy, 12.0)
    pad("youth_monument", to_xy(*YOUTH_MONUMENT), 10.0)

    # Mom Da Tu Than: a limestone ledge jutting from the rim nearest the Panorama, over the gorge.
    pi = nearest_index(road_xy, pan_xy)
    best = None
    for di_ in range(-90, 91, 6):                          # search +-180 m of road for the steepest drop
        i = int(np.clip(pi + di_, 0, len(road_xy) - 1))
        rp = road_xy[i]
        rpt = river_xy[nearest_index(river_xy, rp)]
        dirv = (rpt - rp) / (np.linalg.norm(rpt - rp) + 1e-6)
        h40 = float(bilinear(height, rp + dirv * 45.0))
        drop = float(road_z[i]) - h40
        if best is None or drop > best[0]:
            best = (drop, i, dirv)
    _, rim_i, sdir = best
    rim_xy = road_xy[rim_i]
    edge = rim_xy + sdir * (ROAD_HALF_WIDTH_M + 2.0)
    for t_ in np.arange(4.0, 60.0, 1.0):
        if bilinear(height, rim_xy + sdir * t_) < road_z[rim_i] - 6.0:
            edge = rim_xy + sdir * t_
            break
    spur_z = float(road_z[rim_i]) - 2.0
    spur_b = edge + sdir * 26.0
    m_ = capsule(rim_xy + sdir * (ROAD_HALF_WIDTH_M + 1.0), spur_b, 4.5, 1.0)
    height = np.maximum(height * (1 - m_) + spur_z * m_, height * (1 - m_)).astype(np.float32)

    # Ta Lang dock: slipway pad + yard behind it.
    dock_z = WATER_ASL_M + 1.6
    m_ = capsule(end, pad_end, 8.0, 6.0)
    height = (height * (1 - m_) + (dock_z - 0.15) * m_).astype(np.float32)
    yard = end - to_w * 14.0
    m_ = capsule(end - to_w * 3.0, yard, 11.0, 6.0)
    height = (height * (1 - m_) + (float(road_z[nearest_index(road_xy, end)]) + 0.45) * m_).astype(np.float32)
    landmarks["dock_yard"] = {"xy": yard, "z": float(road_z[nearest_index(road_xy, end)]) + 0.6,
                              "yaw": math.degrees(math.atan2(to_w[1], to_w[0])), "up": -to_w}
    boat_center = shore + to_w * 9.0
    print(f"pads {time.time() - t0:.1f}s")

    # --- quantise ---
    height = np.clip(height, BASE_ASL_M + 1.0, MAX_ASL_M - 1.0)
    h16 = np.round((height - BASE_ASL_M) / M_PER_UNIT).astype(np.uint16)
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    proc.write_png16(OUT_DIR / "HaGiang_4033.png", h16)
    h16.astype("<u2").tofile(OUT_DIR / "HaGiang_4033.r16")

    # --- layout json (UE cm) ---
    def cm3(x, y, z_):
        return [round(float(x) * 100.0, 1), round(float(y) * 100.0, 1), round(float(z_) * 100.0, 1)]

    # Every 4 m: coarser output collapses the 8 m-radius hairpins into corners (spline + AI).
    road_out = [cm3(road_xy[i, 0], road_xy[i, 1], road_z[i]) for i in range(0, len(road_xy), 2)]
    if (len(road_xy) - 1) % 2:
        road_out.append(cm3(road_xy[-1, 0], road_xy[-1, 1], road_z[-1]))

    # Water bodies: rotated planes along the reservoir (each ~260 m), one below the dam.
    res_line = river_xy[:di + 1]
    bodies = []
    seg = 260.0
    s_r = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(res_line, axis=0), axis=1))])
    for a0 in np.arange(0.0, s_r[-1], seg):
        a1 = min(a0 + seg, s_r[-1])
        pa = np.array([np.interp(a0, s_r, res_line[:, 0]), np.interp(a0, s_r, res_line[:, 1])])
        pb = np.array([np.interp(a1, s_r, res_line[:, 0]), np.interp(a1, s_r, res_line[:, 1])])
        ext = 40.0 if a0 > 0 else 0.0
        c = (pa + pb) / 2
        L = float(np.linalg.norm(pb - pa)) + ext + (0.0 if a1 >= s_r[-1] else 40.0)
        yaw = math.degrees(math.atan2(pb[1] - pa[1], pb[0] - pa[0]))
        bodies.append({"name": f"Water_NhoQue_Reservoir_{len(bodies):02d}", "z_cm": WATER_ASL_M * 100.0,
                       "center_cm": cm3(c[0], c[1], WATER_ASL_M)[:2], "size_cm": [L * 100.0, 420.0 * 100.0], "yaw": yaw})
    low_c = dam_c + flow * 400.0
    bodies.append({"name": "Water_NhoQue_BelowDam", "z_cm": DOWN_ASL_M * 100.0, "center_cm": cm3(low_c[0], low_c[1], 0)[:2],
                   "size_cm": [800.0 * 100.0, 300.0 * 100.0], "yaw": math.degrees(math.atan2(flow[1], flow[0]))})
    dam_info = {"location_cm": cm3(dam_c[0], dam_c[1], DOWN_ASL_M - 8.0),
                "yaw": math.degrees(math.atan2(flow[1], flow[0])), "crest_z_cm": (WATER_ASL_M + 4.0) * 100.0,
                "width_cm": (dam_len + 40.0) * 100.0}

    real_alt = {"doc_tham_ma": dem_point(dem, *THAMMA_VIEW)}
    start_i = 15
    layout = {
        "heightmap": {"size": N, "png": "Generated/Heightmaps/HaGiang_4033.png",
                      "r16": "Generated/Heightmaps/HaGiang_4033.r16",
                      "location_cm": list(LANDSCAPE_LOCATION_CM), "scale": list(LANDSCAPE_SCALE),
                      "cell_m": CELL_M, "half_m": HALF_M,
                      "section_size_quads": 63, "sections_per_component": 2, "components": [32, 32],
                      "base_asl_m": BASE_ASL_M, "m_per_unit": M_PER_UNIT},
        "geo": {"source": "Copernicus GLO-30 DEM + OpenStreetMap (ODbL)", "center_latlon": [LAT0, LON0],
                "north": "-Y", "tham_ma_patch": {"center_m": PATCH_CENTER.tolist(), "core_m": PATCH_CORE,
                                                  "band_m": PATCH_BAND, "offset_m": patch_offset}},
        "water": {"surface_z_cm": WATER_ASL_M * 100.0, "river_cm": [cm3(p[0], p[1], WATER_ASL_M) for p in res_line],
                  "downstream_surface_z_cm": DOWN_ASL_M * 100.0, "bodies": bodies, "dam": dam_info},
        "road": {"half_width_cm": ROAD_HALF_WIDTH_M * 100.0, "length_m": float(road_s[-1]),
                 "concrete_half_width_cm": CONCRETE_HALF_WIDTH_M * 100.0, "concrete_from_m": float(concrete_from),
                 "points_cm": road_out,
                 "sections_m": {k_: float(v) for k_, v in piece_len.items()},
                 "stubs_cm": [[cm3(p[0], p[1], zz) for p, zz in zip(st[::2], sz[::2])] for st, sz in stubs]},
        "viewpoints": {
            "doc_tham_ma": {"name": "DOC THAM MA", "altitude_m": round(real_alt["doc_tham_ma"]),
                            "location_cm": cm3(*vp1_xy, vp1_z)},
            "ma_pi_leng": {"name": "DEO MA PI LENG - MOM DA TU THAN", "altitude_m": round(spur_z),
                           "location_cm": cm3(*spur_b, spur_z), "road_cm": cm3(*rim_xy, road_z[rim_i])},
            "tu_san": {"name": "HEM VUC TU SAN - SONG NHO QUE", "altitude_m": round(WATER_ASL_M),
                       "location_cm": cm3(*pad_end, dock_z)},
        },
        "spawns": {
            "player_start": {"location_cm": cm3(road_xy[start_i, 0], road_xy[start_i, 1], road_z[start_i] + 1.6),
                             "yaw": road_heading(start_i)},
            "bikes": [
                {"location_cm": cm3(road_xy[start_i + 4, 0], road_xy[start_i + 4, 1], road_z[start_i + 4] + 0.6),
                 "yaw": road_heading(start_i + 4)},
                {"location_cm": cm3(*(vp1_xy + (road_xy[vp1_i] - vp1_xy) * 0.4), vp1_z + 0.6), "yaw": road_heading(vp1_i)},
                {"location_cm": cm3(*(road_xy[rim_i - 6]), road_z[rim_i - 6] + 0.6), "yaw": road_heading(rim_i - 6)},
            ],
            "boats": [
                {"type": "Cano", "location_cm": cm3(*(boat_center + np.array([-to_w[1], to_w[0]]) * 8), WATER_ASL_M)},
                {"type": "Kayak", "location_cm": cm3(*(boat_center - np.array([-to_w[1], to_w[0]]) * 8), WATER_ASL_M)},
            ],
            "dock": {"location_cm": cm3(*pad_end, dock_z), "yaw": math.degrees(math.atan2(to_w[1], to_w[0]))},
        },
        "landmarks": {k_: {"location_cm": cm3(*v["xy"], v["z"]), "yaw": v["yaw"], "up": [float(c_) for c_ in v["up"]]}
                      for k_, v in landmarks.items()},
        "seed": SEED,
    }
    (OUT_DIR / "HaGiang_layout.json").write_text(json.dumps(layout, indent=1), encoding="utf-8")
    write_preview(height, road_xy, res_line, layout)
    print(f"done in {time.time() - t0:.1f}s -> {OUT_DIR}")
    print(f"altitude {height.min():.0f}..{height.max():.0f} m; Tham Ma view {vp1_z:.0f} m (real "
          f"{real_alt['doc_tham_ma']:.0f}), Mom Da {spur_z:.0f} m, water {WATER_ASL_M} m")
    return layout


def write_preview(height, road_xy, river_xy, layout, size=1344):
    step = 3
    h = height[::step, ::step]
    gy, gx = np.gradient(h, step * CELL_M)
    light = np.array([-0.6, -0.6, 0.55])
    light /= np.linalg.norm(light)
    nz = 1.0 / np.sqrt(gx * gx + gy * gy + 1.0)
    shade = np.clip((-gx * light[0] - gy * light[1] + light[2]) * nz, 0, 1)
    t = np.clip((h - 450.0) / (1600.0 - 450.0), 0, 1)
    base = np.stack([0.35 + 0.45 * t, 0.45 + 0.35 * t, 0.30 + 0.40 * t], axis=-1)
    rgb = base * (0.35 + 0.65 * shade[..., None])
    rgb[h < layout["water"]["surface_z_cm"] / 100.0] = [0.05, 0.55, 0.50]
    img = (np.clip(rgb, 0, 1) * 255).astype(np.uint8)
    for p in to_px(road_xy[::2]) / step:
        c, r = int(p[0]), int(p[1])
        img[max(r - 1, 0):r + 1, max(c - 1, 0):c + 1] = [235, 60, 40]
    for vp in list(layout["viewpoints"].values()) + list(layout["landmarks"].values()):
        c, r = (to_px(np.array(vp["location_cm"][:2]) / 100.0) / step).astype(int)
        img[max(r - 5, 0):r + 5, max(c - 5, 0):c + 5] = [255, 220, 0]
    Image.fromarray(img).save(OUT_DIR / "HaGiang_preview.png")


if __name__ == "__main__":
    generate()
