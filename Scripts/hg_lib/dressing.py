"""Round 3B Ha Giang set dressing (agent B): placement for AHGDressingProp (C++ mesh-kit props).

* H'Mong houses - stone / rammed-earth walls, yin-yang clay-tile roofs, corn drying under the eaves, dry-stone
  yard walls - replace the grey-roofed hamlet houses of gen_scatter (green / blue tin roofs stay: modern houses).
* Terraced corn plots around each hamlet: young corn on the contour terraces (late-March sowing; ART_BIBLE's
  locked season). No buckwheat flowers: they bloom October-November, not in the art bible's late March.
* Ma Pi Leng: a tall flag pole with the national flag and a carved limestone name stone at the plaza.
* Wooden name boards at Doc Tham Ma and at the Ta Lang dock (Tu San gorge).

Everything keeps clear of the road (layout road points) and is spawned on the generated terrain heights.
"""
from __future__ import annotations

import math

import unreal

from . import editor_utils as hg

HOUSE_ROAD_CLEAR_CM = 1100.0     # house centre to road centre line (house 10 x 6 m + plinth)
FENCE_ROAD_CLEAR_CM = 2200.0     # yard wall reaches ~11 m in front of the house
PROP_ROAD_CLEAR_CM = 650.0
HOUSE_HALF_CM = (570.0, 380.0)   # walls 10 x 6.2 m + 0.7 m eaves (HGDressingProp BuildHouse)
HOUSE_MAX_RELIEF_CM = 250.0      # more than this across the footprint: too steep for a house (round 5, agent A)


def _house_ground(terrain, x, y, yaw):
    """(lowest, highest) terrain z under the house footprint incl. eaves (3 x 3 samples)."""
    a = math.radians(yaw)
    ca, sa = math.cos(a), math.sin(a)
    zs = [terrain.z(x + ca * HOUSE_HALF_CM[0] * u - sa * HOUSE_HALF_CM[1] * v,
                    y + sa * HOUSE_HALF_CM[0] * u + ca * HOUSE_HALF_CM[1] * v)
          for u in (-1, 0, 1) for v in (-1, 0, 1)]
    return min(zs), max(zs)


def _road_points(layout):
    return [(p[0], p[1]) for p in layout["road"]["points_cm"]]


def _road_dist(pts, x, y):
    return min(math.hypot(px - x, py - y) for px, py in pts)


def remaining_grey_houses(layout, items):
    """Grey-roofed scatter houses NOT replaced by kit H'Mong houses (too close to the road for the kit footprint)."""
    if not hasattr(unreal, "HGDressingProp"):
        return items
    pts = _road_points(layout)
    return [it for it in items if _road_dist(pts, it[0], it[1]) < HOUSE_ROAD_CLEAR_CM]


def _sign_material(key):
    path = f"{hg.GENERATED_ROOT}/Materials/M_HG_Sign_{key}"
    return unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None


def _spawn(cls_kind, label, loc, yaw, tag, folder, variant=0, text=None, radius=None, sign=None):
    a = hg.spawn_actor(unreal.HGDressingProp, label, loc, (0, yaw, 0), tag, folder)
    if sign and _sign_material(sign):
        a.set_editor_property("sign_material", _sign_material(sign))
    a.set_editor_property("kind", cls_kind)
    a.set_editor_property("variant", int(variant))
    if text:
        a.set_editor_property("sign_text", unreal.Text(text))
    if radius:
        a.set_editor_property("radius", float(radius))
    return a


