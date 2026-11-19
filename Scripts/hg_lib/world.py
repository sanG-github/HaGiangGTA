"""World-building steps shared by the checkpoint scripts (CP02+).

Each checkpoint script composes these with its own tag / materials so that
checking out any checkpoint and running its script rebuilds that exact scene.
"""
from __future__ import annotations

import json
import math
import os

import unreal

from . import editor_utils as hg
from . import dressing, placements

ROAD_MESH_OFFSET_CM = -11.0   # landscape is carved 15 cm under the road profile; asphalt top 4 cm above it


def require_cpp(*names):
    missing = [n for n in names if not hasattr(unreal, n)]
    if missing:
        raise RuntimeError(f"C++ classes not loaded: {missing}. Compile HaGiangGTA + HaGiangGTAEditor first.")


def rerun(actor):
    """set_editor_property already re-runs construction in the editor; call it explicitly when exposed."""
    fn = getattr(actor, "rerun_construction_scripts", None)
    if callable(fn):
        fn()


def find_tagged(tag, cls):
    t = unreal.Name(tag)
    return [a for a in hg.actor_subsystem().get_all_level_actors() if isinstance(a, cls) and t in a.tags]


# ---------------------------------------------------------------------------
# Terrain, road, water, dock, vehicles  (CP02 base world)
# ---------------------------------------------------------------------------

def build_landscape(layout, tag, folder, material=None):
    hm = layout["heightmap"]
    landscape = unreal.HGLandscapeBuilder.import_landscape_from_raw16(
        layout["_r16_abs"], hm["size"], hg.vec(hm["location_cm"]), hg.vec(hm["scale"]),
        hm["section_size_quads"], hm["sections_per_component"], material, tag)
    if landscape is None:
        raise RuntimeError("Landscape import failed - see Output Log (LogHGLandscapeBuilder).")
    landscape.set_folder_path(folder)
    return landscape


def build_water(layout, material, tag, folder):
    """Nho Que 1 reservoir (420 m) up to the dam, and the lower river below it."""
    water = layout["water"]
    if water.get("bodies"):
        # Real map: rotated planes following the reservoir + one below the dam (gen_heightmap.py).
        actors = []
        half = layout["heightmap"].get("half_m", 2016.0) * 100.0 - 2000.0
        for b in water["bodies"]:
            cx, cy, (sx, sy) = b["center_cm"][0], b["center_cm"][1], b["size_cm"]
            # Keep every plane inside the landscape: the below-dam plane ran 590 m past the map edge, a flat lake
            # floating beyond the end of the terrain in the rim views. Trim the end that sticks out.
            yr = math.radians(b["yaw"])
            ux, uy, vx, vy = math.cos(yr), math.sin(yr), -math.sin(yr), math.cos(yr)
            for _ in range(400):
                ends = [max(max(abs(cx + a * sx / 2 * ux + c * sy / 2 * vx), abs(cy + a * sx / 2 * uy + c * sy / 2 * vy)) for c in (-1, 1)) for a in (-1, 1)]
                if max(ends) <= half or sx < 4000.0:
                    break
                a = -1 if ends[0] > ends[1] else 1
                if ends[0] > half and ends[1] > half:
                    sy = max(sy - 2000.0, 4000.0)   # too wide for the map at both ends: narrow it
                    continue
                sx -= 2000.0
                cx -= a * ux * 1000.0
                cy -= a * uy * 1000.0
            actor = hg.spawn_actor(unreal.HGWaterBody, b["name"], (cx, cy, b["z_cm"]), (0, b["yaw"], 0), tag, folder)
            actor.set_editor_property("water_material", material)
            actor.set_editor_property("size_cm", unreal.Vector2D(sx, sy))
            rerun(actor)
            actors.append(actor)
        return actors[0]
    half = layout["heightmap"].get("half_m", 2016.0) * 100.0
    dam_y = water.get("dam_y_cm", half)
    bodies = [("Water_NhoQue_Reservoir", water["surface_z_cm"], -half, dam_y)]
    if dam_y < half:
        bodies.append(("Water_NhoQue_BelowDam", water.get("downstream_surface_z_cm", water["surface_z_cm"]), dam_y, half))
    actors = []
    for name, z, y0, y1 in bodies:
        actor = hg.spawn_actor(unreal.HGWaterBody, name, (0, (y0 + y1) * 0.5, z), (0, 0, 0), tag, folder)
        actor.set_editor_property("water_material", material)
        actor.set_editor_property("size_cm", unreal.Vector2D(2 * half, y1 - y0))
        rerun(actor)
        actors.append(actor)
    return actors[0]


def build_dam(layout, mats, tag, folder):
    """Nho Que 1 hydropower dam: concrete gravity wall with spillway piers and a powerhouse."""
    dam = layout["water"].get("dam")
    if not dam:
        return
    x, y, z0 = dam["location_cm"]
    crest = dam["crest_z_cm"]
    yaw = dam["yaw"] + 90.0                         # wall across the river
    concrete = mats.get("pole")
    h = crest - z0
    hg.box("Dam_NhoQue1_Wall", (x, y, z0 + h * 0.5), (dam["width_cm"], 1600, h), yaw=yaw, material=concrete,
           tag=tag, folder=folder)
    r = math.radians(yaw)
    for i in range(5):
        off = (i - 2) * 1700.0
        hg.box(f"Dam_Pier_{i}", (x + math.cos(r) * off, y + math.sin(r) * off, crest + 350), (250, 1700, 700),
               yaw=yaw, material=concrete, tag=tag, folder=folder)
    hg.box("Dam_Bridge", (x, y, crest + 760), (dam["width_cm"], 700, 60), yaw=yaw, material=concrete, tag=tag,
           folder=folder)
    side = dam["width_cm"] * 0.5 + 1500.0
    hg.box("Dam_Powerhouse", (x + math.cos(r) * side, y + math.sin(r) * side, z0 + 900), (2200, 1600, 1800), yaw=yaw,
           material=mats.get("house_wall"), tag=tag, folder=folder)


