#!/usr/bin/env python3
"""Scatter + viewpoint layout for CP03 (run after gen_heightmap.py).

Reads Generated/Heightmaps/HaGiang_4033.r16 + HaGiang_layout.json and writes
Generated/Heightmaps/HaGiang_scatter.json with instance transforms
[x, y, z (cm), yaw, pitch, roll (deg), sx, sy, sz] for:

  cliffs    Nanite karst slabs on the steep canyon walls / cone flanks, thin axis facing downhill
  summits   upright slabs crowning karst cone tops (sharp "tai meo" peaks)
  boulders  on 25-50 deg slopes
  bushes    on gentle slopes away from the road
  posts     red/white road posts on the valley side of the road every 8 m
  spur      hand-placed rocks under Mom Da Tu Than

plus flycam orbit data for the 3 viewpoints. Deterministic (fixed seed).
"""
from __future__ import annotations

import json
import math
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
HM_DIR = ROOT / "Generated" / "Heightmaps"
SEED = 4242


def load():
    layout = json.loads((HM_DIR / "HaGiang_layout.json").read_text())
    hm = layout["heightmap"]
    n = hm["size"]
    h16 = np.fromfile(HM_DIR / "HaGiang_4033.r16", dtype="<u2").reshape(n, n)
    alt = hm["base_asl_m"] + h16.astype(np.float32) * hm["m_per_unit"]
    return layout, alt


class Terrain:
    def __init__(self, alt, layout):
        self.alt = alt
        self.n = alt.shape[0]
        self.half = (self.n - 1) / 2.0
        gy, gx = np.gradient(alt)          # metres per metre (1 m grid)
        self.gx, self.gy = gx.astype(np.float32), gy.astype(np.float32)
        self.slope = np.degrees(np.arctan(np.hypot(gx, gy))).astype(np.float32)
        self.water = layout["water"]["surface_z_cm"] / 100.0

    def idx(self, x, y):
        c = np.clip(np.round(np.asarray(x) + self.half).astype(int), 0, self.n - 1)
        r = np.clip(np.round(np.asarray(y) + self.half).astype(int), 0, self.n - 1)
        return r, c

    def height(self, x, y):
        fx = np.clip(np.asarray(x, float) + self.half, 0, self.n - 1.001)
        fy = np.clip(np.asarray(y, float) + self.half, 0, self.n - 1.001)
        c0, r0 = np.floor(fx).astype(int), np.floor(fy).astype(int)
        tx, ty = fx - c0, fy - r0
        a = self.alt[r0, c0]; b = self.alt[r0, c0 + 1]
        c = self.alt[r0 + 1, c0]; d = self.alt[r0 + 1, c0 + 1]
        return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty


