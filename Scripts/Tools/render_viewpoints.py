#!/usr/bin/env python3
"""Offline preview renderer for the 3 viewpoints (no Unreal needed).

Ray-marches the generated heightmap with a golden-hour sun (with terrain
shadows), slope-based limestone / grass / road albedo, emerald water with a
Fresnel sky reflection, aerial perspective and the low "sea of clouds" height
fog. It is a rough stand-in for the UE scene, used to judge terrain shape and
composition of each viewpoint before the engine is available.

Usage: .venv/bin/python Scripts/Tools/render_viewpoints.py [--width 960] [--stride 2]
Writes Generated/Previews/VP_*.png and VP_contact_sheet.png
"""
from __future__ import annotations

import argparse
import json
import math
import time
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
HM = ROOT / "Generated" / "Heightmaps"
OUT = ROOT / "Generated" / "Previews"

SUN_PITCH, SUN_YAW = -13.0, 120.0          # same as world.build_golden_hour
WATER_Z = 700.0


def normalize(v):
    return v / (np.linalg.norm(v, axis=-1, keepdims=True) + 1e-9)


class Field:
    def __init__(self, alt, stride):
        self.s = stride
        self.h = np.ascontiguousarray(alt[::stride, ::stride]).astype(np.float32)
        self.n = self.h.shape[0]
        self.half = (alt.shape[0] - 1) / 2.0
        gy, gx = np.gradient(self.h, float(stride))
        self.gx, self.gy = gx.astype(np.float32), gy.astype(np.float32)

    def _idx(self, x, y):
        fx = np.clip((x + self.half) / self.s, 0, self.n - 1.001)
        fy = np.clip((y + self.half) / self.s, 0, self.n - 1.001)
        c0 = fx.astype(np.int32); r0 = fy.astype(np.int32)
        return c0, r0, (fx - c0).astype(np.float32), (fy - r0).astype(np.float32)

    def sample(self, arr, x, y):
        c0, r0, tx, ty = self._idx(x, y)
        a = arr[r0, c0]; b = arr[r0, c0 + 1]; c = arr[r0 + 1, c0]; d = arr[r0 + 1, c0 + 1]
        return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty

    def height(self, x, y):
        return self.sample(self.h, x, y)

    def inside(self, x, y):
        return (np.abs(x) < self.half) & (np.abs(y) < self.half)


def march(field, origin, dirs, t_max=7000.0, max_iter=420):
    """Returns hit distance (inf = sky) and a flag for water hits."""
    n = dirs.shape[0]
    t = np.full(n, 0.5, np.float32)
    hit = np.full(n, np.inf, np.float32)
    water = np.zeros(n, bool)
    active = np.arange(n)
    ox, oy, oz = origin
    for _ in range(max_iter):
        if active.size == 0:
            break
        d = dirs[active]
        ta = t[active]
        x = ox + d[:, 0] * ta; y = oy + d[:, 1] * ta; z = oz + d[:, 2] * ta
        h = field.height(x, y)
        clear = z - np.maximum(h, WATER_Z)
        done = clear < 0.05 + ta * 0.0004
        out = (ta > t_max) | ~field.inside(x, y) | ((z > 2200) & (d[:, 2] > 0))
        if done.any():
            idx = active[done]
            hit[idx] = ta[done]
            water[idx] = h[done] < WATER_Z
        keep = ~(done | out)
        step = np.maximum(clear * 0.45, 0.4 + ta * 0.002)
        t[active[keep]] = ta[keep] + step[keep]
        active = active[keep]
    return hit, water


def shadow(field, pts, sun, max_iter=160):
    lit = np.ones(len(pts), np.float32)
    t = np.full(len(pts), 2.0, np.float32)
    active = np.arange(len(pts))
    for _ in range(max_iter):
        if active.size == 0:
            break
        p = pts[active]; ta = t[active]
        x = p[:, 0] + sun[0] * ta; y = p[:, 1] + sun[1] * ta; z = p[:, 2] + sun[2] * ta
        h = field.height(x, y)
        clear = z - h
        blocked = clear < 0
        lit[active[blocked]] = 0.0
        out = ~field.inside(x, y) | (z > 1700) | (ta > 6000)
        keep = ~(blocked | out)
        t[active[keep]] = ta[keep] + np.maximum(clear[keep] * 0.5, 1.5 + ta[keep] * 0.01)
        active = active[keep]
    return lit


