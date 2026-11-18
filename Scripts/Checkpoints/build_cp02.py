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
from hg_lib import world  # noqa: E402

importlib.reload(hg)
importlib.reload(world)

LEVEL_PATH = "/Game/Maps/L_HaGiang_CP02"
TAG = "HG_CP02"
GAME_MODE = "/Script/HaGiangGTA.HGOpenWorldGameMode"


def build():
    world.require_cpp("HGLandscapeBuilder", "HGRoadSplineActor", "HGWaterBody", "HGBikePawn", "NhoQueBoatPawn")
    with unreal.ScopedSlowTask(8, "Building HaGiang CP02") as task:
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
        world.build_landscape(layout, TAG, "CP02/Landscape")

        task.enter_progress_frame(1, "Road spline")
        world.build_road(layout, mat_asphalt, TAG, "CP02/Road")

        task.enter_progress_frame(1, "River + dock + viewpoints + vehicles")
        world.build_water(layout, mat_water, TAG, "CP02/Water")
        world.build_dock(layout, mat_wood, TAG, "CP02/Dock")
        world.build_checkin_rings(layout, mat_ring, TAG, "CP02/Viewpoints")
        world.build_vehicles(layout, TAG, "CP02/Vehicles")

        task.enter_progress_frame(1, "Sky + PlayerStart")
        hg.spawn_basic_sky(TAG, folder="CP02/Lighting", sun_pitch=-22.0, sun_yaw=200.0,
                           sun_lux=10.0, fog_density=0.0025)
        for a in world.find_tagged(TAG, unreal.ExponentialHeightFog):
            a.set_actor_location(unreal.Vector(0, 0, 70000), False, False)
        world.build_player_start(layout, TAG, "CP02/Gameplay")

        task.enter_progress_frame(1, "World settings + save")
        hg.set_world_game_mode(GAME_MODE)
        hg.editor_world().get_world_settings().set_editor_property("kill_z", 60000.0)
        hg.save_current_level()

    hg.log("CP02 built. Play: WASD walk, F ride the Honda Win, drive to Ta Lang dock, F to take the cano/kayak.")


build()
