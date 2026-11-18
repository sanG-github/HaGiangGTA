"""Run a checkpoint build script inside the editor unattended, record the result, then quit.

Launched by Scripts/run_checkpoint.sh --ci <NN>:
  UnrealEditor HaGiangGTA.uproject -ExecutePythonScript=Scripts/Tools/ci_run_checkpoint.py -unattended -nocrashreports
with HG_CP=<NN> in the environment. Writes Saved/HG_CI/cp<NN>.txt ("OK" or "FAIL" + traceback).
"""
import os
import traceback

import unreal

cp = os.environ.get("HG_CP", "01")
project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
script = os.path.join(project, "Scripts", "Checkpoints", f"build_cp{cp}.py")
out_dir = os.path.join(project, "Saved", "HG_CI")
os.makedirs(out_dir, exist_ok=True)

try:
    with open(script, "r", encoding="utf-8") as f:
        code = compile(f.read(), script, "exec")
    exec(code, {"__file__": script, "__name__": "__main__"})
    status = "OK"
except Exception:
    status = "FAIL\n" + traceback.format_exc()

with open(os.path.join(out_dir, f"cp{cp}.txt"), "w", encoding="utf-8") as f:
    f.write(status)
unreal.log(f"[HG_CI] cp{cp}: {status.splitlines()[0]}")
if os.environ.get("HG_CI_KEEP_OPEN") != "1":
    unreal.SystemLibrary.quit_editor()
