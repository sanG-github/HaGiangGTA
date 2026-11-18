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

from hg_lib import assets, materials, scenes, world  # noqa: E402
from hg_lib import editor_utils as hg  # noqa: E402

for _m in (hg, world, assets, materials, scenes):
    importlib.reload(_m)

LEVEL_PATH = "/Game/Maps/L_HaGiang_CP03"
TAG = "HG_CP03"


def build():
    with unreal.ScopedSlowTask(scenes.PHOTOREAL_STEPS + 1, "Building HaGiang CP03 (photoreal)") as task:
        task.make_dialog(True)
        scenes.build_photoreal_world(LEVEL_PATH, TAG, "CP03", task)
        task.enter_progress_frame(1, "World settings + save")
        scenes.finish_level()
    hg.log("CP03 built. Ride to a check-in ring and press V for the flycam.")


build()
