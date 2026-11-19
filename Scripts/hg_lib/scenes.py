"""Whole-scene recipes reused by later checkpoints (CP03 photoreal world is the base of CP04/CP05)."""
from __future__ import annotations

import unreal

from . import vegetation
from . import assets, dressing, materials, placements, world
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
    mats = materials.build_all(tex, layout["water"]["surface_z_cm"], layout)
    mat_ring = hg.color_material("MI_GB_CheckIn", (0.72, 0.52, 0.06))
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
    road = world.build_road(layout, mats["asphalt"], tag, f"{prefix}/Road", meshes.get("SM_HG_RoadStrip"),
                            concrete=mats.get("concrete"))
    world.build_water(layout, mats["water"], tag, f"{prefix}/Water")
    world.build_dock(layout, mat_wood, tag, f"{prefix}/Dock")
    world.build_dam(layout, mats, tag, f"{prefix}/Dam")
    world.build_checkin_rings(layout, mat_ring, tag, f"{prefix}/Viewpoints")
    world.build_vehicles(layout, tag, f"{prefix}/Vehicles")

    # Road furniture (posts, chevrons, guardrails, rim walls) is placed against the UE road spline, not taken
    # from gen_scatter's older polyline (signs stood on the carriageway, rails missed the hairpin edges).
    scatter = dict(scatter)
    scatter.update(placements.road_furniture(placements.ctx_for(layout, world.load_scatter())))

    step("Scatter: cliffs, summits, boulders, shrubs, posts")
    world.build_scatter(scatter, meshes, mats, tag, f"{prefix}/Scatter")
    vegetation.build(meshes, mats, tag, f"{prefix}/Vegetation")

    step("Golden hour atmosphere")
    world.build_golden_hour(tag, f"{prefix}/Lighting")

    step("Viewpoints + PlayerStart")
    world.build_viewpoints(scatter, flute, tag, f"{prefix}/Viewpoints", viewpoint_names)
    world.build_viewpoint_props(layout, scatter, meshes, mats, tag, f"{prefix}/Viewpoints")
    world.build_landmarks(layout, meshes, mats, mat_wood, tag, f"{prefix}/Landmarks")
    world.build_set_dressing(scatter, meshes, mats, tag, f"{prefix}/Dressing")
    dressing.build(layout, world.load_scatter(), tag, f"{prefix}/Dressing")
    world.build_river_traffic(layout, tag, f"{prefix}/River")
    world.build_player_start(layout, tag, f"{prefix}/Gameplay")

    return {"layout": layout, "scatter": scatter, "road": road, "meshes": meshes, "mats": mats}


PHOTOREAL_STEPS = 9


def finish_level(game_mode: str = GAME_MODE) -> None:
    hg.set_world_game_mode(game_mode)
    hg.editor_world().get_world_settings().set_editor_property("kill_z", 30000.0)   # river is at 420 m
    # All heightmap mips resident: streamed ones showed stair-stepped terrain after every teleport.
    if hasattr(unreal, "HGLandscapeBuilder"):
        hg.log(f"Landscape textures pinned: {unreal.HGLandscapeBuilder.pin_landscape_textures()}")
    hg.save_current_level()


# ---------------------------------------------------------------------------
# CP04 living world (convoys, trucks, crowds, cows, Happy Water)
#
# All positions come from placements.py (pure Python), which Scripts/Tools/validate_layout.py checks offline
# against the heightmap: run it after regenerating the map.
# ---------------------------------------------------------------------------


