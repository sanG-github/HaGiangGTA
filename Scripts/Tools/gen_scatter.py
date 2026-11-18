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
import sys
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
        self.cell = layout["heightmap"].get("cell_m", 1.0)
        self.half = (self.n - 1) * self.cell / 2.0            # metres
        gy, gx = np.gradient(alt, self.cell)                    # metres per metre
        self.gx, self.gy = gx.astype(np.float32), gy.astype(np.float32)
        self.slope = np.degrees(np.arctan(np.hypot(gx, gy))).astype(np.float32)
        self.water = layout["water"]["surface_z_cm"] / 100.0

    def idx(self, x, y):
        c = np.clip(np.round((np.asarray(x) + self.half) / self.cell).astype(int), 0, self.n - 1)
        r = np.clip(np.round((np.asarray(y) + self.half) / self.cell).astype(int), 0, self.n - 1)
        return r, c

    def height(self, x, y):
        fx = np.clip((np.asarray(x, float) + self.half) / self.cell, 0, self.n - 1.001)
        fy = np.clip((np.asarray(y, float) + self.half) / self.cell, 0, self.n - 1.001)
        c0, r0 = np.floor(fx).astype(int), np.floor(fy).astype(int)
        tx, ty = fx - c0, fy - r0
        a = self.alt[r0, c0]; b = self.alt[r0, c0 + 1]
        c = self.alt[r0 + 1, c0]; d = self.alt[r0 + 1, c0 + 1]
        return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty


def road_distance_coarse(road_xy, n, half, stride=2, margin=60.0):
    """stride in metres; half in metres."""
    m = int(2 * half // stride) + 1
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
    # QL4C beyond the route (stubs) counts as road too.
    rd = road_distance_coarse(np.vstack([road_xy] + [np.array(st, float)[:, :2] / 100.0 for st in layout["road"].get("stubs_cm", [])]),
                              T.n, T.half, stride)

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
    # Exposed limestone wherever the real slope is cliff-steep (Tu San walls, karst faces);
    # vegetated slopes are left to the terrain material + trees.
    keep = (slope > 52) & (rng.random(x.size) < np.clip((slope - 40.0) / 30.0, 0, 1)) & (road_dist(x, y) > 16) & (z > T.water + 5) & ~near_vp
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
        center = np.array([xi, yi, zi]) - n * (0.35 * thick)                # mostly buried: a rock face, not a plate
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
        xi, yi = pc * s * T.cell - T.half, pr * s * T.cell - T.half
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
        & (rng.random(x.size) < 0.25)
    out["boulders"] = [inst(xi, yi, zi - 0.4, rng.uniform(0, 360), rng.uniform(-15, 15), rng.uniform(-15, 15),
                            *(rng.uniform(0.4, 1.6) * np.array([1, rng.uniform(0.8, 1.2), rng.uniform(0.7, 1.1)])))
                       for xi, yi, zi in zip(x[keep], y[keep], z[keep])]

    # ---- bushes ----
    x, y = jitter_grid(7.0, T.half - 5, rng)
    r, c = T.idx(x, y)
    z = T.height(x, y)
    keep = (T.slope[r, c] < 58) & (road_dist(x, y) > 5.5) & (z > T.water + 4.5) & (rng.random(x.size) < 0.3)
    out["bushes"] = [inst(xi, yi, zi - 0.1, rng.uniform(0, 360), 0, 0, *([rng.uniform(0.6, 1.5)] * 3))
                     for xi, yi, zi in zip(x[keep], y[keep], z[keep])]

    # ---- trees: lush broadleaf + pines on the west (Tham Ma), sparse trees and red kapok in the canyon ----
    x, y = jitter_grid(16.0, T.half - 5, rng)
    r, c = T.idx(x, y)
    z = T.height(x, y)
    sl = T.slope[r, c]
    # Tree density by region (ART_BIBLE): forested Tham Ma valley; dry grass + scattered trees on the
    # canyon slopes; a greener strip with red kapok just above the reservoir.
    tm = layout.get("geo", {}).get("tham_ma_patch")
    if tm:
        cheb = np.maximum(np.abs(x - tm["center_m"][0]), np.abs(y - tm["center_m"][1]))
        lush = 0.18 + 0.7 * np.clip((tm["core_m"] + tm["band_m"] - cheb) / tm["band_m"], 0, 1)
    else:
        lush = np.clip((-200.0 - x) / 900.0, 0, 1) * 0.75 + 0.18
    canyon = lush < 0.5
    ok = (sl < np.where(canyon, 56, 42)) & (road_dist(x, y) > 7) & (z > T.water + 6)
    near_vp = np.zeros(x.size, bool)
    for vp in vp_pts:
        near_vp |= np.hypot(x - vp[0], y - vp[1]) < 25.0
    low = np.clip((T.water + 160.0 - z) / 120.0, 0, 1) * canyon            # greener strip above the water
    pick = ok & ~near_vp & (rng.random(x.size) < (lush + low * 0.35) * 0.55)
    kinds = rng.random(x.size)
    canyon = canyon & (z < T.water + 260.0)                                  # kapok only low in the valley
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
    concrete_from = layout["road"].get("concrete_from_m", 1e9)   # Ta Lang concrete road: no posts / chevrons
    targets = np.arange(2.0, s_road[-1] - 6.0, 4.0)
    px = np.interp(targets, s_road, road[:, 0]); py = np.interp(targets, s_road, road[:, 1])
    pz = np.interp(targets, s_road, road[:, 2])
    pz4 = np.interp(targets + 4.0, s_road, road[:, 2])
    tx = np.interp(targets + 1.0, s_road, road[:, 0]) - np.interp(targets - 1.0, s_road, road[:, 0])
    ty = np.interp(targets + 1.0, s_road, road[:, 1]) - np.interp(targets - 1.0, s_road, road[:, 1])
    norm = np.hypot(tx, ty) + 1e-9
    nx, ny = -ty / norm, tx / norm
    posts, rails, walls = [], [], []
    mpl_road = np.array(layout["viewpoints"]["ma_pi_leng"]["road_cm"][:2]) / 100.0
    mpl_s = float(s_road[np.argmin(np.linalg.norm(road_xy - mpl_road, axis=1))])
    for i in range(len(targets)):
        yaw = math.degrees(math.atan2(ty[i], tx[i]))
        guard = targets[i] < tham_end
        if targets[i] > concrete_from - 10.0:
            break
        if not guard and i % 2:
            continue
        for side in (1, -1):
            off = half_w + (0.6 if guard else 0.45)
            sx_, sy_ = px[i] + nx[i] * off * side, py[i] + ny[i] * off * side
            beyond = T.height(px[i] + nx[i] * (off + 6) * side, py[i] + ny[i] * (off + 6) * side)
            if beyond >= pz[i] - 1.5:   # no drop-off on this side
                continue
            in_wall_zone = abs(targets[i] - mpl_s) < 650.0 and abs(targets[i] - mpl_s) > 12.0
            if in_wall_zone and (int(targets[i] // 4.0) % 9) < 6:      # 24 m wall runs, 12 m gaps with posts
                pitch = math.degrees(math.atan2(pz4[i] - pz[i], 4.0))
                wx_, wy_ = px[i] + nx[i] * (half_w + 0.5) * side, py[i] + ny[i] * (half_w + 0.5) * side
                # Wall mesh runs along +X from its pivot: start the 4 m piece 2 m behind the sample point.
                wx_ -= tx[i] / norm[i] * 2.0; wy_ -= ty[i] / norm[i] * 2.0
                walls.append(inst(wx_, wy_, pz[i] - 0.25, yaw, pitch, 0, 1, rng.uniform(0.9, 1.1), rng.uniform(0.85, 1.1)))
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
    out["stone_walls"] = walls


    # ---- set dressing from the reference photos ----
    # Curve chevrons on the outside of every sharp bend (radius < 40 m), facing the traffic.
    seg_s = s_road
    hd = np.unwrap(np.arctan2(np.gradient(road[:, 1]), np.gradient(road[:, 0])))
    curv = np.abs(np.gradient(hd) / np.maximum(np.gradient(seg_s), 1e-3))           # 1/m
    chevrons = []
    last = -1e9
    for i in range(5, len(road) - 5):
        if curv[i] > 1.0 / 40.0 and seg_s[i] - last > 30.0 and seg_s[i] < concrete_from:
            last = seg_s[i]
            turn = np.sign(np.gradient(hd)[i])                      # +1 = turning left
            nx_, ny_ = -np.sin(hd[i]), np.cos(hd[i])                # left normal
            out_side = -turn                                       # outside of the bend
            for k in (-1, 0, 1):
                j = min(max(i + k * 4, 0), len(road) - 1)
                ox, oy = road[j, 0] + nx_ * out_side * (half_w + 1.2), road[j, 1] + ny_ * out_side * (half_w + 1.2)
                yaw = math.degrees(hd[j]) + 90.0 * out_side
                chevrons.append(inst(ox, oy, road[j, 2] - 0.1, yaw, 0, 0, 1, 1, 1))
    out["chevrons"] = chevrons

    # Concrete utility poles every ~45 m on the uphill side.
    poles = []
    for t in np.arange(20.0, s_road[-1] - 20.0, 45.0):
        x0 = np.interp(t, s_road, road[:, 0]); y0 = np.interp(t, s_road, road[:, 1]); z0 = np.interp(t, s_road, road[:, 2])
        dx_ = np.interp(t + 1, s_road, road[:, 0]) - np.interp(t - 1, s_road, road[:, 0])
        dy_ = np.interp(t + 1, s_road, road[:, 1]) - np.interp(t - 1, s_road, road[:, 1])
        n_ = np.hypot(dx_, dy_) + 1e-9
        hw_ = half_w if t < concrete_from else layout["road"]["concrete_half_width_cm"] / 100.0
        cand = []
        for side in (1, -1):
            px_, py_ = x0 - dy_ / n_ * side * (hw_ + 3.0), y0 + dx_ / n_ * side * (hw_ + 3.0)
            far = T.height(x0 - dy_ / n_ * side * (hw_ + 12.0), y0 + dx_ / n_ * side * (hw_ + 12.0))
            cand.append((far, px_, py_))
        far, px_, py_ = max(cand)                                        # uphill side
        if far > z0 - 1.0 and road_dist(px_, py_) > hw_ + 1.5:
            poles.append(inst(px_, py_, T.height(px_, py_) - 0.3, math.degrees(math.atan2(dy_, dx_)), 0, 0, 1, 1, 1))
    out["poles"] = poles

    # Hmong hamlets where they really are: OSM landuse=residential (Pai Lung, Ta Lang ...), plus the
    # houses above the Ta Lang dock and in the Tham Ma valley.
    dock_xy = np.array(layout["spawns"]["dock"]["location_cm"][:2]) / 100.0
    rim_xy = np.array(layout["viewpoints"]["ma_pi_leng"]["road_cm"][:2]) / 100.0
    hamlets = [(dock_xy + [-60, 0], 12, 130)]
    if tm:
        hamlets.append((road_xy[min(40, len(road_xy) - 1)] + [60, 60], 8, 120))
    try:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import gen_heightmap as geo
        lu = json.loads((ROOT / "Data/Geo/osm_landuse.json").read_text(encoding="utf-8"))
        for e in lu["elements"]:
            if e.get("tags", {}).get("landuse") == "residential" and "geometry" in e:
                ll = np.array([(q["lat"], q["lon"]) for q in e["geometry"]]).mean(axis=0)
                c_xy = geo.to_xy(*ll)
                if np.abs(c_xy).max() < T.half - 150:
                    hamlets.append((c_xy, 10, 140))
    except Exception as exc:                                   # no geo data: keep the two hamlets above
        print(f"hamlets: {exc}")
    houses = {"grey": [], "green": [], "blue": []}
    for centre, n_houses, radius in hamlets:
        placed = []
        for _ in range(400):
            if len(placed) >= n_houses:
                break
            hx, hy = centre + rng.uniform(-radius, radius, 2)
            r_, c_ = T.idx(hx, hy)
            hz = float(T.height(hx, hy))
            if T.slope[r_, c_] > 24 or road_dist(hx, hy) < 9 or hz < T.water + 8:
                continue
            if any(np.hypot(hx - qx, hy - qy) < 16 for qx, qy in placed):
                continue
            placed.append((hx, hy))
            gx_, gy_ = float(T.gx[r_, c_]), float(T.gy[r_, c_])
            yaw = math.degrees(math.atan2(gy_, gx_)) + 90.0 + rng.uniform(-10, 10)   # ridge along the contour
            sc = rng.uniform(0.8, 1.2)
            houses[rng.choice(["grey", "grey", "green", "blue"])].append(inst(hx, hy, hz - 0.8, yaw, 0, 0, sc, sc * rng.uniform(0.8, 1.1), 1))
    out["houses"] = houses
    peach = []
    for hx_, hy_, *_ in [h for v in houses.values() for h in v]:
        for _ in range(2):
            a_ = rng.uniform(0, 2 * math.pi); r0 = rng.uniform(9, 16)
            tx_, ty_ = hx_ / 100.0 + math.cos(a_) * r0, hy_ / 100.0 + math.sin(a_) * r0
            r_, c_ = T.idx(tx_, ty_)
            if T.slope[r_, c_] > 35 or road_dist(tx_, ty_) < 6 or T.height(tx_, ty_) < T.water + 6:
                continue
            sc_ = rng.uniform(0.45, 0.65)
            peach.append(inst(tx_, ty_, T.height(tx_, ty_) - 0.2, rng.uniform(0, 360), 0, 0, sc_, sc_, sc_))
    out["trees"]["peach"] = peach

    # Footpath mask (2 m / px): zig-zag trails across the canyon slopes and around the hamlets,
    # the pale lines that stand out on the dry grass in the Nho Que photos.
    res = int(2 * T.half / 2.0)
    mask = np.zeros((res, res), np.float32)
    def plot(px_, py_, val=1.0):
        c_ = int((px_ + T.half) / 2.0); r_ = int((py_ + T.half) / 2.0)
        if 0 <= c_ < res and 0 <= r_ < res:
            mask[r_, c_] = max(mask[r_, c_], val)
    river = np.array(layout["water"]["river_cm"]) / 100.0
    starts = list(river[::14, :2]) + [np.array(h) for h in (dock_xy, rim_xy)]
    for sx0, sy0 in starts:
        for side in (-1, 1):
            px_, py_ = sx0 + side * rng.uniform(110, 260), sy0 + rng.uniform(-60, 60)
            heading = rng.choice([-1, 1])
            leg = rng.uniform(60, 150)
            walked = 0.0
            for _ in range(900):
                r_, c_ = T.idx(px_, py_)
                gx_, gy_ = float(T.gx[r_, c_]), float(T.gy[r_, c_])
                g_ = math.hypot(gx_, gy_) + 1e-6
                if T.height(px_, py_) > 1550 or T.height(px_, py_) < T.water + 2:
                    break
                cx_, cy_ = -gy_ / g_ * heading, gx_ / g_ * heading          # along the contour
                ux_, uy_ = gx_ / g_, gy_ / g_                                # uphill
                dx_, dy_ = cx_ * 0.93 + ux_ * 0.37, cy_ * 0.93 + uy_ * 0.37  # ~20 deg climb
                px_ += dx_ * 1.0; py_ += dy_ * 1.0
                walked += 1.0
                plot(px_, py_)
                if walked > leg:                                            # switchback
                    heading = -heading; walked = 0.0; leg = rng.uniform(50, 140)
    mask = np.maximum(mask, 0.5 * (np.roll(mask, 1, 0) + np.roll(mask, 1, 1)))   # soften 1 px
    from PIL import Image
    (ROOT / "Generated/Assets/Textures").mkdir(parents=True, exist_ok=True)
    Image.fromarray((np.clip(mask, 0, 1) * 255).astype(np.uint8)).save(ROOT / "Generated/Assets/Textures/T_HG_PathMask.png")

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
    ax_, ay_ = (sx2 - rx2), (sy2 - ry2)
    al_ = math.hypot(ax_, ay_) + 1e-6
    ax_, ay_ = ax_ / al_, ay_ / al_
    fence = []
    for k in range(2):                                      # two 4 m pieces = half the spur root
        fx = rx2 + ax_ * 12.0 + (-ay_) * (-0.5 + 4.0 * k)
        fy = ry2 + ay_ * 12.0 + (ax_) * (-0.5 + 4.0 * k)
        fence.append(inst(fx, fy, T.height(fx, fy) - 0.2, math.degrees(math.atan2(ax_, -ay_)), 0, 0, 1, 1, 1))
    out["stone_walls"] += fence

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
    counts.update({f"house_{k}": len(v) for k, v in out["houses"].items()})
    print(f"scatter {counts} in {time.time() - t0:.1f}s")


if __name__ == "__main__":
    main()
