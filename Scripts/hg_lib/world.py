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
    actor = hg.spawn_actor(unreal.HGWaterBody, "Water_NhoQue", (0, 0, layout["water"]["surface_z_cm"]), (0, 0, 0),
                           tag, folder)
    actor.set_editor_property("water_material", material)
    actor.set_editor_property("size_cm", unreal.Vector2D(403200.0, 403200.0))
    rerun(actor)
    return actor


def build_road(layout, material, tag, folder, strip_mesh=None):
    """strip_mesh: SM_HG_RoadStrip (top at z=0, lane-paint UVs). Without it the engine cube is used."""
    road = layout["road"]
    actor = hg.spawn_actor(unreal.HGRoadSplineActor, "Road_MaPiLeng", (0, 0, 0), (0, 0, 0), tag, folder)
    actor.set_editor_property("road_material", material)
    actor.set_editor_property("road_width", road["half_width_cm"] * 2.0)
    offset = ROAD_MESH_OFFSET_CM
    if strip_mesh is not None:
        actor.set_editor_property("road_mesh", strip_mesh)
        actor.set_editor_property("road_thickness", 100.0)   # Z scale 1; the actor lowers by half -> add it back
        offset += 50.0
    pts = [unreal.Vector(p[0], p[1], p[2] + offset) for p in road["points_cm"]]
    actor.set_road_points(pts)
    actor.rebuild_road_meshes()
    hg.log(f"Road: {len(pts)} points, {road['length_m'] / 1000:.2f} km")
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
    for key, vp in layout["viewpoints"].items():
        x, y, z = vp["location_cm"]
        hg.spawn_mesh(f"CheckIn_{key}", hg.CYLINDER, (x, y, z + 3), scale=(4.0, 4.0, 0.03),
                      material=mat_ring, tag=tag, folder=folder, collision=False)


def build_vehicles(layout, tag, folder):
    spawns = layout["spawns"]
    colors = [unreal.LinearColor(0.55, 0.02, 0.02, 1), unreal.LinearColor(0.05, 0.08, 0.35, 1),
              unreal.LinearColor(0.08, 0.08, 0.08, 1)]
    for i, bike in enumerate(spawns["bikes"]):
        actor = hg.spawn_actor(unreal.HGBikePawn, f"HondaWin_{i:02d}", bike["location_cm"], (0, bike["yaw"], 0),
                               tag, folder)
        actor.set_editor_property("body_color", colors[i % len(colors)])
    for i, boat in enumerate(spawns["boats"]):
        x, y, z = boat["location_cm"]
        actor = hg.spawn_actor(unreal.NhoQueBoatPawn, f"Boat_{boat['type']}_{i:02d}", (x, y, z),
                               (0, spawns["dock"]["yaw"] + 90, 0), tag, folder)
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


