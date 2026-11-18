#!/usr/bin/env python3
"""Offline gameplay-placement checker for the real Ha Giang map (no Unreal; numpy for the bulk checks).

Loads Generated/Heightmaps/HaGiang_layout.json + HaGiang_scatter.json + HaGiang_4033.r16 and checks every
gameplay placement the scene scripts make, using the same math (Scripts/hg_lib/placements.py):

  spawns      player start, layout bikes / boats on valid ground or water
  crowds      parked bikes + tourists at Doc Tham Ma / Ma Pi Leng (flat, dry, off the road, outside buildings)
  cows        herds on the verge
  pickups     Happy Water bottles
  river       pontoons, moored and cruising boats float (terrain below surface - depth over the hull + margin)
  traffic     convoy / truck spawn distances; offline replay of the C++ auto-drive controller (pure pursuit,
              curve speed limit, lane offset) over the route - wheels must stay on the asphalt / concrete
  police      bike spawn points out of sight; helicopter terrain clearance vs the relief
  viewpoints  altitude_m label vs terrain, convoys reaching each viewpoint
  terrain     ground sticking out of the reservoir (boats run aground on it)

  --legacy    evaluate the pre-fix scene math / controller (CP04 numbers tuned on the old 4 km map) instead.

Prints a table; exit code 1 when any check FAILs (WARN does not fail).

    .venv/bin/python Scripts/Tools/validate_layout.py [--gen DIR] [--legacy] [--no-sim] [-v]
"""
from __future__ import annotations

import argparse
import bisect
import json
import math
import os
import random
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Scripts"))
from hg_lib import placements as P  # noqa: E402  (pure Python, no unreal)

SLOPE_MAX = P.MAX_GROUND_SLOPE_DEG


# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------

class Report:
    def __init__(self, verbose):
        self.rows = []
        self.verbose = verbose

    def add(self, cat, item, status, detail=""):
        self.rows.append((cat, item, status, detail))

    def check(self, cat, item, problem, ok_detail="", warn=False):
        self.add(cat, item, ("WARN" if warn else "FAIL") if problem else "PASS", problem or ok_detail)

    def print(self):
        shown = [r for r in self.rows if self.verbose or r[2] != "PASS"]
        w0 = max([len(r[0]) for r in self.rows] + [8])
        w1 = min(max([len(r[1]) for r in self.rows] + [4]), 34)
        print(f"{'CATEGORY':<{w0}}  {'ITEM':<{w1}}  STATUS  DETAIL")
        print("-" * (w0 + w1 + 60))
        for cat, item, st, det in shown:
            print(f"{cat:<{w0}}  {item[:w1]:<{w1}}  {st:<6}  {det}")
        print("-" * (w0 + w1 + 60))
        cats = sorted({r[0] for r in self.rows})
        for c in cats:
            rs = [r for r in self.rows if r[0] == c]
            n = {s: sum(1 for r in rs if r[2] == s) for s in ("PASS", "WARN", "FAIL")}
            print(f"{c:<{w0}}  {len(rs):4d} checks  {n['PASS']:4d} pass  {n['WARN']:3d} warn  {n['FAIL']:3d} FAIL")
        tot = {s: sum(1 for r in self.rows if r[2] == s) for s in ("PASS", "WARN", "FAIL")}
        print(f"{'TOTAL':<{w0}}  {len(self.rows):4d} checks  {tot['PASS']:4d} pass  {tot['WARN']:3d} warn  {tot['FAIL']:3d} FAIL")
        return tot["FAIL"]


# ---------------------------------------------------------------------------
# Independent numpy terrain checks (do not reuse the helper's sampler for the verdicts)
# ---------------------------------------------------------------------------

class Grid:
    def __init__(self, layout, r16):
        hm = layout["heightmap"]
        n = hm["size"]
        self.n, self.cell, self.half = n, hm["cell_m"] * 100.0, hm["half_m"] * 100.0
        raw = np.fromfile(r16, dtype="<u2").reshape(n, n).astype(np.float64)
        self.z = hm["base_asl_m"] * 100.0 + raw * hm["m_per_unit"] * 100.0
        gy, gx = np.gradient(self.z, self.cell)
        self.slope = np.degrees(np.arctan(np.hypot(gx, gy)))

    def idx(self, x, y):
        return (int(np.clip(round((y + self.half) / self.cell), 0, self.n - 1)),
                int(np.clip(round((x + self.half) / self.cell), 0, self.n - 1)))

    def patch(self, arr, x, y, r):
        k = max(0, int(math.ceil(r / self.cell)))
        iy, ix = self.idx(x, y)
        return arr[max(iy - k, 0):iy + k + 1, max(ix - k, 0):ix + k + 1]

    def ground(self, x, y):
        return float(self.patch(self.z, x, y, 0)[0, 0])

    def max_slope(self, x, y, r):
        return float(self.patch(self.slope, x, y, r).max())

    def max_z(self, x, y, r):
        return float(self.patch(self.z, x, y, r).max())


