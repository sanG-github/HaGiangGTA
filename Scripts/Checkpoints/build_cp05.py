"""Checkpoint 05 - 24 h: full GTA loop with a 5-star wanted level.

Rebuilds /Game/Maps/L_HaGiang_CP05 = CP04 living world +
  * AHGGtaGameMode / AHGGtaHUD: C++ UMG HUD - money (500.000 VND), 5 wanted stars, GPS minimap of the
    pass road with convoys / trucks / police / viewpoints, speedometer, GTA location cards with the
    Vietnamese names of the 3 viewpoints, flycam letterbox, BUSTED / Happy Water banners
  * UHGWantedSubsystem: hitting road posts, tour convoys, trucks, cows or tourists raises 1..5 stars ->
    siren patrol bikes (AHGPoliceBikePawn), from 3 stars a helicopter (AHGPoliceHelicopter) with a
    volumetric searchlight hunting you along the Ma Pi Leng rim and through the Tu San canyon
  * looping engine sounds (bike / truck / boat), siren and rotor; all one-shots spatialized in C++

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

from hg_lib import assets, materials, scenes, world  # noqa: E402
from hg_lib import editor_utils as hg  # noqa: E402

for _m in (hg, world, assets, materials, scenes):
    importlib.reload(_m)

LEVEL_PATH = "/Game/Maps/L_HaGiang_CP05"
TAG = "HG_CP05"
PREFIX = "CP05"
GAME_MODE = "/Script/HaGiangGTA.HGGtaGameMode"
VIEWPOINT_NAMES = {
    "doc_tham_ma": "DỐC THẨM MÃ",
    "ma_pi_leng": "ĐÈO MÃ PÍ LÈNG · MỎM ĐÁ TỬ THẦN",
    "tu_san": "HẺM VỰC TU SẢN · SÔNG NHO QUẾ",
}


def build():
    with unreal.ScopedSlowTask(scenes.PHOTOREAL_STEPS + 3, "Building HaGiang CP05 (full GTA)") as task:
        task.make_dialog(True)
        world.require_cpp("HGGtaGameMode", "HGPoliceBikePawn", "HGPoliceHelicopter")
        base = scenes.build_photoreal_world(LEVEL_PATH, TAG, PREFIX, task, VIEWPOINT_NAMES)
        task.enter_progress_frame(1, "Traffic, crowds, cows, Happy Water")
        scenes.build_living_world(base, TAG, PREFIX)
        task.enter_progress_frame(1, "Engine / siren / rotor loops")
        assets.import_loops()
        task.enter_progress_frame(1, "World settings + save")
        scenes.finish_level(GAME_MODE)
    hg.log("CP05 built. Hit a road post or a convoy to get stars; lose the police and the helicopter in the canyon.")


build()