def build_scatter(scatter, meshes, mats, tag, folder):
    """Instanced scatter. On the M1 profile (no Nanite) the meshes carry LOD chains; far cliffs are
    culled at 2.2 km (fog hides the pop), shrubs don't cast shadows and are thinned by a third."""
    m1 = not hg.USE_NANITE
    cliff_cull = (180000.0, 220000.0) if m1 else (0.0, 0.0)
    cliff_meshes = [meshes[f"SM_HG_KarstCliff_0{i}"] for i in range(1, 5)]
    for name, cull in (("cliffs", cliff_cull), ("summits", (0.0, 0.0))):
        items = scatter[name]
        for v, mesh in enumerate(cliff_meshes):
            spawn_scatter(f"Scatter_{name}_{v + 1}", mesh, mats["rock"], items[v::4], tag, folder, cull)
    spawn_scatter("Scatter_spur", cliff_meshes[0], mats["rock"], scatter["spur"], tag, folder)
    boulders = [meshes[f"SM_HG_Boulder_0{i}"] for i in range(1, 4)]
    for v, mesh in enumerate(boulders):
        spawn_scatter(f"Scatter_boulders_{v + 1}", mesh, mats["rock"], scatter["boulders"][v::3], tag, folder,
                      cull=(45000.0, 60000.0))
    bushes = [meshes["SM_HG_Bush_01"], meshes["SM_HG_Bush_02"]]
    bush_items = [b for i, b in enumerate(scatter["bushes"]) if not m1 or i % 3 != 2]
    for v, mesh in enumerate(bushes):
        spawn_scatter(f"Scatter_bushes_{v + 1}", mesh, mats["bush"], bush_items[v::2], tag, folder,
                      cull=(14000.0, 20000.0) if m1 else (18000.0, 26000.0), collision=False, shadows=not m1)
    # Trees: trunk + crown HISMs sharing the same instance transforms.
    tree_parts = {
        "kapok": (("SM_HG_Kapok_Trunk", "kapok_bark"), ("SM_HG_Kapok_Bloom", "kapok_bloom")),
        "broadleaf": (("SM_HG_Tree_Trunk", "bark"), ("SM_HG_Tree_Crown", "leaf")),
        "pine": (("SM_HG_Pine_Trunk", "bark"), ("SM_HG_Pine_Crown", "pine")),
    }
    for kind, parts in tree_parts.items():
        items = scatter.get("trees", {}).get(kind, [])
        for mesh_name, mat_name in parts:
            spawn_scatter(f"Scatter_tree_{kind}_{mesh_name[6:]}", meshes[mesh_name], mats[mat_name], items, tag, folder,
                          cull=(110000.0, 150000.0) if m1 else (0.0, 0.0), collision=("Trunk" in mesh_name),
                          shadows=True)
    spawn_scatter("Scatter_road_posts", meshes["SM_HG_RoadPost"], mats["post"], scatter["posts"], tag, folder,
                  cull=(15000.0, 22000.0), collision=True, shadows=True, extra_tags=("RoadPosts",))
    if scatter.get("guardrails") and "SM_HG_Guardrail" in meshes:
        spawn_scatter("Scatter_guardrails", meshes["SM_HG_Guardrail"], mats["guardrail"], scatter["guardrails"], tag,
                      folder, cull=(18000.0, 26000.0), collision=True, shadows=True, extra_tags=("RoadPosts",))
    counts = {k: len(v) for k, v in scatter.items() if isinstance(v, list)}
    hg.log(f"Scatter instances: {counts}")