def ground_problem(ctx, g, x, y, *, r=60.0, slope=SLOPE_MAX, ref_z=None, max_dz=None, road_clear=None,
                   fp_margin=0.0, spawn_z=None, z_band=(-50.0, 300.0)):
    """Independent verdict for something standing at (x, y)."""
    if not ctx.terrain.inside(x, y, 200.0):
        return "outside the map"
    gz = g.ground(x, y)
    ws = P.water_surface(ctx.layout, x, y)
    if ws is not None and gz < ws + 20.0:
        return f"in water (ground {(gz - ws) / 100:+.1f} m vs surface)"
    s = g.max_slope(x, y, r)
    if s > slope:
        return f"slope {s:.0f} deg > {slope:.0f}"
    if ref_z is not None and max_dz is not None and abs(gz - ref_z) > max_dz:
        return f"ground {(gz - ref_z) / 100:+.1f} m from reference"
    name = P.inside_footprint(ctx.fps, x, y, fp_margin)
    if name:
        return f"inside {name}"
    if road_clear is not None:
        c = min(ctx.road.edge_clearance(x, y), ctx.stubs.edge_clearance(x, y))
        if c < road_clear:
            return f"{'on the road' if c < 0 else 'at the road edge'} ({c / 100:+.1f} m from edge)"
    if spawn_z is not None:
        dz = spawn_z - gz
        if not (z_band[0] <= dz <= z_band[1]):
            return f"spawn z {dz / 100:+.1f} m from ground"
    return None


# ---------------------------------------------------------------------------
# Legacy (pre-fix) scene math, replicated from scenes.py / world.py @ 44ea1fa
# ---------------------------------------------------------------------------

def legacy_offset(fr, along, side, up=0.0):
    x, y, z, fx, fy, rx, ry = fr
    return x + fx * along + rx * side, y + fy * along + ry * side, z + up


def legacy_living_world(ctx):
    road, layout, sec = ctx.road, ctx.layout, ctx.layout["road"]["sections_m"]
    rng = random.Random(404)
    out = {"crowds": {}, "cows": [], "pickups": []}
    rim_m = ctx.viewpoint_road_m("ma_pi_leng")
    for key, road_m in (("doc_tham_ma", sec["tham_ma"] + 40), ("ma_pi_leng", rim_m - 30)):
        fr = road.frame(road_m)
        bikes = [legacy_offset(fr, -900 + i * 240, 520, 70) for i in range(5)]
        cx, cy, cz = layout["viewpoints"][key]["location_cm"]
        tourists = []
        for _ in range(9):
            a, r = rng.uniform(0, 2 * math.pi), rng.uniform(250, 900)
            tourists.append((cx + math.cos(a) * r, cy + math.sin(a) * r, cz + 120))
        out["crowds"][key] = {"road_m": road_m, "bikes": bikes, "tourists": tourists}
    for at_m, count in ((sec["tham_ma"] + 0.45 * (sec["rim"] - sec["tham_ma"]), 5), (rim_m + 450, 4)):
        fr = road.frame(at_m)
        for _ in range(count):
            x, y, z = legacy_offset(fr, rng.uniform(-700, 700), rng.uniform(-300, 300), 120)
            rng.uniform(0, 360)
            out["cows"].append((x, y, z))
    L = road.length_m
    for i in range(9):
        m = 400 + (L - 800) * i / 8
        out["pickups"].append((f"HappyWater_{i}",) + legacy_offset(road.frame(m), 0, 150 if i % 2 else -150, 20))
    for key, vp in layout["viewpoints"].items():
        x, y, z = vp["location_cm"]
        out["pickups"].append((f"HappyWater_{key}", x + 400, y, z + 20))
    return out


def legacy_river(ctx):
    dx, dy, yaw, fx, fy, sx, sy = P.dock_frame(ctx.layout)
    pontoons = [(dx + fx * 3000 + sx * (i - 1) * 2600, dy + fy * 3000 + sy * (i - 1) * 2600, yaw + 90) for i in range(3)]
    moored = [(dx + fx * 4800 + sx * (i - 2.5) * 420, dy + fy * 4800 + sy * (i - 2.5) * 420, yaw) for i in range(6)]
    pts = [(p[0], p[1]) for p in ctx.layout["water"]["river_cm"][::8]]
    n = len(pts)
    return {"pontoons": pontoons, "moored": moored, "route": pts, "starts": [int(n * 0.25), int(n * 0.45), int(n * 0.62)]}


def legacy_traffic(ctx):
    L = ctx.road.length_m
    convoys = [{"name": "c650", "start_m": 650, "dir": 1, "bikes": 10, "kmh": 38},
               {"name": "c5200", "start_m": min(5200, L - 600), "dir": 1, "bikes": 8, "kmh": 46},
               {"name": "c6400", "start_m": min(6400, L - 300), "dir": -1, "bikes": 12, "kmh": 44}]
    trucks = [{"name": "t6100", "start_m": min(6100, L - 200), "dir": -1, "kmh": 36, "min_m": 0, "max_m": L},
              {"name": "t2600", "start_m": 2600, "dir": -1, "kmh": 34, "min_m": 0, "max_m": L}]
    return {"convoys": convoys, "trucks": trucks, "bike_spacing_m": 11.0}


