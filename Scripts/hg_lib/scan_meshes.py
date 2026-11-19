"""Round 7A (agent A): photoscanned CC0 assets from Poly Haven (Scripts/Tools/fetch_polyhaven.py -> Data/PolyHaven).

Imports each scanned model into /Game/HaGiang/PolyHaven/<id>/ (sha1 of the source file kept as a metadata tag:
reimported only when the download changes), gives the M1 profile a classic LOD chain and cheap collision, and
re-tints the warm scans towards grey Ha Giang limestone through their material instances' base-colour factor.
The downloads are data only; nothing in them is executed.
"""
from __future__ import annotations

import hashlib
import json
import os

import unreal

from . import editor_utils as hg

SCAN_ROOT = "/Game/HaGiang/PolyHaven"
HASH_TAG = "HGSourceHash"

# role -> ids. Cliffs: tall faces for the gorge walls / summits; boulders: 1-4 m; outcrops: small rock sets for the
# slope scatter (replace the procedural blades where the scan set has an equivalent).
ROLES = {
    "cliff": ["rock_face_01", "rock_face_02", "mountainside", "namaqualand_cliff_01", "namaqualand_cliff_02"],
    "boulder": ["namaqualand_boulder_02", "namaqualand_boulder_03", "namaqualand_boulder_04", "namaqualand_boulder_05",
                "namaqualand_boulder_06", "boulder_01"],
    "outcrop": ["namaqualand_rocks_01", "rock_moss_set_01", "rock_moss_set_02", "rock_07", "rock_09", "stone_01"],
    # fir_tree_01 is left out: every needle is modelled (238 MB FBX) and building its LODs took 47 min and timed
    # the import out - fir_sapling_medium (3-4 m firs, 3 variants) is the conifer.
    # fir_sapling_medium (480-780k verts per tree) is downloaded but not imported for now: decimated far enough
    # for 700 ridge trees its needles read as bare sticks. The procedural cypress / pines stay.
    "shrub": ["shrub_02", "shrub_03", "shrub_04"],
    "fern": ["fern_02"],
    "grass": ["grass_medium_01", "grass_medium_02"],
    "deadwood": ["dry_branches_medium_01", "dead_tree_trunk"],
}
ROCK_ROLES = ("cliff", "boulder", "outcrop")
# Base-colour multiply per role: pulls the warm (Namaqualand / Verdant Trail) scans towards the cool grey of
# Ha Giang limestone (ART_BIBLE: grey karst, not red sandstone).
TINT = {"cliff": (0.80, 0.84, 0.90), "boulder": (0.80, 0.84, 0.90), "outcrop": (0.82, 0.86, 0.90)}
LOD_CHAIN = {
    "cliff": [(1.0, 1.0), (0.3, 0.4), (0.08, 0.15), (0.02, 0.05)],
    "boulder": [(1.0, 1.0), (0.3, 0.3), (0.08, 0.1), (0.02, 0.03)],
    "outcrop": [(1.0, 1.0), (0.3, 0.3), (0.08, 0.1), (0.02, 0.03)],
    "tree": [(1.0, 1.0), (0.25, 0.3), (0.06, 0.1), (0.015, 0.03)],
    # 480-780k verts per sapling (modelled needles): drop fast - LOD1 at 50 % screen height already 10 %.
    "sapling": [(1.0, 1.0), (0.1, 0.5), (0.02, 0.2), (0.005, 0.07)],
    "shrub": [(1.0, 1.0), (0.3, 0.25), (0.08, 0.08)],
    "fern": [(1.0, 1.0), (0.3, 0.2), (0.1, 0.06)],
    "grass": [(1.0, 1.0), (0.3, 0.2), (0.1, 0.06)],
    "deadwood": [(1.0, 1.0), (0.3, 0.2), (0.08, 0.06)],
}


def data_dir() -> str:
    return os.path.join(hg.project_dir(), "Data", "PolyHaven")


def manifest() -> dict:
    path = os.path.join(hg.project_dir(), "Data", "polyhaven_manifest.json")
    if not os.path.isfile(path):
        return {}
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def role_of(asset_id: str) -> str:
    return next((r for r, ids in ROLES.items() if asset_id in ids), "")


