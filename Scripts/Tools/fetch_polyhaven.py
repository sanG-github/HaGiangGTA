#!/usr/bin/env python3
"""Fetch the CC0 Poly Haven assets used by HaGiangGTA (Round 7A) into Data/PolyHaven/<id>/.

Stdlib only. Uses the public API (api.polyhaven.com /files/<id>) with a generic User-Agent and sends nothing else.
Idempotent: a file that is already present with the right md5 is skipped. Downloads are kept as data only - nothing
from them is ever executed.

    python3 Scripts/Tools/fetch_polyhaven.py --dry-run   # list files + total size, download nothing
    python3 Scripts/Tools/fetch_polyhaven.py             # download (hard cap 1 GB), write the manifest

All Poly Haven assets are CC0 (https://polyhaven.com/license) - see Data/POLYHAVEN_CREDITS.md.
"""
import argparse
import hashlib
import json
import os
import sys
import time
import urllib.request

API = "https://api.polyhaven.com"
UA = "asset-fetch/1.0"
CAP_BYTES = 1_000_000_000
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "Data", "PolyHaven")
MANIFEST = os.path.join(ROOT, "Data", "polyhaven_manifest.json")

# Models: glTF 1K (mesh + 1K diffuse / normal (GL) / ARM). Grey rock first - the Ha Giang karst look (ART_BIBLE).
MODELS = [
    # cliffs / rock faces
    "rock_face_01", "rock_face_02", "mountainside", "namaqualand_cliff_01", "namaqualand_cliff_02",
    # boulders / outcrops / scatter rocks
    "namaqualand_boulder_02", "namaqualand_boulder_03", "namaqualand_boulder_04", "namaqualand_boulder_05",
    "namaqualand_boulder_06", "namaqualand_rocks_01", "rock_moss_set_01", "rock_moss_set_02", "boulder_01",
    "rock_07", "rock_09", "stone_01",
    # trees / plants
    "fir_tree_01", "fir_sapling_medium", "fern_02", "dry_branches_medium_01",
    "dead_tree_trunk", "shrub_02", "shrub_03", "shrub_04", "grass_medium_01", "grass_medium_02",
]
# Textures: 2K jpg (diffuse, normal GL, ARM) for B's terrain / road / house materials and the cliff re-tint.
TEXTURES = [
    "marble_cliff_01", "marble_cliff_03", "marble_cliff_05", "lichen_rock", "gray_rocks", "dry_ground_rocks",
    "brown_mud_dry", "aerial_grass_rock", "dry_riverbed_rock", "rocky_terrain_02", "sparse_grass",
    "forrest_ground_01", "asphalt_02",
]
MODEL_RES = "1k"
# The fir's glTF .bin is 478 MB (every needle modelled); its FBX is 270 MB. pine_tree_01 (958 MB) and
# tree_small_02 (101 MB) are left out for the 1 GB cap.
MODEL_FORMAT = {"fir_tree_01": "fbx"}
TEX_RES = "2k"
TEX_MAPS = ["Diffuse", "nor_gl", "arm"]
# Alpha maps of the plants (the glTF set references JPG diffuse maps without alpha; prep_scan_alpha.py merges these
# into RGBA PNGs). Fetched into <id>/textures/ with --alpha only.
ALPHA_MAPS = {"shrub_02": "Alpha", "fern_02": "Alpha", "grass_medium_01": "Alpha", "grass_medium_02": "Alpha",
              "fir_sapling_medium": "twigs_alpha"}


def get_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


def plan_files(asset_id, kind):
    """[(relative path under Data/PolyHaven/<id>, url, size, md5)] for one asset."""
    files = get_json(f"{API}/files/{asset_id}")
    out = []
    if kind == "model":
        fmt = MODEL_FORMAT.get(asset_id, "gltf")
        g = files[fmt][MODEL_RES][fmt]
        out.append((os.path.basename(g["url"]), g["url"], g["size"], g["md5"]))
        for rel, f in g.get("include", {}).items():
            out.append((rel, f["url"], f["size"], f["md5"]))
    elif kind == "alpha":
        f = files[ALPHA_MAPS[asset_id]][MODEL_RES]["jpg"]
        out.append(("textures/" + os.path.basename(f["url"]), f["url"], f["size"], f["md5"]))
    else:
        for m in TEX_MAPS:
            f = files[m][TEX_RES]["jpg"]
            out.append((os.path.basename(f["url"]), f["url"], f["size"], f["md5"]))
    return out


def md5_of(path):
    h = hashlib.md5()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def safe_join(base, rel):
    p = os.path.normpath(os.path.join(base, rel))
    if not p.startswith(base + os.sep):
        raise ValueError(f"unsafe path in API listing: {rel}")
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--alpha", action="store_true", help="also fetch the plants' alpha maps (ALPHA_MAPS)")
    args = ap.parse_args()

    plan = []
    groups = [("model", MODELS), ("texture", TEXTURES)] + ([("alpha", list(ALPHA_MAPS))] if args.alpha else [])
    for kind, ids in groups:
        for asset_id in ids:
            try:
                for rel, url, size, md5 in plan_files(asset_id, kind):
                    plan.append((asset_id, kind, rel, url, size, md5))
            except (KeyError, OSError, ValueError) as e:
                print(f"  skip {asset_id}: {e}", file=sys.stderr)
            time.sleep(0.1)
    total = sum(p[4] for p in plan)
    per = {}
    for asset_id, kind, rel, url, size, md5 in plan:
        per.setdefault((asset_id, kind), 0)
        per[(asset_id, kind)] += size
    for (asset_id, kind), b in per.items():
        print(f"{kind:8s} {asset_id:28s} {b / 1e6:7.1f} MB")
    print(f"TOTAL {len(per)} assets, {len(plan)} files, {total / 1e6:.1f} MB (cap {CAP_BYTES / 1e6:.0f} MB)")
    if total > CAP_BYTES:
        print("over the 1 GB cap - trim the lists", file=sys.stderr)
        return 1
    if args.dry_run:
        return 0

    manifest = {}
    for asset_id, kind, rel, url, size, md5 in plan:
        if not url.startswith("https://dl.polyhaven.org/"):
            print(f"  refuse non-Poly Haven url {url}", file=sys.stderr)
            continue
        base = os.path.join(OUT, asset_id)
        dst = safe_join(base, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if not (os.path.exists(dst) and os.path.getsize(dst) == size and md5_of(dst) == md5):
            req = urllib.request.Request(url, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=300) as r, open(dst + ".part", "wb") as fh:
                while True:
                    chunk = r.read(1 << 20)
                    if not chunk:
                        break
                    fh.write(chunk)
            if md5_of(dst + ".part") != md5:
                os.remove(dst + ".part")
                print(f"  md5 mismatch {asset_id}/{rel}", file=sys.stderr)
                continue
            os.replace(dst + ".part", dst)
            print(f"  got {asset_id}/{rel} ({size / 1e6:.1f} MB)")
        if kind == "alpha":
            kind = "model"
        e = manifest.setdefault(asset_id, {"kind": kind, "resolution": MODEL_RES if kind == "model" else TEX_RES,
                                           "format": MODEL_FORMAT.get(asset_id, "gltf") if kind == "model" else "jpg",
                                           "files": [], "bytes": 0, "license": "CC0",
                                           "source": f"https://polyhaven.com/a/{asset_id}"})
        e["files"].append(rel)
        e["bytes"] += size
    with open(MANIFEST, "w") as fh:
        json.dump(manifest, fh, indent=1, sort_keys=True)
    print(f"manifest: {MANIFEST} ({len(manifest)} assets)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