# ---------------------------------------------------------------------------
# Offline replay of AHGGroundVehicle::ComputeAutoDriveInput + Tick (2D kinematics)
# ---------------------------------------------------------------------------

VEHICLES = {
    #            max    accel brake steer  hs    latA  look  halfW halfL
    "bike":   (2500.0, 950.0, 2200.0, 115.0, 0.42, 380.0, 1300.0, 38.0, 95.0),
    "truck":  (1700.0, 420.0, 1500.0, 60.0, 0.60, 260.0, 1700.0, 125.0, 380.0),
    "police": (3300.0, 1300.0, 2200.0, 115.0, 0.42, 520.0, 1300.0, 38.0, 95.0),
}


# Lane-keeping gains, mirrored from AHGGroundVehicle (LaneKeepGain, LanePreviewSeconds, ...).
CTRL = {"k": 1.5, "preview_s": 0.3, "preview_min_cm": 150.0, "v0": 300.0}


class RoadFast:
    """Index-based view of placements.Road for the replay loop."""

    def __init__(self, road):
        self.r = road
        self.d = road.d
        self.x, self.y = road.xs, road.ys
        n = len(self.d)
        self.hx = [0.0] * n
        self.hy = [0.0] * n
        for i in range(n - 1):
            L = max(self.d[i + 1] - self.d[i], 1e-6)
            self.hx[i] = (self.x[i + 1] - self.x[i]) / L
            self.hy[i] = (self.y[i + 1] - self.y[i]) / L
        self.hx[-1], self.hy[-1] = self.hx[-2], self.hy[-2]
        self.length = road.length

    def at(self, dcm):
        dcm = min(max(dcm, 0.0), self.length)
        i = min(max(bisect.bisect_right(self.d, dcm) - 1, 0), len(self.d) - 2)
        t = (dcm - self.d[i]) / max(self.d[i + 1] - self.d[i], 1e-6)
        return self.x[i] + (self.x[i + 1] - self.x[i]) * t, self.y[i] + (self.y[i + 1] - self.y[i]) * t, self.hx[i], self.hy[i]

    def closest_near(self, x, y, near, window):
        i0 = max(bisect.bisect_left(self.d, near - window) - 1, 0)
        i1 = min(bisect.bisect_right(self.d, near + window) + 1, len(self.d) - 1)
        best, bd = 1e30, near
        for i in range(i0, i1):
            ax, ay = self.x[i], self.y[i]
            vx, vy = self.x[i + 1] - ax, self.y[i + 1] - ay
            L2 = vx * vx + vy * vy or 1e-6
            t = max(0.0, min(1.0, ((x - ax) * vx + (y - ay) * vy) / L2))
            dx, dy = x - ax - vx * t, y - ay - vy * t
            dd = dx * dx + dy * dy
            if dd < best:
                best, bd = dd, self.d[i] + (self.d[i + 1] - self.d[i]) * t
        return bd, math.sqrt(best)

    def min_radius_ahead(self, d, direction, look, step):
        _, _, px, py = self.at(d)
        best = 1e30
        s = step
        while s <= look:
            _, _, hx, hy = self.at(d + s * direction)
            c = max(-1.0, min(1.0, px * hx + py * hy))
            a = math.acos(c)
            if a > 1e-4:
                best = min(best, step / a)
            px, py = hx, hy
            s += step
        return best


