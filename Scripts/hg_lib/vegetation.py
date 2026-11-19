"""Round 6B (agent A): Ha Giang karst vegetation from Scripts/Tools/gen_vegetation.py.

Limestone outcrops scattered densely over the slopes with scrub and grass in the cracks, po mu / cypress stands on
the ridges, bamboo clumps by the hamlets, wild banana in the valleys, green and dry grass tufts on the verges.
One HISM per mesh (shared transforms for trunk + crown), distance-culled, LOD chains from assets.LOD_CHAINS.
"""
from __future__ import annotations

import json
import os

from . import editor_utils as hg

VEG_JSON = os.path.join("Generated", "Heightmaps", "HaGiang_vegetation.json")


def _load():
    path = os.path.join(hg.project_dir(), VEG_JSON)
    if not os.path.isfile(path):
        return None
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def mesh_names():
    """Vegetation meshes to import (empty when gen_vegetation.py has not run)."""
    data = _load()
    return sorted(data["species"].keys()) if data else []


def build(meshes, mats, tag, folder):
    from . import world   # late import: world imports a lot
    data = _load()
    if not data:
        hg.warn("no HaGiang_vegetation.json: run Scripts/Tools/gen_vegetation.py")
        return
    total = 0
    for name, spec in data["species"].items():
        items = data["instances"].get(name, [])
        mesh = meshes.get(name)
        mat = mats.get(spec["material"])
        if not items or mesh is None:
            continue
        cull = spec["cull"]
        world.spawn_scatter(f"Veg_{name[10:]}", mesh, mat, items, tag, folder, cull=(float(cull[0]), float(cull[1])),
                            collision=False, shadows=bool(spec["shadows"]))
        total += len(items)
    hg.log(f"Vegetation: {total} instances in {len(data['species'])} meshes")