def build_road(layout, material, tag, folder, strip_mesh=None, concrete=None):
    """strip_mesh: SM_HG_RoadStrip (top at z=0, lane-paint UVs). Without it the engine cube is used.
    concrete: material for the narrow Ta Lang concrete road (from layout road.concrete_from_m on)."""
    road = layout["road"]
    actor = hg.spawn_actor(unreal.HGRoadSplineActor, "Road_MaPiLeng", (0, 0, 0), (0, 0, 0), tag, folder)
    actor.set_editor_property("road_material", material)
    actor.set_editor_property("road_width", road["half_width_cm"] * 2.0)
    if concrete is not None and "concrete_from_m" in road:
        actor.set_editor_property("secondary_material", concrete)
        actor.set_editor_property("secondary_width", road["concrete_half_width_cm"] * 2.0)
        # Measure where the concrete starts on the UE spline itself (its length differs slightly from
        # the generator's polyline length).
        acc, prev, start = 0.0, None, road["points_cm"][-1]
        for p in road["points_cm"]:
            if prev is not None:
                acc += math.dist(prev[:2], p[:2]) / 100.0
            if acc >= road["concrete_from_m"]:
                start = p
                break
            prev = p
        concrete_flag = start
    offset = ROAD_MESH_OFFSET_CM
    if strip_mesh is not None:
        actor.set_editor_property("road_mesh", strip_mesh)
        actor.set_editor_property("road_thickness", 100.0)   # Z scale 1; the actor lowers by half -> add it back
        offset += 50.0
    pts = [unreal.Vector(p[0], p[1], p[2] + offset) for p in road["points_cm"]]
    actor.set_road_points(pts)
    if concrete is not None and "concrete_from_m" in road:
        d = actor.get_distance_along_road(unreal.Vector(*concrete_flag))
        actor.set_editor_property("secondary_from_distance", d)
        hg.log(f"Concrete Ta Lang road from {d / 100.0:.0f} m on the spline (layout {road['concrete_from_m']:.0f} m)")
    actor.rebuild_road_meshes()
    hg.log(f"Road: {len(pts)} points, {road['length_m'] / 1000:.2f} km")
    # QL4C continuing beyond the route to the map edge (scenery; AI/minimap use the longest road).
    for k, stub in enumerate(road.get("stubs_cm", [])):
        if len(stub) < 3:
            continue
        sa = hg.spawn_actor(unreal.HGRoadSplineActor, f"Road_QL4C_Stub_{k}", (0, 0, 0), (0, 0, 0), tag, folder)
        sa.set_editor_property("road_material", material)
        sa.set_editor_property("road_width", road["half_width_cm"] * 2.0)
        if strip_mesh is not None:
            sa.set_editor_property("road_mesh", strip_mesh)
            sa.set_editor_property("road_thickness", 100.0)
        sa.set_road_points([unreal.Vector(p[0], p[1], p[2] + offset) for p in stub])
        sa.rebuild_road_meshes()
    return actor


def build_dock(layout, mat_wood, tag, folder):
    """Ta Lang wooden pier: from the dock pad out over the water."""
    dock = layout["spawns"]["dock"]
    dx, dy, _ = dock["location_cm"]
    yaw = dock["yaw"]
    fx, fy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    length = 2200.0
    cx, cy = dx + fx * length * 0.5, dy + fy * length * 0.5
    deck_top = layout["water"]["surface_z_cm"] + 90.0
    hg.box("Dock_TaLang_Deck", (cx, cy, deck_top - 10), (length, 400, 20), yaw=yaw,
           material=mat_wood, tag=tag, folder=folder)
    for i in range(6):
        t = (i + 0.5) / 6.0
        px, py = dx + fx * length * t, dy + fy * length * t
        for side in (-1, 1):
            ox, oy = -fy * 180 * side, fx * 180 * side
            hg.spawn_mesh(f"Dock_Pile_{i}_{'L' if side < 0 else 'R'}", hg.CYLINDER,
                          (px + ox, py + oy, deck_top - 260), scale=(0.3, 0.3, 5.0),
                          material=mat_wood, tag=tag, folder=folder)


def build_checkin_rings(layout, mat_ring, tag, folder):
    # A painted dashed ring on the ground (round 1 was a solid 4 m yellow disc that read as a plastic plate
    # lying in every check-in photo).
    for key, vp in layout["viewpoints"].items():
        x, y, z = vp["location_cm"]
        dashes = 14
        for i in range(dashes):
            a = 2.0 * math.pi * i / dashes
            hg.box(f"CheckIn_{key}_{i}", (x + math.cos(a) * 170.0, y + math.sin(a) * 170.0, z + 1.5), (45.0, 11.0, 2.0),
                   yaw=math.degrees(a) + 90.0, material=mat_ring, tag=tag, folder=folder, collision=False)


def build_vehicles(layout, tag, folder):
    """Parked Honda Wins + the dock cano / kayak. Positions go through placements.vehicle_spawns: bikes are
    dropped onto the landscape, boats pushed off the slipway until the whole hull floats (validate_layout.py)."""
    spawns = placements.vehicle_spawns(placements.ctx_for(layout))
    colors = [unreal.LinearColor(0.55, 0.02, 0.02, 1), unreal.LinearColor(0.05, 0.08, 0.35, 1),
              unreal.LinearColor(0.08, 0.08, 0.08, 1)]
    for i, bike in enumerate(spawns["bikes"]):
        actor = hg.spawn_actor(unreal.HGBikePawn, f"HondaWin_{i:02d}", bike["location_cm"], (0, bike["yaw"], 0),
                               tag, folder)
        actor.set_editor_property("body_color", colors[i % len(colors)])
        # Parked bikes wait empty: the xe om job puts a backpacker on (traffic bikes keep theirs).
        actor.set_editor_property("has_passenger", False)
        # The player's rental at the start: the classic plain Win (the automatic pick could turn a bike with a
        # passenger into a luggage-laden Easy Rider tour bike that hides the rider in the chase view).
        actor.set_editor_property("bike_model", unreal.HGBikeModel.WIN)
        actor.set_editor_property("bike_seed", i * 7 + 1)
        actor.set_editor_property("model_chosen", True)
        actor.set_editor_property("paint_chosen", True)
    for i, boat in enumerate(spawns["boats"]):
        x, y, z = boat["location_cm"]
        actor = hg.spawn_actor(unreal.NhoQueBoatPawn, f"Boat_{boat['type']}_{i:02d}", (x, y, z),
                               (0, boat["yaw"], 0), tag, folder)
        boat_type = unreal.HGBoatType.CANO if boat["type"] == "Cano" else unreal.HGBoatType.KAYAK
        actor.set_editor_property("boat_type", boat_type)
        rerun(actor)