def replay(rf, kind, start_m, direction, cruise_kmh, lane, legacy, max_s=None, end_m=None, dt=1.0 / 30.0, cusps=()):
    """Drive one AI vehicle along the road; returns stats of how far its body leaves the road surface."""
    vmax, acc, brake, steer_deg, hs, lat_a, look0, half_w, _ = VEHICLES[kind]
    road = rf.r
    cruise = cruise_kmh / 0.036
    d = start_m * 100.0
    x, y, hx, hy = rf.at(d)
    hx, hy = hx * direction, hy * direction
    rx, ry = -hy, hx
    lane0 = lane

    def lane_at(dd):
        if legacy:
            return lane0
        hw = min(road.half_width_at(dd / 100.0), road.half_width_at(dd / 100.0 + 15.0 * direction))
        lim = hw - P.LANE_EDGE_MARGIN_CM - half_w                               # GetLaneOffsetAt
        return math.copysign(min(abs(lane0), max(lim, 0.0)), lane0)

    x, y = x + rx * lane_at(d), y + ry * lane_at(d)
    heading = math.atan2(hy, hx)
    v = cruise * 0.5
    resync = 0.0
    t = 0.0
    off_max, off_len, worst_m, cusp_off = 0.0, 0.0, None, 0.0
    dist = 0.0
    stop_m = end_m if end_m is not None else (road.length_m - 16 if direction > 0 else 16)
    while True:
        if max_s is not None and t > max_s:
            break
        if (direction > 0 and d >= stop_m * 100) or (direction < 0 and d <= stop_m * 100):
            break
        # --- ComputeAutoDriveInput ---
        d += v * dt * direction
        resync -= dt
        if resync <= 0.0:
            resync = 0.25
            d = rf.r.closest(x, y)[0] * 100.0              # global closest point (FindInputKeyClosestToWorldLocation)
        if legacy:
            # Chase a point Look ahead on the lane (cuts the inside of every bend by ~R - sqrt(R^2 - L^2)).
            look = look0 + abs(v) * 0.35
            td = d + look * direction
            tx, ty, thx, thy = rf.at(td)
            thx, thy = thx * direction, thy * direction
            ln = lane_at(td)
            tx, ty = tx - thy * ln, ty + thx * ln
            desired = math.atan2(ty - y, tx - x)
        else:
            # Lane keeping (AHGGroundVehicle::ComputeAutoDriveInput): one Newton step keeps the road distance
            # exact between resyncs, then steer for the road heading a little ahead minus a cross-track term.
            px, py, pfx, pfy = rf.at(d)
            d = min(max(d + (x - px) * pfx + (y - py) * pfy, 0.0), rf.length)
            px, py, pfx, pfy = rf.at(d)
            rx_, ry_ = -pfy * direction, pfx * direction
            ln = lane_at(d)
            e = (x - px - rx_ * ln) * rx_ + (y - py - ry_ * ln) * ry_
            _, _, thx, thy = rf.at(d + direction * max(abs(v) * CTRL["preview_s"], CTRL["preview_min_cm"]))
            desired = math.atan2(thy * direction, thx * direction) - math.atan2(CTRL["k"] * e, abs(v) + CTRL["v0"])
        err = math.degrees((desired - heading + math.pi) % (2 * math.pi) - math.pi)
        steer = max(-1.0, min(1.0, err / 22.0))
        if legacy:
            radius = rf.min_radius_ahead(d, direction, 3600.0, 600.0)
        else:
            radius = max(rf.min_radius_ahead(d, direction, 3600.0, 300.0) - abs(lane_at(d)), 150.0)
        target = min(cruise, math.sqrt(lat_a * radius))
        throttle = max(-1.0, min(1.0, (target - v) / 300.0))
        if kind == "police" and legacy:
            throttle = max(throttle, 0.6)                    # old "race along the road" floor
        elif kind == "police" and throttle > 0.0:
            throttle = max(throttle, 0.6)
        # --- Tick ---
        alpha = min(abs(v) / 600.0, 1.0)
        hss = 1.0 + (hs - 1.0) * min(abs(v) / vmax, 1.0)
        heading += math.radians(steer * steer_deg * alpha * hss) * dt
        if throttle > 1e-4:
            v += acc * throttle * dt
        elif throttle < -1e-4:
            v += (brake if v > 10 else acc) * throttle * dt
        v = min(v, vmax)
        x += math.cos(heading) * v * dt
        y += math.sin(heading) * v * dt
        dist += v * dt
        t += dt
        # --- verdict: body edge vs road edge ---
        m_here, lat = rf.closest_near(x, y, d, 4000.0)
        over = lat + half_w - road.half_width_at(m_here / 100.0)
        ci = bisect.bisect_left(cusps, m_here / 100.0)
        if any(abs(cusps[j] - m_here / 100.0) < 25.0 for j in (ci - 1, ci) if 0 <= j < len(cusps)):
            cusp_off = max(cusp_off, over)
            continue                                      # spline cusp: nothing can follow it (data issue)
        if over > off_max:
            off_max, worst_m = over, m_here / 100.0
        if over > 10.0:
            off_len += v * dt
    return {"off_max_cm": off_max, "off_len_m": off_len / 100.0, "worst_m": worst_m, "driven_m": dist / 100.0,
            "cusp_off_cm": cusp_off,
            "time_s": t, "end_m": d / 100.0}


# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------

def check_spawns(ctx, g, rep, legacy, river):
    sp = ctx.layout["spawns"]
    river_obstacles = [(x, y, yaw, *P.PONTOON_HALF) for x, y, yaw in river["pontoons"]] + \
                      [(x, y, yaw, *P.CANO_HALF) for x, y, yaw in river["moored"]]
    x, y, z = sp["player_start"]["location_cm"]
    rep.check("spawns", "player_start", ground_problem(ctx, g, x, y, r=80, spawn_z=z, z_band=(0, 350)))
    if legacy:
        bikes, boats = sp["bikes"], [dict(b, yaw=sp["dock"]["yaw"] + 90) for b in sp["boats"]]
    else:
        vs = P.vehicle_spawns(ctx)                 # what world.build_vehicles spawns
        bikes, boats = vs["bikes"], vs["boats"]
    for i, b in enumerate(bikes):
        x, y, z = b["location_cm"]
        rep.check("spawns", f"bike_{i}", ground_problem(ctx, g, x, y, r=100, spawn_z=z, z_band=(-20, 300)))
    for i, b in enumerate(boats):
        x, y, _ = b["location_cm"]
        half = P.CANO_HALF if b["type"] == "Cano" else P.KAYAK_HALF
        ok = P.rect_navigable(ctx, x, y, b["yaw"], half[0], half[1], 0.0, P.BOAT_MIN_DEPTH_CM)
        moved = math.hypot(x - sp["boats"][i]["location_cm"][0], y - sp["boats"][i]["location_cm"][1]) / 100.0
        clash = [o for o in river_obstacles if P.rect_distance(x, y, *o) < max(half) + 30.0]
        if ok and clash:
            ok = False
            rep.add("spawns", f"boat_{b['type']}_{i} berth", "FAIL", "overlaps a pontoon / moored boat")
        rep.check("spawns", f"boat_{b['type']}_{i}", None if ok else
                  f"aground: ground {(g.max_z(x, y, half[0]) - ctx.layout['water']['surface_z_cm']) / 100:+.1f} m vs surface",
                  f"afloat (moved {moved:.1f} m from the layout spawn)" if moved > 0.05 else "afloat")


