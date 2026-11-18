"""Checkpoint 02 - 1 h: real heightmap + Honda Win + Nho Que boats.

Rebuilds /Game/Maps/L_HaGiang_CP02 from code + generated data:
  * runs Scripts/Tools/gen_heightmap.py if Generated/Heightmaps is missing
    (4033 x 4033 16-bit heightmap: Doc Tham Ma 9 hairpins -> Ma Pi Leng rim
    road + Mom Da Tu Than -> Ta Lang descent to the Nho Que river)
  * imports the landscape through C++ (UHGLandscapeBuilder, 32x32 components)
  * AHGRoadSplineActor along the carved centreline (asphalt spline meshes)
  * AHGWaterBody for the river surface (700 m)
  * Ta Lang dock, check-in rings at the 3 viewpoints
  * Honda Win bikes (AHGBikePawn), cano + kayak (ANhoQueBoatPawn)
  * PlayerStart + GameMode AHGOpenWorldGameMode (on-foot player, [F] to ride)

Run inside the Unreal Editor (Tools > Execute Python Script). Safe to re-run.
The landscape import takes ~1 minute.
"""
import math
import os
import sys

import unreal


def _add_scripts_path() -> None:
    candidates = []
    here = globals().get("__file__")
    if here:
        candidates.append(os.path.dirname(os.path.dirname(os.path.abspath(here))))
    project_dir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    candidates.append(os.path.join(project_dir, "Scripts"))
    for path in candidates:
        if os.path.isdir(os.path.join(path, "hg_lib")):
            if path not in sys.path:
                sys.path.insert(0, path)
            return
    raise RuntimeError("Could not locate the project's Scripts/ folder")


_add_scripts_path()

import importlib  # noqa: E402

from hg_lib import editor_utils as hg  # noqa: E402

importlib.reload(hg)  # pick up edits when re-running in the same editor session

LEVEL_PATH = "/Game/Maps/L_HaGiang_CP02"
TAG = "HG_CP02"
GAME_MODE = "/Script/HaGiangGTA.HGOpenWorldGameMode"
ROAD_MESH_OFFSET_CM = -11.0   # landscape is carved 15 cm under the road profile; asphalt top 4 cm above it


def _rerun(actor):
    """set_editor_property already re-runs construction in the editor; call it explicitly when exposed."""
    fn = getattr(actor, "rerun_construction_scripts", None)
    if callable(fn):
        fn()


def _require_cpp():
    missing = [n for n in ("HGLandscapeBuilder", "HGRoadSplineActor", "HGWaterBody", "HGBikePawn", "NhoQueBoatPawn")
               if not hasattr(unreal, n)]
    if missing:
        raise RuntimeError(f"C++ classes not loaded: {missing}. Compile HaGiangGTA + HaGiangGTAEditor first.")


def build_landscape(layout):
    hm = layout["heightmap"]
    landscape = unreal.HGLandscapeBuilder.import_landscape_from_raw16(
        layout["_r16_abs"], hm["size"], hg.vec(hm["location_cm"]), hg.vec(hm["scale"]),
        hm["section_size_quads"], hm["sections_per_component"], None, TAG)
    if landscape is None:
        raise RuntimeError("Landscape import failed - see Output Log (LogHGLandscapeBuilder).")
    landscape.set_folder_path("CP02/Landscape")
    return landscape


def build_water(layout, mat_water):
    water = layout["water"]
    actor = hg.spawn_actor(unreal.HGWaterBody, "Water_NhoQue", (0, 0, water["surface_z_cm"]), (0, 0, 0),
                           TAG, "CP02/Water")
    actor.set_editor_property("water_material", mat_water)
    actor.set_editor_property("size_cm", unreal.Vector2D(403200.0, 403200.0))
    _rerun(actor)
    return actor


def build_road(layout, mat_asphalt):
    road = layout["road"]
    actor = hg.spawn_actor(unreal.HGRoadSplineActor, "Road_MaPiLeng", (0, 0, 0), (0, 0, 0), TAG, "CP02/Road")
    actor.set_editor_property("road_material", mat_asphalt)
    actor.set_editor_property("road_width", road["half_width_cm"] * 2.0)
    pts = [unreal.Vector(p[0], p[1], p[2] + ROAD_MESH_OFFSET_CM) for p in road["points_cm"]]
    actor.set_road_points(pts)
    actor.rebuild_road_meshes()
    hg.log(f"Road: {len(pts)} points, {road['length_m'] / 1000:.2f} km")
    return actor