def road_distance_coarse(road_xy, n, half, stride=2, margin=60.0):
    m = (n - 1) // stride + 1
    dist = np.full((m, m), np.inf, np.float32)
    coords = np.arange(m) * stride - half
    for i in range(len(road_xy) - 1):
        a, b = road_xy[i], road_xy[i + 1]
        lo = np.minimum(a, b) - margin
        hi = np.maximum(a, b) + margin
        c0 = max(0, int((lo[0] + half) // stride)); c1 = min(m - 1, int((hi[0] + half) // stride) + 1)
        r0 = max(0, int((lo[1] + half) // stride)); r1 = min(m - 1, int((hi[1] + half) // stride) + 1)
        px = coords[c0:c1 + 1][None, :] - a[0]
        py = coords[r0:r1 + 1][:, None] - a[1]
        ab = b - a
        t = np.clip((px * ab[0] + py * ab[1]) / max(ab @ ab, 1e-9), 0, 1)
        d = np.hypot(px - t * ab[0], py - t * ab[1])
        sub = dist[r0:r1 + 1, c0:c1 + 1]
        np.minimum(sub, d, out=sub)
    return dist


def jitter_grid(spacing, half, rng):
    xs = np.arange(-half + spacing / 2, half, spacing)
    gx, gy = np.meshgrid(xs, xs)
    gx = gx.ravel() + rng.uniform(-0.45, 0.45, gx.size) * spacing
    gy = gy.ravel() + rng.uniform(-0.45, 0.45, gy.size) * spacing
    return gx, gy


def inst(x, y, z, yaw, pitch, roll, sx, sy, sz):
    return [round(float(x) * 100, 1), round(float(y) * 100, 1), round(float(z) * 100, 1),
            round(float(yaw), 2), round(float(pitch), 2), round(float(roll), 2),
            round(float(sx), 3), round(float(sy), 3), round(float(sz), 3)]


def mesh_dims(name):
    """(width_x, depth_y, height_z) in metres of a generated OBJ (pivot at the base)."""
    v = np.array([[float(t) for t in l.split()[1:4]]
                  for l in (ROOT / "Generated/Assets/Meshes" / f"{name}.obj").read_text().splitlines() if l.startswith("v ")])
    return np.ptp(v, axis=0) / 100.0


def rotator_from_axes(X, Z):
    """UE FRotator (pitch, yaw, roll in degrees) for a basis with forward X and up Z (like FRotationMatrix::MakeFromXZ)."""
    X = X / np.linalg.norm(X)
    Y = np.cross(Z, X); Y /= np.linalg.norm(Y)
    Z = np.cross(X, Y)
    pitch = math.degrees(math.atan2(X[2], math.hypot(X[0], X[1])))
    yaw = math.degrees(math.atan2(X[1], X[0]))
    # Y axis of FRotator(pitch, yaw, 0) for the roll term.
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    sy_axis = np.array([-sy, cy, 0.0])
    sz_axis = np.array([-sp * cy, -sp * sy, cp])
    roll = math.degrees(math.atan2(np.dot(Z, sy_axis), np.dot(Z, sz_axis)))
    return pitch, yaw, roll


def main():
    t0 = time.time()
    rng = np.random.default_rng(SEED)
    layout, alt = load()
    T = Terrain(alt, layout)
    road = np.array(layout["road"]["points_cm"], float) / 100.0
    road_xy = road[:, :2]
    stride = 2
    rd = road_distance_coarse(road_xy, T.n, T.half, stride)

    def road_dist(x, y):
        c = np.clip(((np.asarray(x) + T.half) / stride).round().astype(int), 0, rd.shape[1] - 1)
        r = np.clip(((np.asarray(y) + T.half) / stride).round().astype(int), 0, rd.shape[0] - 1)
        return rd[r, c]

    out = {}

    # ---- cliffs: rock slabs laid ONTO steep faces ----
    # The slab's thin axis (local Y) follows the terrain normal and its height axis (local Z) runs
    # up the slope, so it reads as exposed bedrock hugging the wall - not as a blade sticking out.
    # Only ~35 % of the slab thickness protrudes from the terrain.
    dims = mesh_dims("SM_HG_KarstCliff_01")          # ~ (10, 5-7, 15-23) m
    vp_pts = [np.array(v["location_cm"][:2]) / 100.0 for v in layout["viewpoints"].values()]
    x, y = jitter_grid(28.0, T.half - 5, rng)
    r, c = T.idx(x, y)
    slope = T.slope[r, c]
    z = T.height(x, y)
    near_vp = np.zeros(x.size, bool)
    for vp in vp_pts:
        near_vp |= np.hypot(x - vp[0], y - vp[1]) < 45.0
    # Exposed cliff only where the photos show it: the Tu San narrows (y ~ -800..800 m) and very
    # steep faces elsewhere; vegetated slopes are left to the terrain material + trees.
    gorge = (y > -800) & (y < 800)
    keep = (((slope > 55) & gorge) | (slope > 66)) & (road_dist(x, y) > 16) & (z > T.water + 5) & ~near_vp
    cliffs = []
    for xi, yi, zi, ri, ci in zip(x[keep], y[keep], z[keep], r[keep], c[keep]):
        gx, gy = float(T.gx[ri, ci]), float(T.gy[ri, ci])
        n = np.array([-gx, -gy, 1.0]); n /= np.linalg.norm(n)               # outward terrain normal
        up_slope = np.array([gx, gy, gx * gx + gy * gy]); up_slope /= np.linalg.norm(up_slope)
        along = np.cross(n, up_slope); along /= np.linalg.norm(along)        # contour direction
        # Small random twist inside the face plane.
        tw = math.radians(rng.uniform(-25, 25))
        X = along * math.cos(tw) + up_slope * math.sin(tw)
        Zax = -along * math.sin(tw) + up_slope * math.cos(tw)
        pitch, yaw, roll = rotator_from_axes(X, Zax)
        sx = rng.uniform(1.4, 2.8)
        sz = rng.uniform(1.0, 1.8)
        sy = rng.uniform(0.8, 1.3)
        thick = dims[1] * sy
        height = dims[2] * sz
        center = np.array([xi, yi, zi]) - n * (0.15 * thick)
        base = center - Zax * (0.5 * height)                                    # mesh pivot is at its base
        cliffs.append(inst(base[0], base[1], base[2], yaw, pitch, roll, sx, sy, sz))
    out["cliffs"] = cliffs

    # ---- summits: local maxima of the karst cones ----
    s = 8
    coarse = alt[::s, ::s]
    from numpy.lib.stride_tricks import sliding_window_view
    win = 9
    pad = np.pad(coarse, win // 2, mode="edge")
    local_max = sliding_window_view(pad, (win, win)).max(axis=(2, 3))
    peaks = np.argwhere((coarse >= local_max) & (coarse > T.water + 150))
    summits = []
    for pr, pc in peaks:
        xi, yi = pc * s - T.half, pr * s - T.half
        if road_dist(xi, yi) < 40:
            continue
        zi = float(T.height(xi, yi))
        # Pinnacle crowning the cone: mostly buried, only the fluted top sticks out (~8-15 m).
        sz = rng.uniform(0.7, 1.1)
        summits.append(inst(xi, yi, zi - dims[2] * sz * 0.55, rng.uniform(0, 360), rng.uniform(-6, 6), rng.uniform(-6, 6),
                            rng.uniform(1.0, 1.6), rng.uniform(0.9, 1.3), sz))
    out["summits"] = summits

    # ---- boulders ----
    x, y = jitter_grid(16.0, T.half - 5, rng)
    r, c = T.idx(x, y)
    z = T.height(x, y)
    keep = (T.slope[r, c] > 24) & (T.slope[r, c] < 52) & (road_dist(x, y) > 7) & (z > T.water + 1) \
        & (rng.random(x.size) < 0.45)
    out["boulders"] = [inst(xi, yi, zi - 0.4, rng.uniform(0, 360), rng.uniform(-15, 15), rng.uniform(-15, 15),
                            *(rng.uniform(0.4, 1.6) * np.array([1, rng.uniform(0.8, 1.2), rng.uniform(0.7, 1.1)])))
                       for xi, yi, zi in zip(x[keep], y[keep], z[keep])]

    # ---- bushes ----
    x, y = jitter_grid(7.0, T.half - 5, rng)
    r, c = T.idx(x, y)
    z = T.height(x, y)
    keep = (T.slope[r, c] < 34) & (road_dist(x, y) > 5.5) & (z > T.water + 1.5) & (rng.random(x.size) < 0.55)
    out["bushes"] = [inst(xi, yi, zi - 0.1, rng.uniform(0, 360), 0, 0, *([rng.uniform(0.6, 1.5)] * 3))
                     for xi, yi, zi in zip(x[keep], y[keep], z[keep])]

    # ---- trees: lush broadleaf + pines on the west (Tham Ma), sparse trees and red kapok in the canyon ----
    x, y = jitter_grid(16.0, T.half - 5, rng)
    r, c = T.idx(x, y)
    z = T.height(x, y)
    sl = T.slope[r, c]
    lush = np.clip((-200.0 - x) / 900.0, 0, 1) * 0.75 + 0.18          # tree density by region
    ok = (sl < 42) & (road_dist(x, y) > 7) & (z > T.water + 6)
    near_vp = np.zeros(x.size, bool)
    for vp in vp_pts:
        near_vp |= np.hypot(x - vp[0], y - vp[1]) < 25.0
    pick = ok & ~near_vp & (rng.random(x.size) < lush * 0.55)
    kinds = rng.random(x.size)
    canyon = x > -200
    trees = {"kapok": [], "broadleaf": [], "pine": []}
    for xi, yi, zi, kv, cz in zip(x[pick], y[pick], z[pick], kinds[pick], canyon[pick]):
        s_ = rng.uniform(0.7, 1.25)
        t = inst(xi, yi, zi - 0.3, rng.uniform(0, 360), rng.uniform(-4, 4), rng.uniform(-4, 4), s_, s_, s_ * rng.uniform(0.9, 1.15))
        if cz and kv < 0.3:
            trees["kapok"].append(t)
        elif kv < 0.45:
            trees["pine"].append(t)
        else:
            trees["broadleaf"].append(t)
    out["trees"] = trees

    # ---- road posts on the valley side ----
    half_w = layout["road"]["half_width_cm"] / 100.0
    s_road = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(road_xy, axis=0), axis=1))])
    # Every 4 m: W-beam guardrails on the Doc Tham Ma hairpins (as in the photos), concrete
    # red/white posts (coc tieu) every 8 m elsewhere. Only on the valley (drop-off) side.
    tham_end = layout["road"]["sections_m"]["tham_ma"] + 300.0
    targets = np.arange(2.0, s_road[-1] - 6.0, 4.0)
    px = np.interp(targets, s_road, road[:, 0]); py = np.interp(targets, s_road, road[:, 1])
    pz = np.interp(targets, s_road, road[:, 2])
    pz4 = np.interp(targets + 4.0, s_road, road[:, 2])
    tx = np.interp(targets + 1.0, s_road, road[:, 0]) - np.interp(targets - 1.0, s_road, road[:, 0])
    ty = np.interp(targets + 1.0, s_road, road[:, 1]) - np.interp(targets - 1.0, s_road, road[:, 1])
    norm = np.hypot(tx, ty) + 1e-9
    nx, ny = -ty / norm, tx / norm
    posts, rails = [], []
    for i in range(len(targets)):
        yaw = math.degrees(math.atan2(ty[i], tx[i]))
        guard = targets[i] < tham_end
        if not guard and i % 2:
            continue
        for side in (1, -1):
            off = half_w + (0.6 if guard else 0.45)
            sx_, sy_ = px[i] + nx[i] * off * side, py[i] + ny[i] * off * side
            beyond = T.height(px[i] + nx[i] * (off + 6) * side, py[i] + ny[i] * (off + 6) * side)
            if beyond >= pz[i] - 1.5:   # no drop-off on this side
                continue
            if guard:
                pitch = math.degrees(math.atan2(pz4[i] - pz[i], 4.0))
                # Rail faces the road: local +Y points away from the road on the left side, mirror on the right.
                ry = yaw if side > 0 else yaw + 180.0
                rails.append(inst(sx_, sy_, pz[i] - 0.15, ry, pitch if side > 0 else -pitch, 0, 1, 1, 1))
            else:
                posts.append(inst(sx_, sy_, pz[i] - 0.12, yaw, 0, 0, 1, 1, 1))
    out["posts"] = posts
    out["guardrails"] = rails

    # ---- Mom Da Tu Than ----
    vp2 = layout["viewpoints"]["ma_pi_leng"]
    sx2, sy2, sz2 = (np.array(vp2["location_cm"]) / 100.0)
    rx2, ry2, _ = (np.array(vp2["road_cm"]) / 100.0)
    spur_yaw = math.degrees(math.atan2(sy2 - ry2, sx2 - rx2))
    spur = []
    # Height (m) of SM_HG_KarstCliff_01 (used for the spur rocks), read from the generated OBJ.
    zs = [float(l.split()[3]) for l in (ROOT / "Generated/Assets/Meshes/SM_HG_KarstCliff_01.obj").read_text().splitlines()
          if l.startswith("v ")]
    cliff_h = (max(zs) - min(zs)) / 100.0
    # Rock pillar under the tip: its top sits 1 m below the spur surface.
    pillar_sz = 4.0
    spur.append(inst(sx2, sy2, sz2 - 1.0 - cliff_h * pillar_sz, spur_yaw + 90, 0, 0, 1.3, 1.1, pillar_sz))
    # Upside-down fractured slabs hanging under the spur (pivot at their base -> flipped hangs down).
    for k in range(4):
        t = (k + 0.5) / 4
        xx = rx2 + (sx2 - rx2) * t; yy = ry2 + (sy2 - ry2) * t
        spur.append(inst(xx, yy, sz2 - 1.5, spur_yaw + rng.uniform(-10, 10), rng.uniform(-8, 8), 180.0,
                         1.4, 1.2, 1.0 + 0.4 * k))
    out["spur"] = spur

    # ---- flycam viewpoints ----
    sec = layout["road"]["sections_m"]
    tham = road_xy[(s_road > sec["approach"]) & (s_road < sec["tham_ma"])]
    tham_z = road[(s_road > sec["approach"]) & (s_road < sec["tham_ma"]), 2]
    river = np.array(layout["water"]["river_cm"]) / 100.0
    dock = np.array(layout["viewpoints"]["tu_san"]["location_cm"]) / 100.0
    river_pt = river[np.argmin(np.linalg.norm(river[:, :2] - dock[:2], axis=1))]
    vps = {
        "doc_tham_ma": {"center_cm": [float(tham[:, 0].mean() * 100), float(tham[:, 1].mean() * 100), float(tham_z.mean() * 100)],
                        "radius_cm": 26000.0, "height_cm": 16000.0},
        "ma_pi_leng": {"center_cm": [float(sx2 * 100), float(sy2 * 100), float(sz2 * 100)],
                       "radius_cm": 18000.0, "height_cm": 6000.0},
        "tu_san": {"center_cm": [float(river_pt[0] * 100), float(river_pt[1] * 100), float(river_pt[2] * 100)],
                   "radius_cm": 34000.0, "height_cm": 26000.0},
    }
    for k, v in vps.items():
        v.update({kk: layout["viewpoints"][k][kk] for kk in ("name", "altitude_m", "location_cm")})
    out["viewpoints"] = vps

    (HM_DIR / "HaGiang_scatter.json").write_text(json.dumps(out, separators=(",", ":")))
    counts = {k: len(v) for k, v in out.items() if isinstance(v, list)}
    counts.update({f"tree_{k}": len(v) for k, v in out["trees"].items()})
    print(f"scatter {counts} in {time.time() - t0:.1f}s")


if __name__ == "__main__":
    main()