def build_traffic(road, ctx, tag, prefix):
    plan = placements.traffic_plan(ctx)
    spawner = hg.spawn_actor(unreal.HGTrafficSpawner, "Traffic_HaGiangLoop", (0, 0, 0), (0, 0, 0), tag, f"{prefix}/Traffic")
    spawner.set_editor_property("road", road)
    spawner.set_editor_property("bike_spacing", plan["bike_spacing_m"] * 100.0)

    def convoy(spec):
        c = unreal.HGConvoySpec()
        c.set_editor_property("start_meters", float(spec["start_m"]))
        c.set_editor_property("direction", int(spec["dir"]))
        c.set_editor_property("num_bikes", int(spec["bikes"]))
        c.set_editor_property("cruise_kmh", float(spec["kmh"]))
        return c

    def truck(spec):
        t = unreal.HGTruckSpec()
        t.set_editor_property("start_meters", float(spec["start_m"]))
        t.set_editor_property("direction", int(spec["dir"]))
        t.set_editor_property("cruise_kmh", float(spec["kmh"]))
        t.set_editor_property("min_meters", float(spec["min_m"]))
        t.set_editor_property("max_meters", float(spec["max_m"]))
        return t

    spawner.set_editor_property("convoys", [convoy(c) for c in plan["convoys"]])
    spawner.set_editor_property("trucks", [truck(t) for t in plan["trucks"]])
    hg.log("Traffic: " + ", ".join(f"{c['name']} @{c['start_m']:.0f} m" for c in plan["convoys"])
           + f"; trucks {plan['trucks'][0]['min_m']:.0f}-{plan['trucks'][0]['max_m']:.0f} m")
    return spawner


def build_checkin_crowd(ctx, key, look_at, rng, tag, prefix):
    crowd = placements.checkin_crowd(ctx, key, rng)
    # Parked tour bikes off the carriageway (convoys brake for anything in their lane).
    for i, (x, y, z, yaw) in enumerate(crowd["bikes"]):
        bike = hg.spawn_actor(unreal.HGBikePawn, f"Parked_{key}_{i}", (x, y, z), (0, yaw, 0), tag, f"{prefix}/Crowds")
        bike.set_editor_property("has_passenger", False)
    styles = [unreal.HGTouristStyle.BACKPACKER] * 5 + [unreal.HGTouristStyle.EASY_RIDER] * 3 + [unreal.HGTouristStyle.LOCAL]
    for i, (x, y, z) in enumerate(crowd["tourists"]):
        npc = hg.spawn_actor(unreal.HGTouristNPC, f"Tourist_{key}_{i}", (x, y, z), (0, 0, 0), tag, f"{prefix}/Crowds")
        npc.set_editor_property("style", styles[i % len(styles)])
        npc.set_editor_property("look_at_point", hg.vec(look_at))


def build_cows(ctx, at_m, count, rng, tag, prefix):
    coats = [unreal.LinearColor(0.32, 0.17, 0.07, 1), unreal.LinearColor(0.12, 0.08, 0.05, 1),
             unreal.LinearColor(0.55, 0.38, 0.2, 1)]
    # Grazing on the verge; a 4 m wander radius still brings them onto the road now and then.
    for i, (x, y, z, yaw) in enumerate(placements.cow_spots(ctx, at_m, count, rng)):
        cow = hg.spawn_actor(unreal.HGCowActor, f"Cow_{int(at_m)}_{i}", (x, y, z), (0, yaw, 0), tag, f"{prefix}/Cows")
        cow.set_editor_property("coat_color", coats[i % len(coats)])
        cow.set_editor_property("wander_radius", 400.0)


