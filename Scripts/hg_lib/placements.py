"""Gameplay placement math shared by the UE scene builders and the offline validator.

Pure Python (no `unreal`, no numpy) so that the Unreal editor scripts (scenes.py / world.py) and
Scripts/Tools/validate_layout.py compute exactly the same positions from HaGiang_layout.json,
HaGiang_scatter.json and the R16 heightmap.

Conventions: world centimetres, X east, Y south (north = -Y), Z = metres above sea level * 100.
"Road metres" are distances along the main route as UE measures them on the spline: the layout
points are densified with the same uniform Catmull-Rom tangents a USplineComponent uses for
ESplinePointType::Curve, so distances here match AHGRoadSplineActor within a few metres.
"""
from __future__ import annotations

import array
import math
import random
import sys

# ---------------------------------------------------------------------------
# Gameplay constants mirrored from the C++ defaults (keep in sync).
# ---------------------------------------------------------------------------
LANE_OFFSET_CM = 170.0          # AHGTrafficSpawner::LaneOffset
LANE_EDGE_MARGIN_CM = 30.0      # AHGGroundVehicle::LaneEdgeMargin: body side stays this far inside the road edge
BOAT_MIN_DEPTH_CM = 70.0        # ANhoQueBoatPawn::MinDepth (Cano)
MAX_GROUND_SLOPE_DEG = 35.0     # nothing stands on steeper ground
WATER_DRY_MARGIN_CM = 30.0      # ground this close to the water surface counts as wet


# ---------------------------------------------------------------------------
# Terrain (R16 heightmap)
# ---------------------------------------------------------------------------

class Terrain:
    """Bilinear heightmap sampler. Grid index = (world_m + half_m) / cell_m, row = Y, column = X."""

    def __init__(self, layout, r16_path):
        hm = layout["heightmap"]
        self.n = int(hm["size"])
        self.cell = float(hm["cell_m"]) * 100.0
        self.half = float(hm["half_m"]) * 100.0
        self.base = float(hm["base_asl_m"]) * 100.0
        self.unit = float(hm["m_per_unit"]) * 100.0
        data = array.array("H")
        with open(r16_path, "rb") as f:
            data.fromfile(f, self.n * self.n)
        if sys.byteorder == "big":
            data.byteswap()
        self.data = data

    def _h(self, ix, iy):
        n = self.n
        ix = 0 if ix < 0 else (n - 1 if ix >= n else ix)
        iy = 0 if iy < 0 else (n - 1 if iy >= n else iy)
        return self.data[iy * n + ix]

    def z(self, x, y):
        """Ground height (cm) at world (x, y) cm."""
        gx = (x + self.half) / self.cell
        gy = (y + self.half) / self.cell
        ix, iy = int(math.floor(gx)), int(math.floor(gy))
        fx, fy = gx - ix, gy - iy
        h00, h10 = self._h(ix, iy), self._h(ix + 1, iy)
        h01, h11 = self._h(ix, iy + 1), self._h(ix + 1, iy + 1)
        h = (h00 * (1 - fx) + h10 * fx) * (1 - fy) + (h01 * (1 - fx) + h11 * fx) * fy
        return self.base + h * self.unit

    def inside(self, x, y, margin_cm=0.0):
        return abs(x) <= self.half - margin_cm and abs(y) <= self.half - margin_cm

    def slope_deg(self, x, y, radius_cm=0.0):
        """Steepest slope (degrees) at (x, y) and, with radius_cm, over a small footprint around it."""
        pts = [(x, y)]
        if radius_cm > 0.0:
            for r in (radius_cm * 0.5, radius_cm, radius_cm + self.cell):
                pts += [(x + math.cos(a) * r, y + math.sin(a) * r) for a in (k * math.pi / 4.0 for k in range(8))]
        c = self.cell
        worst = 0.0
        for px, py in pts:
            gx = (self.z(px + c, py) - self.z(px - c, py)) / (2 * c)
            gy = (self.z(px, py + c) - self.z(px, py - c)) / (2 * c)
            worst = max(worst, math.degrees(math.atan(math.hypot(gx, gy))))
        return worst

    def max_z(self, x, y, radius_cm):
        """Highest ground within radius_cm (centre + two rings)."""
        best = self.z(x, y)
        for r in (radius_cm * 0.5, radius_cm):
            for k in range(12):
                a = k * math.pi / 6.0
                best = max(best, self.z(x + math.cos(a) * r, y + math.sin(a) * r))
        return best


# ---------------------------------------------------------------------------
# Water (replicates AHGWaterBody::FindSurface over layout["water"]["bodies"])
# ---------------------------------------------------------------------------

def water_surface(layout, x, y):
    """Highest water-plane Z (cm) covering (x, y), or None."""
    best = None
    for b in layout["water"].get("bodies", []):
        cx, cy = b["center_cm"][:2]
        r = math.radians(b["yaw"])
        dx, dy = x - cx, y - cy
        lx = dx * math.cos(r) + dy * math.sin(r)
        ly = -dx * math.sin(r) + dy * math.cos(r)
        if abs(lx) <= b["size_cm"][0] * 0.5 and abs(ly) <= b["size_cm"][1] * 0.5:
            z = b["z_cm"]
            best = z if best is None else max(best, z)
    return best


