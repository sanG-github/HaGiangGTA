"""Checkpoint 01 - 10 min greybox.

Rebuilds /Game/Maps/L_HaGiang_CP01 from scratch:
  * 4 km x 4 km white grid ground plane (matches the final 4033x4033 landscape footprint)
  * 300 m approach road, a 200 m white box ramp ("Doc Tham Ma" stand-in, +30 m)
    with red/white posts (coc tieu) on both edges, and a 100 m summit platform
    with a check-in ring placeholder
  * a small kicker ramp beside the road to test airborne physics
  * basic Lumen-friendly sky (sun, SkyAtmosphere, realtime SkyLight, height fog)
  * PlayerStart + World Settings GameMode = AHGGreyboxGameMode (C++ box car)

Run inside the Unreal Editor: Tools > Execute Python Script... > this file.
Safe to re-run: everything tagged HG_CP01 is deleted and rebuilt.
"""
import os
import sys

import unreal


def _add_scripts_path() -> None:
    """Make Scripts/ importable however this file is launched (menu, -ExecutePythonScript, exec())."""
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

from hg_lib import editor_utils as hg  # noqa: E402

LEVEL_PATH = "/Game/Maps/L_HaGiang_CP01"
TAG = "HG_CP01"
GAME_MODE = "/Script/HaGiangGTA.HGGreyboxGameMode"

# Layout (cm). +X is "up the pass".
MAP_SIZE = 400_000.0          # 4 km
ROAD_WIDTH = 1_200.0          # 12 m greybox road (real road ~6 m; wide for testing)
APPROACH_LEN = 30_000.0       # 300 m
RAMP_LEN = 20_000.0           # 200 m
RAMP_RISE = 3_000.0           # 30 m  (~8.5 deg)
SUMMIT_LEN = 10_000.0         # 100 m
SUMMIT_WIDTH = 3_000.0
POST_SPACING = 1_000.0        # one post every 10 m


def build():
    with unreal.ScopedSlowTask(8, "Building HaGiang CP01 greybox") as task:
        task.make_dialog(True)

        task.enter_progress_frame(1, "Opening level")
        hg.open_or_create_level(LEVEL_PATH)
        hg.clear_tagged(TAG)

        task.enter_progress_frame(1, "Materials")
        grid = hg.load_material(hg.WORLD_GRID_MATERIAL)
        mat_road = hg.color_material("MI_GB_Road", (0.08, 0.08, 0.09))
        mat_white = hg.color_material("MI_GB_White", (0.9, 0.9, 0.9))
        mat_red = hg.color_material("MI_GB_Red", (0.8, 0.03, 0.03))
        mat_ring = hg.color_material("MI_GB_CheckIn", (1.0, 0.75, 0.05))

        task.enter_progress_frame(1, "Ground")
        hg.box("Ground_4km", (0, 0, -50), (MAP_SIZE, MAP_SIZE, 100), material=grid,
               tag=TAG, folder="CP01/Ground")

        task.enter_progress_frame(1, "Road + ramp")
        # Approach road: thin dark strip, top at z = 2 cm (car steps onto it).
        hg.box("Road_Approach", (APPROACH_LEN * 0.5, 0, -8), (APPROACH_LEN, ROAD_WIDTH, 20),
               material=mat_road, tag=TAG, folder="CP01/Road")

        ramp_start_x = APPROACH_LEN
        hg.ramp("Ramp_DocThamMa", (ramp_start_x, 0, 2), RAMP_LEN, RAMP_RISE, ROAD_WIDTH,
                thickness_cm=80, material=mat_white, tag=TAG, folder="CP01/Road")

        summit_x0 = ramp_start_x + RAMP_LEN
        summit_top = 2 + RAMP_RISE
        hg.box("Summit_Platform", (summit_x0 + SUMMIT_LEN * 0.5, 0, summit_top * 0.5),
               (SUMMIT_LEN, SUMMIT_WIDTH, summit_top), material=mat_white, tag=TAG, folder="CP01/Road")

        task.enter_progress_frame(1, "Posts (coc tieu)")
        n_posts = int(RAMP_LEN // POST_SPACING) + 1
        for i in range(n_posts):
            x = ramp_start_x + i * POST_SPACING
            surface_z = 2 + RAMP_RISE * (i * POST_SPACING) / RAMP_LEN
            mat = mat_red if i % 2 == 0 else mat_white
            for side, sy in (("L", -1), ("R", 1)):
                hg.box(f"Post_{side}_{i:02d}", (x, sy * (ROAD_WIDTH * 0.5 - 40), surface_z + 50),
                       (25, 25, 100), material=mat, tag=TAG, folder="CP01/Posts")

        task.enter_progress_frame(1, "Check-in ring + kicker")
        ring_x = summit_x0 + SUMMIT_LEN * 0.6
        hg.spawn_mesh("CheckIn_Ring", hg.CYLINDER, (ring_x, 0, summit_top + 3),
                      scale=(8.0, 8.0, 0.04), material=mat_ring, tag=TAG,
                      folder="CP01/Viewpoint", collision=False)
        # Edge barrier at the far end of the summit so you don't drive off by accident.
        hg.box("Summit_Barrier", (summit_x0 + SUMMIT_LEN - 50, 0, summit_top + 60),
               (100, SUMMIT_WIDTH, 120), material=mat_red, tag=TAG, folder="CP01/Viewpoint")
        # Kicker beside the approach road (y = +25 m) to test jumps.
        hg.ramp("Kicker_Jump", (12_000, 2_500, 0), 1_500, 300, 600, thickness_cm=40,
                material=mat_red, tag=TAG, folder="CP01/Test")

        task.enter_progress_frame(1, "Sky + PlayerStart")
        hg.spawn_basic_sky(TAG, folder="CP01/Lighting", sun_pitch=-28.0, sun_yaw=150.0)
        hg.spawn_actor(unreal.PlayerStart, "PlayerStart_CP01", (500, 0, 150), (0, 0, 0),
                       TAG, "CP01/Gameplay")

        task.enter_progress_frame(1, "World settings + save")
        hg.set_world_game_mode(GAME_MODE)
        settings = hg.editor_world().get_world_settings()
        settings.set_editor_property("kill_z", -50_000.0)
        hg.save_current_level()

    hg.log("CP01 greybox built. Press Play: W/S drive, A/D steer, Shift boost, Space handbrake, R reset.")


build()