def build_player_start(layout, tag, folder):
    ps = layout["spawns"]["player_start"]
    hg.spawn_actor(unreal.PlayerStart, "PlayerStart", ps["location_cm"], (0, ps["yaw"], 0), tag, folder)


# ---------------------------------------------------------------------------
# CP03: scatter, golden-hour atmosphere, viewpoints
# ---------------------------------------------------------------------------

def load_scatter():
    path = os.path.join(hg.project_dir(), "Generated", "Heightmaps", "HaGiang_scatter.json")
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def spawn_scatter(label, mesh, material, instances, tag, folder, cull=(0.0, 0.0), collision=True, shadows=True,
                  extra_tags=()):
    actor = hg.spawn_actor(unreal.HGInstancedScatterActor, label, (0, 0, 0), (0, 0, 0), tag, folder)
    if extra_tags:
        actor.set_editor_property("tags", list(actor.tags) + [unreal.Name(t) for t in extra_tags])
    actor.setup_scatter(mesh, material, float(cull[0]), float(cull[1]), collision, shadows)
    packed = []
    for inst in instances:
        packed.extend(inst)
    if packed:
        actor.add_packed_instances(packed)
    return actor


def spawn_scan_scatter(label, gen_mesh, pieces, instances, tag, folder, cull=(0.0, 0.0), collision=True, shadows=True,
                       mode="size", sink=0.0):
    """Round 7A: place Poly Haven scan pieces where the scatter data has instances of a generated mesh.

    Instances are dealt round-robin over the pieces. Each piece is fitted to the generated mesh it stands in for
    (mode "size": largest extent, "height": z extent, "axes": per axis within 0.6-1.8x of the uniform fit), keeps
    its own (re-tinted) materials, and is re-centred: multi-piece glTF sets lay their pieces out in a row away from
    the origin, so the rotated piece offset is taken off the instance position and its base is put on the ground
    (generated meshes have their pivot at the ground). `sink` buries that share of the piece's height."""
    if not pieces or not instances:
        return 0
    gb = gen_mesh.get_bounding_box()
    gdim = (gb.max.x - gb.min.x, gb.max.y - gb.min.y, gb.max.z - gb.min.z)
    n = len(pieces)
    for i, (mesh, b) in enumerate(pieces):
        sub = instances[i::n]
        if not sub:
            continue
        sdim = (max(b.max.x - b.min.x, 1.0), max(b.max.y - b.min.y, 1.0), max(b.max.z - b.min.z, 1.0))
        if mode == "height":
            f = (gdim[2] / sdim[2],) * 3
        elif mode == "axes":
            u = max(gdim[0], gdim[1]) / max(sdim[0], sdim[1])
            f = tuple(min(max(g / sd, u * 0.6), u * 1.8) for g, sd in zip(gdim, sdim))   # limit texel smear
        else:
            u = max(gdim) / max(sdim)
            f = (u, u, u)
        cx, cy = (b.min.x + b.max.x) * 0.5, (b.min.y + b.max.y) * 0.5
        out = []
        for it in sub:
            x, y, z, yaw, pitch, roll, sx, sy, sz = (list(it) + [1.0, 1.0, 1.0])[:9]
            S = (sx * f[0], sy * f[1], sz * f[2])
            a = math.radians(yaw)
            ox, oy = cx * S[0], cy * S[1]
            dx, dy = ox * math.cos(a) - oy * math.sin(a), ox * math.sin(a) + oy * math.cos(a)
            out.append([x - dx, y - dy, z - b.min.z * S[2] - sink * sdim[2] * S[2], yaw, pitch, roll, S[0], S[1], S[2]])
        spawn_scatter(f"{label}_{i + 1}", mesh, None, out, tag, folder, cull, collision, shadows)
    return len(instances)


def _seat_houses(terrain, items, max_relief_cm=300.0):
    """Round 5 (agent A): scatter houses stood at the ground height of their centre, so on a slope the downhill
    side hung up to 2 m in the air (the blue-roofed house 15 m from the Doc Tham Ma spawn read as a floating brown
    block over the camera). Seat each on the lowest ground under its footprint (+0.7 m eaves) and drop spots
    with more than 3 m of relief across it."""
    out, dropped = [], 0
    for it in items:
        x, y, yaw, sx, sy = it[0], it[1], it[3], it[6], it[7]
        a = math.radians(yaw)
        ca, sa = math.cos(a), math.sin(a)
        hx, hy = 450.0 * sx + 70.0, 300.0 * sy + 70.0
        zs = [terrain.z(x + ca * hx * u - sa * hy * v, y + sa * hx * u + ca * hy * v) for u in (-1, 0, 1) for v in (-1, 0, 1)]
        if max(zs) - min(zs) > max_relief_cm:
            dropped += 1
            continue
        out.append([x, y, min(zs)] + list(it[3:]))
    if dropped:
        hg.log(f"scatter houses: {dropped} on slopes too steep dropped, {len(out)} seated on their lowest ground")
    return out