def build(layout, scatter, tag, folder):
    if not hasattr(unreal, "HGDressingProp"):
        hg.warn("HGDressingProp not compiled: no Round 3B set dressing")
        return
    K = unreal.HGDressingKind
    pts = _road_points(layout)
    try:
        from . import placements
        terrain = placements.ctx_for(layout).terrain
    except Exception as exc:  # pragma: no cover - heightmap missing next to the layout
        hg.warn(f"house ground snap skipped: {exc}")
        terrain = None
    skipped = 0

    # --- H'Mong houses on the grey-roofed scatter spots ---
    houses = []
    for i, it in enumerate(scatter.get("houses", {}).get("grey", [])):
        x, y, z, yaw = it[0], it[1], it[2], it[3]
        d = _road_dist(pts, x, y)
        if d < HOUSE_ROAD_CLEAR_CM:
            continue
        if terrain is not None:
            # Round 5 (agent A, review "brown block over the spawn camera"): the house stood at the scatter point's
            # height, so on a slope its downhill side hung in the air and from the road below it read as a floating
            # block. Seat it on the lowest ground under the footprint (the uphill side is dug in, as on the real
            # hillsides) and skip spots too steep for a house.
            # Long side along the contour, as hill houses are built: least relief over 12 headings.
            best = None
            for k in range(12):
                cand = yaw + 15.0 * k
                lo_c, hi_c = _house_ground(terrain, x, y, cand)
                if best is None or (hi_c - lo_c) < best[0] - 20.0:
                    best = (hi_c - lo_c, cand, lo_c, hi_c)
            _, yaw, lo, hi = best
            if hi - lo > HOUSE_MAX_RELIEF_CM:
                skipped += 1
                continue
            z = lo
        variant = i % 6
        if d < FENCE_ROAD_CLEAR_CM and variant % 3 != 2:
            variant = 2 if variant < 3 else 5            # no yard wall this close to the road
        # The front (door, yard) faces the road side where possible.
        _spawn(K.HMONG_HOUSE, f"HmongHouse_{i:02d}", (x, y, z), yaw, tag, folder, variant)
        houses.append((x, y, z))

    # --- Corn terraces: one plot per hamlet cluster (houses within 250 m) ---
    clusters = []
    for h in houses:
        for c in clusters:
            if math.hypot(c[0][0] - h[0], c[0][1] - h[1]) < 25000.0:
                c.append(h)
                break
        else:
            clusters.append([h])
    for i, c in enumerate(clusters):
        cx = sum(p[0] for p in c) / len(c)
        cy = sum(p[1] for p in c) / len(c)
        cz = sum(p[2] for p in c) / len(c)
        corn = _spawn(K.CORN_FIELD, f"CornTerraces_{i:02d}", (cx, cy, cz), 0.0, tag, folder,
                      radius=3000.0 + 500.0 * min(len(c), 5))   # 30-55 m around the hamlet
        corn.set_editor_property("min_z", float(layout["water"]["surface_z_cm"]) + 1500.0)   # off the river banks

    # --- Viewpoint props ---
    marks = layout.get("landmarks", {})

    def frame(key):
        m = marks[key]
        x, y, z = m["location_cm"]
        yaw = m["yaw"]
        tx, ty = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        ux, uy = m["up"]
        face_road = math.degrees(math.atan2(ux, -uy))      # local +Y (sign face) towards the road side
        return (lambda along, up, dz=0.0: (x + tx * along * 100 + ux * up * 100, y + ty * along * 100 + uy * up * 100,
                                           z + dz * 100)), face_road

    def clear(loc, min_cm=PROP_ROAD_CLEAR_CM):
        return _road_dist(pts, loc[0], loc[1]) >= min_cm

    if "ma_pi_leng_plaza" in marks:
        at, face = frame("ma_pi_leng_plaza")
        for along, up in ((10.0, 7.0), (12.0, 9.0), (8.0, 10.0)):
            loc = at(along, up, -0.1)
            if clear(loc, 900.0):
                _spawn(K.FLAG_POLE, "MPL_FlagPole", loc, face, tag, folder)
                break
        for along, up in ((-6.0, -4.0), (-6.0, -2.0), (-8.0, 0.0)):
            loc = at(along, up, -0.3)
            if clear(loc):
                _spawn(K.STONE_SIGN, "MPL_NameStone_Carved", loc, face, tag, folder, text="ĐÈO MÃ PÍ LÈNG", sign="MaPiLeng")
                break
    if "tham_ma_cafe" in marks:
        at, face = frame("tham_ma_cafe")
        for along, up in ((8.0, -2.5), (9.0, 0.0), (-8.0, 0.0)):
            loc = at(along, up)
            if clear(loc):
                _spawn(K.WOOD_SIGN, "ThamMa_NameBoard", loc, face, tag, folder, text="DỐC THẨM MÃ", sign="ThamMa")
                break
    if "dock_yard" in marks:
        at, face = frame("dock_yard")
        for along, up in ((-7.0, 3.0), (7.0, 3.0), (-7.0, 6.0)):
            loc = at(along, up)
            if clear(loc):
                _spawn(K.WOOD_SIGN, "TaLang_DockBoard", loc, face, tag, folder, text="SÔNG NHO QUẾ · TU SẢN", sign="TuSan")
                break
    hg.log(f"Set dressing: {len(houses)} H'Mong houses ({skipped} slope spots skipped), {len(clusters)} corn terraces, Ma Pi Leng flag + name stone, 2 boards")
