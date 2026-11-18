"""Checkpoint 03 - 6 h: three photoreal viewpoints.

Rebuilds /Game/Maps/L_HaGiang_CP03 = CP02 world + photoreal pass:
  * procedural assets (gen_assets.py): limestone/water normal maps, breakup noise,
    Nanite karst cliffs & boulders, shrubs, road post mesh, Hmong flute WAV
  * node-built materials: auto-slope karst limestone + moss landscape, dew-wet asphalt
    with glossy puddles, SingleLayerWater emerald Nho Que, fluted limestone rock, red/white posts
  * Nanite landscape, HISM scatter (gen_scatter.py): ~12k canyon cliffs, cone summits,
    boulders, ~55k shrubs, ~900 road posts on the valley edge, Mom Da Tu Than rocks
  * golden hour: low warm sun with light shafts, sea of clouds 750-1100 m,
    valley + volumetric fog, filmic post-process
  * AHGViewpointActor at Doc Tham Ma / Ma Pi Leng / Tu San: press [V] for the orbiting
    CineCamera flycam with name + altitude card and the flute theme

Run inside the Unreal Editor (Tools > Execute Python Script). Safe to re-run.
"""
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

from hg_lib import assets, materials, world  # noqa: E402
from hg_lib import editor_utils as hg  # noqa: E402

for _m in (hg, world, assets, materials):
    importlib.reload(_m)

LEVEL_PATH = "/Game/Maps/L_HaGiang_CP03"
TAG = "HG_CP03"
GAME_MODE = "/Script/HaGiangGTA.HGOpenWorldGameMode"


def build():
    world.require_cpp("HGLandscapeBuilder", "HGRoadSplineActor", "HGWaterBody", "HGBikePawn", "NhoQueBoatPawn",
                      "HGInstancedScatterActor", "HGViewpointActor")
    with unreal.ScopedSlowTask(10, "Building HaGiang CP03 (photoreal)") as task:
        task.make_dialog(True)

        task.enter_progress_frame(1, "Generated data (heightmap, assets, scatter)")
        layout = hg.ensure_generated_heightmap()
        assets.ensure_generated()
        scatter = world.load_scatter()

        task.enter_progress_frame(1, "Importing textures, Nanite meshes, flute")
        tex = assets.import_textures()
        meshes = assets.import_meshes()
        flute = assets.import_flute()

        task.enter_progress_frame(1, "Building materials")
        mats = materials.build_all(tex)
        mat_ring = hg.color_material("MI_GB_CheckIn", (1.0, 0.75, 0.05))
        mat_wood = hg.color_material("MI_GB_Wood", (0.25, 0.14, 0.06))

        task.enter_progress_frame(1, "Opening level")
        hg.open_or_create_level(LEVEL_PATH)
        hg.clear_tagged(TAG)

        task.enter_progress_frame(1, "Importing Nanite landscape (slow)")
        landscape = world.build_landscape(layout, TAG, "CP03/Landscape", mats["terrain"])
        try:
            landscape.set_editor_property("enable_nanite", True)
        except Exception as exc:
            hg.warn(f"Nanite landscape: {exc} (enable it in the Landscape details panel)")

        task.enter_progress_frame(1, "Road, river, dock, vehicles")
        world.build_road(layout, mats["asphalt"], TAG, "CP03/Road")
        world.build_water(layout, mats["water"], TAG, "CP03/Water")
        world.build_dock(layout, mat_wood, TAG, "CP03/Dock")
        world.build_checkin_rings(layout, mat_ring, TAG, "CP03/Viewpoints")
        world.build_vehicles(layout, TAG, "CP03/Vehicles")

        task.enter_progress_frame(1, "Scatter: cliffs, summits, boulders, shrubs, posts")
        world.build_scatter(scatter, meshes, mats, TAG, "CP03/Scatter")

        task.enter_progress_frame(1, "Golden hour atmosphere")
        world.build_golden_hour(TAG, "CP03/Lighting")

        task.enter_progress_frame(1, "Viewpoints + PlayerStart")
        world.build_viewpoints(scatter, flute, TAG, "CP03/Viewpoints")
        world.build_player_start(layout, TAG, "CP03/Gameplay")

        task.enter_progress_frame(1, "World settings + save")
        hg.set_world_game_mode(GAME_MODE)
        hg.editor_world().get_world_settings().set_editor_property("kill_z", 60000.0)
        hg.save_current_level()

    hg.log("CP03 built. Ride to a check-in ring and press V for the flycam.")


build()