def build_roadside_life(ctx, tag, prefix):
    """Round 6A (agent A): goats on the Ma Pi Leng road, kids waving by the hamlets, locals on the shoulder."""
    life = placements.roadside_life(ctx)
    goat_coats = [unreal.LinearColor(0.9, 0.88, 0.82, 1), unreal.LinearColor(0.08, 0.07, 0.06, 1),
                  unreal.LinearColor(0.45, 0.3, 0.18, 1)]
    for i, (x, y, z, yaw) in enumerate(life["goats"]):
        g = hg.spawn_actor(unreal.HGCowActor, f"Goat_MPL_{i}", (x, y, z), (0, yaw, 0), tag, f"{prefix}/Goats")
        g.set_editor_property("goat", True)
        g.set_editor_property("coat_color", goat_coats[i % len(goat_coats)])
        g.set_editor_property("wander_radius", 600.0)
    for i, (x, y, z, yaw) in enumerate(life["kids"]):
        k = hg.spawn_actor(unreal.HGTouristNPC, f"Kid_{i}", (x, y, z), (0, yaw + 90.0, 0), tag, f"{prefix}/Roadside")
        k.set_editor_property("style", unreal.HGTouristStyle.LOCAL)
        k.set_editor_property("kid", True)
        k.set_editor_property("wander", False)
    for i, (x, y, z, yaw) in enumerate(life["walkers"]):
        w = hg.spawn_actor(unreal.HGTouristNPC, f"ShoulderWalker_{i}", (x, y, z), (0, yaw, 0), tag, f"{prefix}/Roadside")
        w.set_editor_property("style", unreal.HGTouristStyle.LOCAL)
        w.set_editor_property("shoulder_walker", True)
    # Round 6B: village dogs - vang (tan), den (black), ven (brindle brown), a white one now and then.
    if hasattr(unreal, "HGDogActor"):
        coats = [unreal.LinearColor(0.62, 0.4, 0.17, 1), unreal.LinearColor(0.04, 0.035, 0.03, 1),
                 unreal.LinearColor(0.3, 0.2, 0.11, 1), unreal.LinearColor(0.7, 0.5, 0.26, 1), unreal.LinearColor(0.78, 0.75, 0.68, 1)]
        for i, (x, y, z, yaw, mode) in enumerate(life["dogs"]):
            d = hg.spawn_actor(unreal.HGDogActor, f"Dog_{mode}_{i}", (x, y, z), (0, yaw, 0), tag, f"{prefix}/Dogs")
            d.set_editor_property("coat_color", coats[i % len(coats)])
            d.set_editor_property("mode", unreal.HGDogMode.TROT if mode == "trot" else unreal.HGDogMode.LIE)
    if hasattr(unreal, "HGMilestoneActor"):
        for i, (x, y, z, yaw, town, km) in enumerate(life["milestones"]):
            ms = hg.spawn_actor(unreal.HGMilestoneActor, f"Milestone_{i}", (x, y, z), (0, yaw, 0), tag, f"{prefix}/Milestones")
            ms.set_editor_property("town", town)
            ms.set_editor_property("km", int(km))
    hg.log(f"Roadside life: {len(life['goats'])} goats, {len(life['kids'])} kids, {len(life['walkers'])} shoulder walkers, "
           f"{len(life['dogs'])} dogs, {len(life['milestones'])} milestones")


def build_happy_water(ctx, rng, tag, prefix):
    for label, x, y, z in placements.happy_water(ctx, rng):
        hg.spawn_actor(unreal.HGHappyWaterPickup, label, (x, y, z), (0, 0, 0), tag, f"{prefix}/HappyWater")


def build_living_world(base: dict, tag: str, prefix: str) -> None:
    world.require_cpp("HGTrafficSpawner", "HGTruckPawn", "HGCowActor", "HGTouristNPC", "HGHappyWaterPickup")
    layout, scatter, road = base["layout"], base["scatter"], base["road"]
    ctx = placements.ctx_for(layout, scatter)
    rng = placements.rng()          # same seed and call order as validate_layout.py
    assets.import_sfx()
    build_traffic(road, ctx, tag, prefix)
    for key in ("doc_tham_ma", "ma_pi_leng"):
        build_checkin_crowd(ctx, key, scatter["viewpoints"][key]["center_cm"], rng, tag, prefix)
    for at_m, count in placements.living_world_cows(ctx):
        build_cows(ctx, at_m, count, rng, tag, prefix)
    build_happy_water(ctx, rng, tag, prefix)
    build_roadside_life(ctx, tag, prefix)