def source_file(asset_id: str) -> str:
    base = os.path.join(data_dir(), asset_id)
    if not os.path.isdir(base):
        return ""
    # prep_scan_alpha.py's copy with the cut-out alpha wired in comes first.
    for f in sorted(os.listdir(base)):
        if f.endswith("_alpha.gltf"):
            return os.path.join(base, f)
    for ext in (".gltf", ".fbx", ".glb"):
        for f in sorted(os.listdir(base)):
            if f.endswith(ext):
                return os.path.join(base, f)
    return ""


def _sha1(path: str) -> str:
    h = hashlib.sha1()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    # glTF: the mesh lives in the .bin next to it - hash that too.
    if path.endswith(".gltf"):
        b = path[:-5].replace("_alpha", "") + ".bin"
        if os.path.isfile(b):
            with open(b, "rb") as fh:
                for chunk in iter(lambda: fh.read(1 << 20), b""):
                    h.update(chunk)
    return h.hexdigest()


def folder_of(asset_id: str) -> str:
    return f"{SCAN_ROOT}/{asset_id}"


def _assets_in(folder: str, cls):
    out = []
    for p in unreal.EditorAssetLibrary.list_assets(folder, recursive=True, include_folder=False):
        a = unreal.EditorAssetLibrary.load_asset(p)
        if isinstance(a, cls):
            out.append(a)
    return out


def static_meshes(asset_id: str):
    return sorted(_assets_in(folder_of(asset_id), unreal.StaticMesh), key=lambda m: m.get_name())


def _set_lods(mesh, chain):
    sm_sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    settings = mesh.get_editor_property("nanite_settings")
    if settings.get_editor_property("enabled") != hg.USE_NANITE:
        settings.set_editor_property("enabled", hg.USE_NANITE)
        mesh.set_editor_property("nanite_settings", settings)
    if hg.USE_NANITE or sm_sub.get_lod_count(mesh) == len(chain):
        return
    opts = unreal.StaticMeshReductionOptions()
    opts.set_editor_property("auto_compute_lod_screen_size", False)
    lst = []
    for pct, screen in chain:
        s = unreal.StaticMeshReductionSettings()
        s.set_editor_property("percent_triangles", float(pct))
        s.set_editor_property("screen_size", float(screen))
        lst.append(s)
    opts.set_editor_property("reduction_settings", lst)
    sm_sub.set_lods(mesh, opts)


def _tint_materials(mesh, tint):
    """Multiply the base colour of the mesh's (Interchange-created) material instances by `tint`."""
    done = 0
    for sm in mesh.get_editor_property("static_materials"):
        mi = sm.get_editor_property("material_interface")
        if not isinstance(mi, unreal.MaterialInstanceConstant):
            continue
        for pname in ("BaseColorFactor", "Base Color Factor", "BaseColor_Factor"):
            try:
                cur = unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(mi, pname)
                ok = unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
                    mi, pname, unreal.LinearColor(tint[0], tint[1], tint[2], cur.a if cur else 1.0))
                if ok:
                    unreal.MaterialEditingLibrary.update_material_instance(mi)
                    unreal.EditorAssetLibrary.save_loaded_asset(mi)
                    done += 1
                    break
            except Exception:
                continue
    return done


def import_scan(asset_id: str, force: bool = False):
    """Import (or reuse) one scanned model; returns its static meshes."""
    src = source_file(asset_id)
    if not src:
        hg.warn(f"scan {asset_id}: no source in Data/PolyHaven (run Scripts/Tools/fetch_polyhaven.py)")
        return []
    folder = folder_of(asset_id)
    digest = _sha1(src)
    meshes = [] if force else static_meshes(asset_id)
    if meshes and any(unreal.EditorAssetLibrary.get_metadata_tag(m, HASH_TAG) != digest for m in meshes):
        hg.log(f"scan {asset_id}: source changed since import -> reimport")
        unreal.EditorAssetLibrary.delete_directory(folder)
        meshes = []
    if not meshes:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", src)
        task.set_editor_property("destination_path", folder)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", True)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        meshes = static_meshes(asset_id)
        if not meshes:
            hg.warn(f"scan {asset_id}: import produced no static mesh")
            return []
        role = role_of(asset_id)
        sm_sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        for m in meshes:
            unreal.EditorAssetLibrary.set_metadata_tag(m, HASH_TAG, digest)
            # Collision: rocks get a simple convex hull (complex-as-simple on a 100k-tri scan is costly for the
            # vehicles' sweeps); plants none.
            sm_sub.remove_collisions(m)
            if role in ROCK_ROLES:
                sm_sub.add_simple_collisions(m, unreal.ScriptCollisionShapeType.NDOP26)
            unreal.EditorAssetLibrary.save_loaded_asset(m)
    role = role_of(asset_id)
    for m in meshes:   # idempotent: re-applied on every build (an interrupted first import kept no tint)
        _set_lods(m, LOD_CHAIN.get(role, LOD_CHAIN["outcrop"]))
        if role in TINT:
            _tint_materials(m, TINT[role])
        if role == "grass":
            _masked_not_translucent(m)
        unreal.EditorAssetLibrary.save_loaded_asset(m)
    return meshes