def is_wet(layout, terrain, x, y):
    s = water_surface(layout, x, y)
    return s is not None and terrain.z(x, y) < s + WATER_DRY_MARGIN_CM


def navigable(layout, terrain, x, y, radius_cm, depth_cm=BOAT_MIN_DEPTH_CM):
    """True if (x, y) is under a water plane and the ground within radius_cm is at least depth_cm below it."""
    s = water_surface(layout, x, y)
    if s is None:
        return False
    return terrain.max_z(x, y, radius_cm) < s - depth_cm


def segment_navigable(layout, terrain, a, b, radius_cm, step_cm=500.0, depth_cm=BOAT_MIN_DEPTH_CM):
    length = math.hypot(b[0] - a[0], b[1] - a[1])
    n = max(1, int(math.ceil(length / step_cm)))
    for i in range(n + 1):
        t = i / n
        if not navigable(layout, terrain, a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, radius_cm, depth_cm):
            return False
    return True


# ---------------------------------------------------------------------------
# Building footprints (oriented rectangles), replicating world.build_landmarks / build_dock / houses
# ---------------------------------------------------------------------------

def _rect(name, cx, cy, yaw, sx_cm, sy_cm):
    return {"name": name, "cx": cx, "cy": cy, "yaw": yaw, "hx": sx_cm * 0.5, "hy": sy_cm * 0.5}


def footprints(layout, scatter=None):
    out = []
    marks = layout.get("landmarks", {})

    def frame(key):
        m = marks[key]
        x, y, _ = m["location_cm"]
        tx, ty = math.cos(math.radians(m["yaw"])), math.sin(math.radians(m["yaw"]))
        ux, uy = m["up"]
        return (lambda along, up: (x + tx * along * 100 + ux * up * 100, y + ty * along * 100 + uy * up * 100)), m["yaw"]

    if "ma_pi_leng_plaza" in marks:
        at, yaw = frame("ma_pi_leng_plaza")
        if "youth_monument" not in marks:
            out.append(_rect("MPL_Monument", *at(6, 2), yaw, 420, 420))
        out.append(_rect("MPL_NameStone", *at(-6, -4), yaw + 90, 260, 140))
        out.append(_rect("MPL_PanoramaCafe", *at(-15, 4.5), yaw, 900 * 1.25, 600 * 1.0))
        out.append(_rect("MPL_DrinkStall", *at(16, 3.5), yaw, 260, 120))
    if "youth_monument" in marks:
        at, yaw = frame("youth_monument")
        out.append(_rect("YouthVolunteers_Monument", *at(0, 2), yaw, 420, 420))
    if "tham_ma_cafe" in marks:
        at, yaw = frame("tham_ma_cafe")
        out.append(_rect("ThamMa_Cafe", *at(0, 3.0), yaw, 900 * 0.95, 600 * 0.85))
    if "dock_yard" in marks:
        at, yaw = frame("dock_yard")
        out.append(_rect("TaLang_TicketBooth", *at(-2, 0), yaw + 90, 900 * 0.38, 600 * 0.4))
        out.append(_rect("TaLang_CafeDeck", *at(2, 9), yaw, 900, 650))
    if scatter:
        for colour, items in scatter.get("houses", {}).items():
            for it in items:
                out.append(_rect(f"House_{colour}", it[0], it[1], it[3], 900 * it[6], 600 * it[7]))
    return out


def inside_footprint(fps, x, y, margin_cm=0.0):
    """Name of the first footprint containing (x, y) (grown by margin_cm), or None."""
    for f in fps:
        if abs(x - f["cx"]) > f["hx"] + f["hy"] + margin_cm + 1.0 and abs(y - f["cy"]) > f["hx"] + f["hy"] + margin_cm + 1.0:
            continue
        r = math.radians(f["yaw"])
        dx, dy = x - f["cx"], y - f["cy"]
        lx = dx * math.cos(r) + dy * math.sin(r)
        ly = -dx * math.sin(r) + dy * math.cos(r)
        if abs(lx) <= f["hx"] + margin_cm and abs(ly) <= f["hy"] + margin_cm:
            return f["name"]
    return None


# ---------------------------------------------------------------------------
# Road (main route)
# ---------------------------------------------------------------------------