def build_scatter(scatter, meshes, mats, tag, folder):
    """Instanced scatter. On the M1 profile (no Nanite) the meshes carry LOD chains; far cliffs are
    culled at 2.2 km (fog hides the pop), shrubs don't cast shadows and are thinned by a third."""
    m1 = not hg.USE_NANITE
    cliff_cull = (180000.0, 220000.0) if m1 else (0.0, 0.0)
    cliff_meshes = [meshes[f"SM_HG_KarstCliff_0{i}"] for i in range(1, 5)]
    # Round 7A: Poly Haven scans (hg_lib.scans) stand in for the procedural cliffs, boulders, shrubs and pines when
    # they are imported; the procedural meshes stay the fallback (and the fitting reference for size).
    from . import scan_meshes as scans
    sc = {r: scans.pieces(r) for r in ("cliff", "boulder")}
    sc["shrub"] = []     # scanned shrubs read as dark thin spikes at review distance - procedural bushes stay
    sc["sapling"] = []   # scanned firs: too heavy (480-780k verts) and stick-like when decimated - pines stay procedural
    for name, cull in (("cliffs", cliff_cull), ("summits", (0.0, 0.0))):
        items = scatter[name]
        for v, mesh in enumerate(cliff_meshes):
            if sc["cliff"]:
                spawn_scan_scatter(f"Scatter_{name}_{v + 1}", mesh, sc["cliff"], items[v::4], tag, folder, cull,
                                   mode="axes", sink=0.04)
            else:
                spawn_scatter(f"Scatter_{name}_{v + 1}", mesh, mats["rock"], items[v::4], tag, folder, cull)
    if sc["cliff"]:
        spawn_scan_scatter("Scatter_spur", cliff_meshes[0], sc["cliff"][2:3], scatter["spur"], tag, folder,
                           mode="axes", sink=0.04)
    else:
        spawn_scatter("Scatter_spur", cliff_meshes[0], mats["rock"], scatter["spur"], tag, folder)
    boulders = [meshes[f"SM_HG_Boulder_0{i}"] for i in range(1, 4)]
    for v, mesh in enumerate(boulders):
        if sc["boulder"]:
            spawn_scan_scatter(f"Scatter_boulders_{v + 1}", mesh, sc["boulder"][v::3],
                               scatter["boulders"][v::3], tag, folder, cull=(45000.0, 60000.0), sink=0.3)
        else:
            spawn_scatter(f"Scatter_boulders_{v + 1}", mesh, mats["rock"], scatter["boulders"][v::3], tag, folder,
                          cull=(45000.0, 60000.0))
    bushes = [meshes["SM_HG_Bush_01"], meshes["SM_HG_Bush_02"]]
    bush_items = [b for i, b in enumerate(scatter["bushes"]) if not m1 or i % 3 != 2]
    for v, mesh in enumerate(bushes):
        bcull = (14000.0, 20000.0) if m1 else (18000.0, 26000.0)
        if sc["shrub"]:
            bcull = (9000.0, 12000.0)   # masked scan foliage: cull hard (overdraw), the slopes beyond keep the rocks
            spawn_scan_scatter(f"Scatter_bushes_{v + 1}", mesh, sc["shrub"][v::2], bush_items[v::2], tag, folder,
                               cull=bcull, collision=False, shadows=False, mode="height")
        else:
            spawn_scatter(f"Scatter_bushes_{v + 1}", mesh, mats["bush"], bush_items[v::2], tag, folder,
                          cull=bcull, collision=False, shadows=not m1)
    # Trees: trunk + crown HISMs sharing the same instance transforms.
    tree_parts = {
        "kapok": (("SM_HG_Kapok_Trunk", "kapok_bark"), ("SM_HG_Kapok_Bloom", "kapok_bloom")),
        "broadleaf": (("SM_HG_Tree_Trunk", "bark"), ("SM_HG_Tree_Crown", "leaf")),
        "pine": (("SM_HG_Pine_Trunk", "bark"), ("SM_HG_Pine_Crown", "pine")),
        "peach": (("SM_HG_Tree_Trunk", "bark"), ("SM_HG_Kapok_Bloom", "peach")),
    }
    for kind, parts in tree_parts.items():
        items = scatter.get("trees", {}).get(kind, [])
        if kind == "pine" and sc["sapling"]:
            # Scanned firs (one mesh: trunk + needles) fitted to the procedural pine's height.
            spawn_scan_scatter("Scatter_tree_pine", meshes["SM_HG_Pine_Trunk"], sc["sapling"], items, tag, folder,
                               cull=(110000.0, 150000.0) if m1 else (0.0, 0.0), collision=False, shadows=True,
                               mode="height")
            continue
        for mesh_name, mat_name in parts:
            spawn_scatter(f"Scatter_tree_{kind}_{mesh_name[6:]}", meshes[mesh_name], mats[mat_name], items, tag, folder,
                          cull=(110000.0, 150000.0) if m1 else (0.0, 0.0), collision=("Trunk" in mesh_name),
                          shadows=True)
    spawn_scatter("Scatter_road_posts", meshes["SM_HG_RoadPost"], mats["post"], scatter["posts"], tag, folder,
                  cull=(15000.0, 22000.0), collision=True, shadows=True, extra_tags=("RoadPosts",))
    if scatter.get("guardrails") and "SM_HG_Guardrail" in meshes:
        spawn_scatter("Scatter_guardrails", meshes["SM_HG_Guardrail"], mats["guardrail"], scatter["guardrails"], tag,
                      folder, cull=(18000.0, 26000.0), collision=True, shadows=True, extra_tags=("RoadPosts",))
    if scatter.get("stone_walls") and "SM_HG_StoneWall" in meshes:
        spawn_scatter("Scatter_stone_walls", meshes["SM_HG_StoneWall"], mats["stone"], scatter["stone_walls"], tag,
                      folder, cull=(30000.0, 40000.0), collision=True, shadows=True, extra_tags=("RoadPosts",))
    counts = {k: len(v) for k, v in scatter.items() if isinstance(v, list)}
    hg.log(f"Scatter instances: {counts}")


