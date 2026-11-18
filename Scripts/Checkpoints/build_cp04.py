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
import math
import os
import random
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
RNG = random.Random(404)


def road_frame(road, meters):
    """(location, forward, right) on the road spline at a distance in metres."""
    spline = road.get_spline()
    d = max(0.0, min(meters * 100.0, spline.get_spline_length()))
    loc = spline.get_location_at_distance_along_spline(d, unreal.SplineCoordinateSpace.WORLD)
    fwd = spline.get_direction_at_distance_along_spline(d, unreal.SplineCoordinateSpace.WORLD)
    right = unreal.Vector(-fwd.y, fwd.x, 0.0)
    return loc, fwd, right


def offset(loc, fwd, right, along, side, up=0.0):
    return (loc.x + fwd.x * along + right.x * side, loc.y + fwd.y * along + right.y * side, loc.z + up)


def build_traffic(road, length_m):
    spawner = hg.spawn_actor(unreal.HGTrafficSpawner, "Traffic_HaGiangLoop", (0, 0, 0), (0, 0, 0), TAG, f"{PREFIX}/Traffic")
    spawner.set_editor_property("road", road)

    def convoy(start, direction, bikes, kmh):
        c = unreal.HGConvoySpec()
        c.set_editor_property("start_meters", float(start))
        c.set_editor_property("direction", int(direction))
        c.set_editor_property("num_bikes", int(bikes))
        c.set_editor_property("cruise_kmh", float(kmh))
        return c

    def truck(start, direction, kmh):
        t = unreal.HGTruckSpec()
        t.set_editor_property("start_meters", float(start))
        t.set_editor_property("direction", int(direction))
        t.set_editor_property("cruise_kmh", float(kmh))
        return t

    spawner.set_editor_property("convoys", [
        convoy(650, +1, 10, 38),                       # climbing the Tham Ma hairpins with the player
        convoy(min(5200, length_m - 600), +1, 8, 46),  # along the Ma Pi Leng rim
        convoy(min(6400, length_m - 300), -1, 12, 44), # coming back down towards Tham Ma
    ])
    spawner.set_editor_property("trucks", [
        truck(min(6100, length_m - 200), -1, 36),
        truck(2600, -1, 34),
    ])
    return spawner


def build_checkin_crowd(road, layout, key, road_m, look_at):
    loc, fwd, right = road_frame(road, road_m)
    vp = layout["viewpoints"][key]
    cx, cy, cz = vp["location_cm"]
    # Parked tour bikes on the shoulder.
    for i in range(5):
        x, y, z = offset(loc, fwd, right, -900 + i * 240, 520, 70)
        bike = hg.spawn_actor(unreal.HGBikePawn, f"Parked_{key}_{i}", (x, y, z),
                              (0, math.degrees(math.atan2(fwd.y, fwd.x)) + 70, 0), TAG, f"{PREFIX}/Crowds")
        bike.set_editor_property("has_passenger", False)
    styles = [unreal.HGTouristStyle.BACKPACKER] * 5 + [unreal.HGTouristStyle.EASY_RIDER] * 3 + [unreal.HGTouristStyle.LOCAL]
    for i, style in enumerate(styles):
        a = RNG.uniform(0, 2 * math.pi)
        r = RNG.uniform(250, 900)
        npc = hg.spawn_actor(unreal.HGTouristNPC, f"Tourist_{key}_{i}", (cx + math.cos(a) * r, cy + math.sin(a) * r, cz + 120),
                             (0, 0, 0), TAG, f"{PREFIX}/Crowds")
        npc.set_editor_property("style", style)
        npc.set_editor_property("look_at_point", hg.vec(look_at))


def build_cows(road, at_m, count):
    loc, fwd, right = road_frame(road, at_m)
    coats = [unreal.LinearColor(0.32, 0.17, 0.07, 1), unreal.LinearColor(0.12, 0.08, 0.05, 1),
             unreal.LinearColor(0.55, 0.38, 0.2, 1)]
    for i in range(count):
        x, y, z = offset(loc, fwd, right, RNG.uniform(-700, 700), RNG.uniform(-300, 300), 120)
        cow = hg.spawn_actor(unreal.HGCowActor, f"Cow_{int(at_m)}_{i}", (x, y, z), (0, RNG.uniform(0, 360), 0),
                             TAG, f"{PREFIX}/Cows")
        cow.set_editor_property("coat_color", coats[i % len(coats)])
        cow.set_editor_property("wander_radius", 600.0)


def build_happy_water(road, length_m, layout):
    n = 9
    for i in range(n):
        m = 400 + (length_m - 800) * i / (n - 1)
        loc, fwd, right = road_frame(road, m)
        side = 150 if i % 2 else -150
        hg.spawn_actor(unreal.HGHappyWaterPickup, f"HappyWater_{i}", offset(loc, fwd, right, 0, side, 20),
                       (0, 0, 0), TAG, f"{PREFIX}/HappyWater")
    # One at each viewpoint, of course.
    for key, vp in layout["viewpoints"].items():
        x, y, z = vp["location_cm"]
        hg.spawn_actor(unreal.HGHappyWaterPickup, f"HappyWater_{key}", (x + 400, y, z + 20), (0, 0, 0), TAG,
                       f"{PREFIX}/HappyWater")


def build():
    with unreal.ScopedSlowTask(scenes.PHOTOREAL_STEPS + 3, "Building HaGiang CP04 (living world)") as task:
        task.make_dialog(True)
        world.require_cpp("HGTrafficSpawner", "HGTruckPawn", "HGCowActor", "HGTouristNPC", "HGHappyWaterPickup")
        base = scenes.build_photoreal_world(LEVEL_PATH, TAG, PREFIX, task)
        layout, scatter, road = base["layout"], base["scatter"], base["road"]
        length_m = road.get_road_length() / 100.0
        sec = layout["road"]["sections_m"]

        task.enter_progress_frame(1, "Traffic, crowds, cows, Happy Water")
        assets.import_sfx()
        build_traffic(road, length_m)
        build_checkin_crowd(road, layout, "doc_tham_ma", sec["tham_ma"] + 40,
                            scatter["viewpoints"]["doc_tham_ma"]["center_cm"])
        rim_m = road.get_distance_along_road(hg.vec(layout["viewpoints"]["ma_pi_leng"]["road_cm"])) / 100.0
        build_checkin_crowd(road, layout, "ma_pi_leng", rim_m - 30,
                            scatter["viewpoints"]["ma_pi_leng"]["center_cm"])
        build_cows(road, sec["tham_ma"] + 0.45 * (sec["rim"] - sec["tham_ma"]), 5)   # link road
        build_cows(road, rim_m + 450, 4)                                             # canyon rim, past the spur
        build_happy_water(road, length_m, layout)

        task.enter_progress_frame(1, "World settings + save")
        scenes.finish_level()
    hg.log("CP04 built. Overtake the convoys, steal a bike with F, mind the trucks and the cows, try the Happy Water.")


build()
