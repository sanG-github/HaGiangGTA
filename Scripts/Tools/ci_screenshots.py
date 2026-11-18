"""Editor-side viewpoint screenshots (UE Python, runs unattended then quits).

Loads a checkpoint map, waits for shaders/streaming, then takes high-res screenshots
from the same six cameras as Scripts/Tools/render_viewpoints.py (player eye + flycam
for each viewpoint). Output: Saved/HG_CI/Shots/<map>/<name>.png

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
    layout, scatter = load("HaGiang_layout.json"), load("HaGiang_scatter.json")
    vps = scatter["viewpoints"]
    river = [p[:2] for p in layout["water"]["river_cm"]]
    water_z = layout["water"]["surface_z_cm"]

    def nearest(xy):
        return min(range(len(river)), key=lambda i: (river[i][0] - xy[0]) ** 2 + (river[i][1] - xy[1]) ** 2)

    shots = []
    p1 = layout["viewpoints"]["doc_tham_ma"]["location_cm"]; c1 = vps["doc_tham_ma"]["center_cm"]
    dx, dy = norm2(c1[0] - p1[0], c1[1] - p1[1])
    shots.append(("VP1_ThamMa_eye", v(p1[0] + dx * 1000, p1[1] + dy * 1000, p1[2] + 170), v(c1[0], c1[1], c1[2] - 4000), 62))
    shots.append(("VP1_ThamMa_flycam", v(c1[0] - dx * 18000 + 12000, c1[1] - dy * 18000, c1[2] + 26000), v(*c1), 50))

    p2 = layout["viewpoints"]["ma_pi_leng"]["location_cm"]; r2 = layout["viewpoints"]["ma_pi_leng"]["road_cm"]
    sx, sy = norm2(p2[0] - r2[0], p2[1] - r2[1])
    j = nearest(p2)
    down = river[max(j - 32, 0)]
    shots.append(("VP2_MaPiLeng_spur", v(p2[0] + sx * 400, p2[1] + sy * 400, p2[2] + 170), v(down[0], down[1], water_z + 3000), 78))
    shots.append(("VP2_MaPiLeng_drone", v(river[j][0] + 6000, river[j][1] - 26000, p2[2] + 4000),
                  v((p2[0] + river[j][0]) / 2, (p2[1] + river[j][1]) / 2, 98000), 60))

    # Classic photo: from the Ma Pi Leng rim down onto the river and the Ta Lang dock.
    dock = layout["spawns"]["dock"]["location_cm"]
    rim_pts = layout["road"]["points_cm"]
    near = [p for p in rim_pts if (p[0] - dock[0]) ** 2 + (p[1] - dock[1]) ** 2 < 70000.0 ** 2] or rim_pts
    top = max(near, key=lambda p: p[2])          # highest road point within 700 m of the dock
    ox, oy = norm2(dock[0] - top[0], dock[1] - top[1])
    shots.append(("VP3_TaLang_from_rim", v(top[0] + ox * 2500, top[1] + oy * 2500, top[2] + 2500), v(dock[0], dock[1], dock[2]), 55))
    # Tham Ma like the reference photo: standing at the parapet, looking out level over the bends.
    shots.append(("VP1_ThamMa_parapet", v(p1[0] + dx * 750, p1[1] + dy * 750, p1[2] + 165),
                  v(c1[0] + dx * 30000, c1[1] + dy * 30000, c1[2] - 3000), 75))

    b = layout["spawns"]["boats"][0]["location_cm"]; k = nearest(b)
    ahead = river[max(k - 45, 0)]
    # Just behind and above the cano (not inside its hull), looking up-canyon.
    bx, by = norm2(ahead[0] - b[0], ahead[1] - b[1])
    shots.append(("VP3_TuSan_boat", v(b[0] - bx * 1700, b[1] - by * 1700, water_z + 650), v(ahead[0], ahead[1], water_z + 14000), 70))
    shots.append(("VP3_TuSan_flycam", v(river[k][0] - 22000, river[k][1] + 12000, water_z + 42000),
                  v(river[max(k - 25, 0)][0], river[max(k - 25, 0)][1], water_z), 55))
    return shots


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
                if isinstance(a, (unreal.HGViewpointActor, unreal.PlayerStart)):
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
            self.cam.get_cine_camera_component().set_editor_property("field_of_view", float(fov))
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