def build_golden_hour(tag, folder):
    """Locked weather (docs/ART_BIBLE.md): late March ~10:30, clear dry-season sky with light haze.
    Sun ~50 deg up from the east-south-east (map +X = east, +Y = south), so the east-facing Ma Pi Leng
    wall and the Tham Ma bends are lit and the gorge floor still gets light. No VolumetricCloud on the
    M1 profile (measured -10..-25 FPS); exponential height fog gives the aerial haze only."""
    # Map axes: +X east, +Y south (north is -Y). Light travelling WNW from an ESE sun: yaw atan2(-0.383, -0.924).
    sun = hg.spawn_actor(unreal.DirectionalLight, "Sun", (0, 0, 200000), (-50.0, -157.5, 0), tag, folder)
    sc = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sc.set_mobility(unreal.ComponentMobility.MOVABLE)
    for k, v in (("intensity", 10.0), ("use_temperature", True), ("temperature", 5100.0),   # 6B: warmer key light
                 ("atmosphere_sun_light", True), ("cast_cloud_shadows", True),
                 ("enable_light_shaft_bloom", True), ("enable_light_shaft_occlusion", True),
                 ("bloom_scale", 0.25), ("volumetric_scattering_intensity", 1.6),
                 # M1 budget: 3 cascaded shadow maps out to 150 m (400 m cost ~12 FPS); no DF shadows (banding).
                 ("dynamic_shadow_cascades", 3), ("dynamic_shadow_distance_movable_light", 15000.0),
                 ("cascade_distribution_exponent", 3.0), ("use_ray_traced_distance_field_shadows", False)):
        try:
            sc.set_editor_property(k, v)
        except Exception as exc:  # property names differ slightly between engine versions
            hg.warn(f"Sun.{k}: {exc}")

    atmo = hg.spawn_actor(unreal.SkyAtmosphere, "SkyAtmosphere", (0, 0, 0), (0, 0, 0), tag, folder)
    ac = atmo.get_component_by_class(unreal.SkyAtmosphereComponent)
    try:
        ac.set_editor_property("mie_scattering_scale", 0.006)
    except Exception as exc:
        hg.warn(f"SkyAtmosphere: {exc}")
    # Aerial perspective from the atmosphere (blue in-scatter that grows with distance but keeps the ridges'
    # light / shade contrast) instead of relying on the flat height-fog colour, which washed the far ranges
    # out to one grey-blue sheet. Property name has UE's own typo.
    for k, v in (("aerial_pespective_view_distance_scale", 2.2), ("height_fog_contribution", 0.6)):
        try:
            ac.set_editor_property(k, v)
        except Exception as exc:
            hg.warn(f"SkyAtmosphere.{k}: {exc}")

    sky = hg.spawn_actor(unreal.SkyLight, "SkyLight", (0, 0, 150000), (0, 0, 0), tag, folder)
    skc = sky.get_component_by_class(unreal.SkyLightComponent)
    skc.set_mobility(unreal.ComponentMobility.MOVABLE)
    skc.set_editor_property("real_time_capture", True)
    skc.set_editor_property("intensity", 1.35)      # brighter shade on the west-facing canyon walls

    if hg.PROFILE == "sm6":
        cloud = hg.spawn_actor(unreal.VolumetricCloud, "Cumulus", (0, 0, 0), (0, 0, 0), tag, folder)
        cc = cloud.get_component_by_class(unreal.VolumetricCloudComponent)
        cloud_mat = unreal.EditorAssetLibrary.load_asset("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst")
        if cloud_mat:
            cc.set_editor_property("material", cloud_mat)
        cc.set_editor_property("layer_bottom_altitude", 2.6)
        cc.set_editor_property("layer_height", 1.5)

    # Dry-season haze: thin fog from the reservoir up, no valley "sea of clouds" at 10:30.
    fog = hg.spawn_actor(unreal.ExponentialHeightFog, "Haze", (0, 0, 42000), (0, 0, 0), tag, folder)
    fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    # UE falloff is per 1000 uu: 0.08 -> density e-folds every ~125 m above the fog actor.
    # Round 2B: thinner, bluer haze that starts further out (the near and mid ranges keep their contrast;
    # the atmosphere's aerial perspective does the depth).
    for k, v in (("fog_density", 0.002), ("fog_height_falloff", 0.04),
                 ("fog_inscattering_luminance", unreal.LinearColor(0.26, 0.34, 0.48, 1.0)),
                 ("directional_inscattering_luminance", unreal.LinearColor(0.75, 0.68, 0.55, 1.0)),
                 ("directional_inscattering_exponent", 6.0),
                 ("enable_volumetric_fog", hg.PROFILE == "sm6"), ("volumetric_fog_scattering_distribution", 0.65),
                 ("volumetric_fog_extinction_scale", 1.2), ("volumetric_fog_distance", 25000.0),
                 ("fog_max_opacity", 0.62), ("start_distance", 2500.0)):
        try:
            fc.set_editor_property(k, v)
        except Exception as exc:
            hg.warn(f"Fog.{k}: {exc}")
    # Round 3B - valley haze: a second, low-lying fog layer that settles in the gorges and valley floors (e-folds
    # every ~200 m above the river) so the distant ranges stand out layer by layer above it, and the shaded
    # gorge gets some in-scattered light instead of going black.
    try:
        second = unreal.ExponentialHeightFogData()
        second.set_editor_property("fog_density", 0.0018)
        second.set_editor_property("fog_height_falloff", 0.05)
        second.set_editor_property("fog_height_offset", 3000.0)
        fc.set_editor_property("second_fog_data", second)
    except Exception as exc:
        hg.warn(f"Fog second layer: {exc}")

    ppv = hg.spawn_actor(unreal.PostProcessVolume, "PP_GoldenHour", (0, 0, 100000), (0, 0, 0), tag, folder)
    ppv.set_editor_property("unbound", True)
    s = ppv.get_editor_property("settings")
    for k, v in (("bloom_intensity", 0.55), ("vignette_intensity", 0.32), ("film_grain_intensity", 0.12),
                 ("auto_exposure_min_brightness", -1.2), ("auto_exposure_max_brightness", 2.5),   # 3B: -1.2 (was 0.6) lets the eye adapt in the sky-lit-only Tu San narrows
                 ("white_temp", 6600.0), ("color_saturation", unreal.Vector4(1.07, 1.06, 1.0, 1.0)),   # 6B grade: richer reds / earth
                 ("color_contrast", unreal.Vector4(1.11, 1.1, 1.09, 1.0)),
                 ("color_gain_shadows", unreal.Vector4(0.91, 0.98, 1.1, 1.0)),   # 6B: cooler shade
                 ("color_gain_highlights", unreal.Vector4(1.07, 1.01, 0.9, 1.0)),   # 6B: golden key
                 ("lumen_final_gather_quality", 1.0), ("lumen_scene_lighting_quality", 1.0),
                 # Round 3B - deep gorge shade (Tu San narrows went near-black): sky light leaks a little into
                 # sky-occluded places (a bounce fill that only matters where Lumen sees no sky), and local
                 # exposure lifts the shadows without flattening the sunlit walls.
                 ("lumen_skylight_leaking", 0.16),
                 ("local_exposure_shadow_contrast_scale", 1.0), ("local_exposure_highlight_contrast_scale", 1.0),
                 ("local_exposure_detail_strength", 1.0)):
        try:
            s.set_editor_property(k, v)
            s.set_editor_property("override_" + k, True)
        except Exception as exc:
            hg.warn(f"PostProcess.{k}: {exc}")
    ppv.set_editor_property("settings", s)
    return sun


def build_viewpoints(scatter, flute, tag, folder, names=None):
    """names: optional {key: display name} override (CP05 uses Vietnamese with diacritics)."""
    for key, vp in scatter["viewpoints"].items():
        actor = hg.spawn_actor(unreal.HGViewpointActor, f"Viewpoint_{key}", vp["location_cm"], (0, 0, 0), tag, folder)
        actor.set_editor_property("viewpoint_name", unreal.Text((names or {}).get(key, vp["name"])))
        actor.set_editor_property("altitude_m", int(vp["altitude_m"]))
        actor.set_editor_property("activation_radius", 4500.0)
        actor.set_editor_property("orbit_center", hg.vec(vp["center_cm"]))
        actor.set_editor_property("orbit_radius", float(vp["radius_cm"]))
        actor.set_editor_property("orbit_height", float(vp["height_cm"]))
        if flute is not None:
            actor.set_editor_property("flute_sound", flute)
        hg.log(f"Viewpoint {vp['name']} ({vp['altitude_m']} m)")