def check_road(ctx, g, rep):
    """Road data problems the gameplay code cannot fix (reported for gen_heightmap.py)."""
    road = ctx.road
    cusps = P.road_cusps(road)
    if cusps:
        rep.add("road", "spline cusps (decimated hairpins)", "WARN",
                f"{len(cusps)} spots turn tighter than 6 m radius - decimated hairpins in road.points_cm (AI and asphalt mesh cut them), e.g. at "
                + ", ".join(f"{m:.0f}" for m in cusps[:8]) + (" ..." if len(cusps) > 8 else "") + " m")
    pts = ctx.layout["road"]["points_cm"]
    dz = np.array([p[2] - g.ground(p[0], p[1]) for p in pts])
    bad = np.nonzero(np.abs(dz) > 150)[0]
    if len(bad):
        k = int(bad[np.argmax(np.abs(dz[bad]))])
        m = road.closest(pts[k][0], pts[k][1])[0]
        rep.add("road", "road profile vs terrain", "WARN",
                f"{len(bad)}/{len(pts)} road points > 1.5 m off the landscape (worst {dz[k] / 100:+.1f} m at {m:.0f} m); "
                "vehicles drive on the landscape")
    return cusps


def check_viewpoints(ctx, g, rep):
    for key, vp in ctx.layout["viewpoints"].items():
        x, y, z = vp["location_cm"]
        gz = g.ground(x, y)
        diff = vp["altitude_m"] - gz / 100.0
        rep.check("viewpoints", f"{key} altitude label", f"location card says {vp['altitude_m']} m, HUD altimeter "
                  f"shows {gz / 100:.0f} m there ({diff:+.0f} m)" if abs(diff) > 15 else None,
                  f"{vp['altitude_m']} m ok", warn=True)


def check_crowds(ctx, g, rep, plan):
    for key, crowd in plan["crowds"].items():
        vp = ctx.layout["viewpoints"][key]
        vm = ctx.viewpoint_road_m(key)
        rep.check("crowds", f"{key} bikes near viewpoint",
                  f"parked at road {crowd['road_m']:.0f} m, viewpoint at {vm:.0f} m" if abs(crowd["road_m"] - vm) > 60 else None)
        if len(crowd["bikes"]) < 5:
            rep.add("crowds", f"{key} bikes", "WARN", f"only {len(crowd['bikes'])} valid parking spots")
        for i, (x, y, z, *_) in enumerate(crowd["bikes"]):
            ref = ctx.road.frame(ctx.road.closest(x, y)[0])[2]
            rep.check("crowds", f"{key} bike {i}", ground_problem(ctx, g, x, y, r=100, slope=20, ref_z=ref, max_dz=150,
                                                                  road_clear=90, spawn_z=z, z_band=(-20, 250)))
        if len(crowd["tourists"]) < 9:
            rep.add("crowds", f"{key} tourists", "WARN", f"only {len(crowd['tourists'])} valid spots")
        for i, (x, y, z) in enumerate(crowd["tourists"]):
            # The NPC traces 3 m up / 20 m down from its spawn point to find the ground.
            rep.check("crowds", f"{key} tourist {i}", ground_problem(ctx, g, x, y, r=50, slope=30, ref_z=vp["location_cm"][2],
                                                                     max_dz=300, road_clear=60, spawn_z=z, z_band=(-300, 2000)))


def check_cows(ctx, g, rep, cows):
    for i, (x, y, z, *_) in enumerate(cows):
        # HGCowActor snaps to the ground with a trace from +2 m to -10 m.
        rep.check("cows", f"cow {i}", ground_problem(ctx, g, x, y, r=120, slope=SLOPE_MAX, spawn_z=z, z_band=(-200, 1000)))


def check_pickups(ctx, g, rep, pickups):
    for label, x, y, z in pickups:
        gz = g.ground(x, y)
        prob = ground_problem(ctx, g, x, y, r=60, slope=40)
        if prob is None and not (-50 <= z - gz <= 250):
            prob = f"floats {(z - gz) / 100:+.1f} m above ground"
        rep.check("pickups", label, prob)


