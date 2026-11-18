"""Whole-scene recipes reused by later checkpoints (CP03 photoreal world is the base of CP04/CP05)."""
from __future__ import annotations

import math
import random

import unreal

from . import assets, materials, world
from . import editor_utils as hg

GAME_MODE = "/Script/HaGiangGTA.HGOpenWorldGameMode"


def build_photoreal_world(level_path: str, tag: str, prefix: str, task=None, viewpoint_names=None) -> dict:
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
    if hg.USE_NANITE:
        try:
            landscape.set_editor_property("enable_nanite", True)
        except Exception as exc:
            hg.warn(f"Nanite landscape: {exc} (enable it in the Landscape details panel)")

    step("Road, river, dock, vehicles")
    road = world.build_road(layout, mats["asphalt"], tag, f"{prefix}/Road", meshes.get("SM_HG_RoadStrip"))
    world.build_water(layout, mats["water"], tag, f"{prefix}/Water")
    world.build_dock(layout, mat_wood, tag, f"{prefix}/Dock")
    world.build_checkin_rings(layout, mat_ring, tag, f"{prefix}/Viewpoints")
    world.build_vehicles(layout, tag, f"{prefix}/Vehicles")

    step("Scatter: cliffs, summits, boulders, shrubs, posts")
    world.build_scatter(scatter, meshes, mats, tag, f"{prefix}/Scatter")

    step("Golden hour atmosphere")
    world.build_golden_hour(tag, f"{prefix}/Lighting")

    step("Viewpoints + PlayerStart")
    world.build_viewpoints(scatter, flute, tag, f"{prefix}/Viewpoints", viewpoint_names)
    world.build_viewpoint_props(layout, scatter, meshes, mats, tag, f"{prefix}/Viewpoints")
    world.build_set_dressing(scatter, meshes, mats, tag, f"{prefix}/Dressing")
    world.build_river_traffic(layout, tag, f"{prefix}/River")
    world.build_player_start(layout, tag, f"{prefix}/Gameplay")

    return {"layout": layout, "scatter": scatter, "road": road, "meshes": meshes, "mats": mats}


PHOTOREAL_STEPS = 9


def finish_level(game_mode: str = GAME_MODE) -> None:
    hg.set_world_game_mode(game_mode)
    hg.editor_world().get_world_settings().set_editor_property("kill_z", 60000.0)
    hg.save_current_level()


# ---------------------------------------------------------------------------
# CP04 living world (convoys, trucks, crowds, cows, Happy Water)
# ---------------------------------------------------------------------------

_RNG = random.Random(404)


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


def build_traffic(road, length_m, tag, prefix):
    spawner = hg.spawn_actor(unreal.HGTrafficSpawner, "Traffic_HaGiangLoop", (0, 0, 0), (0, 0, 0), tag, f"{prefix}/Traffic")
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


def build_checkin_crowd(road, layout, key, road_m, look_at, tag, prefix):
    loc, fwd, right = road_frame(road, road_m)
    vp = layout["viewpoints"][key]
    cx, cy, cz = vp["location_cm"]
    # Parked tour bikes on the shoulder.
    for i in range(5):
        x, y, z = offset(loc, fwd, right, -900 + i * 240, 520, 70)
        bike = hg.spawn_actor(unreal.HGBikePawn, f"Parked_{key}_{i}", (x, y, z),
                              (0, math.degrees(math.atan2(fwd.y, fwd.x)) + 70, 0), tag, f"{prefix}/Crowds")
        bike.set_editor_property("has_passenger", False)
    styles = [unreal.HGTouristStyle.BACKPACKER] * 5 + [unreal.HGTouristStyle.EASY_RIDER] * 3 + [unreal.HGTouristStyle.LOCAL]
    for i, style in enumerate(styles):
        a = _RNG.uniform(0, 2 * math.pi)
        r = _RNG.uniform(250, 900)
        npc = hg.spawn_actor(unreal.HGTouristNPC, f"Tourist_{key}_{i}", (cx + math.cos(a) * r, cy + math.sin(a) * r, cz + 120),
                             (0, 0, 0), tag, f"{prefix}/Crowds")
        npc.set_editor_property("style", style)
        npc.set_editor_property("look_at_point", hg.vec(look_at))


def build_cows(road, at_m, count, tag, prefix):
    loc, fwd, right = road_frame(road, at_m)
    coats = [unreal.LinearColor(0.32, 0.17, 0.07, 1), unreal.LinearColor(0.12, 0.08, 0.05, 1),
             unreal.LinearColor(0.55, 0.38, 0.2, 1)]
    for i in range(count):
        x, y, z = offset(loc, fwd, right, _RNG.uniform(-700, 700), _RNG.uniform(-300, 300), 120)
        cow = hg.spawn_actor(unreal.HGCowActor, f"Cow_{int(at_m)}_{i}", (x, y, z), (0, _RNG.uniform(0, 360), 0),
                             tag, f"{prefix}/Cows")
        cow.set_editor_property("coat_color", coats[i % len(coats)])
        cow.set_editor_property("wander_radius", 600.0)


def build_happy_water(road, length_m, layout, tag, prefix):
    n = 9
    for i in range(n):
        m = 400 + (length_m - 800) * i / (n - 1)
        loc, fwd, right = road_frame(road, m)
        side = 150 if i % 2 else -150
        hg.spawn_actor(unreal.HGHappyWaterPickup, f"HappyWater_{i}", offset(loc, fwd, right, 0, side, 20),
                       (0, 0, 0), tag, f"{prefix}/HappyWater")
    # One at each viewpoint, of course.
    for key, vp in layout["viewpoints"].items():
        x, y, z = vp["location_cm"]
        hg.spawn_actor(unreal.HGHappyWaterPickup, f"HappyWater_{key}", (x + 400, y, z + 20), (0, 0, 0), tag,
                       f"{prefix}/HappyWater")


def build_living_world(base: dict, tag: str, prefix: str) -> None:
    world.require_cpp("HGTrafficSpawner", "HGTruckPawn", "HGCowActor", "HGTouristNPC", "HGHappyWaterPickup")
    _RNG.seed(404)
    layout, scatter, road = base["layout"], base["scatter"], base["road"]
    length_m = road.get_road_length() / 100.0
    sec = layout["road"]["sections_m"]
    assets.import_sfx()
    build_traffic(road, length_m, tag, prefix)
    build_checkin_crowd(road, layout, "doc_tham_ma", sec["tham_ma"] + 40,
                        scatter["viewpoints"]["doc_tham_ma"]["center_cm"], tag, prefix)
    rim_m = road.get_distance_along_road(hg.vec(layout["viewpoints"]["ma_pi_leng"]["road_cm"])) / 100.0
    build_checkin_crowd(road, layout, "ma_pi_leng", rim_m - 30,
                        scatter["viewpoints"]["ma_pi_leng"]["center_cm"], tag, prefix)
    build_cows(road, sec["tham_ma"] + 0.45 * (sec["rim"] - sec["tham_ma"]), 5, tag, prefix)   # link road
    build_cows(road, rim_m + 450, 4, tag, prefix)                                             # canyon rim
    build_happy_water(road, length_m, layout, tag, prefix)