# ---------------------------------------------------------------------------
# Reference-photo props: Tham Ma stone parapet + flower sellers, Ta Lang floating dock + boat traffic
# ---------------------------------------------------------------------------

def build_viewpoint_props(layout, scatter, meshes, mats, tag, folder):
    """Doc Tham Ma overlook: low stone parapet on the valley edge, red buckets of white/yellow
    wildflowers and a Hmong flower-seller girl, like the classic check-in photo."""
    vp = layout["viewpoints"]["doc_tham_ma"]["location_cm"]
    centre = scatter["viewpoints"]["doc_tham_ma"]["center_cm"]
    dx, dy = centre[0] - vp[0], centre[1] - vp[1]
    d = math.hypot(dx, dy) or 1.0
    ox, oy = dx / d, dy / d                       # outward (towards the hairpins)
    px, py = -oy, ox                              # along the parapet
    yaw = math.degrees(math.atan2(py, px))
    stone = mats.get("stone")
    # Positions from placements.tham_ma_parapet: blocks on/near the road are dropped (validate_layout checks it).
    blocks, buckets = placements.tham_ma_parapet(placements.ctx_for(layout, scatter))
    for i, (cx, cy, gz, byaw) in enumerate(blocks):
        hg.box(f"Parapet_{i}", (cx, cy, gz + 40), (400, 55, 95), yaw=byaw, material=stone, tag=tag, folder=folder)
    red = hg.color_material("MI_GB_BucketRed", (0.6, 0.03, 0.03))
    for i, ((bx, by, gz), flower) in enumerate(zip(buckets, ("flowers_white", "flowers_yellow", "flowers_white"))):
        hg.spawn_mesh(f"FlowerBucket_{i}", hg.CYLINDER, (bx, by, gz + 30), scale=(0.55, 0.55, 0.6),
                      material=red, tag=tag, folder=folder)
        bush = hg.spawn_mesh(f"FlowerBunch_{i}", "/Game/HaGiang/Generated/Meshes/SM_HG_Bush_01.SM_HG_Bush_01",
                             (bx, by, gz + 60), scale=(0.45, 0.45, 0.55), material=mats[flower], tag=tag,
                             folder=folder, collision=False)
    girl = hg.spawn_actor(unreal.HGTouristNPC, "FlowerSeller", (vp[0] + ox * 760 + px * -480, vp[1] + oy * 760 + py * -480, vp[2] + 100),
                          (0, 0, 0), tag, folder)
    girl.set_editor_property("style", unreal.HGTouristStyle.LOCAL)
    girl.set_editor_property("look_at_point", hg.vec(centre))


def _cafe_piers(layout, at, yaw, concrete, tag, folder):
    """Columns under the Panorama cafe wherever the ground is more than 0.4 m below its floor (house mesh
    footprint 10.4 x 7.6 m scaled x1.25 along the road; columns every ~3.2 m along, 3 rows across)."""
    try:
        terrain = placements.ctx_for(layout).terrain
    except Exception as exc:  # no heightmap next to the layout: skip the piers rather than the whole level
        hg.warn(f"cafe piers skipped: {exc}")
        return
    ca, cu = placements.MPL_CAFE_AT
    floor = at(ca, cu, -0.1)[2]
    n = 0
    lo = floor
    for i in range(5):
        along = ca + (-6.2 + 3.1 * i)
        for up in (cu - 3.5, cu, cu + 3.5):
            x, y, _ = at(along, up)
            gz = terrain.z(x, y)
            drop = floor - gz
            if drop < 40.0:
                continue
            h = drop + 120.0                     # 1.2 m footing into the slope
            hg.box(f"MPL_PanoramaCafe_Pier_{i}_{n}", (x, y, floor - h * 0.5), (40, 40, h), yaw=yaw,
                   material=concrete, tag=tag, folder=folder)
            n += 1
            lo = min(lo, gz)
    if n:
        # Deck slab under the floor so the columns carry something (and the open underside isn't a void).
        c = at(ca, cu, -0.1)
        hg.box("MPL_PanoramaCafe_Deck", (c[0], c[1], floor - 15.0), (1340, 780, 30), yaw=yaw,
               material=concrete, tag=tag, folder=folder)
    hg.log(f"Panorama cafe: {n} piers (lowest ground {(floor - lo) / 100:.1f} m under the floor)")


