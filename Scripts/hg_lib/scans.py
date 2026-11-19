"""Round 7B (agent B): CC0 photoscanned texture sets from Poly Haven for the ground, rock, road and house materials.

Agent A downloads the sets to Data/PolyHaven/<id>/ (gitignored; the manifest is committed). Each material role
below lists Poly Haven ids in order of preference; the first id whose folder exists is used. A role with no set
on disk is simply absent, and the materials fall back to their procedural colours, so this module is safe to run
before the download.

Files per set (Poly Haven names): <id>_diff_2k.*, <id>_nor_dx_2k.* or <id>_nor_gl_2k.* (GL -> green flipped),
<id>_rough_2k.* or <id>_arm_2k.* (AO / roughness / metal packed: roughness in G).
Imported to /Game/HaGiang/PolyHaven/<id>/; re-imported only when the source file's content changes (sha1 tag).
"""
from __future__ import annotations

import glob
import hashlib
import os

import unreal

from . import editor_utils as hg

DEST = "/Game/HaGiang/PolyHaven"
HASH_TAG = "HGSourceHash"

# role -> preferred Poly Haven ids (first one present on disk wins)
ROLES = {
    "rock": ["marble_cliff_01", "marble_cliff_02", "lichen_rock", "gray_rocks", "rock_face", "rock_wall_08"],
    "ground": ["dry_ground_rocks", "rocky_terrain_02", "rocky_trail", "aerial_rocks_02", "gravelly_sand"],
    "mud": ["red_mud_stones", "dry_mud_field", "brown_mud_02", "mud_cracked_dry_03", "red_dirt_mud_01"],
    "grass": ["sparse_grass", "aerial_grass_rock", "rocky_grass", "forest_ground_04", "grass_path_2"],
    "riverbed": ["river_small_rocks", "dry_riverbed_rock", "pebbles", "gravel_stones"],
    "asphalt": ["asphalt_02", "asphalt_track", "asphalt_01"],
    "concrete": ["concrete_floor_02", "concrete_pavement", "concrete_floor_worn_001"],
    "stonewall": ["dry_stone_wall", "stone_wall", "castle_wall_slates", "stone_wall_04"],
    "earthwall": ["mud_wall", "clay_plaster", "brown_mud_02", "dry_mud_field"],
}


def _root():
    return os.path.join(hg.project_dir(), "Data", "PolyHaven")


def _pick(folder, keys):
    for key in keys:
        hits = sorted(glob.glob(os.path.join(folder, f"*{key}*")))
        hits = [h for h in hits if os.path.splitext(h)[1].lower() in (".jpg", ".jpeg", ".png", ".tga", ".exr", ".tif", ".tiff")]
        if hits:
            # Prefer 2k, then the smallest file name (the plain map).
            hits.sort(key=lambda h: (0 if "2k" in os.path.basename(h).lower() else 1, len(h)))
            return hits[0]
    return None


def _sha1(path):
    with open(path, "rb") as fh:
        return hashlib.sha1(fh.read()).hexdigest()


def _import(src, folder, name, kind, flip_green=False):
    path = f"{folder}/{name}"
    h = _sha1(src)
    tex = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if tex is None or unreal.EditorAssetLibrary.get_metadata_tag(tex, HASH_TAG) != h:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", src)
        task.set_editor_property("destination_path", folder)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        tex = unreal.EditorAssetLibrary.load_asset(path)
        if tex is None:
            hg.warn(f"scan import failed: {src}")
            return None
        unreal.EditorAssetLibrary.set_metadata_tag(tex, HASH_TAG, h)
    # Settings every time (cheap): colour maps sRGB, data maps linear, normals as normal maps.
    if kind == "diff":
        tex.set_editor_property("srgb", True)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    elif kind == "nor":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("flip_green_channel", bool(flip_green))
    else:
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    try:
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
        tex.set_editor_property("max_texture_size", 2048)
    except Exception as exc:  # property names vary a little between engine versions
        hg.warn(f"scan {name}: {exc}")
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    return tex


def import_scans() -> dict:
    """role -> {"id", "diff", "nor", "rough" (texture or None), "rough_channel" ('R' or 'G' for ARM)}."""
    root = _root()
    out = {}
    if not os.path.isdir(root):
        hg.log("Poly Haven scans: Data/PolyHaven missing - procedural materials")
        return out
    for role, ids in ROLES.items():
        for pid in ids:
            folder = os.path.join(root, pid)
            if not os.path.isdir(folder):
                continue
            diff = _pick(folder, ("_diff", "_col", "_albedo", "_basecolor"))
            if diff is None:
                continue
            nor = _pick(folder, ("_nor_dx",))
            flip = False
            if nor is None:
                nor = _pick(folder, ("_nor_gl", "_normal"))
                flip = nor is not None
            rough = _pick(folder, ("_rough",))
            chan = "R"
            if rough is None:
                rough = _pick(folder, ("_arm",))
                chan = "G"
            dest = f"{DEST}/{pid}"
            entry = {"id": pid, "rough_channel": chan,
                     "diff": _import(diff, dest, f"T_{pid}_diff", "diff"),
                     "nor": _import(nor, dest, f"T_{pid}_nor", "nor", flip) if nor else None,
                     "rough": _import(rough, dest, f"T_{pid}_rough", "rough") if rough else None}
            if entry["diff"] is not None:
                out[role] = entry
                break
    hg.log("Poly Haven scans: " + (", ".join(f"{r}={e['id']}" for r, e in out.items()) or "none on disk"))
    return out