def value_noise2(x, y, scale, seed=7):
    rng = np.random.default_rng(seed)
    g = rng.random((257, 257)).astype(np.float32)
    fx = (x / scale) % 256; fy = (y / scale) % 256
    i0 = np.minimum(fx.astype(np.int32), 255); j0 = np.minimum(fy.astype(np.int32), 255)
    tx = fx - i0; ty = fy - j0
    tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty)
    a = g[j0, i0]; b = g[j0, i0 + 1]; c = g[j0 + 1, i0]; d = g[j0 + 1, i0 + 1]
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty


def road_mask_raster(layout, field):
    pts = np.array(layout["road"]["points_cm"], float) / 100.0
    mask = np.zeros((field.n, field.n), np.float32)
    hw = layout["road"]["half_width_cm"] / 100.0
    for i in range(len(pts) - 1):
        a, b = pts[i, :2], pts[i + 1, :2]
        L = max(np.linalg.norm(b - a), 1e-6)
        for s in np.arange(0, L, field.s * 0.5):
            p = a + (b - a) * (s / L)
            c = int((p[0] + field.half) / field.s); r = int((p[1] + field.half) / field.s)
            k = max(1, int(round(hw / field.s)))
            mask[max(r - k, 0):r + k + 1, max(c - k, 0):c + k + 1] = 1.0
    return mask