def build_landmarks(layout, meshes, mats, mat_wood, tag, folder):
    """Landmarks from docs/ART_BIBLE.md section 3, on the pads stamped by gen_heightmap.py:
    Ma Pi Leng summit (Youth Volunteers monument, name stone, Panorama cafe), Doc Tham Ma coffee shop
    with Hmong pack horses, Ta Lang dock yard (ticket booth, cafe deck)."""
    marks = layout.get("landmarks", {})
    if not marks:
        hg.warn("layout has no landmarks; regenerate the heightmap")
        return
    walls, roof = meshes.get("SM_HG_HouseWalls"), meshes.get("SM_HG_HouseRoof")
    mesh_path = lambda m: m.get_path_name()

    def frame(key):
        m = marks[key]
        x, y, z = m["location_cm"]
        yaw = m["yaw"]
        tx, ty = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        ux, uy = m["up"]
        at = lambda along, up, dz=0.0: (x + tx * along * 100 + ux * up * 100, y + ty * along * 100 + uy * up * 100, z + dz * 100)
        return at, yaw

    def building(label, at_xyz, yaw, scale, wall_mat, roof_mat):
        hg.spawn_mesh(f"{label}_Walls", mesh_path(walls), at_xyz, (0, yaw, 0), scale, wall_mat, tag, folder)
        hg.spawn_mesh(f"{label}_Roof", mesh_path(roof), at_xyz, (0, yaw, 0), scale, roof_mat, tag, folder, collision=False)

    if "ma_pi_leng_plaza" in marks:
        at, yaw = frame("ma_pi_leng_plaza")
        if "youth_monument" not in marks:
            hg.spawn_mesh("MPL_Monument", mesh_path(meshes["SM_HG_Monument"]), at(6, 2, -0.1), (0, yaw, 0), (1, 1, 1),
                          mats["monument"], tag, folder)
        # Name stone: a big upright limestone block by the road edge (the carved kit stone of dressing.py
        # replaces it when the C++ prop is compiled).
        if not hasattr(unreal, "HGDressingProp"):
          hg.spawn_mesh("MPL_NameStone", mesh_path(meshes["SM_HG_Boulder_01"]), at(-6, -4, -0.3), (0, yaw + 90, 0),
                      (1.1, 0.45, 1.0), mats["monument"], tag, folder)
        building("MPL_PanoramaCafe", at(*placements.MPL_CAFE_AT, -0.1), yaw, (1.25, 1.0, 1.05), mats["cafe_wall"], mats["roof_green"])
        # On the pad now; any corner still over the edge stands on concrete piers like the real cliff-edge cafes.
        _cafe_piers(layout, at, yaw, mats.get("pole"), tag, folder)
        stall = at(*placements.MPL_STALL_AT, 0.0)
        hg.box("MPL_DrinkStall", (stall[0], stall[1], stall[2] + 55), (260, 120, 110), yaw=yaw, material=mat_wood, tag=tag, folder=folder)
        hg.box("MPL_StallAwning", (stall[0], stall[1], stall[2] + 240), (320, 220, 8), yaw=yaw,
               material=mats["roof_blue"], tag=tag, folder=folder, collision=False)
        # Stabilisation (review rim_120: "floating blue bar"): the awning had nothing under it - four posts from
        # the stall corners up to the awning, and the stall snapped to the plaza ground.
        ca, sa = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        for px, py in ((-150, -100), (150, -100), (-150, 100), (150, 100)):
            hg.box(f"MPL_StallPost_{px}_{py}", (stall[0] + ca * px - sa * py, stall[1] + sa * px + ca * py, stall[2] + 118),
                   (8, 8, 236), yaw=yaw, material=mat_wood, tag=tag, folder=folder, collision=False)

    if "youth_monument" in marks:
        at, yaw = frame("youth_monument")
        hg.spawn_mesh("YouthVolunteers_Monument", mesh_path(meshes["SM_HG_Monument"]), at(0, 2, -0.1), (0, yaw, 0),
                      (1, 1, 1), mats["monument"], tag, folder)

    if "tham_ma_cafe" in marks:
        at, yaw = frame("tham_ma_cafe")
        building("ThamMa_Cafe", at(0, 3.0, -0.1), yaw, (0.95, 0.85, 0.9), mats["cafe_wall"], mats["roof_blue"])
        for i, (along, coat) in enumerate(((-8.0, (0.28, 0.15, 0.07)), (-10.5, (0.75, 0.72, 0.66)))):
            horse = hg.spawn_actor(unreal.HGCowActor, f"ThamMa_PackHorse_{i}", at(along, -0.5, 1.0), (0, yaw + 180, 0), tag, folder)
            horse.set_editor_property("horse", True)
            horse.set_editor_property("wander_radius", 250.0)
            horse.set_editor_property("coat_color", unreal.LinearColor(*coat, 1.0))
        granny = hg.spawn_actor(unreal.HGTouristNPC, "ThamMa_FlowerGranny", at(6, -0.5, 1.0), (0, yaw, 0), tag, folder)
        granny.set_editor_property("style", unreal.HGTouristStyle.LOCAL)

    if "dock_yard" in marks:
        at, yaw = frame("dock_yard")
        # The yard frame's "along" axis points at the river; build sideways along the bank.
        building("TaLang_TicketBooth", at(-2, 0, -0.1), yaw + 90, (0.38, 0.4, 0.75),
                 mats["cafe_wall"], mats["roof_blue"])
        deck = at(2, 9, 0.0)
        hg.box("TaLang_CafeDeck", (deck[0], deck[1], deck[2] + 20), (900, 650, 40), yaw=yaw, material=mat_wood, tag=tag, folder=folder)
        hg.spawn_mesh("TaLang_CafeRoof", mesh_path(roof), (deck[0], deck[1], deck[2] - 70), (0, yaw, 0), (0.85, 0.85, 0.7),
                      mats["roof_green"], tag, folder, collision=False)
        for k, (ax, ay) in enumerate(((-380, -280), (380, -280), (-380, 280), (380, 280))):
            c, s_ = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
            hg.box(f"TaLang_CafePost_{k}", (deck[0] + ax * c - ay * s_, deck[1] + ax * s_ + ay * c, deck[2] + 120),
                   (14, 14, 210), yaw=yaw, material=mat_wood, tag=tag, folder=folder)
    hg.log(f"Landmarks: {sorted(marks)}")


def build_river_traffic(layout, tag, folder):
    """Ta Lang: green-roofed floating pontoons, moored tourist boats, and boats cruising the gorge.
    Placement (placements.river_traffic) keeps every hull and deck on water deep enough for it: the reservoir
    at the dock is only ~35 m wide, so the old fixed 30 m / 48 m offsets put the moored boats on the far bank."""
    river = placements.river_traffic(placements.ctx_for(layout))
    water_z = layout["water"]["surface_z_cm"]
    deck = hg.color_material("MI_GB_PontoonDeck", (0.18, 0.16, 0.14))
    roof_green = hg.color_material("MI_GB_RoofGreen", (0.05, 0.25, 0.12))
    for i, (cx, cy, ryaw) in enumerate(river["pontoons"]):
        hg.box(f"Pontoon_{i}", (cx, cy, water_z + 30), (1800, 900, 60), yaw=ryaw, material=deck, tag=tag, folder=folder)
        hg.box(f"PontoonRoof_{i}", (cx, cy, water_z + 380), (1600, 800, 20), yaw=ryaw, material=roof_green, tag=tag,
               folder=folder, collision=False)
        for k, (ox_, oy_) in enumerate(((-700, -350), (700, -350), (-700, 350), (700, 350))):
            r = math.radians(ryaw)
            px_ = cx + ox_ * math.cos(r) - oy_ * math.sin(r)
            py_ = cy + ox_ * math.sin(r) + oy_ * math.cos(r)
            hg.spawn_mesh(f"PontoonPost_{i}_{k}", hg.CYLINDER, (px_, py_, water_z + 205), scale=(0.15, 0.15, 3.5),
                          material=deck, tag=tag, folder=folder, collision=False)
    # Moored boats alongside the pontoons.
    for i, (bx, by, byaw) in enumerate(river["moored"]):
        boat = hg.spawn_actor(unreal.NhoQueBoatPawn, f"Boat_Moored_{i}", (bx, by, water_z), (0, byaw, 0), tag, folder)
        boat.set_editor_property("boat_type", unreal.HGBoatType.CANO)
        rerun(boat)
    # Cruising boats ping-pong along the navigable stretch of the reservoir through the gorge.
    route = river["route"]
    if len(route) < 2:
        hg.warn("River traffic: no navigable cruise route (see Scripts/Tools/validate_layout.py)")
        return
    pts = [unreal.Vector(p[0], p[1], water_z) for p in route]
    for i, start in enumerate(river["starts"]):
        p0, p1 = route[start], route[min(start + 1, len(route) - 1)]
        yaw = math.degrees(math.atan2(p1[1] - p0[1], p1[0] - p0[0]))
        boat = hg.spawn_actor(unreal.NhoQueBoatPawn, f"Boat_Cruise_{i}", (p0[0], p0[1], water_z), (0, yaw, 0), tag, folder)
        boat.set_editor_property("boat_type", unreal.HGBoatType.CANO)
        boat.set_editor_property("cruise_points", pts)
        boat.set_editor_property("auto_cruise", True)
        boat.set_editor_property("cruise_throttle", 0.45 + 0.1 * i)
        rerun(boat)


