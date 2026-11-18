"""Review camera shots shared by the offline renderer (Scripts/Tools/render_viewpoints.py) and the
in-editor screenshot runner (Scripts/Tools/ci_screenshots.py), so both frame exactly the same views.

Pure Python (no numpy / unreal): positions in metres, computed from HaGiang_layout.json and
HaGiang_scatter.json. Each shot mirrors one of the reference photos listed in docs/ART_BIBLE.md.
"""
import math


def _m(v):
    return [float(c) / 100.0 for c in v]


def _norm2(x, y):
    d = math.hypot(x, y) or 1.0
    return x / d, y / d


def _nearest(points, x, y):
    return min(range(len(points)), key=lambda i: (points[i][0] - x) ** 2 + (points[i][1] - y) ** 2)


def shots(layout, scatter):
    """[(name, cam_xyz_m, look_xyz_m, fov_deg)]"""
    out = []
    water_z = layout["water"]["surface_z_cm"] / 100.0
    river = [_m(p) for p in layout["water"]["river_cm"]]
    road = [_m(p) for p in layout["road"]["points_cm"]]
    sec = layout["road"]["sections_m"]
    marks = {k: v for k, v in layout.get("landmarks", {}).items()}

    def road_at(s_m):
        acc = 0.0
        for i in range(1, len(road)):
            seg = math.dist(road[i - 1][:2], road[i][:2])
            if acc + seg >= s_m:
                t = (s_m - acc) / (seg or 1.0)
                return [road[i - 1][k] + (road[i][k] - road[i - 1][k]) * t for k in range(3)]
            acc += seg
        return road[-1]

    # --- VP1 Doc Tham Ma ------------------------------------------------------------------------
    # Reference: from the parapet at the top, looking down the cluster of hairpins into the valley.
    p1 = _m(layout["viewpoints"]["doc_tham_ma"]["location_cm"])
    c1 = _m(scatter["viewpoints"]["doc_tham_ma"]["center_cm"])
    # The bends: the road between the end of the approach and the viewpoint.
    vi = _nearest(road, p1[0], p1[1])
    i0 = _nearest(road, *road_at(sec["approach"])[:2])
    bends = road[i0:vi + 1] or road[max(vi - 30, 0):vi + 1]
    bc = [sum(q[k_] for q in bends) / len(bends) for k_ in range(3)]
    dx, dy = _norm2(bc[0] - p1[0], bc[1] - p1[1])
    # Standing on the outer edge of the top hairpin, looking down the bends (the classic photo).
    top_ = road[vi]
    out.append(("VP1_ThamMa_eye", [p1[0] - dx * 6.0, p1[1] - dy * 6.0, p1[2] + 7.0],
                [bc[0] + dx * 60.0, bc[1] + dy * 60.0, bc[2] - 45.0], 72))
    # Reference: drone above the valley looking up at the head-wall bends (Google Maps / drone photo).
    # Near top-down over the bends, like the satellite view (camera on the viewpoint side).
    out.append(("VP1_ThamMa_drone", [bc[0] - dx * 120.0, bc[1] - dy * 120.0, max(p1[2], bc[2]) + 280.0],
                [bc[0] + dx * 20.0, bc[1] + dy * 20.0, bc[2]], 55))

    # --- VP2 Ma Pi Leng / Mom Da Tu Than --------------------------------------------------------
    p2 = _m(layout["viewpoints"]["ma_pi_leng"]["location_cm"])
    r2 = _m(layout["viewpoints"]["ma_pi_leng"]["road_cm"])
    j = _nearest(river, p2[0], p2[1])
    down = river[max(j - 40, 0)]
    sx, sy = _norm2(p2[0] - r2[0], p2[1] - r2[1])
    # Reference: standing on the ledge, river like a thread far below.
    out.append(("VP2_MomDa_ledge", [p2[0] + sx * 3.0, p2[1] + sy * 3.0, p2[2] + 1.7], [down[0], down[1], water_z + 40.0], 75))
    # Reference: the classic Ma Pi Leng road photo - road cut into the cliff, gorge on the right.
    if "ma_pi_leng_plaza" in marks:
        pl = _m(marks["ma_pi_leng_plaza"]["location_cm"])
        rv = river[_nearest(river, pl[0], pl[1])]
        # From the Panorama cafe's roof terrace (~2 storeys), over the knoll to the gorge.
        tx_, ty_ = _norm2(rv[0] - pl[0], rv[1] - pl[1])
        out.append(("VP2_MaPiLeng_plaza", [pl[0] + tx_ * 16.0, pl[1] + ty_ * 16.0, pl[2] + 6.0], [rv[0], rv[1], water_z + 120.0], 70))
    # Drone over the gorge looking back at the ledge and the rim road.
    ox, oy = _norm2(river[j][0] - p2[0], river[j][1] - p2[1])          # from the ledge out over the gorge
    out.append(("VP2_MaPiLeng_drone", [river[j][0] + ox * 220.0, river[j][1] + oy * 220.0, p2[2] + 40.0],
                [(p2[0] + river[j][0]) / 2, (p2[1] + river[j][1]) / 2, (p2[2] + water_z) / 2], 60))

    # --- VP3 Tu San / Ta Lang / Nho Que ----------------------------------------------------------
    dock = _m(layout["spawns"]["dock"]["location_cm"])
    k = _nearest(river, dock[0], dock[1])
    # Reference: Nho Que from the rim above the Ta Lang road, the river bending through the canyon.
    # Mid-descent, like the "Dinh nhin toan canh xuong song" stop: the river below, the gorge beyond.
    top = road_at(sec["rim"] + 0.62 * (sec["descent"] - sec["rim"]))
    rk = river[min(k + 40, len(river) - 1)]
    out.append(("VP3_NhoQue_from_rim", [top[0], top[1], top[2] + 8.0], [rk[0], rk[1], water_z + 40.0], 62))
    # Reference: Ta Lang dock - pontoons, covered boats, the road zig-zagging down behind.
    if "dock_yard" in marks:
        yd = _m(marks["dock_yard"]["location_cm"])
        tx, ty = _norm2(river[k][0] - yd[0], river[k][1] - yd[1])
        out.append(("VP3_TaLang_dock", [yd[0], yd[1], yd[2] + 3.5], [river[k][0] + tx * 200.0, river[k][1] + ty * 200.0, water_z + 60.0], 72))
    # Reference: boat in the Tu San narrows (between the Ta Lang dock and the Ma Pi Leng bend).
    g = k + (len(river) - 1 - k) // 3
    ahead = river[max(g - 50, 0)]
    out.append(("VP3_TuSan_boat", [river[g][0], river[g][1], water_z + 2.5], [ahead[0], ahead[1], water_z + 70.0], 70))
    fx, fy = _norm2(river[g][0] - ahead[0], river[g][1] - ahead[1])
    out.append(("VP3_TuSan_flycam", [river[g][0] + fx * 200.0 - fy * 120.0, river[g][1] + fy * 200.0 + fx * 120.0, water_z + 520.0],
                [river[max(g - 30, 0)][0], river[max(g - 30, 0)][1], water_z], 55))
    return out


def write_review_cameras(gen_dir):
    """Generated/Heightmaps/HaGiang_review_cameras.json: the review shots in cm for the game's -hgperf mode."""
    import json
    import os
    with open(os.path.join(gen_dir, "HaGiang_layout.json"), encoding="utf-8") as f:
        layout = json.load(f)
    with open(os.path.join(gen_dir, "HaGiang_scatter.json"), encoding="utf-8") as f:
        scatter = json.load(f)
    cams = [{"name": n, "cam_cm": [c * 100.0 for c in cam], "look_cm": [c * 100.0 for c in look], "fov": fov}
            for n, cam, look, fov in shots(layout, scatter)]
    path = os.path.join(gen_dir, "HaGiang_review_cameras.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"cameras": cams}, f, indent=1)
    return path, len(cams)


if __name__ == "__main__":
    import sys
    print(*write_review_cameras(sys.argv[1] if len(sys.argv) > 1 else "Generated/Heightmaps"))
