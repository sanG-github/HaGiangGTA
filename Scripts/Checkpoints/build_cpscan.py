"""Round 7A: import the Poly Haven scans (hg_lib.scans) and write Saved/HG_CI/scans.json (bounds, LODs, materials)."""
import importlib
import json
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Scripts"))
from hg_lib import editor_utils as hg, scan_meshes as scans  # noqa: E402
importlib.reload(scans)

res = scans.import_all(force=os.environ.get("HG_SCAN_FORCE") == "1")
report = {}
for asset_id, meshes in res.items():
    report[asset_id] = {"role": scans.role_of(asset_id), "meshes": [scans.describe(m) for m in meshes]}
    for m in meshes[:1]:
        for sm in m.get_editor_property("static_materials")[:1]:
            mi = sm.get_editor_property("material_interface")
            if isinstance(mi, unreal.MaterialInstanceConstant):
                report[asset_id]["vector_params"] = [
                    (str(v.get_editor_property("parameter_info").get_editor_property("name")),
                     [round(v.get_editor_property("parameter_value").r, 3), round(v.get_editor_property("parameter_value").g, 3),
                      round(v.get_editor_property("parameter_value").b, 3)])
                    for v in mi.get_editor_property("vector_parameter_values")]
                report[asset_id]["parent"] = mi.get_editor_property("parent").get_path_name() if mi.get_editor_property("parent") else ""
out = os.path.join(hg.project_dir(), "Saved", "HG_CI", "scans.json")
with open(out, "w") as f:
    json.dump(report, f, indent=1)
hg.log(f"[HG_SCAN] {len(report)} scans, {sum(len(v['meshes']) for v in report.values())} meshes -> {out}")