def render(field, layout, cam, look, fov_deg, width, height, roadmask):
    t0 = time.time()
    cam = np.array(cam, np.float32); look = np.array(look, np.float32)
    fwd = normalize(look - cam)
    right = normalize(np.cross(fwd, [0, 0, 1]))
    up = np.cross(right, fwd)
    aspect = width / height
    tan = math.tan(math.radians(fov_deg) / 2)
    px = (np.arange(width) + 0.5) / width * 2 - 1
    py = 1 - (np.arange(height) + 0.5) / height * 2
    PX, PY = np.meshgrid(px * tan * aspect, py * tan)
    dirs = normalize(fwd[None, None, :] + PX[..., None] * right + PY[..., None] * up).reshape(-1, 3).astype(np.float32)

    sp, sy = math.radians(SUN_PITCH), math.radians(SUN_YAW)
    light_travel = np.array([math.cos(sp) * math.cos(sy), math.cos(sp) * math.sin(sy), math.sin(sp)])
    sun = -light_travel / np.linalg.norm(light_travel)

    hit, water = march(field, cam, dirs)
    sky_mask = ~np.isfinite(hit)

    # Sky: warm horizon, deep zenith, sun glow.
    el = np.clip(dirs[:, 2], -0.2, 1)
    sun_dot = np.clip(dirs @ sun, 0, 1)
    sky = (np.array([1.0, 0.72, 0.45]) * (1 - el[:, None]) ** 3 + np.array([0.25, 0.42, 0.75]) * (1 - (1 - el[:, None]) ** 3))
    sky += np.array([1.0, 0.75, 0.4]) * (sun_dot[:, None] ** 64 * 1.5 + sun_dot[:, None] ** 8 * 0.35)
    sky = sky * 0.95
    col = sky.copy()

    idx = np.where(~sky_mask)[0]
    t = hit[idx]
    p = cam + dirs[idx] * t[:, None]
    gx = field.sample(field.gx, p[:, 0], p[:, 1]); gy = field.sample(field.gy, p[:, 0], p[:, 1])
    nrm = normalize(np.stack([-gx, -gy, np.ones_like(gx)], axis=1))
    wat = water[idx]
    nrm[wat] = [0, 0, 1]
    p[wat, 2] = WATER_Z

    # Albedo: slope + noise + road.
    nz = nrm[:, 2]
    n1 = value_noise2(p[:, 0], p[:, 1], 40.0, 1); n2 = value_noise2(p[:, 0], p[:, 1], 7.0, 2)
    rock_amt = np.clip((0.64 + (n1 - 0.5) * 0.3 - nz) * 6, 0, 1)
    dry = np.clip((n1 - 0.55) * 4, 0, 1)[:, None]
    grass = np.array([0.055, 0.085, 0.03]) * (0.75 + 0.5 * n2[:, None]) * (1 - dry) + np.array([0.12, 0.11, 0.05]) * dry
    lime = np.array([0.42, 0.41, 0.38]) * (0.55 + 0.6 * n2[:, None])
    moss = np.clip(n1 * 1.6 - 0.45, 0, 1)[:, None] * 0.55
    rock = lime * (1 - moss) + np.array([0.05, 0.09, 0.035]) * moss
    alb = grass * (1 - rock_amt[:, None]) + rock * rock_amt[:, None]
    rm = field.sample(roadmask, p[:, 0], p[:, 1])
    alb = alb * (1 - rm[:, None]) + np.array([0.05, 0.05, 0.055]) * rm[:, None]
    wet = np.clip((715 - p[:, 2]) / 15, 0, 1)
    alb *= (1 - 0.5 * wet)[:, None]

    lit = shadow(field, p + nrm * 1.5, sun)
    ndl = np.clip(nrm @ sun, 0, 1) * lit
    sun_col = np.array([1.0, 0.68, 0.42]) * 3.2
    sky_amb = np.array([0.35, 0.45, 0.6]) * (0.45 + 0.55 * nz[:, None]) * 0.55
    bounce = np.array([0.25, 0.2, 0.12]) * (1 - nz[:, None]) * 0.25
    shade = alb * (sun_col * ndl[:, None] + sky_amb + bounce)

    # Water: emerald body + Fresnel reflection of the sky.
    if wat.any():
        v = dirs[idx][wat]
        cos_i = np.clip(-v[:, 2], 0, 1)
        fres = 0.02 + 0.98 * (1 - cos_i) ** 5
        refl_dir = v * np.array([1, 1, -1])
        rel = np.clip(refl_dir[:, 2], 0, 1)
        refl = np.array([1.0, 0.72, 0.45]) * (1 - rel[:, None]) ** 3 + np.array([0.25, 0.42, 0.75]) * (1 - (1 - rel[:, None]) ** 3)
        body = np.array([0.02, 0.16, 0.12]) * (0.6 + 0.4 * lit[wat, None])
        shade[wat] = body * (1 - fres[:, None]) + refl * fres[:, None] * 0.9

    # Aerial perspective + valley fog (exponential height fog, analytic integral).
    fog_col = np.array([0.78, 0.68, 0.6]) + np.array([0.6, 0.35, 0.1]) * (dirs[idx] @ sun)[:, None].clip(0) ** 4
    zc, zh = cam[2], p[:, 2]
    k = 1.0 / 120.0
    dz = np.where(np.abs(zh - zc) < 1e-3, 1e-3, zh - zc)
    dens0 = 0.0005
    integ = dens0 * np.exp(-k * (np.minimum(zc, zh) - 760)) * (1 - np.exp(-k * np.abs(dz))) / (k * np.abs(dz)) * t
    integ = np.minimum(integ, 20) + t * 0.00006
    fogf = 1 - np.exp(-integ)
    shade = shade * (1 - fogf[:, None]) + fog_col * fogf[:, None]
    col[idx] = shade

    # Filmic tonemap.
    col = col * 1.1
    col = (col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14)
    img = (np.clip(col, 0, 1) ** (1 / 2.2) * 255).astype(np.uint8).reshape(height, width, 3)
    print(f"  rendered {width}x{height} in {time.time() - t0:.1f}s")
    return Image.fromarray(img)


