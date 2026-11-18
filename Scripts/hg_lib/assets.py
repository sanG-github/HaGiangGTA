"""Import the procedural assets from Generated/Assets (gen_assets.py) into /Game/HaGiang/Generated."""
from __future__ import annotations

import os
import shutil
import subprocess

import unreal

from . import editor_utils as hg

DEST = f"{hg.GENERATED_ROOT}"
MESHES = ["SM_HG_KarstCliff_01", "SM_HG_KarstCliff_02", "SM_HG_KarstCliff_03", "SM_HG_KarstCliff_04",
          "SM_HG_Boulder_01", "SM_HG_Boulder_02", "SM_HG_Boulder_03",
          "SM_HG_Bush_01", "SM_HG_Bush_02", "SM_HG_RoadPost",
          "SM_HG_Kapok_Trunk", "SM_HG_Kapok_Bloom", "SM_HG_Tree_Trunk", "SM_HG_Tree_Crown",
          "SM_HG_Pine_Trunk", "SM_HG_Pine_Crown", "SM_HG_RoadStrip", "SM_HG_Guardrail",
          "SM_HG_DistantRanges", "SM_HG_Chevron", "SM_HG_UtilityPole", "SM_HG_HouseWalls", "SM_HG_HouseRoof",
          "SM_HG_StoneWall", "SM_HG_Monument"]
PROPS = ("SM_HG_RoadPost", "SM_HG_RoadStrip", "SM_HG_Guardrail", "SM_HG_DistantRanges", "SM_HG_Chevron",
         "SM_HG_UtilityPole", "SM_HG_HouseWalls", "SM_HG_HouseRoof", "SM_HG_StoneWall", "SM_HG_Monument")
NANITE = {m for m in MESHES if m not in PROPS}
TREE_PARTS = {m for m in MESHES if any(k in m for k in ("Kapok", "Tree_", "Pine"))}
COMPLEX_COLLISION = {m for m in MESHES if "Cliff" in m or "Boulder" in m}
TEXTURES = {"T_HG_RockNormal": "normal", "T_HG_WaterNormal": "normal", "T_HG_Noise": "mask", "T_HG_PathMask": "mask"}


def _python():
    root = hg.project_dir()
    for c in (os.path.join(root, ".venv", "bin", "python"), os.path.join(root, ".venv", "Scripts", "python.exe"),
              shutil.which("python3") or "", shutil.which("python") or ""):
        if c and os.path.isfile(c):
            return c
    raise RuntimeError("No external Python found (create .venv, see README)")


def run_generator(script: str, check_file: str) -> None:
    root = hg.project_dir()
    if os.path.isfile(os.path.join(root, check_file)):
        return
    hg.log(f"Running {script} ...")
    res = subprocess.run([_python(), os.path.join(root, "Scripts", "Tools", script)], cwd=root,
                         capture_output=True, text=True)
    hg.log(res.stdout[-1500:])
    if res.returncode != 0:
        raise RuntimeError(f"{script} failed:\n{res.stderr[-3000:]}")


def ensure_generated() -> None:
    run_generator("gen_assets.py", "Generated/Assets/Meshes/SM_HG_Monument.obj")
    run_generator("gen_scatter.py", "Generated/Assets/Textures/T_HG_PathMask.png")


def _import(src: str, dest_folder: str, name: str):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", src)
    task.set_editor_property("destination_path", dest_folder)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    asset = unreal.EditorAssetLibrary.load_asset(f"{dest_folder}/{name}")
    if asset is None:
        # Interchange may name the asset after the file / mesh node; fall back to the imported paths.
        for path in task.get_editor_property("imported_object_paths") or []:
            a = unreal.EditorAssetLibrary.load_asset(path)
            if isinstance(a, (unreal.StaticMesh, unreal.Texture2D, unreal.SoundWave)):
                asset = a
                break
    if asset is None:
        raise RuntimeError(f"Import failed: {src}")
    return asset


def import_textures(force: bool = False) -> dict:
    folder = f"{DEST}/Textures"
    out = {}
    for name, kind in TEXTURES.items():
        path = f"{folder}/{name}"
        tex = None if force else (unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None)
        if tex is None:
            src = os.path.join(hg.project_dir(), "Generated", "Assets", "Textures", f"{name}.png")
            tex = _import(src, folder, name)
        # Always (re)apply settings so older imports get fixed too. All non-normal textures are
        # TC_Masks (linear) -> sampled with the Masks sampler in hg_lib.materials.
        want_cs = unreal.TextureCompressionSettings.TC_NORMALMAP if kind == "normal" else unreal.TextureCompressionSettings.TC_MASKS
        if tex.get_editor_property("compression_settings") != want_cs or tex.get_editor_property("srgb"):
            tex.set_editor_property("srgb", False)
            tex.set_editor_property("compression_settings", want_cs)
            if kind == "normal":
                tex.set_editor_property("flip_green_channel", True)
            unreal.EditorAssetLibrary.save_loaded_asset(tex)
        out[name] = tex
    return out


