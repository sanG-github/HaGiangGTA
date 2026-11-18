"""Whole-scene recipes reused by later checkpoints (CP03 photoreal world is the base of CP04/CP05)."""
from __future__ import annotations

import unreal

from . import assets, materials, world
from . import editor_utils as hg

GAME_MODE = "/Script/HaGiangGTA.HGOpenWorldGameMode"


def build_photoreal_world(level_path: str, tag: str, prefix: str, task=None) -> dict:
    """CP03 scene: Nanite landscape, wet road, emerald river, Nanite karst scatter, golden hour,
    viewpoints with flycam. Returns handles the caller can build on."""
    def step(msg):
        if task is not None:
            task.enter_progress_frame(1, msg)
        hg.log(msg)

    world.require_cpp("HGLandscapeBuilder", "HGRoadSplineActor", "HGWaterBody", "HGBikePawn", "NhoQueBoatPawn",
                      "HGInstancedScatterActor", "HGViewpointActor")

    step("Generated data (heightmap, assets, scatter)")
    layout = hg.ensure_generated_heightmap()
    assets.ensure_generated()
    scatter = world.load_scatter()

    step("Importing textures, Nanite meshes, audio")
    tex = assets.import_textures()
    meshes = assets.import_meshes()
    flute = assets.import_flute()

    step("Building materials")
    mats = materials.build_all(tex)
    mat_ring = hg.color_material("MI_GB_CheckIn", (1.0, 0.75, 0.05))
    mat_wood = hg.color_material("MI_GB_Wood", (0.25, 0.14, 0.06))

    step("Opening level")
    hg.open_or_create_level(level_path)
    hg.clear_tagged(tag)

    step("Importing Nanite landscape (slow)")
    landscape = world.build_landscape(layout, tag, f"{prefix}/Landscape", mats["terrain"])
    try:
        landscape.set_editor_property("enable_nanite", True)
    except Exception as exc:
        hg.warn(f"Nanite landscape: {exc} (enable it in the Landscape details panel)")

    step("Road, river, dock, vehicles")
    road = world.build_road(layout, mats["asphalt"], tag, f"{prefix}/Road")
    world.build_water(layout, mats["water"], tag, f"{prefix}/Water")
    world.build_dock(layout, mat_wood, tag, f"{prefix}/Dock")
    world.build_checkin_rings(layout, mat_ring, tag, f"{prefix}/Viewpoints")
    world.build_vehicles(layout, tag, f"{prefix}/Vehicles")

    step("Scatter: cliffs, summits, boulders, shrubs, posts")
    world.build_scatter(scatter, meshes, mats, tag, f"{prefix}/Scatter")

    step("Golden hour atmosphere")
    world.build_golden_hour(tag, f"{prefix}/Lighting")

    step("Viewpoints + PlayerStart")
    world.build_viewpoints(scatter, flute, tag, f"{prefix}/Viewpoints")
    world.build_player_start(layout, tag, f"{prefix}/Gameplay")

    return {"layout": layout, "scatter": scatter, "road": road, "meshes": meshes, "mats": mats}


PHOTOREAL_STEPS = 9


def finish_level(game_mode: str = GAME_MODE) -> None:
    hg.set_world_game_mode(game_mode)
    hg.editor_world().get_world_settings().set_editor_property("kill_z", 60000.0)
    hg.save_current_level()
