"""Shared Unreal Editor (5.5) Python helpers for the HaGiangGTA checkpoint scripts.

Every helper is idempotent-friendly: actors spawned by a checkpoint carry a tag
(e.g. ``HG_CP01``) so re-running the script first wipes the previous build.
Runs only inside the Unreal Editor (``import unreal``).
"""
from __future__ import annotations

import math
from typing import Iterable, Optional, Sequence

import unreal

GENERATED_ROOT = "/Game/HaGiang/Generated"
CUBE = "/Engine/BasicShapes/Cube.Cube"
CYLINDER = "/Engine/BasicShapes/Cylinder.Cylinder"
SPHERE = "/Engine/BasicShapes/Sphere.Sphere"
BASIC_SHAPE_MATERIAL = "/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"
WORLD_GRID_MATERIAL = "/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"


# ---------------------------------------------------------------------------
# Logging / subsystems
# ---------------------------------------------------------------------------

def log(msg: str) -> None:
    unreal.log(f"[HaGiang] {msg}")


def warn(msg: str) -> None:
    unreal.log_warning(f"[HaGiang] {msg}")


def actor_subsystem() -> unreal.EditorActorSubsystem:
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def level_subsystem() -> unreal.LevelEditorSubsystem:
    return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def editor_world() -> unreal.World:
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


# ---------------------------------------------------------------------------
# Levels
# ---------------------------------------------------------------------------

def open_or_create_level(level_path: str) -> unreal.World:
    """Open ``level_path`` (e.g. /Game/Maps/L_Foo) or create it empty."""
    lvl = level_subsystem()
    if unreal.EditorAssetLibrary.does_asset_exist(level_path):
        log(f"Loading existing level {level_path}")
        if not lvl.load_level(level_path):
            raise RuntimeError(f"Could not load level {level_path}")
    else:
        log(f"Creating new level {level_path}")
        if not lvl.new_level(level_path):
            raise RuntimeError(f"Could not create level {level_path}")
    return editor_world()


def save_current_level() -> None:
    if not level_subsystem().save_current_level():
        warn("save_current_level() returned False")
    else:
        log("Level saved.")


def set_world_game_mode(class_path: str) -> bool:
    """Set the World Settings GameMode override, e.g. /Script/HaGiangGTA.HGGreyboxGameMode."""
    gm_class = unreal.load_class(None, class_path)
    if gm_class is None:
        warn(f"GameMode class {class_path} not found - compile the C++ module first.")
        return False
    settings = editor_world().get_world_settings()
    settings.set_editor_property("default_game_mode", gm_class)
    log(f"GameMode override -> {class_path}")
    return True


# ---------------------------------------------------------------------------
# Actors
# ---------------------------------------------------------------------------

def clear_tagged(tag: str) -> int:
    """Destroy every actor in the current level carrying ``tag``."""
    sub = actor_subsystem()
    tag_name = unreal.Name(tag)
    doomed = [a for a in sub.get_all_level_actors() if tag_name in a.tags]
    if doomed:
        sub.destroy_actors(doomed)
    log(f"Removed {len(doomed)} actors tagged {tag}")
    return len(doomed)


def _finish_actor(actor: unreal.Actor, label: str, tag: str, folder: Optional[str]) -> unreal.Actor:
    actor.set_actor_label(label)
    tags = list(actor.tags)
    tags.append(unreal.Name(tag))
    actor.set_editor_property("tags", tags)
    if folder:
        actor.set_folder_path(folder)
    return actor


def spawn_actor(cls, label: str, location: Sequence[float], rotation: Sequence[float] = (0, 0, 0),
                tag: str = "HG", folder: Optional[str] = None) -> unreal.Actor:
    """Spawn an actor of ``cls``. ``rotation`` is (pitch, yaw, roll) in degrees."""
    loc = unreal.Vector(*location)
    rot = unreal.Rotator(pitch=rotation[0], yaw=rotation[1], roll=rotation[2])
    actor = actor_subsystem().spawn_actor_from_class(cls, loc, rot)
    if actor is None:
        raise RuntimeError(f"Failed to spawn {cls} for {label}")
    return _finish_actor(actor, label, tag, folder)


