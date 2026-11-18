"""Checkpoint 04 - 12 h: the living pass.

Rebuilds /Game/Maps/L_HaGiang_CP04 = CP03 photoreal world +
  * AHGTrafficSpawner: 3 Easy Rider tour convoys (8-12 Honda Wins each, guide in a reflective vest,
    backpacker in a red "co do sao vang" T-shirt with a nylon-wrapped backpack) nose-to-tail along the
    road spline, and 2 blue dump trucks coming the other way that blast their air horn at blind hairpins
  * check-in crowds at the Doc Tham Ma and Ma Pi Leng viewpoints (parked bikes + tourists taking photos)
  * cow herds standing in the road on the link road and on the canyon rim
  * Happy Water (ruou ngo) bottles along the road: 2x nitro, wobbling/smeared screen, inverted steering for 12 s
  * screaming backpacker on the player's bike near the cliff edge or when airborne (C++)

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

LEVEL_PATH = "/Game/Maps/L_HaGiang_CP04"
TAG = "HG_CP04"
PREFIX = "CP04"


def build():
    with unreal.ScopedSlowTask(scenes.PHOTOREAL_STEPS + 2, "Building HaGiang CP04 (living world)") as task:
        task.make_dialog(True)
        base = scenes.build_photoreal_world(LEVEL_PATH, TAG, PREFIX, task)
        task.enter_progress_frame(1, "Traffic, crowds, cows, Happy Water")
        scenes.build_living_world(base, TAG, PREFIX)
        task.enter_progress_frame(1, "World settings + save")
        scenes.finish_level()
    hg.log("CP04 built. Overtake the convoys, steal a bike with F, mind the trucks and the cows, try the Happy Water.")


build()