def cameras(layout, scatter):
    vps = scatter["viewpoints"]
    cm = lambda v: np.array(v, float) / 100.0
    out = []
    # VP1 Doc Tham Ma: from the check-in pad, eye height, looking down over the 9 hairpins.
    p1 = cm(layout["viewpoints"]["doc_tham_ma"]["location_cm"]); c1 = cm(vps["doc_tham_ma"]["center_cm"])
    d = c1[:2] - p1[:2]; d /= np.linalg.norm(d)
    # Stand at the edge of the overlook terrace (the check-in ring is in its middle).
    out.append(("VP1_ThamMa_eye", np.r_[p1[:2] + d * 10.0, p1[2] + 1.7], c1 + [0, 0, -40], 62))
    out.append(("VP1_ThamMa_flycam", np.r_[c1[:2] - d * 180 + [120, 0], c1[2] + 260], c1, 50))
    # VP2 Mom Da Tu Than: standing on the spur tip looking down the canyon, and the drone shot of the spur.
    p2 = cm(layout["viewpoints"]["ma_pi_leng"]["location_cm"]); river = cm(layout["water"]["river_cm"])[:, :2]
    j = np.argmin(np.linalg.norm(river - p2[:2], axis=1))
    down = river[max(j - 32, 0)]
    road2 = cm(layout["viewpoints"]["ma_pi_leng"]["road_cm"])
    spur_dir = (p2[:2] - road2[:2]) / np.linalg.norm(p2[:2] - road2[:2])
    edge = p2[:2] + spur_dir * 4.0
    out.append(("VP2_MaPiLeng_spur", np.r_[edge, p2[2] + 1.7], np.r_[down, WATER_Z + 30], 78))
    # Drone over the canyon, looking back at the spur with the river below.
    out.append(("VP2_MaPiLeng_drone", np.r_[river[j][:2] + [60, -260], p2[2] + 40], np.r_[(p2[:2] + river[j][:2]) / 2, 980], 60))
    # VP3 Tu San: from a boat on the river, looking up-canyon at the walls.
    b = cm(layout["spawns"]["boats"][0]["location_cm"]); k = np.argmin(np.linalg.norm(river - b[:2], axis=1))
    ahead = river[max(k - 45, 0)]
    out.append(("VP3_TuSan_boat", np.r_[b[:2], WATER_Z + 2.0], np.r_[ahead, WATER_Z + 140], 70))
    out.append(("VP3_TuSan_flycam", np.r_[river[k][:2] + [-220, 120], WATER_Z + 420], np.r_[river[max(k - 25, 0)], WATER_Z], 55))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--width", type=int, default=960)
    ap.add_argument("--stride", type=int, default=2)
    ap.add_argument("--only", default="")
    args = ap.parse_args()
    layout = json.loads((HM / "HaGiang_layout.json").read_text())
    scatter = json.loads((HM / "HaGiang_scatter.json").read_text())
    n = layout["heightmap"]["size"]
    h16 = np.fromfile(HM / "HaGiang_4033.r16", dtype="<u2").reshape(n, n)
    alt = layout["heightmap"]["base_asl_m"] + h16.astype(np.float32) * layout["heightmap"]["m_per_unit"]
    field = Field(alt, args.stride)
    roadmask = road_mask_raster(layout, field)
    OUT.mkdir(parents=True, exist_ok=True)
    w, h = args.width, args.width * 9 // 16
    shots = []
    for name, cam, look, fov in cameras(layout, scatter):
        if args.only and args.only not in name:
            continue
        print(name)
        img = render(field, layout, cam, look, fov, w, h, roadmask)
        img.save(OUT / f"{name}.png")
        shots.append((name, img))
    if shots:
        cols = 2
        rows = (len(shots) + 1) // 2
        sheet = Image.new("RGB", (w * cols, h * rows), (0, 0, 0))
        dr = ImageDraw.Draw(sheet)
        for i, (name, img) in enumerate(shots):
            x, y = (i % cols) * w, (i // cols) * h
            sheet.paste(img, (x, y))
            dr.text((x + 10, y + 10), name, fill=(255, 255, 255))
        sheet.save(OUT / "VP_contact_sheet.png")


if __name__ == "__main__":
    main()