def spawn_mesh(label: str, mesh_path: str, location: Sequence[float],
               rotation: Sequence[float] = (0, 0, 0), scale: Sequence[float] = (1, 1, 1),
               material: Optional[unreal.MaterialInterface] = None, tag: str = "HG",
               folder: Optional[str] = None, collision: bool = True) -> unreal.StaticMeshActor:
    actor = spawn_actor(unreal.StaticMeshActor, label, location, rotation, tag, folder)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    smc = actor.static_mesh_component
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    if mesh is None:
        raise RuntimeError(f"Mesh not found: {mesh_path}")
    smc.set_static_mesh(mesh)
    if material is not None:
        smc.set_material(0, material)
    if not collision:
        smc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    return actor


def box(label: str, center: Sequence[float], size_cm: Sequence[float], yaw: float = 0.0,
        material=None, tag: str = "HG", folder: Optional[str] = None, collision: bool = True):
    """Axis-aligned (optionally yawed) box from the engine cube (100 uu)."""
    return spawn_mesh(label, CUBE, center, (0.0, yaw, 0.0),
                      (size_cm[0] / 100.0, size_cm[1] / 100.0, size_cm[2] / 100.0),
                      material, tag, folder, collision)


def ramp(label: str, start_top: Sequence[float], length_cm: float, rise_cm: float, width_cm: float,
         thickness_cm: float = 60.0, yaw: float = 0.0, material=None, tag: str = "HG",
         folder: Optional[str] = None):
    """A sloped slab whose *top surface* goes from ``start_top`` (x, y, z) to
    ``start_top + length along yaw`` rising by ``rise_cm``."""
    pitch_rad = math.atan2(rise_cm, length_cm)
    slab_len = math.hypot(length_cm, rise_cm)
    yaw_rad = math.radians(yaw)
    fx, fy = math.cos(yaw_rad), math.sin(yaw_rad)

    # Midpoint of the top surface, then push down half a thickness along the slab normal.
    mx = start_top[0] + fx * length_cm * 0.5
    my = start_top[1] + fy * length_cm * 0.5
    mz = start_top[2] + rise_cm * 0.5
    nx_local, nz = -math.sin(pitch_rad), math.cos(pitch_rad)  # normal in (forward, up) plane
    cx = mx - fx * nx_local * thickness_cm * 0.5
    cy = my - fy * nx_local * thickness_cm * 0.5
    cz = mz - nz * thickness_cm * 0.5

    return spawn_mesh(label, CUBE, (cx, cy, cz), (math.degrees(pitch_rad), yaw, 0.0),
                      (slab_len / 100.0, width_cm / 100.0, thickness_cm / 100.0),
                      material, tag, folder, True)


# ---------------------------------------------------------------------------
# Materials
# ---------------------------------------------------------------------------

def load_material(path: str) -> unreal.MaterialInterface:
    mat = unreal.EditorAssetLibrary.load_asset(path)
    if mat is None:
        raise RuntimeError(f"Material not found: {path}")
    return mat


def color_material(name: str, color: Iterable[float], folder: str = f"{GENERATED_ROOT}/Materials",
                   parent_path: str = BASIC_SHAPE_MATERIAL) -> unreal.MaterialInstanceConstant:
    """Create (or update) a MaterialInstanceConstant of BasicShapeMaterial with a ``Color``."""
    asset_path = f"{folder}/{name}"
    mi = unreal.EditorAssetLibrary.load_asset(asset_path) if unreal.EditorAssetLibrary.does_asset_exist(asset_path) else None
    if mi is None:
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        mi = tools.create_asset(name, folder, unreal.MaterialInstanceConstant,
                                unreal.MaterialInstanceConstantFactoryNew())
        if mi is None:
            raise RuntimeError(f"Could not create material instance {asset_path}")
    unreal.MaterialEditingLibrary.set_material_instance_parent(mi, load_material(parent_path))
    r, g, b = list(color)[:3]
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
        mi, "Color", unreal.LinearColor(r, g, b, 1.0))
    unreal.MaterialEditingLibrary.update_material_instance(mi)
    unreal.EditorAssetLibrary.save_loaded_asset(mi)
    return mi


