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
          "SM_HG_Bush_01", "SM_HG_Bush_02", "SM_HG_RoadPost"]
NANITE = {m for m in MESHES if m != "SM_HG_RoadPost"}
COMPLEX_COLLISION = {m for m in MESHES if "Cliff" in m or "Boulder" in m}
TEXTURES = {"T_HG_RockNormal": "normal", "T_HG_WaterNormal": "normal", "T_HG_Noise": "mask"}


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
    run_generator("gen_assets.py", "Generated/Assets/Audio/SW_HG_Moo.wav")
    run_generator("gen_scatter.py", "Generated/Heightmaps/HaGiang_scatter.json")


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
            tex.set_editor_property("srgb", False)
            if kind == "normal":
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
                tex.set_editor_property("flip_green_channel", True)
            else:
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
            unreal.EditorAssetLibrary.save_loaded_asset(tex)
        out[name] = tex
    return out


def _enable_nanite(mesh: unreal.StaticMesh) -> None:
    settings = mesh.get_editor_property("nanite_settings")
    settings.set_editor_property("enabled", True)
    mesh.set_editor_property("nanite_settings", settings)


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
            if name in NANITE:
                _enable_nanite(mesh)
            body = mesh.get_editor_property("body_setup")
            if name in COMPLEX_COLLISION and body is not None:
                body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
            elif name == "SM_HG_RoadPost":
                sm_sub.remove_collisions(mesh)
                sm_sub.add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.BOX)
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
