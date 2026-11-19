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
# Quitting from inside the script (frame 2) crashed the editor on EVERY run at static teardown: a Slate async-task
# notification (asset / shader compile toast) was still ticking when Slate shut down, and its ticker delegate
# released a widget tree after Slate was gone (SNotificationExtendable / FSlateAsyncTaskNotificationImpl in the
# callstack). The crash happens after the command line is released, so -nocrashreports no longer applied and a
# CrashReportClient started each time and spun at ~95 % CPU. Let the editor tick until the asset/shader
# compilers are idle and the toasts have faded (>= 12 s, at most 180 s), then quit.
if os.environ.get("HG_CI_KEEP_OPEN") != "1":
    _state = {"t": 0.0, "idle": 0.0, "handle": None}

    def _busy():
        try:
            if unreal.AssetCompilingManager.get().get_num_remaining_assets() > 0:   # may not be exposed
                return True
        except Exception:
            pass
        try:
            return unreal.ShaderCompilingManager.get().get_num_remaining_jobs() > 0   # may not be exposed
        except Exception:
            return False

    def _tick(dt):
        dt = min(dt, 0.5)   # the first tick carries the whole script's frame time (~200 s)
        _state["t"] += dt
        _state["idle"] = 0.0 if _busy() else _state["idle"] + dt
        if (_state["t"] >= 12.0 and _state["idle"] >= 8.0) or _state["t"] >= 180.0:
            unreal.unregister_slate_post_tick_callback(_state["handle"])
            unreal.log(f"[HG_CI] quitting after {_state['t']:.0f} s (idle {_state['idle']:.0f} s)")
            unreal.SystemLibrary.quit_editor()

    _state["handle"] = unreal.register_slate_post_tick_callback(_tick)