def build_viewpoints(layout, mat_ring, mat_wood):
    for key, vp in layout["viewpoints"].items():
        x, y, z = vp["location_cm"]
        hg.spawn_mesh(f"CheckIn_{key}", hg.CYLINDER, (x, y, z + 5), scale=(10.0, 10.0, 0.04),
                      material=mat_ring, tag=TAG, folder="CP02/Viewpoints", collision=False)
        hg.log(f"Viewpoint {vp['name']} @ {vp['altitude_m']} m")

    # Ta Lang wooden pier: from the dock pad out over the water.
    dock = layout["spawns"]["dock"]
    dx, dy, dz = dock["location_cm"]
    yaw = dock["yaw"]
    fx, fy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    length = 2200.0
    cx, cy = dx + fx * length * 0.5, dy + fy * length * 0.5
    deck_top = layout["water"]["surface_z_cm"] + 90.0
    hg.box("Dock_TaLang_Deck", (cx, cy, deck_top - 10), (length, 400, 20), yaw=yaw,
           material=mat_wood, tag=TAG, folder="CP02/Dock")
    for i in range(6):
        t = (i + 0.5) / 6.0
        px, py = dx + fx * length * t, dy + fy * length * t
        for side in (-1, 1):
            ox, oy = -fy * 180 * side, fx * 180 * side
            hg.spawn_mesh(f"Dock_Pile_{i}_{'L' if side < 0 else 'R'}", hg.CYLINDER,
                          (px + ox, py + oy, deck_top - 260), scale=(0.3, 0.3, 5.0),
                          material=mat_wood, tag=TAG, folder="CP02/Dock")


def build_vehicles(layout):
    spawns = layout["spawns"]
    colors = [unreal.LinearColor(0.55, 0.02, 0.02, 1), unreal.LinearColor(0.05, 0.08, 0.35, 1),
              unreal.LinearColor(0.08, 0.08, 0.08, 1)]
    for i, bike in enumerate(spawns["bikes"]):
        actor = hg.spawn_actor(unreal.HGBikePawn, f"HondaWin_{i:02d}", bike["location_cm"], (0, bike["yaw"], 0),
                               TAG, "CP02/Vehicles")
        actor.set_editor_property("body_color", colors[i % len(colors)])

    for i, boat in enumerate(spawns["boats"]):
        x, y, z = boat["location_cm"]
        actor = hg.spawn_actor(unreal.NhoQueBoatPawn, f"Boat_{boat['type']}_{i:02d}", (x, y, z), (0, spawns["dock"]["yaw"] + 90, 0),
                               TAG, "CP02/Vehicles")
        boat_type = unreal.HGBoatType.CANO if boat["type"] == "Cano" else unreal.HGBoatType.KAYAK
        actor.set_editor_property("boat_type", boat_type)
        _rerun(actor)


def build():
    _require_cpp()
    with unreal.ScopedSlowTask(9, "Building HaGiang CP02") as task:
        task.make_dialog(True)

        task.enter_progress_frame(1, "Generated heightmap data")
        layout = hg.ensure_generated_heightmap()

        task.enter_progress_frame(1, "Opening level")
        hg.open_or_create_level(LEVEL_PATH)
        hg.clear_tagged(TAG)

        task.enter_progress_frame(1, "Materials")
        mat_asphalt = hg.color_material("MI_GB_Asphalt", (0.035, 0.035, 0.04))
        mat_water = hg.color_material("MI_GB_Water", (0.02, 0.32, 0.27))
        mat_ring = hg.color_material("MI_GB_CheckIn", (1.0, 0.75, 0.05))
        mat_wood = hg.color_material("MI_GB_Wood", (0.25, 0.14, 0.06))

        task.enter_progress_frame(1, "Importing 4033x4033 landscape (slow)")
        build_landscape(layout)

        task.enter_progress_frame(1, "Road spline")
        build_road(layout, mat_asphalt)

        task.enter_progress_frame(1, "River + dock + viewpoints")
        build_water(layout, mat_water)
        build_viewpoints(layout, mat_ring, mat_wood)

        task.enter_progress_frame(1, "Vehicles")
        build_vehicles(layout)

        task.enter_progress_frame(1, "Sky + PlayerStart")
        sun = hg.spawn_basic_sky(TAG, folder="CP02/Lighting", sun_pitch=-22.0, sun_yaw=200.0,
                                 sun_lux=10.0, fog_density=0.0025)
        for a in hg.actor_subsystem().get_all_level_actors():
            if isinstance(a, unreal.ExponentialHeightFog) and unreal.Name(TAG) in a.tags:
                a.set_actor_location(unreal.Vector(0, 0, 70000), False, False)
        ps = layout["spawns"]["player_start"]
        hg.spawn_actor(unreal.PlayerStart, "PlayerStart_CP02", ps["location_cm"], (0, ps["yaw"], 0),
                       TAG, "CP02/Gameplay")

        task.enter_progress_frame(1, "World settings + save")
        hg.set_world_game_mode(GAME_MODE)
        hg.editor_world().get_world_settings().set_editor_property("kill_z", 60000.0)
        hg.save_current_level()

    hg.log("CP02 built. Play: WASD walk, F ride the Honda Win, drive to Ta Lang dock, F to take the cano/kayak.")


build()