class Road:
    """Main route as UE sees it: Catmull-Rom spline through layout["road"]["points_cm"]."""

    SUB = 5   # samples per spline segment

    def __init__(self, layout, points_cm=None):
        road = layout["road"]
        pts = points_cm if points_cm is not None else road["points_cm"]
        self.half_width = float(road["half_width_cm"])
        self.secondary_from = float(road["concrete_from_m"]) * 100.0 if "concrete_from_m" in road else -1.0
        self.secondary_half_width = float(road.get("concrete_half_width_cm", self.half_width))
        self.xs, self.ys, self.zs, self.d = [], [], [], []
        n = len(pts)
        for i in range(n - 1):
            p0, p1 = pts[max(i - 1, 0)], pts[i]
            p2, p3 = pts[i + 1], pts[min(i + 2, n - 1)]
            # Uniform Catmull-Rom (UE auto tangents for Curve points; end tangents from the end segment).
            m1 = [(p2[k] - p0[k]) * (0.5 if i > 0 else 1.0) for k in range(3)]
            m2 = [(p3[k] - p1[k]) * (0.5 if i + 2 < n else 1.0) for k in range(3)]
            for s in range(self.SUB):
                t = s / self.SUB
                t2, t3 = t * t, t * t * t
                h00, h10, h01, h11 = 2 * t3 - 3 * t2 + 1, t3 - 2 * t2 + t, -2 * t3 + 3 * t2, t3 - t2
                q = [h00 * p1[k] + h10 * m1[k] + h01 * p2[k] + h11 * m2[k] for k in range(3)]
                self._add(q)
        self._add(pts[-1])
        self.length = self.d[-1]
        self.length_m = self.length / 100.0
        # Coarse grid for closest-point queries.
        self._cell = 5000.0
        self._grid = {}
        for i in range(len(self.xs)):
            self._grid.setdefault((int(self.xs[i] // self._cell), int(self.ys[i] // self._cell)), []).append(i)

    def _add(self, q):
        if self.xs:
            self.d.append(self.d[-1] + math.hypot(q[0] - self.xs[-1], q[1] - self.ys[-1]))
        else:
            self.d.append(0.0)
        self.xs.append(q[0])
        self.ys.append(q[1])
        self.zs.append(q[2])

    def _index(self, dist_cm):
        lo, hi = 0, len(self.d) - 1
        dist_cm = min(max(dist_cm, 0.0), self.length)
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if self.d[mid] <= dist_cm:
                lo = mid
            else:
                hi = mid
        return lo

    def frame(self, meters):
        """(x, y, z, fx, fy, rx, ry) at a distance in metres; right = Cross(Up, Fwd) like UE."""
        dcm = min(max(meters * 100.0, 0.0), self.length)
        i = min(self._index(dcm), len(self.d) - 2)
        seg = max(self.d[i + 1] - self.d[i], 1e-6)
        t = (dcm - self.d[i]) / seg
        x = self.xs[i] + (self.xs[i + 1] - self.xs[i]) * t
        y = self.ys[i] + (self.ys[i + 1] - self.ys[i]) * t
        z = self.zs[i] + (self.zs[i + 1] - self.zs[i]) * t
        fx, fy = (self.xs[i + 1] - self.xs[i]) / seg, (self.ys[i + 1] - self.ys[i]) / seg
        return x, y, z, fx, fy, -fy, fx

    def yaw_deg(self, meters):
        _, _, _, fx, fy, _, _ = self.frame(meters)
        return math.degrees(math.atan2(fy, fx))

    def closest(self, x, y, near_m=None, window_m=None):
        """(road metres, distance cm) of the closest road point; optionally only within near_m +- window_m."""
        if near_m is not None and window_m is not None:
            i0, i1 = self._index((near_m - window_m) * 100.0), self._index((near_m + window_m) * 100.0) + 1
            cand = range(i0, min(i1 + 1, len(self.xs)))
        else:
            gx, gy = int(x // self._cell), int(y // self._cell)
            cand = []
            for ring in range(0, 60):
                for ix in range(gx - ring, gx + ring + 1):
                    for iy in range(gy - ring, gy + ring + 1):
                        if max(abs(ix - gx), abs(iy - gy)) == ring:
                            cand.extend(self._grid.get((ix, iy), ()))
                if cand and ring >= 1:
                    break
        best, bi = float("inf"), 0
        for i in cand:
            dd = (self.xs[i] - x) ** 2 + (self.ys[i] - y) ** 2
            if dd < best:
                best, bi = dd, i
        # Refine on the two neighbouring segments.
        bd, bm = math.sqrt(best), self.d[bi]
        for j in (bi - 1, bi):
            if 0 <= j < len(self.xs) - 1:
                ax, ay, bx, by = self.xs[j], self.ys[j], self.xs[j + 1], self.ys[j + 1]
                vx, vy = bx - ax, by - ay
                L2 = vx * vx + vy * vy or 1e-6
                t = max(0.0, min(1.0, ((x - ax) * vx + (y - ay) * vy) / L2))
                px, py = ax + vx * t, ay + vy * t
                dd = math.hypot(x - px, y - py)
                if dd < bd:
                    bd, bm = dd, self.d[j] + (self.d[j + 1] - self.d[j]) * t
        return bm / 100.0, bd

    def half_width_at(self, meters):
        """Replicates AHGRoadSplineActor::GetWidthAtDistance / 2 (20 m blend to the concrete road)."""
        if self.secondary_from < 0:
            return self.half_width
        a = min(max((meters * 100.0 - self.secondary_from + 1000.0) / 2000.0, 0.0), 1.0)
        return self.half_width + (self.secondary_half_width - self.half_width) * a

    def edge_clearance(self, x, y):
        """Distance (cm) from (x, y) to the nearest road edge; negative = on the road."""
        m, dist = self.closest(x, y)
        return dist - self.half_width_at(m)

    def radius_at(self, meters, base_m=6.0):
        """Turn radius (cm) at a distance, from the heading change over +-base_m/2."""
        h = base_m * 50.0
        a = self.frame((meters * 100.0 - h) / 100.0)
        b = self.frame((meters * 100.0 + h) / 100.0)
        ang = abs((math.atan2(b[4], b[3]) - math.atan2(a[4], a[3]) + math.pi) % (2 * math.pi) - math.pi)
        return float("inf") if ang < 1e-6 else (base_m * 100.0) / ang

    def min_radius(self, m0, m1, step_m=2.0):
        r, m = float("inf"), m0
        while m <= m1:
            r = min(r, self.radius_at(m))
            m += step_m
        return r


class StubRoads:
    """Scenery roads (QL4C beyond the route): polylines only, for clearance checks."""

    def __init__(self, layout):
        self.lines = [s for s in layout["road"].get("stubs_cm", []) if len(s) >= 2]
        self.half_width = float(layout["road"]["half_width_cm"])

    def edge_clearance(self, x, y):
        best = float("inf")
        for line in self.lines:
            for a, b in zip(line, line[1:]):
                vx, vy = b[0] - a[0], b[1] - a[1]
                L2 = vx * vx + vy * vy or 1e-6
                t = max(0.0, min(1.0, ((x - a[0]) * vx + (y - a[1]) * vy) / L2))
                best = min(best, math.hypot(x - a[0] - vx * t, y - a[1] - vy * t))
        return best - self.half_width


# ---------------------------------------------------------------------------
# Placement context
# ---------------------------------------------------------------------------

class Ctx:
    def __init__(self, layout, scatter, r16_path):
        self.layout = layout
        self.scatter = scatter
        self.terrain = Terrain(layout, r16_path)
        self.road = Road(layout)
        self.stubs = StubRoads(layout)
        self.fps = footprints(layout, scatter)
        self.sec = layout["road"]["sections_m"]
        # Section ends are measured on gen_heightmap's 2 m resample; rescale to spline metres.
        self.sec_scale = self.road.length_m / float(layout["road"]["length_m"])

    def sec_m(self, key):
        return self.sec[key] * self.sec_scale

    def viewpoint_road_m(self, key):
        vp = self.layout["viewpoints"][key]
        p = vp.get("road_cm", vp["location_cm"])
        return self.road.closest(p[0], p[1])[0]

    def ground_ok(self, x, y, *, max_slope=MAX_GROUND_SLOPE_DEG, foot_cm=60.0, ref_z=None, max_dz=None,
                  road_clear_cm=None, fp_margin_cm=60.0):
        """None if (x, y) is a valid standing spot, else the reason."""
        t = self.terrain
        if not t.inside(x, y, 500.0):
            return "outside map"
        if is_wet(self.layout, t, x, y):
            return "in water"
        s = t.slope_deg(x, y, foot_cm)
        if s > max_slope:
            return f"slope {s:.0f} deg"
        if ref_z is not None and max_dz is not None and abs(t.z(x, y) - ref_z) > max_dz:
            return f"ground {(t.z(x, y) - ref_z) / 100:+.1f} m off"
        name = inside_footprint(self.fps, x, y, fp_margin_cm)
        if name:
            return f"inside {name}"
        if road_clear_cm is not None:
            c = min(self.road.edge_clearance(x, y), self.stubs.edge_clearance(x, y))
            if c < road_clear_cm:
                return f"{max(c, -999) / 100:.1f} m from road edge"
        return None


# ---------------------------------------------------------------------------
# Placements
# ---------------------------------------------------------------------------

def parked_bikes(ctx, road_m, count=5, pad=None):
    """Tour bikes parked on the shoulder near road_m, off the carriageway so convoys never brake for them.
    pad: optional (x, y, z) of a flat landmark pad by the road (Ma Pi Leng Panorama plaza) used when the
    shoulder is too steep. Returns [(x, y, z, yaw)]."""
    road = ctx.road
    cands = []
    for k in range(-16, 17):
        along = k * 2.4
        m = road_m + along
        if not (5.0 < m < road.length_m - 5.0):
            continue
        x0, y0, z0, fx, fy, rx, ry = road.frame(m)
        hw = road.half_width_at(m) / 100.0
        for side in (1, -1):
            for lat in (hw + 1.4, hw + 2.0, hw + 2.7, hw + 3.5):
                x, y = x0 + rx * side * lat * 100, y0 + ry * side * lat * 100
                why = ctx.ground_ok(x, y, max_slope=15.0, foot_cm=150.0, ref_z=z0, max_dz=140.0, road_clear_cm=100.0)
                if why is None:
                    # Angled 30 deg, nose towards the road (ready to pull out): at 70 deg nose-out [W] rode into the bank.
                    yaw = math.degrees(math.atan2(fy, fx)) + (-30.0 if side > 0 else 30.0)
                    cands.append((abs(along) + 0.3 * lat, x, y, yaw))
    cands.sort()
    if pad is not None:
        px, py, pz = pad
        for i in range(-10, 11):
            for j in range(-10, 11):
                x, y = px + i * 120.0, py + j * 120.0
                if ctx.ground_ok(x, y, max_slope=15.0, foot_cm=150.0, ref_z=pz, max_dz=100.0, road_clear_cm=100.0) is None:
                    rm = road.closest(x, y)[0]
                    rx_, ry_ = road.frame(rm)[:2]
                    yaw = math.degrees(math.atan2(ry_ - y, rx_ - x))      # nose to the road
                    cands.append((1000.0 + math.hypot(i, j), x, y, yaw))  # after the shoulder spots
    out = []
    for _, x, y, yaw in cands:
        if all(math.hypot(x - o[0], y - o[1]) > 230.0 for o in out):
            out.append((x, y, ctx.terrain.z(x, y) + 70.0, yaw))
        if len(out) == count:
            break
    return out


PARAPET_SEG = (400.0, 55.0)       # length, thickness (cm) of one Tham Ma parapet block
PARAPET_ROAD_CLEAR_CM = 80.0


def tham_ma_parapet(ctx):
    """Doc Tham Ma overlook parapet: 5 x 4 m stone blocks on the valley edge, 9 m out from the viewpoint towards
    the hairpins. Blocks that would stand on (or within 0.8 m of) the road are dropped - the old fixed row put one
    across the hairpin below the overlook, where it stopped every bike dead. Returns [(x, y, ground_z, yaw)] and the
    flower-bucket spots [(x, y, ground_z)] that are clear of the road too."""
    layout, scatter = ctx.layout, ctx.scatter
    vp = layout["viewpoints"]["doc_tham_ma"]["location_cm"]
    centre = scatter["viewpoints"]["doc_tham_ma"]["center_cm"]
    dx, dy = centre[0] - vp[0], centre[1] - vp[1]
    d = math.hypot(dx, dy) or 1.0
    ox, oy = dx / d, dy / d                       # outward (towards the hairpins)
    px, py = -oy, ox                              # along the parapet
    yaw = math.degrees(math.atan2(py, px))

    def clear(x, y, margin):
        return min(ctx.road.edge_clearance(x, y), ctx.stubs.edge_clearance(x, y)) >= margin

    blocks = []
    half = PARAPET_SEG[0] * 0.5
    for i in range(-2, 3):
        cx, cy = vp[0] + ox * 900 + px * i * 420, vp[1] + oy * 900 + py * i * 420
        ends = [(cx + px * s * half + ox * t, cy + py * s * half + oy * t)
                for s in (-1.0, 0.0, 1.0) for t in (-PARAPET_SEG[1] * 0.5, PARAPET_SEG[1] * 0.5)]
        if all(clear(x, y, PARAPET_ROAD_CLEAR_CM) for x, y in ends):
            blocks.append((cx, cy, ctx.terrain.z(cx, cy), yaw))
    buckets = []
    for along in (-700, -250, 450):
        bx, by = vp[0] + ox * 820 + px * along, vp[1] + oy * 820 + py * along
        if clear(bx, by, 60.0):
            buckets.append((bx, by, ctx.terrain.z(bx, by)))
    return blocks, buckets


def checkin_tourists(ctx, center, rng, count=9, r_min=250.0, r_max=900.0, max_dz=250.0, others=()):
    """Tourists on the viewpoint pad around center (x, y, z): random ring as before, but only flat, dry
    spots off the road and outside buildings. Returns [(x, y, z)]."""
    cx, cy, cz = center
    out = []
    tries = 0
    r_hi = r_max
    while len(out) < count and tries < 4000:
        tries += 1
        if tries % 600 == 0:
            r_hi *= 1.3   # small pad: widen the ring a little
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(r_min, r_hi)
        x, y = cx + math.cos(a) * r, cy + math.sin(a) * r
        if ctx.ground_ok(x, y, max_slope=26.0, foot_cm=60.0, ref_z=cz, max_dz=max_dz, road_clear_cm=80.0):
            continue
        if any(math.hypot(x - o[0], y - o[1]) < 120.0 for o in list(out) + list(others)):
            continue
        out.append((x, y, ctx.terrain.z(x, y) + 120.0))
    return out


def checkin_crowd(ctx, key, rng):
    """Parked bikes + tourists at a viewpoint (replaces the old road_frame offsets, which put the Doc Tham Ma
    bikes 570 m from the viewpoint and parked bikes 5 m off the road edge over the drop)."""
    vp = ctx.layout["viewpoints"][key]
    road_m = ctx.viewpoint_road_m(key)
    pad = None
    for mark in ctx.layout.get("landmarks", {}).values():      # e.g. the Panorama plaza 80 m along the rim
        lx, ly, lz = mark["location_cm"]
        lm, ld = ctx.road.closest(lx, ly)
        if abs(lm - road_m) < 150.0 and ld < 2500.0:
            pad = (lx, ly, lz)
    return {"road_m": road_m, "bikes": parked_bikes(ctx, road_m, pad=pad),
            "tourists": checkin_tourists(ctx, vp["location_cm"], rng)}


def cow_spots(ctx, at_m, count, rng):
    """Cows grazing on the verge around at_m (they wander onto the road by themselves).
    Returns [(x, y, z, yaw)]."""
    road = ctx.road
    out = []
    tries = 0
    while len(out) < count and tries < 3000:
        tries += 1
        m = at_m + rng.uniform(-25.0, 25.0)
        x0, y0, z0, fx, fy, rx, ry = road.frame(m)
        side = rng.choice((1, -1))
        lat = road.half_width_at(m) + rng.uniform(80.0, 450.0)
        x, y = x0 + rx * side * lat, y0 + ry * side * lat
        if ctx.ground_ok(x, y, max_slope=30.0, foot_cm=150.0, ref_z=z0, max_dz=250.0, road_clear_cm=40.0):
            continue
        if any(math.hypot(x - o[0], y - o[1]) < 300.0 for o in out):
            continue
        out.append((x, y, ctx.terrain.z(x, y) + 120.0, rng.uniform(0, 360)))
    return out


def pickup_near(ctx, center, rng, r_min=300.0, r_max=700.0, max_dz=150.0):
    cx, cy, cz = center
    for _ in range(800):
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(r_min, r_max)
        x, y = cx + math.cos(a) * r, cy + math.sin(a) * r
        if ctx.ground_ok(x, y, max_slope=22.0, foot_cm=80.0, ref_z=cz, max_dz=max_dz, road_clear_cm=50.0) is None:
            return (x, y, ctx.terrain.z(x, y) + 20.0)
    return (cx, cy, cz + 20.0)


def happy_water(ctx, rng, spacing_m=1500.0):
    """Roadside bottles about every spacing_m along the whole route + one at each viewpoint.
    Returns [(label, x, y, z)]."""
    road = ctx.road
    L = road.length_m
    n = max(2, int(round((L - 800.0) / spacing_m)) + 1)
    out = []
    for i in range(n):
        m = 400.0 + (L - 800.0) * i / (n - 1)
        x0, y0, z0, fx, fy, rx, ry = road.frame(m)
        side = 1 if i % 2 else -1
        lat = road.half_width_at(m) - 150.0          # on the asphalt, just inside the edge
        out.append((f"HappyWater_{i}", x0 + rx * side * lat, y0 + ry * side * lat, z0 + 20.0))
    for key, vp in ctx.layout["viewpoints"].items():
        x, y, z = pickup_near(ctx, vp["location_cm"], rng)
        out.append((f"HappyWater_{key}", x, y, z))
    return out


# ---------------------------------------------------------------------------
# River traffic (Ta Lang dock)
# ---------------------------------------------------------------------------

CANO_HALF = (640.0, 140.0)          # ANhoQueBoatPawn Cano collision half extents (cm)
KAYAK_HALF = (210.0, 40.0)


def rect_navigable(ctx, cx, cy, yaw, hx, hy, margin_cm, depth_cm):
    """Every point of an oriented rectangle (grown by margin) has water at least depth_cm deep."""
    r = math.radians(yaw)
    c, s = math.cos(r), math.sin(r)
    hx, hy = hx + margin_cm, hy + margin_cm
    nx, ny = max(2, int(hx // 200) + 1), max(1, int(hy // 200) + 1)
    for i in range(-nx, nx + 1):
        for j in range(-ny, ny + 1):
            lx, ly = hx * i / nx, hy * j / ny
            x, y = cx + lx * c - ly * s, cy + lx * s + ly * c
            if not navigable(ctx.layout, ctx.terrain, x, y, 0.0, depth_cm):
                return False
    return True


def dock_frame(layout):
    dock = layout["spawns"]["dock"]
    dx, dy, _ = dock["location_cm"]
    yaw = dock["yaw"]
    fx, fy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    return dx, dy, yaw, fx, fy, -fy, fx


def rect_distance(px, py, cx, cy, yaw, hx, hy):
    """Distance (cm) from a point to an oriented rectangle (0 inside)."""
    r = math.radians(yaw)
    dx, dy = px - cx, py - cy
    lx = abs(dx * math.cos(r) + dy * math.sin(r)) - hx
    ly = abs(-dx * math.sin(r) + dy * math.cos(r)) - hy
    return math.hypot(max(lx, 0.0), max(ly, 0.0))


PONTOON_HALF = (900.0, 450.0)        # 18 x 9 m deck (world.build_river_traffic)
PIER_LENGTH_CM = 2200.0              # world.build_dock
PIER_HALF_WIDTH_CM = 200.0
DOCK_BERTH_CM = 1700.0


def river_traffic(ctx, cruise_stride=6, hull_margin_cm=150.0, keep_clear_cm=300.0):
    """Pontoons, moored boats and cruise route for world.build_river_traffic.

    At Ta Lang the reservoir is only ~35 m wide (bank to bank ~10-45 m out from the dock pad). The old layout
    moored the canos bow-out 48 m from the dock - on the far bank - and the reservoir centreline the cruise boats
    follow runs through the pontoons. Now:
      * the three pontoons lie along the near bank either side of the pier, each placed where its whole deck
        floats (bank terrain has ridges running into the water here, see validate_layout.py);
      * moored canos lie alongside the pontoons' outer edge, each checked for depth under the whole hull plus a
        margin;
      * the cruise route is the longest navigable run of the centreline (2.8 m hull + margin each side, clear
        of the dam), with points near the dock pushed out until they keep keep_clear_cm from every pontoon,
        moored boat and the pier."""
    layout = ctx.layout
    dx, dy, yaw, fx, fy, sx, sy = dock_frame(layout)
    at = lambda out, side: (dx + fx * out + sx * side, dy + fy * out + sy * side)
    obstacles = [(*at(PIER_LENGTH_CM * 0.5, 0.0), yaw, PIER_LENGTH_CM * 0.5, PIER_HALF_WIDTH_CM)]

    def clear_of(cx, cy, gyaw, hx, hy, gap):
        corners = [(cx + a * hx * math.cos(math.radians(gyaw)) - b * hy * math.sin(math.radians(gyaw)),
                    cy + a * hx * math.sin(math.radians(gyaw)) + b * hy * math.cos(math.radians(gyaw)))
                   for a in (-1, 0, 1) for b in (-1, 0, 1)]
        return all(rect_distance(px, py, *o) >= gap for o in obstacles for px, py in corners) and \
            all(rect_distance(o[0], o[1], cx, cy, gyaw, hx, hy) >= gap for o in obstacles)

    # Pontoons along the near bank either side of the pier (long side along the bank), as close to the
    # shore as the depth allows, nearest the pier first. DOCK_BERTH_CM either side of the pier stays free for
    # the player's cano / kayak (layout spawns, ~8 m beside the slipway).
    pontoons = []
    p_slots = [(out, sgn * (PIER_HALF_WIDTH_CM + DOCK_BERTH_CM + PONTOON_HALF[0] + k * (2 * PONTOON_HALF[0] + 300.0)))
               for k in range(4) for sgn in (1, -1) for out in range(1450, 2100, 100)]
    for out, side in p_slots:
        if len(pontoons) == 3:
            break
        x, y = at(float(out), side)
        if any(abs(side - ps) < 2 * PONTOON_HALF[0] for _, _, _, ps in pontoons):
            continue
        if rect_navigable(ctx, x, y, yaw + 90, *PONTOON_HALF, 50.0, 30.0) and clear_of(x, y, yaw + 90, *PONTOON_HALF, 150.0):
            pontoons.append((x, y, yaw + 90, side))
            obstacles.append((x, y, yaw + 90, *PONTOON_HALF))
    pontoons = [p[:3] for p in pontoons]

    # Moored canos: two alongside the outer edge of each pontoon (parallel to the bank), whole hull afloat.
    moored = []
    for px, py, pyaw in pontoons:
        ox, oy = px - dx, py - dy
        p_out, p_side = ox * fx + oy * fy, ox * sx + oy * sy
        for along in (-450.0, 450.0):
            for gap in (40.0, 120.0, 200.0):
                x, y = at(p_out + PONTOON_HALF[1] + gap + CANO_HALF[1], p_side + along)
                if rect_navigable(ctx, x, y, yaw + 90, *CANO_HALF, 60.0, BOAT_MIN_DEPTH_CM) and \
                        all(rect_distance(x, y, *o) > CANO_HALF[1] + 20.0 for o in obstacles):
                    moored.append((x, y, yaw + 90))
                    break
    for x, y, myaw in moored:
        obstacles.append((x, y, myaw, *CANO_HALF))

    # Cruise route.
    water = layout["water"]
    pts = [(p[0], p[1]) for p in water["river_cm"][::cruise_stride]]
    dam = water.get("dam")
    radius = CANO_HALF[1] + hull_margin_cm

    def near_obstacle(p):
        return min(rect_distance(p[0], p[1], *o) for o in obstacles) < keep_clear_cm + radius

    shifted = []
    for p in pts:
        q = p
        if near_obstacle(p):
            for k in range(1, 40):
                cand = (p[0] + fx * k * 100.0, p[1] + fy * k * 100.0)
                if not near_obstacle(cand) and navigable(layout, ctx.terrain, cand[0], cand[1], radius):
                    q = cand
                    break
        shifted.append(q)
    pts = shifted

    def ok_pt(p):
        if dam and math.hypot(p[0] - dam["location_cm"][0], p[1] - dam["location_cm"][1]) < dam["width_cm"] * 0.5 + 8000.0:
            return False
        return navigable(layout, ctx.terrain, p[0], p[1], radius) and not near_obstacle(p)

    good = [ok_pt(p) for p in pts]
    best, cur = (0, -1), None
    for i in range(len(pts)):
        if not good[i]:
            cur = None
            continue
        if cur is None or not segment_navigable(layout, ctx.terrain, pts[i - 1], pts[i], radius):
            cur = i
        if i - cur > best[1] - best[0]:
            best = (cur, i)
    route = pts[best[0]:best[1] + 1]
    n = len(route)
    starts = [int(n * f) for f in (0.25, 0.5, 0.75)] if n >= 8 else []
    return {"pontoons": pontoons, "moored": moored, "obstacles": obstacles, "route": route, "route_range": best,
            "route_all": pts, "starts": starts, "cruise_radius_cm": radius}


def nudge_to_water(ctx, x, y, yaw, half, margin_cm=60.0, depth_cm=BOAT_MIN_DEPTH_CM, obstacles=()):
    """Move a spawned boat outward along the dock heading until its hull floats (and clears obstacles).
    Returns (x, y)."""
    _, _, _, fx, fy, _, _ = dock_frame(ctx.layout)
    for k in range(0, 60):
        px, py = x + fx * k * 200.0, y + fy * k * 200.0
        if rect_navigable(ctx, px, py, yaw, half[0], half[1], margin_cm, depth_cm) and \
                all(rect_distance(px, py, *o) > max(half) + 80.0 for o in obstacles):
            return px, py
    return x, y


def vehicle_spawns(ctx):
    """Parked bikes and dock boats from layout["spawns"], made valid for world.build_vehicles:
    bikes are dropped onto the terrain (the layout uses the road-profile z, up to 5 m above the landscape
    near the Ma Pi Leng spur), boats are pushed out from the slipway until the whole hull floats."""
    sp = ctx.layout["spawns"]
    bikes = []
    for b in sp["bikes"]:
        x, y, _ = b["location_cm"]
        bikes.append({"location_cm": (x, y, ctx.terrain.z(x, y) + 60.0), "yaw": b["yaw"]})
    boats = []
    yaw = sp["dock"]["yaw"] + 90
    obstacles = river_traffic(ctx)["obstacles"][1:]        # pontoons + moored boats (not the pier)
    for b in sp["boats"]:
        x, y, z = b["location_cm"]
        half = CANO_HALF if b["type"] == "Cano" else KAYAK_HALF
        nx, ny = nudge_to_water(ctx, x, y, yaw, half, obstacles=obstacles)
        boats.append({"type": b["type"], "location_cm": (nx, ny, z), "yaw": yaw})
    return {"bikes": bikes, "boats": boats}


def road_cusps(road, r_min_cm=600.0, step_m=1.0):
    """Road metres where the spline turns tighter than r_min_cm (the real road is >= ~8 m; tighter = a hairpin
    decimated to one or two vertices in points_cm, so the spline has a near-cusp
    that no vehicle - and no road mesh - can follow). Returns cluster centres."""
    out, m, run = [], 0.0, []
    while m <= road.length_m:
        if road.radius_at(m) < r_min_cm:
            run.append(m)
        elif run:
            out.append(0.5 * (run[0] + run[-1]))
            run = []
        m += step_m
    if run:
        out.append(0.5 * (run[0] + run[-1]))
    return out


# ---------------------------------------------------------------------------
# Traffic plan (AHGTrafficSpawner convoys / trucks)
# ---------------------------------------------------------------------------

def traffic_plan(ctx):
    """Convoys timed to reach the three viewpoints soon after BeginPlay, trucks confined to QL4C.

    Distances are spline metres. Convoys run the whole route (end-of-road = U-turn). Trucks ping-pong between
    min_m and max_m on QL4C: real trucks do not take the 4 m concrete Ta Lang road with its 8 m hairpins, so they
    turn around 300 m before it."""
    L = ctx.road.length_m
    tm = ctx.viewpoint_road_m("doc_tham_ma")
    mpl = ctx.viewpoint_road_m("ma_pi_leng")
    rim_end = ctx.sec_m("rim")                     # = start of the concrete Ta Lang road
    clamp = lambda m: max(20.0, min(L - 20.0, m))
    convoys = [
        # Doc Tham Ma: one convoy already in the bends below the overlook climbing away, one coming down
        # through all the hairpins towards the overlook (then U-turns at the road start and climbs again).
        {"name": "tham_ma_up", "start_m": clamp(ctx.sec_m("tham_ma") - 130.0), "dir": 1, "bikes": 8, "kmh": 36.0},
        {"name": "tham_ma_down", "start_m": clamp(ctx.sec_m("tham_ma") + 260.0), "dir": -1, "bikes": 8, "kmh": 38.0},
        # Ma Pi Leng Panorama: one arriving from the link road, one from the north along the rim.
        {"name": "mpl_up", "start_m": clamp(mpl - 380.0), "dir": 1, "bikes": 7, "kmh": 40.0},
        {"name": "mpl_down", "start_m": clamp(mpl + 900.0), "dir": -1, "bikes": 9, "kmh": 42.0},
        # Mid rim, towards the Youth Volunteers monument.
        {"name": "rim_mid", "start_m": clamp(0.5 * (mpl + rim_end)), "dir": 1, "bikes": 5, "kmh": 46.0},
        # Ta Lang descent: one coming down the last hairpins to the dock, one climbing away from it.
        {"name": "talang_down", "start_m": clamp(L - 900.0), "dir": 1, "bikes": 6, "kmh": 28.0},
        {"name": "talang_up", "start_m": clamp(L - 120.0), "dir": -1, "bikes": 4, "kmh": 26.0},
    ]
    # South end 400 m past the Panorama: the generated link road has decimated hairpins a 7.6 m truck leaves the
    # road on (validate_layout replay), and the U-turn should not happen in front of the check-in crowd.
    truck_min = clamp(mpl + 400.0)
    truck_max = clamp(rim_end - 300.0)
    trucks = [
        {"name": "truck_rim_south", "start_m": clamp(mpl + 2400.0), "dir": -1, "kmh": 36.0, "min_m": truck_min, "max_m": truck_max},
        {"name": "truck_rim_north", "start_m": clamp(rim_end - 2500.0), "dir": 1, "kmh": 34.0, "min_m": truck_min, "max_m": truck_max},
    ]
    return {"convoys": convoys, "trucks": trucks, "bike_spacing_m": 11.0}


def living_world_cows(ctx):
    """(at_m, count) herds: link road, canyon rim past the Panorama, a Ta Lang hamlet bend."""
    mpl = ctx.viewpoint_road_m("ma_pi_leng")
    link_mid = 0.5 * (ctx.sec_m("tham_ma") + ctx.sec_m("link"))
    descent_mid = 0.5 * (ctx.sec_m("rim") + ctx.sec_m("descent"))
    return [(link_mid, 5), (mpl + 450.0, 4), (descent_mid, 3)]


_CTX_CACHE = {}


def ctx_for(layout, scatter=None, r16_path=None):
    """Ctx for a layout dict from editor_utils.ensure_generated_heightmap() (cached: the heightmap is 32 MB)."""
    path = r16_path or layout["_r16_abs"]
    key = (path, id(layout), id(scatter))
    if key not in _CTX_CACHE:
        _CTX_CACHE.clear()
        _CTX_CACHE[key] = Ctx(layout, scatter, path)
    return _CTX_CACHE[key]


def rng(seed=404):
    return random.Random(seed)
