#!/usr/bin/env python3
"""Ha Giang karst vegetation (round 6B, agent A): species meshes + where they grow.

The old scatter (generic pines and round broadleaf crowns on grassy slopes) read as "anywhere". The Dong Van karst
plateau reads as: grey limestone outcrops scattered densely over every slope with corn and grass growing between
the rocks, few trees - columnar po mu / cypress on the ridges, scrub in the rock cracks, bamboo clumps by the
hamlets, wild banana down in the valleys, pine plantations on some slopes - and tufts of grass, green and dry.

Writes
  Generated/Assets/Meshes/SM_HG_Veg_*.obj         several variants per species (names carry the LOD-chain key:
                                                 Boulder / Bush / Crown / Trunk, assets.LOD_CHAINS)
  Generated/Heightmaps/HaGiang_vegetation.json    {mesh name: [[x, y, z, yaw, pitch, roll, sx, sy, sz], ...]}
Late March (ART_BIBLE season): no buckwheat bloom (October-November), young corn on the terraces (agent B's corn
fields), dry brown grass on the rocky ground.

Usage: .venv/bin/python Scripts/Tools/gen_vegetation.py   (after gen_heightmap.py and gen_scatter.py)
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Scripts" / "Tools"))
sys.path.insert(0, str(ROOT / "Scripts"))
import gen_assets as ga                     # noqa: E402  (mesh helpers: icosphere, _tube, _blob, karst_rock, write_obj)
from hg_lib import placements as P          # noqa: E402

GEN = ROOT / "Generated" / "Heightmaps"
MESH_OUT = ROOT / "Generated" / "Assets" / "Meshes"
SEED = 6061

# mesh name -> (material key in materials.py, cull start / end cm, cast shadows). Read by hg_lib/vegetation.py.
SPECIES = {}


def _reg(name, mat, cull, shadows):
    SPECIES[name] = {"material": mat, "cull": cull, "shadows": shadows}
    return name


# ---------------------------------------------------------------------------------------------------------------
# Meshes
# ---------------------------------------------------------------------------------------------------------------

def _cone(base_z, top_z, radius, sides=9, rng=None, jag=0.0):
    """Closed-ish cone (open base) from base_z (radius) to a point at top_z."""
    ang = np.linspace(0, 2 * np.pi, sides, endpoint=False)
    r = radius * (1.0 + (rng.uniform(-jag, jag, sides) if rng is not None else 0.0))
    ring = np.stack([np.cos(ang) * r, np.sin(ang) * r, np.full(sides, base_z)], 1)
    v = np.vstack([ring, [[0, 0, top_z]], [[0, 0, base_z]]])
    f = []
    for i in range(sides):
        j = (i + 1) % sides
        f += [(i, j, sides), (j, i, sides + 1)]
    return v, np.array(f)


def _blade(rng, h, w, lean):
    """One grass blade: a thin bent two-sided triangle strip (3 segments)."""
    ang = rng.uniform(0, 2 * np.pi)
    d = np.array([math.cos(ang), math.sin(ang), 0.0])
    side = np.array([-d[1], d[0], 0.0]) * w
    pts, f = [], []
    for k in range(4):
        t = k / 3.0
        c = d * lean * t * t + np.array([0, 0, h * t])
        ww = (1.0 - t) * 0.5 + 0.02
        pts += [c - side * ww, c + side * ww]
    for k in range(3):
        a, b, c2, e = 2 * k, 2 * k + 1, 2 * k + 2, 2 * k + 3
        f += [(a, c2, e), (a, e, b), (a, e, c2), (a, b, e)]   # both sides
    return np.array(pts), np.array(f)


def make_meshes(rng):
    MESH_OUT.mkdir(parents=True, exist_ok=True)
    out = []

    # Limestone outcrops: low, wide, fractured grey blocks half sunk in the slope (the karst "rock garden").
    for i, (size, aspect) in enumerate(((180, (1.0, 0.8, 0.55)), (260, (1.0, 0.7, 0.45)), (120, (1.0, 0.9, 0.8)),
                                        (340, (1.0, 0.6, 0.38)))):
        v, f = ga.karst_rock(rng, 3, size, aspect, flute_count=rng.integers(5, 9), flute_depth=0.06,
                             bumpiness=0.12, flat_bottom=0.9, cuts=7)
        v[:, 2] -= size * 0.12                          # sunk into the ground a little
        name = _reg(f"SM_HG_Veg_Outcrop_Boulder_{i}", "rock", (60000, 80000), True)
        ga.write_obj(MESH_OUT / f"{name}.obj", v, f)
        out.append(name)

    # Po mu / cypress: tall narrow conifer of the ridges - dark, columnar, slightly ragged.
    for i, (h, r) in enumerate(((1400, 150), (1050, 120), (1700, 170))):
        v, f = ga._tube((0, 0, -30), (0, 0, h * 0.35), 14 + i * 3, 9, 6)
        name = _reg(f"SM_HG_Veg_Cypress_Trunk_{i}", "bark", (100000, 130000), True)
        ga.write_obj(MESH_OUT / f"{name}.obj", v, f)
        parts = []
        for k in range(5):
            z0 = h * (0.18 + k * 0.15)
            rr = r * (1.0 - k * 0.13) * rng.uniform(0.9, 1.1)
            cv, cf = _cone(z0, z0 + h * 0.34, rr, 9, rng, 0.25)
            cv[:, :2] += rng.normal(0, rr * 0.08, 2)
            parts.append((cv, cf))
        v, f = ga._merge(parts)
        name = _reg(f"SM_HG_Veg_Cypress_Crown_{i}", "pine", (100000, 130000), True)
        ga.write_obj(MESH_OUT / f"{name}.obj", v, f)
        out.append(name)

    # Scrub in the rock cracks: low irregular clusters of small blobs.
    for i in range(3):
        parts = [ga._blob(np.array([rng.normal(0, 45), rng.normal(0, 45), rng.uniform(25, 70)]), rng.uniform(35, 70),
                          rng, 1, (1, 1, 0.7), 0.5) for _ in range(rng.integers(4, 7))]
        v, f = ga._merge(parts)
        name = _reg(f"SM_HG_Veg_Scrub_Bush_{i}", "bush" if i < 2 else "leaf", (30000, 40000), False)
        ga.write_obj(MESH_OUT / f"{name}.obj", v, f)
        out.append(name)

    # Bamboo clumps by the hamlets: 12-18 thin culms arching out, feathery leaf masses on the upper half.
    for i in range(2):
        culms, leaves = [], []
        for _ in range(rng.integers(12, 19)):
            a = rng.uniform(0, 2 * np.pi)
            lean = rng.uniform(0.05, 0.35)
            h = rng.uniform(700, 1100)
            base = np.array([math.cos(a), math.sin(a), 0]) * rng.uniform(5, 45)
            top = base + np.array([math.cos(a) * lean * h, math.sin(a) * lean * h, h])
            mid = (base + top) * 0.5 + np.array([0, 0, 40])
            culms.append(ga._tube(base, mid, 5, 4, 5))
            culms.append(ga._tube(mid, top, 4, 1.5, 5))
            for t in (0.55, 0.7, 0.85, 1.0):
                c = base + (top - base) * t
                leaves.append(ga._blob(c, rng.uniform(60, 110), rng, 1, (1.3, 1.3, 0.45), 0.6))
        v, f = ga._merge(culms)
        name = _reg(f"SM_HG_Veg_Bamboo_Trunk_{i}", "bush", (60000, 80000), True)
        ga.write_obj(MESH_OUT / f"{name}.obj", v, f)
        v, f = ga._merge(leaves)
        name = _reg(f"SM_HG_Veg_Bamboo_Crown_{i}", "leaf", (60000, 80000), True)
        ga.write_obj(MESH_OUT / f"{name}.obj", v, f)
        out.append(name)

    # Wild banana in the valleys: a few pseudostems, big paddle leaves arching out (two-sided slabs, some torn).
    for i in range(2):
        stems, leaves = [], []
        for s in range(rng.integers(2, 4)):
            off = rng.normal(0, 40, 2)
            h = rng.uniform(250, 380)
            stems.append(ga._tube((off[0], off[1], -20), (off[0], off[1], h), 16, 11, 7))
            for k in range(rng.integers(5, 8)):
                a = rng.uniform(0, 2 * np.pi)
                d = np.array([math.cos(a), math.sin(a), 0.0])
                n = np.array([-d[1], d[0], 0.0])
                L = rng.uniform(170, 260)
                W = rng.uniform(28, 40)
                pts, f = [], []
                for q in range(6):
                    t = q / 5.0
                    c = np.array([off[0], off[1], h]) + d * L * t + np.array([0, 0, 80 * math.sin(math.pi * t * 0.8) - 60 * t * t])
                    w = W * math.sin(math.pi * min(1.0, t * 1.1 + 0.05))
                    pts += [c - n * w, c + n * w]
                for q in range(5):
                    a0, b0, c0, e0 = 2 * q, 2 * q + 1, 2 * q + 2, 2 * q + 3
                    f += [(a0, c0, e0), (a0, e0, b0), (a0, e0, c0), (a0, b0, e0)]
                leaves.append((np.array(pts), np.array(f)))
        v, f = ga._merge(stems)
        name = _reg(f"SM_HG_Veg_Banana_Trunk_{i}", "bush", (50000, 70000), True)
        ga.write_obj(MESH_OUT / f"{name}.obj", v, f)
        v, f = ga._merge(leaves)
        name = _reg(f"SM_HG_Veg_Banana_Crown_{i}", "leaf", (50000, 70000), True)
        ga.write_obj(MESH_OUT / f"{name}.obj", v, f)
        out.append(name)

    # Grass tufts: green (in the corn / by water) and dry brown (on the rock ground, late March).
    for i, (n_bl, h, mat) in enumerate(((18, 45, "bush"), (26, 60, "bush"), (22, 55, "house_wall"), (30, 75, "house_wall"))):
        parts = [_blade(rng, h * rng.uniform(0.6, 1.2), rng.uniform(2.5, 4.5), rng.uniform(8, 30)) for _ in range(n_bl)]
        parts = [(p + np.array([rng.normal(0, 9), rng.normal(0, 9), 0]), f) for p, f in parts]
        v, f = ga._merge(parts)
        name = _reg(f"SM_HG_Veg_Grass_Bush_{i}", mat, (9000, 13000), False)
        ga.write_obj(MESH_OUT / f"{name}.obj", v, f)
        out.append(name)
    return out


# ---------------------------------------------------------------------------------------------------------------
# Placement
# ---------------------------------------------------------------------------------------------------------------

def _road_distance_field(road_pts, half_cm, cell_cm):
    n = int(2 * half_cm // cell_cm) + 1
    xs = -half_cm + np.arange(n) * cell_cm
    gx, gy = np.meshgrid(xs, xs, indexing="ij")
    flat = np.stack([gx.ravel(), gy.ravel()], 1)
    best = np.full(len(flat), np.inf)
    for i in range(0, len(road_pts), 400):
        chunk = road_pts[i:i + 400]
        d = np.min(np.linalg.norm(flat[:, None, :] - chunk[None, :, :], axis=2), axis=1)
        best = np.minimum(best, d)
    return best.reshape(n, n), xs


def place(ctx, layout, scatter, rng):
    half = layout["heightmap"]["half_m"] * 100.0
    t = ctx.terrain
    road = np.array(layout["road"]["points_cm"], float)[:, :2]
    road = road[:: max(1, len(road) // 2500)]
    cell = 2000.0
    dist, xs = _road_distance_field(road, half, cell)
    water_z = layout["water"]["surface_z_cm"]
    houses = [(h[0], h[1]) for items in scatter.get("houses", {}).values() for h in items]

    def road_d(x, y):
        i = int(round((x + half) / cell)); j = int(round((y + half) / cell))
        if 0 <= i < dist.shape[0] and 0 <= j < dist.shape[1]:
            return dist[i, j]
        return np.inf

    def slope(x, y):
        e = 300.0
        dzx = t.z(x + e, y) - t.z(x - e, y)
        dzy = t.z(x, y + e) - t.z(x, y - e)
        return math.degrees(math.atan(math.hypot(dzx, dzy) / (2 * e)))

    def off_road(x, y, clear_cm):
        return ctx.road.edge_clearance(x, y) >= clear_cm

    def item(x, y, z, s, yaw=None, tilt=0.0):
        return [round(x, 1), round(y, 1), round(z, 1), round(rng.uniform(0, 360) if yaw is None else yaw, 1),
                round(rng.uniform(-tilt, tilt), 2), round(rng.uniform(-tilt, tilt), 2), round(s, 3), round(s, 3),
                round(s * rng.uniform(0.85, 1.15), 3)]

    out = {name: [] for name in SPECIES}
    rocks = [n for n in SPECIES if "Outcrop" in n]
    scrub = [n for n in SPECIES if "Scrub" in n]
    grass_green = [n for n in SPECIES if "Grass" in n and SPECIES[n]["material"] == "bush"]
    grass_dry = [n for n in SPECIES if "Grass" in n and SPECIES[n]["material"] != "bush"]

    def rock_field(x, y):
        # Clustered "rock gardens": smooth noise over the map, stronger on steeper and higher ground.
        return 0.5 + 0.5 * math.sin(x / 9100.0 + 1.3 * math.sin(y / 13300.0)) * math.sin(y / 7700.0 + 0.7 * math.cos(x / 11900.0))

    # 1) Limestone outcrops: dense within 900 m of the road (what the rider sees), clustered, on 8-45 deg slopes.
    tries = 0
    while sum(len(out[n]) for n in rocks) < 14000 and tries < 220000:
        tries += 1
        x, y = rng.uniform(-half, half), rng.uniform(-half, half)
        d = road_d(x, y)
        if d > 90000 or not off_road(x, y, 350.0):
            continue
        z = t.z(x, y)
        if z < water_z + 600.0:
            continue
        s_deg = slope(x, y)
        if s_deg < 6 or s_deg > 48:
            continue
        p = rock_field(x, y) * (0.5 + s_deg / 60.0) * (1.4 if d < 25000 else 1.0)
        if rng.random() > p:
            continue
        name = rocks[rng.integers(0, len(rocks))]
        s = rng.uniform(0.55, 1.5) * (1.2 if "_3" in name else 1.0)
        out[name].append(item(x, y, z - 15.0, s, tilt=6.0))
        # Scrub and grass in the cracks around the rock.
        if rng.random() < 0.35 and d < 60000:
            a = rng.uniform(0, 2 * np.pi)
            sx, sy = x + math.cos(a) * 160 * s, y + math.sin(a) * 160 * s
            if off_road(sx, sy, 250.0):
                out[scrub[rng.integers(0, len(scrub))]].append(item(sx, sy, t.z(sx, sy) - 10.0, rng.uniform(0.6, 1.3)))
        if d < 22000:
            for _ in range(rng.integers(1, 4)):
                a = rng.uniform(0, 2 * np.pi)
                gx, gy = x + math.cos(a) * rng.uniform(90, 220) * s, y + math.sin(a) * rng.uniform(90, 220) * s
                if off_road(gx, gy, 120.0):
                    pool = grass_dry if rng.random() < 0.65 else grass_green
                    out[pool[rng.integers(0, len(pool))]].append(item(gx, gy, t.z(gx, gy) - 3.0, rng.uniform(0.7, 1.4)))

    # 2) Grass tufts along the road shoulders and verges (the near ground the rider looks at).
    for i in range(0, len(road), 2):
        for _ in range(5):
            k = min(len(road) - 1, i + rng.integers(0, 2))
            a = rng.uniform(0, 2 * np.pi)
            r = rng.uniform(500, 2500)
            x, y = road[k][0] + math.cos(a) * r, road[k][1] + math.sin(a) * r
            if not off_road(x, y, 80.0) or slope(x, y) > 40:
                continue
            pool = grass_dry if rng.random() < 0.5 else grass_green
            out[pool[rng.integers(0, len(pool))]].append(item(x, y, t.z(x, y) - 3.0, rng.uniform(0.7, 1.5)))

    # 3) Po mu / cypress on ridges and crests (higher than the ground 60 m around), in small stands.
    cyp_c = [n for n in SPECIES if "Cypress_Crown" in n]
    tries = 0
    while sum(len(out[n]) for n in cyp_c) < 700 and tries < 60000:
        tries += 1
        x, y = rng.uniform(-half, half), rng.uniform(-half, half)
        if road_d(x, y) > 150000 or not off_road(x, y, 600.0):
            continue
        z = t.z(x, y)
        ring = np.mean([t.z(x + math.cos(a) * 6000, y + math.sin(a) * 6000) for a in np.linspace(0, 2 * np.pi, 6, endpoint=False)])
        if z < ring + 900.0 or slope(x, y) > 38:
            continue
        for _ in range(rng.integers(1, 5)):
            cx, cy = x + rng.normal(0, 700), y + rng.normal(0, 700)
            v = rng.integers(0, len(cyp_c))
            s = rng.uniform(0.75, 1.25)
            it = item(cx, cy, t.z(cx, cy) - 20.0, s, tilt=2.0)
            out[cyp_c[v]].append(it)
            out[cyp_c[v].replace("Crown", "Trunk")].append(it)

    # 4) Bamboo clumps and fruit trees by the houses.
    bam = [n for n in SPECIES if "Bamboo_Crown" in n]
    for hx, hy in houses:
        for _ in range(rng.integers(1, 4)):
            a = rng.uniform(0, 2 * np.pi)
            r = rng.uniform(900, 2200)
            x, y = hx + math.cos(a) * r, hy + math.sin(a) * r
            if not off_road(x, y, 500.0) or slope(x, y) > 35:
                continue
            v = rng.integers(0, len(bam))
            it = item(x, y, t.z(x, y) - 10.0, rng.uniform(0.8, 1.2), tilt=3.0)
            out[bam[v]].append(it)
            out[bam[v].replace("Crown", "Trunk")].append(it)

    # 5) Wild banana in the valleys: low (within 150 m of the river level), gentle ground, near the road / houses.
    ban = [n for n in SPECIES if "Banana_Crown" in n]
    tries = 0
    while sum(len(out[n]) for n in ban) < 260 and tries < 80000:
        tries += 1
        x, y = rng.uniform(-half, half), rng.uniform(-half, half)
        z = t.z(x, y)
        if not (water_z + 300.0 < z < water_z + 15000.0) or road_d(x, y) > 60000 or slope(x, y) > 30 or not off_road(x, y, 500.0):
            continue
        for _ in range(rng.integers(1, 4)):
            cx, cy = x + rng.normal(0, 400), y + rng.normal(0, 400)
            v = rng.integers(0, len(ban))
            it = item(cx, cy, t.z(cx, cy) - 10.0, rng.uniform(0.8, 1.3))
            out[ban[v]].append(it)
            out[ban[v].replace("Crown", "Trunk")].append(it)
    return out


def main():
    layout = json.loads((GEN / "HaGiang_layout.json").read_text(encoding="utf-8"))
    scatter = json.loads((GEN / "HaGiang_scatter.json").read_text(encoding="utf-8"))
    r16 = GEN / Path(layout["heightmap"]["r16"]).name
    ctx = P.Ctx(layout, scatter, str(r16))
    rng = np.random.default_rng(SEED)
    make_meshes(rng)
    placed = place(ctx, layout, scatter, rng)
    data = {"species": SPECIES, "instances": placed}
    (GEN / "HaGiang_vegetation.json").write_text(json.dumps(data), encoding="utf-8")
    counts = {k.replace("SM_HG_Veg_", ""): len(v) for k, v in placed.items()}
    print(f"vegetation: {sum(counts.values())} instances {counts}")


if __name__ == "__main__":
    main()