# ---------------------------------------------------------------------------
# Lighting
# ---------------------------------------------------------------------------

def spawn_basic_sky(tag: str, folder: str = "Lighting", sun_pitch: float = -35.0, sun_yaw: float = 135.0,
                    sun_lux: float = 10.0, fog_density: float = 0.004):
    """Sun + SkyAtmosphere + realtime SkyLight + height fog (Lumen friendly, all movable)."""
    sun = spawn_actor(unreal.DirectionalLight, "Sun", (0, 0, 2000), (sun_pitch, sun_yaw, 0), tag, folder)
    sun_comp = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sun_comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    sun_comp.set_editor_property("intensity", sun_lux)
    sun_comp.set_editor_property("atmosphere_sun_light", True)

    spawn_actor(unreal.SkyAtmosphere, "SkyAtmosphere", (0, 0, 0), (0, 0, 0), tag, folder)

    sky = spawn_actor(unreal.SkyLight, "SkyLight", (0, 0, 2500), (0, 0, 0), tag, folder)
    sky_comp = sky.get_component_by_class(unreal.SkyLightComponent)
    sky_comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky_comp.set_editor_property("real_time_capture", True)

    fog = spawn_actor(unreal.ExponentialHeightFog, "HeightFog", (0, 0, 0), (0, 0, 0), tag, folder)
    fog_comp = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    fog_comp.set_editor_property("fog_density", fog_density)
    return sun


# ---------------------------------------------------------------------------
# Project paths / generated data (CP02+)
# ---------------------------------------------------------------------------

def project_dir() -> str:
    import os
    return os.path.normpath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))


def ensure_generated_heightmap() -> dict:
    """Return the parsed HaGiang_layout.json, running gen_heightmap.py first if the
    generated files are missing (needs a Python with numpy + pillow; the script
    tries <project>/.venv first, then python3 on PATH)."""
    import json
    import os
    import shutil
    import subprocess

    root = project_dir()
    out_dir = os.path.join(root, "Generated", "Heightmaps")
    layout_path = os.path.join(out_dir, "HaGiang_layout.json")
    r16_path = os.path.join(out_dir, "HaGiang_4033.r16")
    if not (os.path.isfile(layout_path) and os.path.isfile(r16_path)):
        gen = os.path.join(root, "Scripts", "Tools", "gen_heightmap.py")
        candidates = [os.path.join(root, ".venv", "bin", "python"),
                      os.path.join(root, ".venv", "Scripts", "python.exe"),
                      shutil.which("python3") or "", shutil.which("python") or ""]
        python = next((c for c in candidates if c and os.path.isfile(c)), None)
        if python is None:
            raise RuntimeError("No external Python found to run gen_heightmap.py "
                               "(create .venv: python3 -m venv .venv && .venv/bin/pip install -r Scripts/Tools/requirements.txt)")
        log(f"Generating heightmap with {python} ...")
        result = subprocess.run([python, gen], cwd=root, capture_output=True, text=True)
        log(result.stdout[-2000:])
        if result.returncode != 0:
            raise RuntimeError(f"gen_heightmap.py failed:\n{result.stderr[-3000:]}")
    with open(layout_path, "r", encoding="utf-8") as f:
        layout = json.load(f)
    layout["_r16_abs"] = r16_path
    return layout


def vec(v) -> unreal.Vector:
    return unreal.Vector(float(v[0]), float(v[1]), float(v[2]))