def layout_for_dressing():
    return hg.ensure_generated_heightmap()


def build_set_dressing(scatter, meshes, mats, tag, folder):
    """Reference-photo dressing: distant blue ranges, chevrons on the bends, utility poles, hamlets."""
    # Round 3B: layered ridge rings (gen_ranges.py, absolute altitude) instead of the single flat band (3 since 4B).
    for i in range(3):
        path = f"/Game/HaGiang/Generated/Meshes/SM_HG_RangeLayer_{i}.SM_HG_RangeLayer_{i}"
        if unreal.EditorAssetLibrary.does_asset_exist(path.split(".")[0]) and f"range_{i}" in mats:
            layer = hg.spawn_mesh(f"RangeLayer_{i}", path, (0, 0, 0), material=mats[f"range_{i}"], tag=tag,
                                  folder=folder, collision=False)
            layer.static_mesh_component.set_editor_property("cast_shadow", False)
            try:
                unreal.EditorStaticMeshLibrary.remove_collisions(layer.static_mesh_component.static_mesh)
            except Exception as exc:
                hg.warn(f"RangeLayer_{i} collision: {exc}")
    ranges = hg.spawn_mesh("DistantRanges", "/Game/HaGiang/Generated/Meshes/SM_HG_DistantRanges.SM_HG_DistantRanges",
                           (0, 0, 80000), material=mats["ranges"], tag=tag, folder=folder, collision=False)
    ranges.set_actor_hidden_in_game(any(f"range_{i}" in mats for i in range(3)))   # kept only as a fallback
    ranges.static_mesh_component.set_editor_property("cast_shadow", False)
    # Scenery only: its auto-generated collision box would enclose the whole map (170..2060 m) and
    # swallow every ground trace and the PlayerStart.
    try:
        unreal.EditorStaticMeshLibrary.remove_collisions(ranges.static_mesh_component.static_mesh)
    except Exception as exc:
        hg.warn(f"DistantRanges collision: {exc}")
    origin, extent = ranges.get_actor_bounds(False)
    hg.log(f"DistantRanges bounds: origin {origin} extent {extent}")
    # Foothills between the map edge and the ranges, in a dry-grass olive (drawn on top of the ranges' skirt).
    apron = hg.spawn_mesh("DistantApron", "/Game/HaGiang/Generated/Meshes/SM_HG_DistantApron.SM_HG_DistantApron",
                          (0, 0, 80000 + 300), material=hg.color_material("MI_GB_Foothills", (0.13, 0.13, 0.065)),
                          tag=tag, folder=folder, collision=False)
    apron.static_mesh_component.set_editor_property("cast_shadow", False)
    # Round 3B: the real DEM terrain beyond the map (gen_farterrain.py) replaces the apron in game (the flat
    # one-colour sheet read as a hazy lake from the Ma Pi Leng rim); the apron stays as the fallback.
    far_path = "/Game/HaGiang/Generated/Meshes/SM_HG_FarTerrain"
    if unreal.EditorAssetLibrary.does_asset_exist(far_path) and "far_terrain" in mats:
        far = hg.spawn_mesh("FarTerrain", far_path + ".SM_HG_FarTerrain", (0, 0, 0), material=mats["far_terrain"],
                            tag=tag, folder=folder, collision=False)
        far.static_mesh_component.set_editor_property("cast_shadow", False)
        try:
            unreal.EditorStaticMeshLibrary.remove_collisions(far.static_mesh_component.static_mesh)
        except Exception as exc:
            hg.warn(f"FarTerrain collision: {exc}")
        apron.set_actor_hidden_in_game(True)
    try:
        unreal.EditorStaticMeshLibrary.remove_collisions(apron.static_mesh_component.static_mesh)
    except Exception as exc:
        hg.warn(f"DistantApron collision: {exc}")
    spawn_scatter("Scatter_chevrons", meshes["SM_HG_Chevron"], mats["chevron"], scatter.get("chevrons", []), tag, folder,
                  cull=(20000.0, 30000.0), collision=True, extra_tags=("RoadPosts",))
    spawn_scatter("Scatter_poles", meshes["SM_HG_UtilityPole"], mats["pole"], scatter.get("poles", []), tag, folder,
                  cull=(50000.0, 70000.0), collision=True)
    try:
        terrain = placements.ctx_for(layout_for_dressing()).terrain
    except Exception as exc:  # heightmap not next to the layout
        hg.warn(f"house ground seat skipped: {exc}")
        terrain = None
    for colour, items in scatter.get("houses", {}).items():
        if colour == "grey":
            items = dressing.remaining_grey_houses(layout_for_dressing(), items)   # the rest are kit H'Mong houses
        if terrain is not None:
            items = _seat_houses(terrain, items)
        spawn_scatter(f"Scatter_houses_{colour}_walls", meshes["SM_HG_HouseWalls"], mats["house_wall"], items, tag, folder,
                      cull=(0.0, 0.0), collision=True)
        spawn_scatter(f"Scatter_houses_{colour}_roofs", meshes["SM_HG_HouseRoof"], mats[f"roof_{colour}"], items, tag, folder,
                      cull=(0.0, 0.0), collision=False)
