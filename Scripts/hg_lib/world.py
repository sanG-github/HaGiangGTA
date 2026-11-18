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


def build_road(layout, material, tag, folder):
    road = layout["road"]
    actor = hg.spawn_actor(unreal.HGRoadSplineActor, "Road_MaPiLeng", (0, 0, 0), (0, 0, 0), tag, folder)
    actor.set_editor_property("road_material", material)
    actor.set_editor_property("road_width", road["half_width_cm"] * 2.0)
    pts = [unreal.Vector(p[0], p[1], p[2] + ROAD_MESH_OFFSET_CM) for p in road["points_cm"]]
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
        hg.spawn_mesh(f"CheckIn_{key}", hg.CYLINDER, (x, y, z + 5), scale=(10.0, 10.0, 0.04),
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
    cliff_meshes = [meshes[f"SM_HG_KarstCliff_0{i}"] for i in range(1, 5)]
    for name, cull in (("cliffs", (0.0, 0.0)), ("summits", (0.0, 0.0))):
        items = scatter[name]
        for v, mesh in enumerate(cliff_meshes):
            spawn_scatter(f"Scatter_{name}_{v + 1}", mesh, mats["rock"], items[v::4], tag, folder, cull)
    spawn_scatter("Scatter_spur", cliff_meshes[0], mats["rock"], scatter["spur"], tag, folder)
    boulders = [meshes[f"SM_HG_Boulder_0{i}"] for i in range(1, 4)]
    for v, mesh in enumerate(boulders):
        spawn_scatter(f"Scatter_boulders_{v + 1}", mesh, mats["rock"], scatter["boulders"][v::3], tag, folder,
                      cull=(45000.0, 60000.0))
    bushes = [meshes["SM_HG_Bush_01"], meshes["SM_HG_Bush_02"]]
    for v, mesh in enumerate(bushes):
        spawn_scatter(f"Scatter_bushes_{v + 1}", mesh, mats["bush"], scatter["bushes"][v::2], tag, folder,
                      cull=(18000.0, 26000.0), collision=False, shadows=True)
    spawn_scatter("Scatter_road_posts", meshes["SM_HG_RoadPost"], mats["post"], scatter["posts"], tag, folder,
                  cull=(15000.0, 22000.0), collision=True, shadows=True, extra_tags=("RoadPosts",))
    counts = {k: len(v) for k, v in scatter.items() if isinstance(v, list)}
    hg.log(f"Scatter instances: {counts}")


def build_golden_hour(tag, folder):
    """Late-afternoon sun from the south-east (13 deg), raking along the N-S Tu San canyon so the
    light shafts run down the gorge, lighting the east-facing Ma Pi Leng rim, the spur and the
    south-facing Tham Ma hairpins. Valley mist (height fog) pools below ~950 m - the 'sea of
    clouds' seen from the pass - while the VolumetricCloud layer sits above the peaks.
    (Composition checked with Scripts/Tools/render_viewpoints.py.)"""
    sun = hg.spawn_actor(unreal.DirectionalLight, "Sun_GoldenHour", (0, 0, 200000), (-13.0, 120.0, 0), tag, folder)
    sc = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sc.set_mobility(unreal.ComponentMobility.MOVABLE)
    for k, v in (("intensity", 8.0), ("use_temperature", True), ("temperature", 4100.0),
                 ("atmosphere_sun_light", True), ("cast_cloud_shadows", True),
                 ("enable_light_shaft_bloom", True), ("enable_light_shaft_occlusion", True),
                 ("bloom_scale", 0.25), ("volumetric_scattering_intensity", 1.6)):
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

    cloud = hg.spawn_actor(unreal.VolumetricCloud, "SeaOfClouds", (0, 0, 0), (0, 0, 0), tag, folder)
    cc = cloud.get_component_by_class(unreal.VolumetricCloudComponent)
    cloud_mat = unreal.EditorAssetLibrary.load_asset("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst")
    if cloud_mat:
        cc.set_editor_property("material", cloud_mat)
    else:
        hg.warn("Engine volumetric cloud material not found; using component default.")
    cc.set_editor_property("layer_bottom_altitude", 1.9)    # km above sea level (world Z 0): above the peaks
    cc.set_editor_property("layer_height", 2.5)

    # Fog actor at 820 m with a steep falloff: dense mist in the valleys and the gorge,
    # clear air at the 1200 m road -> looking down from the pass reads as a sea of clouds.
    fog = hg.spawn_actor(unreal.ExponentialHeightFog, "ValleyMist", (0, 0, 82000), (0, 0, 0), tag, folder)
    fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    # UE falloff is per 1000 uu: 0.08 -> density e-folds every ~125 m above the fog actor.
    for k, v in (("fog_density", 0.05), ("fog_height_falloff", 0.08),
                 ("fog_inscattering_luminance", unreal.LinearColor(0.45, 0.42, 0.40, 1.0)),
                 ("directional_inscattering_luminance", unreal.LinearColor(1.0, 0.62, 0.32, 1.0)),
                 ("directional_inscattering_exponent", 6.0),
                 ("enable_volumetric_fog", True), ("volumetric_fog_scattering_distribution", 0.65),
                 ("volumetric_fog_extinction_scale", 1.2), ("volumetric_fog_distance", 25000.0),
                 ("fog_max_opacity", 0.92), ("start_distance", 300.0)):
        try:
            fc.set_editor_property(k, v)
        except Exception as exc:
            hg.warn(f"Fog.{k}: {exc}")

    ppv = hg.spawn_actor(unreal.PostProcessVolume, "PP_GoldenHour", (0, 0, 100000), (0, 0, 0), tag, folder)
    ppv.set_editor_property("unbound", True)
    s = ppv.get_editor_property("settings")
    for k, v in (("bloom_intensity", 0.55), ("vignette_intensity", 0.32), ("film_grain_intensity", 0.12),
                 ("auto_exposure_min_brightness", 0.6), ("auto_exposure_max_brightness", 2.5),
                 ("white_temp", 6100.0), ("color_saturation", unreal.Vector4(1.06, 1.04, 1.0, 1.0)),
                 ("color_contrast", unreal.Vector4(1.05, 1.05, 1.05, 1.0)),
                 ("color_gain_shadows", unreal.Vector4(0.92, 0.98, 1.06, 1.0)),
                 ("color_gain_highlights", unreal.Vector4(1.06, 1.0, 0.92, 1.0)),
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
