#!/usr/bin/env python3
"""Round 7A: give the scanned plants their cut-out alpha (run with .venv/bin/python, needs Pillow).

Poly Haven's glTF downloads reference JPG diffuse maps, which carry no alpha, so leaf / needle / grass cards
rendered as solid quads. For every model whose glTF material is MASK or BLEND and whose `<map>_alpha_1k.jpg` is
present (fetch_polyhaven.py, ALPHA_MAPS), write `<diffuse>_rgba_1k.png` (diffuse + alpha) and a copy of the glTF,
`<id>_1k_alpha.gltf`, that points at it and uses MASK (no translucency on thousands of tufts). hg_lib.scans imports
the `_alpha` copy when it exists. The downloaded files themselves are not modified.
"""
import json
import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA = os.path.join(ROOT, "Data", "PolyHaven")


def prep(asset_id: str) -> bool:
    base = os.path.join(DATA, asset_id)
    src = next((os.path.join(base, f) for f in sorted(os.listdir(base)) if f.endswith("_1k.gltf")), None)
    if not src:
        return False
    with open(src, "r", encoding="utf-8") as fh:
        g = json.load(fh)
    cut = [m for m in g.get("materials", []) if m.get("alphaMode") in ("MASK", "BLEND")]
    if not cut:
        return False
    changed = False
    for m in cut:
        tex = m.get("pbrMetallicRoughness", {}).get("baseColorTexture")
        if tex is None:
            continue
        img = g["images"][g["textures"][tex["index"]]["source"]]
        uri = img["uri"]
        if not uri.endswith("_diff_1k.jpg"):
            continue
        alpha = os.path.join(base, uri.replace("_diff_1k.jpg", "_alpha_1k.jpg"))
        if not os.path.isfile(alpha):
            print(f"  {asset_id}: no {os.path.basename(alpha)} (fetch_polyhaven.py --alpha)")
            continue
        out_uri = uri.replace("_diff_1k.jpg", "_diff_rgba_1k.png")
        rgb = Image.open(os.path.join(base, uri)).convert("RGB")
        a = Image.open(alpha).convert("L").resize(rgb.size)
        rgba = rgb.copy()
        rgba.putalpha(a)
        rgba.save(os.path.join(base, out_uri))
        img["uri"] = out_uri
        img["mimeType"] = "image/png"
        m["alphaMode"] = "MASK"
        m["alphaCutoff"] = 0.4
        m["doubleSided"] = True
        changed = True
        print(f"  {asset_id}: {os.path.basename(out_uri)}")
    if changed:
        dst = src[:-5] + "_alpha.gltf"
        with open(dst, "w", encoding="utf-8") as fh:
            json.dump(g, fh, indent=1)
    return changed


def main():
    n = 0
    for asset_id in sorted(os.listdir(DATA)):
        if os.path.isdir(os.path.join(DATA, asset_id)):
            n += prep(asset_id)
    print(f"{n} plant models with cut-out alpha")
    return 0


if __name__ == "__main__":
    sys.exit(main())
