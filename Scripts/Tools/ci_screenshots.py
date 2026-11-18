"""Editor-side viewpoint screenshots (UE Python, runs unattended then quits).

Loads a checkpoint map, waits for shaders/streaming, then takes high-res screenshots
from the shared review shots in Scripts/hg_lib/shots.py (same framing as the offline
renderer, one per reference photo). Output: Saved/HG_CI/Shots/<map>/<name>.png

  HG_MAP=/Game/Maps/L_HaGiang_CP05 UnrealEditor HaGiangGTA.uproject \
      -ExecutePythonScript=Scripts/Tools/ci_screenshots.py -nocrashreports
"""
import json
import math
import os

import unreal

MAP = os.environ.get("HG_MAP", "/Game/Maps/L_HaGiang_CP05")
RES = (1600, 900)
PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJECT, "Saved", "HG_CI", "Shots", MAP.rsplit("/", 1)[-1])
os.makedirs(OUT, exist_ok=True)


def load(name):
    with open(os.path.join(PROJECT, "Generated", "Heightmaps", name), "r", encoding="utf-8") as f:
        return json.load(f)


def v(x, y, z):
    return unreal.Vector(float(x), float(y), float(z))


def norm2(dx, dy):
    d = math.hypot(dx, dy) or 1.0
    return dx / d, dy / d


def cameras():
    """Same shots as the offline renderer (Scripts/hg_lib/shots.py), converted to cm."""
    import sys
    sys.path.insert(0, os.path.join(PROJECT, "Scripts"))
    from hg_lib import shots as shot_lib
    layout, scatter = load("HaGiang_layout.json"), load("HaGiang_scatter.json")
    return [(name, v(*[c * 100.0 for c in cam]), v(*[c * 100.0 for c in look]), fov)
            for name, cam, look, fov in shot_lib.shots(layout, scatter)]


class Shooter:
    def __init__(self):
        self.shots = cameras()
        self.i = 0
        self.wait = 0
        self.cam = None
        self.state = "load"
        self.busy = False
        self.done = False
        # -ExecutePythonScript quits the editor as soon as the script returns unless told otherwise.
        unreal.EditorPythonScripting.set_keep_python_script_alive(True)
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def tick(self, dt):
        # Loading/flush calls pump Slate, which re-enters this callback: ignore nested ticks.
        if self.busy or self.done:
            return
        self.busy = True
        try:
            self.step()
        except Exception as exc:  # never leave the editor hanging
            unreal.log_error(f"[HG_SHOTS] {exc}")
            self.finish()
        finally:
            self.busy = False

    def step(self):
        if self.state == "load":
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(MAP)
            # Editor-only helpers (camera proxy meshes, trigger spheres) must not show up in the shots.
            for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
                # NPCs / vehicles are only painted at BeginPlay (dynamic materials): hide them in editor shots.
                if isinstance(a, (unreal.HGViewpointActor, unreal.PlayerStart, unreal.HGTouristNPC,
                                  unreal.HGVehicleBase, unreal.HGCowActor, unreal.HGHappyWaterPickup)):
                    a.set_is_temporarily_hidden_in_editor(True)
            self.state, self.wait = "warm", 0
            return
        if self.state == "warm":
            self.wait += 1
            if self.wait == 5:
                unreal.AutomationLibrary.finish_loading_before_screenshot()
            if self.wait < 120:
                return
            self.state = "shoot"
            return
        if self.state == "shoot":
            if self.i >= len(self.shots):
                self.finish()
                return
            if self.wait > 0:
                self.wait -= 1
                return
            name, loc, look, fov = self.shots[self.i]
            rot = unreal.MathLibrary.find_look_at_rotation(loc, look)
            if self.cam is None:
                self.cam = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(
                    unreal.CineCameraActor, loc, rot)
            self.cam.set_actor_location_and_rotation(loc, rot, False, False)
            cc = self.cam.get_cine_camera_component()
            # A CineCamera ignores field_of_view: set the focal length for this horizontal FOV instead.
            fb = cc.get_editor_property("filmback")
            fb.set_editor_property("sensor_width", 36.0)
            fb.set_editor_property("sensor_height", 20.25)
            cc.set_editor_property("filmback", fb)
            cc.set_editor_property("current_focal_length", 18.0 / math.tan(math.radians(float(fov)) * 0.5))
            unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(loc, rot)
            self.i += 1
            unreal.AutomationLibrary.take_high_res_screenshot(RES[0], RES[1], os.path.join(OUT, f"{name}.png"), self.cam)
            unreal.log(f"[HG_SHOTS] {name}")
            self.wait = 150   # let the screenshot + streaming settle before moving the camera

    def finish(self):
        if self.done:
            return
        self.done = True
        unreal.unregister_slate_post_tick_callback(self.handle)
        unreal.log(f"[HG_SHOTS] done -> {OUT}")
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
        unreal.SystemLibrary.quit_editor()


Shooter()