def _masked_not_translucent(mesh):
    """Interchange gives the glTF grass a translucent (Blend) instance: sorting and overdraw on thousands of tufts.
    Reparent to the matching masked instance (same parameter set)."""
    for sm in mesh.get_editor_property("static_materials"):
        mi = sm.get_editor_property("material_interface")
        if not isinstance(mi, unreal.MaterialInstanceConstant):
            continue
        parent = mi.get_editor_property("parent")
        if parent is None or "Blend" not in parent.get_name():
            continue
        masked_path = parent.get_path_name().replace("Blend", "Mask")
        masked = unreal.EditorAssetLibrary.load_asset(masked_path)
        if masked is None:
            hg.warn(f"{mesh.get_name()}: no masked parent {masked_path}")
            continue
        unreal.MaterialEditingLibrary.set_material_instance_parent(mi, masked)
        unreal.MaterialEditingLibrary.update_material_instance(mi)
        unreal.EditorAssetLibrary.save_loaded_asset(mi)


# Pieces used by the scatter (world.spawn_scan_scatter): (role, [asset ids], [mesh-name filters or None]).
# Small pebbles (rock_07 / rock_09 / stone_01 / namaqualand_rocks_01, 10-25 cm) are not scaled up into 2-4 m
# outcrops (x20 texel stretch); the moss rock sets are 2-3 m already.
PIECES = {
    "cliff": ["rock_face_01", "rock_face_02", "mountainside", "namaqualand_cliff_01", "namaqualand_cliff_02"],
    "boulder": ["namaqualand_boulder_02", "namaqualand_boulder_03", "namaqualand_boulder_04", "namaqualand_boulder_05",
                "namaqualand_boulder_06", "boulder_01"],
    "outcrop": ["rock_moss_set_01", "rock_moss_set_02"],
    "sapling": ["fir_sapling_medium"],
    "shrub": ["shrub_02", "fern_02"],
    "grass": ["grass_medium_02", "grass_medium_01"],
}
PIECE_LIMIT = {"outcrop": 8, "shrub": 6, "grass": 6}


def pieces(role: str):
    """[(mesh, bounding box)] for a scatter role, [] when the scans are not imported."""
    out = []
    for asset_id in PIECES.get(role, []):
        for m in static_meshes(asset_id):
            if role == "grass" and "large" in m.get_name():
                continue   # grass_medium_01 large clumps: 7k verts each, mid / small ones are enough
            out.append((m, m.get_bounding_box()))
    lim = PIECE_LIMIT.get(role)
    if lim and len(out) > lim:
        step = len(out) / float(lim)
        out = [out[int(i * step)] for i in range(lim)]
    return out


def describe(mesh) -> dict:
    sm_sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    b = mesh.get_bounding_box()
    mats = []
    for sm in mesh.get_editor_property("static_materials"):
        mi = sm.get_editor_property("material_interface")
        mats.append(mi.get_path_name() if mi else "None")
    return {"name": mesh.get_name(), "min": [round(b.min.x), round(b.min.y), round(b.min.z)],
            "max": [round(b.max.x), round(b.max.y), round(b.max.z)],
            "lods": sm_sub.get_lod_count(mesh), "verts0": sm_sub.get_number_verts(mesh, 0), "materials": mats}


def import_all(force: bool = False) -> dict:
    """id -> [static meshes] for every model in the manifest that has a role."""
    out = {}
    for asset_id, e in manifest().items():
        if e.get("kind") != "model" or not role_of(asset_id):
            continue
        out[asset_id] = import_scan(asset_id, force)
    return out