def _set_nanite(mesh: unreal.StaticMesh, enabled: bool) -> None:
    settings = mesh.get_editor_property("nanite_settings")
    if settings.get_editor_property("enabled") != enabled:
        settings.set_editor_property("enabled", enabled)
        mesh.set_editor_property("nanite_settings", settings)


# Classic LOD chains for the M1 profile (percent of LOD0 triangles, screen size).
LOD_CHAINS = {
    "Cliff": [(1.0, 1.0), (0.18, 0.45), (0.05, 0.16), (0.012, 0.05), (0.004, 0.015)],
    "Boulder": [(1.0, 1.0), (0.2, 0.3), (0.05, 0.1), (0.015, 0.03)],
    "Bush": [(1.0, 1.0), (0.25, 0.25), (0.06, 0.08), (0.02, 0.025)],
    "Crown": [(1.0, 1.0), (0.3, 0.2), (0.08, 0.06), (0.03, 0.02)],
    "Bloom": [(1.0, 1.0), (0.3, 0.2), (0.08, 0.06)],
    "Trunk": [(1.0, 1.0), (0.5, 0.15), (0.25, 0.05)],
}


def _apply_profile(mesh: unreal.StaticMesh, name: str) -> None:
    """Nanite on SM6 hardware; on M1 (SM5) Nanite is unavailable, so build reduced LODs instead."""
    if name not in NANITE:
        return
    if hg.USE_NANITE:
        _set_nanite(mesh, True)
        return
    _set_nanite(mesh, False)
    chain = next((c for k, c in LOD_CHAINS.items() if k in name), None)
    if chain is None:
        return
    sm_sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if sm_sub.get_lod_count(mesh) == len(chain):
        return
    opts = unreal.StaticMeshReductionOptions()
    opts.set_editor_property("auto_compute_lod_screen_size", False)
    settings = []
    for pct, screen in chain:
        s = unreal.StaticMeshReductionSettings()
        s.set_editor_property("percent_triangles", float(pct))
        s.set_editor_property("screen_size", float(screen))
        settings.append(s)
    opts.set_editor_property("reduction_settings", settings)
    lods = sm_sub.set_lods(mesh, opts)
    hg.log(f"{name}: {lods} LODs (M1 profile, Nanite off)")


def import_meshes(force: bool = False) -> dict:
    folder = f"{DEST}/Meshes"
    sm_sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    out = {}
    for name in MESHES:
        path = f"{folder}/{name}"
        mesh = None if force else (unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None)
        if mesh is None:
            src = os.path.join(hg.project_dir(), "Generated", "Assets", "Meshes", f"{name}.obj")
            mesh = _import(src, folder, name)
            body = mesh.get_editor_property("body_setup")
            if name in COMPLEX_COLLISION and body is not None:
                body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
            elif name in ("SM_HG_RoadPost", "SM_HG_Guardrail", "SM_HG_UtilityPole", "SM_HG_HouseWalls",
                          "SM_HG_StoneWall", "SM_HG_Monument"):
                sm_sub.remove_collisions(mesh)
                sm_sub.add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.BOX)
            unreal.EditorAssetLibrary.save_loaded_asset(mesh)
        _apply_profile(mesh, name)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
        out[name] = mesh
    return out


def import_sound(name: str, force: bool = False):
    folder = f"{DEST}/Audio"
    path = f"{folder}/{name}"
    if not force and unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.EditorAssetLibrary.load_asset(path)
    src = os.path.join(hg.project_dir(), "Generated", "Assets", "Audio", f"{name}.wav")
    snd = _import(src, folder, name)
    unreal.EditorAssetLibrary.save_loaded_asset(snd)
    return snd


def import_flute(force: bool = False):
    return import_sound("SW_HG_SaoMong", force)


def import_sfx(force: bool = False) -> dict:
    """CP04 sounds; C++ loads them by path (/Game/HaGiang/Generated/Audio/<name>)."""
    return {n: import_sound(n, force) for n in ("SW_HG_AirHorn", "SW_HG_Scream", "SW_HG_Moo")}


LOOPS = ("SW_HG_BikeEngine_Loop", "SW_HG_TruckEngine_Loop", "SW_HG_BoatMotor_Loop", "SW_HG_Siren_Loop", "SW_HG_Heli_Loop")


def import_loops(force: bool = False) -> dict:
    """CP05 looping sounds (engines, siren, helicopter); C++ loads them by path."""
    out = {}
    for n in LOOPS:
        snd = import_sound(n, force)
        snd.set_editor_property("looping", True)
        unreal.EditorAssetLibrary.save_loaded_asset(snd)
        out[n] = snd
    return out