def check_props(ctx, g, rep):
    """Tham Ma parapet blocks must stay off the road (a block across the hairpin stopped every bike)."""
    blocks, buckets = P.tham_ma_parapet(ctx)
    half = P.PARAPET_SEG[0] * 0.5
    for i, (x, y, z, yaw) in enumerate(blocks):
        a = math.radians(yaw)
        c = min(ctx.road.edge_clearance(x + math.cos(a) * s * half, y + math.sin(a) * s * half) for s in (-1, 0, 1))
        rep.check("props", f"parapet block {i}", None if c >= P.PARAPET_ROAD_CLEAR_CM - 1 else f"{c / 100:.1f} m from the road edge")
    if not blocks:
        rep.add("props", "parapet", "WARN", "no parapet block fits off the road")
    for i, (x, y, z) in enumerate(buckets):
        c = ctx.road.edge_clearance(x, y)
        rep.check("props", f"flower bucket {i}", None if c >= 50 else f"{c / 100:.1f} m from the road edge")


def check_river(ctx, g, rep, river, legacy):
    surf = ctx.layout["water"]["surface_z_cm"]
    for i, (x, y, yaw) in enumerate(river["pontoons"]):
        ok = P.rect_navigable(ctx, x, y, yaw, *P.PONTOON_HALF, 0.0, 30.0)
        rep.check("river", f"pontoon {i}", None if ok else
                  f"deck on the bank: ground up to {(g.max_z(x, y, P.PONTOON_HALF[0]) - surf) / 100:+.1f} m vs surface")
    if len(river["pontoons"]) < 3:
        rep.add("river", "pontoons", "WARN", f"only {len(river['pontoons'])} of 3 fit")
    for i, (x, y, yaw) in enumerate(river["moored"]):
        ok = P.rect_navigable(ctx, x, y, yaw, *P.CANO_HALF, 0.0, P.BOAT_MIN_DEPTH_CM)
        wide = P.rect_navigable(ctx, x, y, yaw, *P.CANO_HALF, 30.0, 0.0)
        rep.check("river", f"moored boat {i}", None if ok and wide else
                  f"aground: ground up to {(g.max_z(x, y, P.CANO_HALF[0]) - surf) / 100:+.1f} m vs surface")
    route = river["route"]
    radius = P.CANO_HALF[1] + 150.0           # 2.8 m hull + 1.5 m margin each side
    bad_pts = [i for i, p in enumerate(route) if not P.navigable(ctx.layout, ctx.terrain, p[0], p[1], radius)]
    bad_seg = [i for i in range(1, len(route))
               if not P.segment_navigable(ctx.layout, ctx.terrain, route[i - 1], route[i], radius)]
    length = sum(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(route, route[1:])) / 100.0
    rep.check("river", "cruise route points", f"{len(bad_pts)}/{len(route)} points aground (first at #{bad_pts[0]})" if bad_pts else None,
              f"{len(route)} points, {length:.0f} m")
    rep.check("river", "cruise route segments", f"{len(bad_seg)}/{max(len(route) - 1, 1)} legs cross ground above surface-0.7 m"
              if bad_seg else None)
    dam = ctx.layout["water"].get("dam")
    if dam and route:
        dd = min(math.hypot(p[0] - dam["location_cm"][0], p[1] - dam["location_cm"][1]) for p in route) / 100.0
        rep.check("river", "cruise route vs dam", f"route comes within {dd:.0f} m of the dam" if dd < dam["width_cm"] / 200 + 50 else None,
                  f"{dd:.0f} m")
    # Obstacles in the channel: cruise boats must not plough through the pontoons / moored boats.
    obstacles = [(x, y, yaw, *P.PONTOON_HALF) for x, y, yaw in river["pontoons"]] + \
                [(x, y, yaw, *P.CANO_HALF) for x, y, yaw in river["moored"]]
    hits = 0
    for a, b in zip(route, route[1:]):
        n = max(1, int(math.hypot(b[0] - a[0], b[1] - a[1]) // 300))
        for k in range(n + 1):
            px, py = a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n
            if any(P.rect_distance(px, py, *o) < P.CANO_HALF[1] + 100 for o in obstacles):
                hits += 1
                break
    rep.check("river", "cruise route vs pontoons", f"{hits} legs pass through pontoons / moored boats" if hits else None)
    for i, s in enumerate(river["starts"]):
        p = route[s]
        rep.check("river", f"cruise boat {i} start", None if P.navigable(ctx.layout, ctx.terrain, p[0], p[1], radius)
                  else "starts aground")
    # Terrain sticking out of the reservoir (terrain generator issue - reported, boats route around it).
    wx = np.arange(g.n) * g.cell - g.half
    k = 3
    z = g.z - surf
    wet = np.zeros_like(z, dtype=bool)                  # cells under a water plane at the reservoir level
    X, Y = np.meshgrid(wx, wx)
    for b in ctx.layout["water"]["bodies"]:
        if abs(b["z_cm"] - surf) > 1.0:
            continue
        r = math.radians(b["yaw"])
        lx = (X - b["center_cm"][0]) * math.cos(r) + (Y - b["center_cm"][1]) * math.sin(r)
        ly = -(X - b["center_cm"][0]) * math.sin(r) + (Y - b["center_cm"][1]) * math.cos(r)
        wet |= (np.abs(lx) <= b["size_cm"][0] / 2) & (np.abs(ly) <= b["size_cm"][1] / 2)
    thin = wet & (z > 0) & (((np.roll(z, k, 0) < -300) & (np.roll(z, -k, 0) < -300)) |
                            ((np.roll(z, k, 1) < -300) & (np.roll(z, -k, 1) < -300)))
    ys, xs = np.nonzero(thin)
    if len(xs):
        dx, dy = ctx.layout["spawns"]["dock"]["location_cm"][:2]
        near = np.hypot(wx[xs] - dx, wx[ys] - dy) < 10000
        rep.add("terrain", "ridges in the reservoir", "WARN",
                f"{len(xs)} cells stand 0-{z[thin].max() / 100:.0f} m above the water between deep cells "
                f"({int(near.sum())} within 100 m of the Ta Lang dock; bbox x {wx[xs].min() / 100:.0f}..{wx[xs].max() / 100:.0f} m, "
                f"y {wx[ys].min() / 100:.0f}..{wx[ys].max() / 100:.0f} m)")


def check_traffic(ctx, rep, plan, legacy, sim, cusps=()):
    road = ctx.road
    L = road.length_m
    rim_end = ctx.sec_m("rim")
    spacing = plan["bike_spacing_m"]
    for c in plan["convoys"]:
        ds = [c["start_m"] - c["dir"] * i * spacing for i in range(c["bikes"])]
        dropped = sum(1 for d in ds if d < 0 or d > L)
        rep.check("traffic", f"convoy {c['name']} spawn", f"{dropped}/{c['bikes']} bikes outside the road (dropped)" if dropped else None,
                  f"{c['start_m']:.0f} m dir {c['dir']:+d}, {c['bikes']} bikes")
    for t in plan["trucks"]:
        lo, hi = t.get("min_m", 0), t.get("max_m", L)
        prob = None
        if hi > rim_end - 50:
            prob = f"drives the 4 m concrete Ta Lang road (range to {hi:.0f} m, concrete from {rim_end:.0f} m)"
        elif not (lo <= t["start_m"] <= hi):
            prob = f"starts at {t['start_m']:.0f} m outside its range {lo:.0f}-{hi:.0f} m"
        rep.check("traffic", f"truck {t['name']} range", prob, f"{lo:.0f}-{hi:.0f} m")
    # Viewpoint coverage: some convoy should pass each viewpoint within ~4 minutes of BeginPlay.
    for key in ctx.layout["viewpoints"]:
        vm = ctx.viewpoint_road_m(key)
        eta = []
        for c in plan["convoys"]:
            gap = (vm - c["start_m"]) * c["dir"]
            if gap < 0:          # moving away: reaches the road end, U-turns, comes back
                end = L if c["dir"] > 0 else 0.0
                gap = abs(end - c["start_m"]) + abs(end - vm)
            eta.append(gap / (c["kmh"] / 3.6 * 0.75))     # ~75 % of cruise on average with the curves
        best = min(eta) if eta else 1e9
        rep.check("traffic", f"convoy reaches {key}", f"first convoy after ~{best / 60:.0f} min" if best > 240 else None,
                  f"~{best / 60:.1f} min", warn=True)
    if not sim:
        return
    rf = RoadFast(road)
    lane = P.LANE_OFFSET_CM
    runs = [("bike", +1, 0.0, None, 38.0, lane + 60.0), ("bike", -1, L, None, 38.0, lane + 60.0)]
    for t in plan["trucks"]:
        lo, hi = t.get("min_m", 0.0), t.get("max_m", L)
        runs.append(("truck", +1, max(lo, 16.0), hi, t["kmh"], lane + 40.0))
        runs.append(("truck", -1, min(hi, L - 16.0), lo, t["kmh"], lane + 40.0))
        break                                     # all trucks share the range
    runs += [("police", +1, 0.0, None, 118.8, 160.0), ("police", -1, L, None, 118.8, 160.0)]
    for kind, direction, start, end, kmh, ln in runs:
        r = replay(rf, kind, start, direction, kmh, ln, legacy, end_m=end, cusps=sorted(cusps))
        tag = f"{kind} {'+' if direction > 0 else '-'} {start:.0f}->{r['end_m']:.0f} m"
        prob = None
        if r["off_max_cm"] > 10.0:
            prob = (f"body {r['off_max_cm'] / 100:.1f} m past the road edge (worst at {r['worst_m']:.0f} m), "
                    f"{r['off_len_m']:.0f} m driven off-road")
        rep.check("autodrive", tag, prob, f"stays on the road, {r['driven_m']:.0f} m in {r['time_s'] / 60:.1f} min"
                  + (f" (up to {r['cusp_off_cm'] / 100:.1f} m off at spline cusps)" if r["cusp_off_cm"] > 10 else ""))


def check_police(ctx, g, rep, legacy):
    """Police bike spawn sight-lines and helicopter terrain clearance along the route."""
    road = ctx.road
    L = road.length_m
    close, none_found = 0, 0
    samples = np.arange(50.0, L - 50.0, 50.0)
    for pm in samples:
        px, py, pz = road.frame(pm)[:3]
        if legacy:
            d = pm - 250.0 - 40.0
            if d < 0:
                d = min(pm + 250.0, L)
            q = road.frame(d)
            if math.hypot(q[0] - px, q[1] - py) < 15000:
                close += 1
        else:
            found = False
            for off in range(250, 1250, 50):
                for sgn in (-1, 1):
                    d = pm + sgn * (off + 40)
                    if 0 <= d <= L:
                        q = road.frame(d)
                        if math.hypot(q[0] - px, q[1] - py) >= 15000:
                            found = True
                            break
                if found:
                    break
            if not found:
                none_found += 1
    n = len(samples)
    if legacy:
        rep.check("police", "bike spawn out of sight", f"{close}/{n} player positions: the bike pops in < 150 m away "
                  f"(250 m along the hairpins)" if close else None)
    else:
        rep.check("police", "bike spawn out of sight", f"{none_found}/{n} positions without a spawn >= 150 m away"
                  if none_found else None, f"{n} positions ok")
    # Helicopter: desired point = player - back * 60 m + 80 m up; old clearance trace started 500 m above it.
    under, blind = 0, 0
    for pm in samples:
        x, y, z, fx, fy, _, _ = road.frame(pm)
        hx, hy, hz = x - fx * 6000, y - fy * 6000, z + 8000
        gz = g.max_z(hx, hy, 1500)
        if gz + 3500 > hz:
            under += 1
            if legacy and gz > hz + 50000:
                blind += 1
    rep.add("police", "heli follow point below terrain", "PASS" if under == 0 else "WARN",
            f"{under}/{n} positions need the terrain-clearance climb (relief {(g.z.max() - g.z.min()) / 100:.0f} m)")
    if legacy:
        rep.check("police", "heli clearance trace", f"{blind}/{n} positions: trace starts underground -> no clearance" if blind else None)
    # Road legs that pass close to each other (global closest-point resync can jump between them).
    rf = RoadFast(road)
    jumps = 0
    for pm in np.arange(0.0, L, 10.0):
        x, y = rf.at(pm * 100)[:2]
        dg = rf.r.closest(x, y)[0]
        if abs(dg - pm) > 80:
            jumps += 1
    if legacy:
        rep.check("autodrive", "road legs closer than the lane", f"{jumps} spots where the global closest point is on another leg"
                  if jumps else None)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    default_gen = ROOT / "Generated" / "Heightmaps"
    ap.add_argument("--gen", type=Path, default=default_gen, help="folder with HaGiang_layout.json / scatter / r16")
    ap.add_argument("--legacy", action="store_true", help="evaluate the pre-fix placement math and controller")
    ap.add_argument("--no-sim", action="store_true", help="skip the auto-drive replay (fast)")
    ap.add_argument("-v", "--verbose", action="store_true", help="also list passing checks")
    args = ap.parse_args()

    gen = args.gen
    if not (gen / "HaGiang_layout.json").is_file():
        print(f"no layout in {gen} (run Scripts/Tools/gen_heightmap.py, or pass --gen)", file=sys.stderr)
        return 2
    layout = json.loads((gen / "HaGiang_layout.json").read_text(encoding="utf-8"))
    scatter = json.loads((gen / "HaGiang_scatter.json").read_text(encoding="utf-8"))
    r16 = gen / Path(layout["heightmap"]["r16"]).name
    ctx = P.Ctx(layout, scatter, str(r16))
    g = Grid(layout, r16)
    rep = Report(args.verbose)
    print(f"route {ctx.road.length_m / 1000:.2f} km (spline), map {2 * layout['heightmap']['half_m'] / 1000:.2f} km, "
          f"{'LEGACY' if args.legacy else 'current'} placement math")

    if args.legacy:
        lw = legacy_living_world(ctx)
        crowds, cows, pickups = lw["crowds"], lw["cows"], lw["pickups"]
        river, traffic = legacy_river(ctx), legacy_traffic(ctx)
    else:
        rng = P.rng()
        crowds = {k: P.checkin_crowd(ctx, k, rng) for k in ("doc_tham_ma", "ma_pi_leng")}
        cows = [c for at_m, n in P.living_world_cows(ctx) for c in P.cow_spots(ctx, at_m, n, rng)]
        pickups = P.happy_water(ctx, rng)
        river, traffic = P.river_traffic(ctx), P.traffic_plan(ctx)

    check_spawns(ctx, g, rep, args.legacy, river)
    cusps = check_road(ctx, g, rep)
    check_viewpoints(ctx, g, rep)
    check_crowds(ctx, g, rep, {"crowds": crowds})
    check_cows(ctx, g, rep, cows)
    check_pickups(ctx, g, rep, pickups)
    check_props(ctx, g, rep)
    check_river(ctx, g, rep, river, args.legacy)
    check_traffic(ctx, rep, traffic, args.legacy, not args.no_sim, cusps)
    check_police(ctx, g, rep, args.legacy)
    fails = rep.print()
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