def build_golden_hour(tag, folder):
    """Late-afternoon sun from the south-east (13 deg), raking along the N-S Tu San canyon so the
    light shafts run down the gorge, lighting the east-facing Ma Pi Leng rim, the spur and the
    south-facing Tham Ma hairpins. Valley mist (height fog) pools below ~950 m - the 'sea of
    clouds' seen from the pass - while the VolumetricCloud layer sits above the peaks.
    (Composition checked with Scripts/Tools/render_viewpoints.py.)"""
    # Bright dry-season afternoon like the reference photos: sun 38 deg up from the south-east.
    sun = hg.spawn_actor(unreal.DirectionalLight, "Sun", (0, 0, 200000), (-62.0, 95.0, 0), tag, folder)   # near-midday, like the photos
    sc = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sc.set_mobility(unreal.ComponentMobility.MOVABLE)
    for k, v in (("intensity", 10.0), ("use_temperature", True), ("temperature", 5600.0),
                 ("atmosphere_sun_light", True), ("cast_cloud_shadows", True),
                 ("enable_light_shaft_bloom", True), ("enable_light_shaft_occlusion", True),
                 ("bloom_scale", 0.25), ("volumetric_scattering_intensity", 1.6),
                 # M1: 3 cascaded shadow maps out to 400 m; no distance-field shadows (banding on steep landscape).
                 ("dynamic_shadow_cascades", 3), ("dynamic_shadow_distance_movable_light", 40000.0),
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

    sky = hg.spawn_actor(unreal.SkyLight, "SkyLight", (0, 0, 150000), (0, 0, 0), tag, folder)
    skc = sky.get_component_by_class(unreal.SkyLightComponent)
    skc.set_mobility(unreal.ComponentMobility.MOVABLE)
    skc.set_editor_property("real_time_capture", True)
    skc.set_editor_property("intensity", 1.35)      # brighter shade on the west-facing canyon walls

    cloud = hg.spawn_actor(unreal.VolumetricCloud, "SeaOfClouds", (0, 0, 0), (0, 0, 0), tag, folder)
    cc = cloud.get_component_by_class(unreal.VolumetricCloudComponent)
    cloud_mat = unreal.EditorAssetLibrary.load_asset("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst")
    if cloud_mat:
        cc.set_editor_property("material", cloud_mat)
    else:
        hg.warn("Engine volumetric cloud material not found; using component default.")
    cc.set_editor_property("layer_bottom_altitude", 1.65)   # km above sea level: clouds hugging the summits
    cc.set_editor_property("layer_height", 2.0)

    # Fog actor at 820 m with a steep falloff: dense mist in the valleys and the gorge,
    # clear air at the 1200 m road -> looking down from the pass reads as a sea of clouds.
    fog = hg.spawn_actor(unreal.ExponentialHeightFog, "ValleyMist", (0, 0, 82000), (0, 0, 0), tag, folder)
    fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    # UE falloff is per 1000 uu: 0.08 -> density e-folds every ~125 m above the fog actor.
    for k, v in (("fog_density", 0.0035), ("fog_height_falloff", 0.05),
                 ("fog_inscattering_luminance", unreal.LinearColor(0.32, 0.38, 0.45, 1.0)),
                 ("directional_inscattering_luminance", unreal.LinearColor(0.9, 0.6, 0.35, 1.0)),
                 ("directional_inscattering_exponent", 6.0),
                 ("enable_volumetric_fog", True), ("volumetric_fog_scattering_distribution", 0.65),
                 ("volumetric_fog_extinction_scale", 1.2), ("volumetric_fog_distance", 25000.0),
                 ("fog_max_opacity", 0.75), ("start_distance", 1500.0)):
        try:
            fc.set_editor_property(k, v)
        except Exception as exc:
            hg.warn(f"Fog.{k}: {exc}")

    ppv = hg.spawn_actor(unreal.PostProcessVolume, "PP_GoldenHour", (0, 0, 100000), (0, 0, 0), tag, folder)
    ppv.set_editor_property("unbound", True)
    s = ppv.get_editor_property("settings")
    for k, v in (("bloom_intensity", 0.55), ("vignette_intensity", 0.32), ("film_grain_intensity", 0.12),
                 ("auto_exposure_min_brightness", 0.6), ("auto_exposure_max_brightness", 2.5),
                 ("white_temp", 6600.0), ("color_saturation", unreal.Vector4(1.04, 1.06, 1.04, 1.0)),
                 ("color_contrast", unreal.Vector4(1.08, 1.08, 1.08, 1.0)),
                 ("color_gain_shadows", unreal.Vector4(0.94, 1.0, 1.08, 1.0)),
                 ("color_gain_highlights", unreal.Vector4(1.03, 1.0, 0.96, 1.0)),
                 ("lumen_final_gather_quality", 1.0), ("lumen_scene_lighting_quality", 1.0)):
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
    for i in range(-2, 3):
        cx, cy = vp[0] + ox * 900 + px * i * 420, vp[1] + oy * 900 + py * i * 420
        hg.box(f"Parapet_{i + 2}", (cx, cy, vp[2] + 40), (400, 55, 95), yaw=yaw, material=stone, tag=tag, folder=folder)
    red = hg.color_material("MI_GB_BucketRed", (0.6, 0.03, 0.03))
    for i, (along, flower) in enumerate(((-700, "flowers_white"), (-250, "flowers_yellow"), (450, "flowers_white"))):
        bx, by = vp[0] + ox * 820 + px * along, vp[1] + oy * 820 + py * along
        hg.spawn_mesh(f"FlowerBucket_{i}", hg.CYLINDER, (bx, by, vp[2] + 30), scale=(0.55, 0.55, 0.6),
                      material=red, tag=tag, folder=folder)
        bush = hg.spawn_mesh(f"FlowerBunch_{i}", "/Game/HaGiang/Generated/Meshes/SM_HG_Bush_01.SM_HG_Bush_01",
                             (bx, by, vp[2] + 60), scale=(0.45, 0.45, 0.55), material=mats[flower], tag=tag,
                             folder=folder, collision=False)
    girl = hg.spawn_actor(unreal.HGTouristNPC, "FlowerSeller", (vp[0] + ox * 760 + px * -480, vp[1] + oy * 760 + py * -480, vp[2] + 100),
                          (0, 0, 0), tag, folder)
    girl.set_editor_property("style", unreal.HGTouristStyle.LOCAL)
    girl.set_editor_property("look_at_point", hg.vec(centre))


def build_river_traffic(layout, tag, folder):
    """Ta Lang: green-roofed floating pontoons, moored tourist boats, and boats cruising the gorge."""
    dock = layout["spawns"]["dock"]
    dx, dy, _ = dock["location_cm"]
    yaw = dock["yaw"]
    fx, fy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    sx, sy = -fy, fx                                    # along the bank
    water_z = layout["water"]["surface_z_cm"]
    deck = hg.color_material("MI_GB_PontoonDeck", (0.18, 0.16, 0.14))
    roof_green = hg.color_material("MI_GB_RoofGreen", (0.05, 0.25, 0.12))
    for i in range(3):
        along = (i - 1) * 2600.0
        cx, cy = dx + fx * 3000 + sx * along, dy + fy * 3000 + sy * along
        ryaw = yaw + 90
        hg.box(f"Pontoon_{i}", (cx, cy, water_z + 30), (1800, 900, 60), yaw=ryaw, material=deck, tag=tag, folder=folder)
        hg.box(f"PontoonRoof_{i}", (cx, cy, water_z + 380), (1600, 800, 20), yaw=ryaw, material=roof_green, tag=tag,
               folder=folder, collision=False)
        for k, (ox_, oy_) in enumerate(((-700, -350), (700, -350), (-700, 350), (700, 350))):
            r = math.radians(ryaw)
            px_ = cx + ox_ * math.cos(r) - oy_ * math.sin(r)
            py_ = cy + ox_ * math.sin(r) + oy_ * math.cos(r)
            hg.spawn_mesh(f"PontoonPost_{i}_{k}", hg.CYLINDER, (px_, py_, water_z + 205), scale=(0.15, 0.15, 3.5),
                          material=deck, tag=tag, folder=folder, collision=False)
    # Moored boats, side by side off the pontoons.
    for i in range(6):
        along = (i - 2.5) * 420.0
        bx, by = dx + fx * 4800 + sx * along, dy + fy * 4800 + sy * along
        boat = hg.spawn_actor(unreal.NhoQueBoatPawn, f"Boat_Moored_{i}", (bx, by, water_z), (0, yaw, 0), tag, folder)
        boat.set_editor_property("boat_type", unreal.HGBoatType.CANO)
        rerun(boat)
    # Cruising boats ping-pong along the river through the gorge.
    river = layout["water"]["river_cm"]
    pts = [unreal.Vector(p[0], p[1], water_z) for p in river[::8]]
    n = len(pts)
    for i, start in enumerate((int(n * 0.25), int(n * 0.45), int(n * 0.62))):
        p0 = pts[start]
        boat = hg.spawn_actor(unreal.NhoQueBoatPawn, f"Boat_Cruise_{i}", (p0.x, p0.y, water_z), (0, 90, 0), tag, folder)
        boat.set_editor_property("boat_type", unreal.HGBoatType.CANO)
        boat.set_editor_property("cruise_points", pts)
        boat.set_editor_property("auto_cruise", True)
        boat.set_editor_property("cruise_throttle", 0.45 + 0.1 * i)
        rerun(boat)



def build_set_dressing(scatter, meshes, mats, tag, folder):
    """Reference-photo dressing: distant blue ranges, chevrons on the bends, utility poles, hamlets."""
    ranges = hg.spawn_mesh("DistantRanges", "/Game/HaGiang/Generated/Meshes/SM_HG_DistantRanges.SM_HG_DistantRanges",
                           (0, 0, 60000), material=mats["ranges"], tag=tag, folder=folder, collision=False)
    ranges.static_mesh_component.set_editor_property("cast_shadow", False)
    origin, extent = ranges.get_actor_bounds(False)
    hg.log(f"DistantRanges bounds: origin {origin} extent {extent}")
    spawn_scatter("Scatter_chevrons", meshes["SM_HG_Chevron"], mats["chevron"], scatter.get("chevrons", []), tag, folder,
                  cull=(20000.0, 30000.0), collision=True, extra_tags=("RoadPosts",))
    spawn_scatter("Scatter_poles", meshes["SM_HG_UtilityPole"], mats["pole"], scatter.get("poles", []), tag, folder,
                  cull=(50000.0, 70000.0), collision=True)
    for colour, items in scatter.get("houses", {}).items():
        spawn_scatter(f"Scatter_houses_{colour}_walls", meshes["SM_HG_HouseWalls"], mats["house_wall"], items, tag, folder,
                      cull=(0.0, 0.0), collision=True)
        spawn_scatter(f"Scatter_houses_{colour}_roofs", meshes["SM_HG_HouseRoof"], mats[f"roof_{colour}"], items, tag, folder,
                      cull=(0.0, 0.0), collision=False)
