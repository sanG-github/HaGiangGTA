#!/usr/bin/env python3
"""Procedural art assets for HaGiangGTA (no Megascans download required).

Everything is deterministic and regenerated from code:

  Generated/Assets/Textures/
    T_HG_RockNormal.png     tileable limestone normal map (ridges + strata)
    T_HG_Noise.png          tileable RGB breakup noise (R low, G mid, B puddle mask)
    T_HG_WaterNormal.png    tileable wave normal map (integer wave vectors -> seamless)
  Generated/Assets/Meshes/  (OBJ, cm units, high-poly -> import as Nanite)
    SM_HG_KarstCliff_01..04 tall fluted limestone "tai meo" slabs (~10 m base)
    SM_HG_Boulder_01..03    rounded karst boulders (~3 m)
    SM_HG_Bush_01..02       shrub clumps (~1.5 m)
    SM_HG_RoadPost          Vietnamese road post (coc tieu), V = height for paint bands
  Generated/Assets/Audio/
    SW_HG_SaoMong.wav       synthesized Hmong-flute style pentatonic phrase (~14 s)
    SW_HG_AirHorn.wav       dump-truck air horn (CP04)
    SW_HG_Scream.wav        backpacker scream (CP04)
    SW_HG_Moo.wav           cow (CP04)

Usage: .venv/bin/python Scripts/Tools/gen_assets.py
"""
from __future__ import annotations

import math
import time
import wave
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Generated" / "Assets"
SEED = 1802


# ---------------------------------------------------------------------------
# Tileable 2D noise
# ---------------------------------------------------------------------------

def tile_value_noise(size, cells, rng):
    grid = rng.random((cells, cells)).astype(np.float32)
    coords = np.arange(size, dtype=np.float32) * (cells / size)
    i0 = np.floor(coords).astype(np.int32)
    f = coords - i0
    f = f * f * (3 - 2 * f)
    i1 = (i0 + 1) % cells
    a = grid[i0][:, i0]; b = grid[i0][:, i1]
    c = grid[i1][:, i0]; d = grid[i1][:, i1]
    top = a + (b - a) * f[None, :]
    bot = c + (d - c) * f[None, :]
    return top + (bot - top) * f[:, None]


def tile_fbm(size, base_cells, octaves, rng, ridged=False, gain=0.5):
    total = np.zeros((size, size), np.float32)
    amp, norm, cells = 1.0, 0.0, base_cells
    for _ in range(octaves):
        n = tile_value_noise(size, cells, rng)
        if ridged:
            n = (1.0 - np.abs(2 * n - 1)) ** 2
        total += amp * n
        norm += amp
        amp *= gain
        cells *= 2
    return total / norm


def height_to_normal(h, strength):
    gx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5 * strength
    gy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5 * strength
    n = np.stack([-gx, -gy, np.ones_like(h)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return ((n * 0.5 + 0.5) * 255).astype(np.uint8)


def make_textures(rng):
    out = OUT / "Textures"
    out.mkdir(parents=True, exist_ok=True)
    size = 1024

    # Limestone: ridged micro relief + horizontal strata + pitting.
    y = np.arange(size, dtype=np.float32)[:, None] / size
    warp = tile_fbm(size, 4, 3, rng)
    strata = 0.5 + 0.5 * np.sin(2 * np.pi * (y * 9 + warp * 1.5))
    rock = (0.55 * tile_fbm(size, 8, 6, rng, ridged=True) + 0.25 * strata ** 3
            + 0.2 * tile_fbm(size, 64, 3, rng))
    Image.fromarray(height_to_normal(rock, 28.0)).save(out / "T_HG_RockNormal.png")

    r = tile_fbm(size, 4, 5, rng)
    g = tile_fbm(size, 16, 4, rng)
    puddles = tile_fbm(size, 6, 4, rng)
    b = np.clip((puddles - 0.48) * 6.0, 0, 1)
    rgb = (np.stack([r, g, b], axis=-1) * 255).astype(np.uint8)
    Image.fromarray(rgb).save(out / "T_HG_Noise.png")

    # Water: sum of integer-frequency waves (perfectly tileable).
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32) / size
    h = np.zeros((size, size), np.float32)
    for _ in range(28):
        kx, ky = rng.integers(-9, 10, size=2)
        if kx == 0 and ky == 0:
            continue
        k = math.hypot(kx, ky)
        amp = 1.0 / (k ** 1.3)
        h += amp * np.sin(2 * np.pi * (kx * xx + ky * yy) + rng.uniform(0, 2 * np.pi))
    h += 0.15 * tile_fbm(size, 32, 3, rng)
    Image.fromarray(height_to_normal(h, 14.0)).save(out / "T_HG_WaterNormal.png")


# ---------------------------------------------------------------------------
# Meshes
# ---------------------------------------------------------------------------

def icosphere(level):
    t = (1 + 5 ** 0.5) / 2
    verts = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t),
             (0, -1, -t), (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4),
             (11, 10, 2), (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8),
             (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    verts = [np.array(v, float) / np.linalg.norm(v) for v in verts]
    for _ in range(level):
        cache = {}
        new_faces = []

        def mid(a, b):
            key = (a, b) if a < b else (b, a)
            if key not in cache:
                m = verts[a] + verts[b]
                verts.append(m / np.linalg.norm(m))
                cache[key] = len(verts) - 1
            return cache[key]

        for a, b, c in faces:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            new_faces += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        faces = new_faces
    return np.array(verts), np.array(faces, dtype=np.int64)


def wave_noise(p, rng, octaves=4, waves=8, base=1.5, ridged=True):
    """Smooth 3D noise from random sine waves (cheap, seamless on closed shapes)."""
    total = np.zeros(len(p))
    amp, freq, norm = 1.0, base, 0.0
    for _ in range(octaves):
        for _ in range(waves):
            d = rng.normal(size=3)
            d /= np.linalg.norm(d)
            s = np.sin(p @ d * freq + rng.uniform(0, 2 * np.pi))
            total += amp * ((1 - np.abs(s)) ** 2 if ridged else s)
        norm += amp * waves
        amp *= 0.5
        freq *= 2.1
    return total / norm


def vertex_normals(v, f):
    fn = np.cross(v[f[:, 1]] - v[f[:, 0]], v[f[:, 2]] - v[f[:, 0]])
    n = np.zeros_like(v)
    for k in range(3):
        np.add.at(n, f[:, k], fn)
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    return n


def write_obj(path, v, f, n=None, uv=None):
    if n is None:
        n = vertex_normals(v, f)
    if uv is None:
        # Cylindrical projection; the materials are world-aligned anyway.
        theta = np.arctan2(v[:, 1], v[:, 0])
        zr = (v[:, 2] - v[:, 2].min()) / max(np.ptp(v[:, 2]), 1e-6)
        uv = np.stack([(theta / (2 * np.pi)) + 0.5, zr], axis=1)
    # Round 5B (agent B): the OBJ import (right-handed file -> left-handed Unreal) mirrors Y - the far terrain came
    # in north-south mirrored in round 3B. Write Y negated with the winding reversed so every mesh arrives as built
    # here (normals / UVs computed above, before the flip).
    v = np.asarray(v, float) * np.array([1.0, -1.0, 1.0])
    n = np.asarray(n, float) * np.array([1.0, -1.0, 1.0])
    f = np.asarray(f)[:, [0, 2, 1]]
    lines = ["# HaGiangGTA procedural mesh (cm)"]
    lines += [f"v {a:.3f} {b:.3f} {c:.3f}" for a, b, c in v]
    lines += [f"vt {a:.5f} {b:.5f}" for a, b in uv]
    lines += [f"vn {a:.4f} {b:.4f} {c:.4f}" for a, b, c in n]
    lines += [f"f {a + 1}/{a + 1}/{a + 1} {b + 1}/{b + 1}/{b + 1} {c + 1}/{c + 1}/{c + 1}" for a, b, c in f]
    path.write_text("\n".join(lines) + "\n")


def karst_rock(rng, level, size_cm, aspect, flute_count, flute_depth, bumpiness, flat_bottom=0.75, cuts=9):
    """Irregular limestone block: random planar cuts (fractured faces) + warped silhouette
    + uneven karren flutes + strata ledges + jagged pinnacle crown."""
    v, f = icosphere(level)
    u = v.copy()                                        # unit directions
    warp = wave_noise(u, rng, octaves=2, waves=5, base=1.1, ridged=False)
    detail = wave_noise(u, rng, octaves=5, waves=7, base=2.2, ridged=True)

    # 1) Base radius with strong low-frequency warp (no ellipsoid look).
    r = 1.0 + 0.45 * warp + bumpiness * (detail - 0.4)

    # 2) Fracture planes: clip the radius against random planes -> flat, angular faces.
    for _ in range(cuts):
        n = rng.normal(size=3)
        n[2] *= 0.5                                     # mostly near-vertical faces
        n /= np.linalg.norm(n)
        d = rng.uniform(0.55, 0.85)
        dots = u @ n
        mask = dots > 1e-3
        plane_r = np.full(len(u), np.inf)
        plane_r[mask] = d / dots[mask]
        r = np.minimum(r, plane_r + 0.09 * detail + 0.05 * warp)

    # 3) Karren flutes: irregular spacing/depth, only on some sectors of the walls.
    theta = np.arctan2(u[:, 1], u[:, 0])
    flute_phase = flute_count * theta + 4.0 * warp + 2.0 * wave_noise(u, rng, 2, 4, 2.0, False)
    flutes = 1.0 - np.abs(np.sin(flute_phase)) ** 0.3
    sector = np.clip(wave_noise(u, rng, octaves=1, waves=4, base=1.5, ridged=False) * 3.0, 0, 1)
    side = np.clip(1.0 - np.abs(u[:, 2]) * 1.2, 0, 1)
    r -= flute_depth * flutes * sector * side

    # 4) Jagged crown: pinnacles on the upper cap.
    crown = np.clip(u[:, 2], 0, 1) ** 2.5
    r += 0.45 * crown * (wave_noise(u, rng, octaves=3, waves=6, base=3.5, ridged=True) - 0.35)

    p = u * r[:, None]
    p *= np.array(aspect, float)

    # 5) Horizontal strata ledges.
    zr = (p[:, 2] - p[:, 2].min()) / np.ptp(p[:, 2])
    ledge = np.floor(zr * 7 + 0.3 * warp) / 7
    radial = np.linalg.norm(p[:, :2], axis=1, keepdims=True) + 1e-6
    p[:, :2] *= 1.0 - 0.035 * (zr - ledge)[:, None] * 7 * (radial / radial.max())

    zmin = p[:, 2].min()
    floor = zmin + np.ptp(p[:, 2]) * (1 - flat_bottom) * 0.25
    p[:, 2] = np.maximum(p[:, 2], floor) - floor        # pivot at the base
    p *= size_cm / max(np.ptp(p[:, 0]), np.ptp(p[:, 1]))
    return p, f


def road_post():
    # 20 x 20 cm concrete post, 110 cm tall (20 cm buried), chamfered top.
    w, z0, z1 = 10.0, -20.0, 90.0
    v, n, uv, f = [], [], [], []

    def quad(a, b, c, d, normal):
        base = len(v)
        for q in (a, b, c, d):
            v.append(q)
            n.append(normal)
            uv.append(((q[0] + q[1]) / 40.0 + 0.5, (q[2] - z0) / (z1 - z0)))
        f.extend([(base, base + 1, base + 2), (base, base + 2, base + 3)])

    top = z1 - 4.0
    for nx, ny in ((1, 0), (0, 1), (-1, 0), (0, -1)):
        tx, ty = -ny, nx
        a = (nx * w - tx * w, ny * w - ty * w, z0)
        b = (nx * w + tx * w, ny * w + ty * w, z0)
        quad(a, b, (b[0], b[1], top), (a[0], a[1], top), (nx, ny, 0))
        # Chamfer ring.
        ic = 0.6
        quad((a[0], a[1], top), (b[0], b[1], top),
             (b[0] * ic, b[1] * ic, z1), (a[0] * ic, a[1] * ic, z1),
             tuple(np.array((nx, ny, 0.9)) / np.linalg.norm((nx, ny, 0.9))))
    s = w * 0.6
    quad((-s, -s, z1), (s, -s, z1), (s, s, z1), (-s, s, z1), (0, 0, 1))
    v, f = np.array(v), np.array(f)
    # Fix winding so faces point along their normals.
    n = np.array(n, float)
    for i, (a, b, c) in enumerate(f):
        if np.dot(np.cross(v[b] - v[a], v[c] - v[a]), n[a]) < 0:
            f[i] = (a, c, b)
    return v, f, n, np.array(uv)


def make_meshes(rng):
    out = OUT / "Meshes"
    out.mkdir(parents=True, exist_ok=True)
    for i in range(4):
        p, f = karst_rock(rng, 6, 1000.0, aspect=(1.0, rng.uniform(0.45, 0.7), rng.uniform(1.5, 2.3)),
                          flute_count=rng.integers(7, 13), flute_depth=0.12, bumpiness=0.3, cuts=11)
        write_obj(out / f"SM_HG_KarstCliff_{i + 1:02d}.obj", p, f)
    for i in range(3):
        p, f = karst_rock(rng, 5, 300.0, aspect=(1.0, rng.uniform(0.7, 0.95), rng.uniform(0.6, 0.9)),
                          flute_count=rng.integers(4, 8), flute_depth=0.06, bumpiness=0.3, cuts=7)
        write_obj(out / f"SM_HG_Boulder_{i + 1:02d}.obj", p, f)
    for i in range(2):
        parts_v, parts_f, off = [], [], 0
        for _ in range(6):
            v, f = icosphere(3)
            r = 1.0 + 0.35 * (wave_noise(v, rng, octaves=3, waves=6, base=3.0) - 0.4)
            v = v * r[:, None] * rng.uniform(35, 60)
            v += np.array([rng.uniform(-45, 45), rng.uniform(-45, 45), rng.uniform(25, 70)])
            parts_v.append(v)
            parts_f.append(f + off)
            off += len(v)
        v = np.vstack(parts_v)
        v[:, 2] = np.maximum(v[:, 2], 0.0)
        write_obj(out / f"SM_HG_Bush_{i + 1:02d}.obj", v, np.vstack(parts_f))
    v, f, n, uv = road_post()
    write_obj(out / "SM_HG_RoadPost.obj", v, f, n, uv)


# ---------------------------------------------------------------------------
# Audio: Hmong flute style phrase
# ---------------------------------------------------------------------------

def make_flute(rng, sr=44100):
    out = OUT / "Audio"
    out.mkdir(parents=True, exist_ok=True)
    # D minor pentatonic, slow ornamented phrase (note, beats).
    base = 293.66  # D4
    semis = {"D": 0, "F": 3, "G": 5, "A": 7, "C": 10, "D5": 12, "F5": 15, "G5": 17, "A5": 19}
    phrase = [("A", 1.5), ("D5", 1.0), ("C", 0.5), ("A", 1.0), ("G", 2.0), ("A", 0.5), ("C", 0.5),
              ("D5", 1.5), ("F5", 0.5), ("D5", 1.0), ("C", 1.0), ("A", 3.0), ("G", 0.5), ("F", 0.5),
              ("D", 3.5)]
    beat = 0.62
    total = sum(b for _, b in phrase) * beat + 2.5
    y = np.zeros(int(total * sr))
    t_cursor = 0.0
    for note, beats in phrase:
        dur = beats * beat
        n = int((dur + 0.25) * sr)
        t = np.arange(n) / sr
        f0 = base * 2 ** (semis[note] / 12)
        # Grace-note slide up from below (characteristic ornament).
        slide = np.exp(-t * 25.0) * -0.06
        vib = 0.012 * np.sin(2 * np.pi * 5.2 * t) * np.clip(t / 0.4, 0, 1)
        phase = 2 * np.pi * np.cumsum(f0 * (1 + slide + vib)) / sr
        tone = np.sin(phase) + 0.22 * np.sin(2 * phase) + 0.08 * np.sin(3 * phase)
        breath = rng.normal(size=n) * 0.05
        breath = np.convolve(breath, np.ones(8) / 8, mode="same")
        env = np.clip(t / 0.08, 0, 1) * np.clip((dur + 0.25 - t) / 0.3, 0, 1)
        start = int(t_cursor * sr)
        seg = (tone + breath) * env * 0.28
        y[start:start + n] += seg[: len(y) - start]
        t_cursor += dur
    # Mountain-valley echo: a few feedback delays.
    for delay, gain in ((0.23, 0.35), (0.41, 0.25), (0.67, 0.18), (1.1, 0.12)):
        d = int(delay * sr)
        y[d:] += y[:-d] * gain
    y /= np.max(np.abs(y)) * 1.05
    pcm = (y * 32767).astype("<i2")
    with wave.open(str(out / "SW_HG_SaoMong.wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(pcm.tobytes())


# ---------------------------------------------------------------------------
# Audio: SFX (CP04) - air horn, tourist scream, cow moo
# ---------------------------------------------------------------------------

def _write_wav(path, y, sr=44100):
    y = y / (np.max(np.abs(y)) * 1.05 + 1e-9)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes((y * 32767).astype("<i2").tobytes())


def _resonator(x, freq, bw, sr):
    """2-pole resonant band-pass (formant)."""
    r = math.exp(-math.pi * bw / sr)
    a1, a2 = -2 * r * math.cos(2 * math.pi * freq / sr), r * r
    g = (1 - r * r) * 0.5
    y = np.zeros_like(x)
    y1 = y2 = 0.0
    for i, xi in enumerate(x):
        yi = g * xi - a1 * y1 - a2 * y2
        y[i] = yi
        y2, y1 = y1, yi
    return y


def _glottal(f0, sr, rng, jitter=0.01):
    """Band-limited-ish sawtooth source following an f0 contour."""
    f = f0 * (1 + jitter * rng.normal(size=len(f0)).cumsum() / np.sqrt(np.arange(1, len(f0) + 1)))
    phase = np.cumsum(f) / sr
    saw = 2 * (phase - np.floor(phase)) - 1
    return saw - np.convolve(saw, np.ones(4) / 4, mode="same") * 0.6


def _voice(f0, formants, sr, rng, breath=0.08):
    src = _glottal(f0, sr, rng) + rng.normal(size=len(f0)) * breath
    out = np.zeros_like(src)
    for freq, bw, gain in formants:
        out += gain * _resonator(src, freq, bw, sr)
    return out


def make_sfx(rng, sr=44100):
    out = OUT / "Audio"
    out.mkdir(parents=True, exist_ok=True)

    # Thaco dump-truck air horn: two detuned reeds, saturated, with valley echo.
    t = np.arange(int(1.9 * sr)) / sr
    env = np.clip(t / 0.04, 0, 1) * np.clip((1.55 - t) / 0.25, 0, 1)
    drop = 1 - 0.03 * np.clip((t - 1.2) / 0.35, 0, 1)
    horn = np.zeros_like(t)
    for f in (311.0, 392.0, 466.0):
        ph = 2 * np.pi * np.cumsum(f * drop * (1 + 0.003 * np.sin(2 * np.pi * 6 * t))) / sr
        horn += sum(np.sin(k * ph) / k for k in range(1, 9))
    horn = np.tanh(horn * 1.4) * env
    for d, gain in ((0.35, 0.35), (0.8, 0.2)):
        k = int(d * sr)
        horn[k:] += horn[:-k] * gain
    _write_wav(out / "SW_HG_AirHorn.wav", horn, sr)

    # Tourist scream "aaaah!": two voices, rising then falling pitch, vowel /a/.
    n = int(1.7 * sr)
    t = np.arange(n) / sr
    env = np.clip(t / 0.06, 0, 1) * np.clip((1.7 - t) / 0.5, 0, 1)
    contour = np.interp(t, [0, 0.25, 0.9, 1.7], [1.0, 1.35, 1.25, 0.8])
    vib = 1 + 0.03 * np.sin(2 * np.pi * 7.5 * t)
    a_formants = [(820, 90, 1.0), (1220, 110, 0.6), (2900, 160, 0.25)]
    scream = _voice(420 * contour * vib, a_formants, sr, rng, 0.15) + \
        0.8 * _voice(260 * contour * vib, [(760, 90, 1.0), (1150, 110, 0.55), (2600, 160, 0.2)], sr, rng, 0.12)
    _write_wav(out / "SW_HG_Scream.wav", scream * env, sr)

    # Cow "mooo": low f0 sliding down, closed /u/ opening to /o/.
    n = int(2.2 * sr)
    t = np.arange(n) / sr
    env = np.clip(t / 0.25, 0, 1) * np.clip((2.2 - t) / 0.6, 0, 1)
    f0 = np.interp(t, [0, 0.4, 1.6, 2.2], [118, 128, 104, 92])
    moo = _voice(f0, [(320, 80, 1.0), (760, 100, 0.5), (2300, 200, 0.08)], sr, rng, 0.04)
    _write_wav(out / "SW_HG_Moo.wav", moo * env, sr)


# ---------------------------------------------------------------------------
# Audio: seamless loops (CP05) - engines, siren, helicopter
# ---------------------------------------------------------------------------

def _loop_wav(path, y, sr=44100):
    """Seamless loop: crossfade the last 50 ms into the first 50 ms and drop the tail."""
    fade = int(0.05 * sr)
    n = len(y)
    out = y[: n - fade].copy()
    w = np.linspace(0.0, 1.0, fade)
    out[:fade] = y[:fade] * w + y[n - fade:] * (1.0 - w)
    _write_wav(path, out, sr)


def _engine(rng, sr, seconds, fire_hz, harmonics, rough, noise):
    n = int(seconds * sr)
    t = np.arange(n) / sr
    # Firing pulses: each power stroke is a decaying burst (exact integer count -> seamless).
    period = int(round(sr / fire_hz))
    pulse_t = np.arange(period) / sr
    pulse = np.exp(-pulse_t * fire_hz * 5.0) * np.sin(2 * np.pi * fire_hz * 3.0 * pulse_t)
    reps = n // period
    y = np.tile(pulse, reps)
    jitter = 1.0 + rough * rng.normal(size=reps)
    y = (y.reshape(reps, period) * jitter[:, None]).ravel()
    t = t[: len(y)]
    for k, a in harmonics:
        y += a * np.sin(2 * np.pi * fire_hz * k * t)
    y += noise * np.convolve(rng.normal(size=len(y)), np.ones(6) / 6, mode="same")
    return np.tanh(y * 1.5)


def make_loops(rng, sr=44100):
    out = OUT / "Audio"
    # Honda Win 110 single cylinder: ~2400 rpm idle-ish base (pitch scaled up in game).
    _loop_wav(out / "SW_HG_BikeEngine_Loop.wav", _engine(rng, sr, 2.0, 20.0, [(1, 0.5), (2, 0.35), (3, 0.15)], 0.08, 0.08), sr)
    # Diesel dump truck: low, rough.
    _loop_wav(out / "SW_HG_TruckEngine_Loop.wav", _engine(rng, sr, 2.0, 12.5, [(1, 0.7), (2, 0.4), (4, 0.2)], 0.12, 0.15), sr)
    # Outboard motor: buzzy.
    _loop_wav(out / "SW_HG_BoatMotor_Loop.wav", _engine(rng, sr, 2.0, 50.0, [(1, 0.3), (2, 0.3), (5, 0.1)], 0.05, 0.1), sr)

    # Police siren: two-tone wail, 2 s loop.
    t = np.arange(int(2.0 * sr)) / sr
    f = np.where((t % 1.0) < 0.5, 960.0, 770.0)
    ph = 2 * np.pi * np.cumsum(f) / sr
    siren = np.tanh(2.0 * (np.sin(ph) + 0.3 * np.sin(2 * ph) + 0.15 * np.sin(3 * ph)))
    _loop_wav(out / "SW_HG_Siren_Loop.wav", siren, sr)

    # Helicopter: blade-slap at 5.5 Hz on a low rotor rumble + turbine whine.
    t = np.arange(int(2.0 * sr)) / sr
    blade = 5.5
    slap = (np.sin(np.pi * ((t * blade) % 1.0)) ** 24)
    rumble = np.convolve(rng.normal(size=len(t)), np.ones(40) / 40, mode="same")
    whine = 0.15 * np.sin(2 * np.pi * 2200 * t) + 0.08 * np.sin(2 * np.pi * 3300 * t)
    heli = slap * (0.8 + 2.0 * rumble) + 0.6 * rumble + whine
    _loop_wav(out / "SW_HG_Heli_Loop.wav", heli, sr)


def _lowpass(x, cutoff_hz, sr):
    """One-pole low-pass (run twice for 12 dB/oct)."""
    a = math.exp(-2.0 * math.pi * cutoff_hz / sr)
    y = np.empty_like(x)
    for _ in range(2):
        acc = 0.0
        for i in range(len(x)):
            acc = (1.0 - a) * x[i] + a * acc
            y[i] = acc
        x = y.copy()
    return y


def make_ambience(rng, sr=44100):
    """Round 2 loops: wind (speed / altitude), mountain birds + insects (slow / on foot), tyres on the road."""
    out = OUT / "Audio"
    n = int(6.0 * sr)
    t = np.arange(n) / sr
    # Wind: low-passed noise with slow gusts (integer cycles per loop -> seamless amplitude).
    noise = rng.normal(size=n)
    body = _lowpass(noise, 500.0, sr)
    hiss = _lowpass(noise, 3000.0, sr) - body
    gust = 0.6 + 0.25 * np.sin(2 * np.pi * t / 6.0) + 0.15 * np.sin(2 * np.pi * 3 * t / 6.0 + 1.3)
    wind = (body * 3.0 + 0.4 * hiss) * gust
    _loop_wav(out / "SW_HG_Wind_Loop.wav", np.tanh(wind / (np.abs(wind).max() + 1e-6) * 1.2) * 0.8, sr)

    # Birds + insects: a soft cicada bed, bulbul-like two-note calls and short trills at random times.
    birds = 0.03 * _lowpass(rng.normal(size=n), 6000.0, sr) * (0.7 + 0.3 * np.sin(2 * np.pi * 37.0 * t))
    for _ in range(14):
        t0 = rng.uniform(0.2, 5.5)
        f0 = rng.uniform(2200.0, 3800.0)
        kind = rng.integers(0, 3)
        notes = [(0.0, f0, 0.09), (0.13, f0 * 1.25, 0.12)] if kind == 0 else \
                [(k * 0.045, f0 * (1.0 + 0.04 * (k % 2)), 0.03) for k in range(rng.integers(5, 10))] if kind == 1 else \
                [(0.0, f0 * 0.8, 0.25)]
        amp = rng.uniform(0.08, 0.2)
        for dt0, f, dur in notes:
            i0 = int((t0 + dt0) * sr)
            m = int(dur * sr)
            if i0 + m >= n:
                continue
            tt = np.arange(m) / sr
            env = np.sin(np.pi * tt / dur) ** 2
            sweep = f * (1.0 + 0.15 * np.sin(np.pi * tt / dur)) if kind != 2 else f * (1.0 - 0.3 * tt / dur)
            birds[i0:i0 + m] += amp * env * np.sin(2 * np.pi * np.cumsum(sweep) / sr)
    _loop_wav(out / "SW_HG_Birds_Loop.wav", birds / (np.abs(birds).max() + 1e-6) * 0.7, sr)

    # Tyres: rolling road noise (band-passed noise) + a faint periodic thump (slab joints / tread).
    roll = _lowpass(rng.normal(size=n), 1200.0, sr) - _lowpass(rng.normal(size=n), 150.0, sr) * 0.5
    thump = np.zeros(n)
    period = int(sr / 4.0)
    for i in range(0, n - 800, period):
        thump[i:i + 800] += np.exp(-np.arange(800) / 120.0) * np.sin(2 * np.pi * 70.0 * np.arange(800) / sr)
    tyre = roll / (np.abs(roll).max() + 1e-6) + 0.35 * thump
    _loop_wav(out / "SW_HG_Tyre_Loop.wav", np.tanh(tyre) * 0.7, sr)


# ---------------------------------------------------------------------------
# Trees (reference photos): bombax / kapok (hoa gao, red), broadleaf, pine.
# Each tree = trunk mesh + crown mesh sharing one transform (two materials, no vertex colours).
# ---------------------------------------------------------------------------

def _merge(parts):
    vs, fs, off = [], [], 0
    for v, f in parts:
        vs.append(v); fs.append(f + off); off += len(v)
    return np.vstack(vs), np.vstack(fs)


def _tube(p0, p1, r0, r1, sides=7):
    """Tapered open cylinder from p0 to p1 (cm)."""
    p0, p1 = np.asarray(p0, float), np.asarray(p1, float)
    axis = p1 - p0
    L = np.linalg.norm(axis)
    a = axis / L
    t = np.cross(a, [0, 0, 1.0]) if abs(a[2]) < 0.99 else np.cross(a, [1.0, 0, 0])
    t /= np.linalg.norm(t)
    b = np.cross(a, t)
    ang = np.linspace(0, 2 * np.pi, sides, endpoint=False)
    ring0 = p0 + r0 * (np.cos(ang)[:, None] * t + np.sin(ang)[:, None] * b)
    ring1 = p1 + r1 * (np.cos(ang)[:, None] * t + np.sin(ang)[:, None] * b)
    v = np.vstack([ring0, ring1])
    f = []
    for i in range(sides):
        j = (i + 1) % sides
        f += [(i, j, sides + j), (i, sides + j, sides + i)]
    return v, np.array(f)


def _blob(center, radius, rng, level=2, squash=(1, 1, 0.8), noise=0.35):
    v, f = icosphere(level)
    r = 1.0 + noise * (wave_noise(v, rng, octaves=2, waves=5, base=2.5) - 0.4)
    return center + v * r[:, None] * radius * np.array(squash), f


def make_trees(rng):
    out = OUT / "Meshes"
    # Kapok / bombax: tall pale trunk, few up-swept branches, sparse red flower clusters, no leaves.
    trunk, bloom = [], []
    h = 1100.0
    trunk.append(_tube((0, 0, -30), (0, 0, h), 28, 10, 8))
    for k in range(7):
        z0 = h * rng.uniform(0.45, 0.9)
        ang = rng.uniform(0, 2 * np.pi)
        L = rng.uniform(250, 420)
        tip = np.array([np.cos(ang) * L * 0.75, np.sin(ang) * L * 0.75, z0 + L * 0.6])
        trunk.append(_tube((0, 0, z0), tip, 8, 3, 5))
        for _ in range(4):
            c = tip + rng.normal(0, 55, 3) + np.array([0, 0, 30])
            bloom.append(_blob(c, rng.uniform(35, 65), rng, 1, (1, 1, 0.8), 0.5))
    v, f = _merge(trunk); write_obj(out / "SM_HG_Kapok_Trunk.obj", v, f)
    v, f = _merge(bloom); write_obj(out / "SM_HG_Kapok_Bloom.obj", v, f)

    # Broadleaf tree: short trunk, lumpy crown.
    v, f = _tube((0, 0, -30), (0, 0, 420), 22, 12, 7); write_obj(out / "SM_HG_Tree_Trunk.obj", v, f)
    crown = [_blob(np.array([rng.uniform(-90, 90), rng.uniform(-90, 90), rng.uniform(420, 620)]),
                   rng.uniform(140, 220), rng, 2, (1, 1, 0.75), 0.4) for _ in range(6)]
    v, f = _merge(crown); write_obj(out / "SM_HG_Tree_Crown.obj", v, f)

    # Pine (Pinus kesiya on the Nho Que slopes): straight trunk, stacked drooping cones.
    v, f = _tube((0, 0, -30), (0, 0, 1300), 20, 6, 7); write_obj(out / "SM_HG_Pine_Trunk.obj", v, f)
    cones = []
    for i in range(7):
        zc = 450 + i * 130
        rad = 260 - i * 30
        cv, cf = icosphere(2)
        cv = cv * np.array([rad, rad, 90]) + np.array([0, 0, zc])
        cv[:, 2] -= np.clip(np.hypot(cv[:, 0], cv[:, 1]) / rad, 0, 1) * 70     # droop at the edge
        r = 1.0 + 0.25 * (wave_noise(cv / 100.0, rng, octaves=2, waves=4, base=1.5) - 0.4)
        cv[:, :2] *= r[:, None]
        cones.append((cv, cf))
    v, f = _merge(cones); write_obj(out / "SM_HG_Pine_Crown.obj", v, f)


# ---------------------------------------------------------------------------
# Road furniture (reference: Doc Tham Ma photos)
# ---------------------------------------------------------------------------

def make_road_furniture():
    out = OUT / "Meshes"
    # Road strip for spline meshes: X = length (100 cm, 10 cuts so it bends), Y = width (100 cm),
    # top at z = 0. UV: U across the road (0..1), V along the segment (0..1) -> lane paint in the material.
    nx = 10
    xs = np.linspace(-50, 50, nx + 1)
    v, uv, f, nrm = [], [], [], []
    for i, x in enumerate(xs):
        for y in (-50.0, 50.0):
            v.append((x, y, 0.0)); uv.append(((y + 50) / 100.0, i / nx)); nrm.append((0, 0, 1))
    for i in range(nx):
        a, b, c, d = 2 * i, 2 * i + 1, 2 * i + 2, 2 * i + 3
        f += [(a, c, d), (a, d, b)]
    # Skirts down the sides so the edge never shows a gap against the terrain.
    base = len(v)
    for i, x in enumerate(xs):
        for y in (-50.0, 50.0):
            v.append((x, y, -12.0)); uv.append(((y + 50) / 100.0, i / nx)); nrm.append((0, np.sign(y), 0))
    for i in range(nx):
        for side in (0, 1):
            t0, t1 = 2 * i + side, 2 * (i + 1) + side
            b0, b1 = base + t0, base + t1
            f += [(t0, b0, b1), (t0, b1, t1)] if side == 1 else [(t0, b1, b0), (t0, t1, b1)]
    write_obj(out / "SM_HG_RoadStrip.obj", np.array(v), np.array(f), np.array(nrm, float), np.array(uv))

    # W-beam guardrail, 4 m long, with one post: pivot at the base of the post, rail along +X.
    parts = []
    def box(cx, cy, cz, sx, sy, sz):
        x0, x1, y0, y1, z0, z1 = cx - sx / 2, cx + sx / 2, cy - sy / 2, cy + sy / 2, cz - sz / 2, cz + sz / 2
        bv = np.array([(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)])
        bf = np.array([(0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7), (0, 1, 5), (0, 5, 4), (1, 2, 6), (1, 6, 5), (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7)])
        return bv, bf
    parts.append(box(0, 0, 35, 12, 12, 110))                    # post (buried 20 cm)
    # Rail on local -Y: that is where it always showed in game (the import mirrored Y) and where gen_scatter's
    # yaw puts the road side; round 5B removed the mirror, so the model states it directly.
    parts.append(box(200, -9, 62, 400, 4, 14))                  # upper rib of the W
    parts.append(box(200, -11, 52, 400, 3, 8))                  # valley of the W
    parts.append(box(200, -9, 42, 400, 4, 14))                  # lower rib
    vs, fs, off = [], [], 0
    for bv, bf in parts:
        vs.append(bv); fs.append(bf + off); off += len(bv)
    write_obj(out / "SM_HG_Guardrail.obj", np.vstack(vs), np.vstack(fs))


# ---------------------------------------------------------------------------
# Reference-photo set dressing: distant ranges, chevrons, utility poles, Hmong houses
# ---------------------------------------------------------------------------

def make_set_dressing(rng):
    out = OUT / "Meshes"
    # Distant karst ranges: a ring 5-14 km from the map centre (outside the 4 km landscape) so the
    # views from the pass end in layered blue mountains instead of an empty horizon.
    seg, rings = 256, 18
    ang = np.linspace(0, 2 * np.pi, seg, endpoint=False)
    radii = np.linspace(520000.0, 1400000.0, rings)
    # Apron from just outside the 5.9 km landscape (half 2.95 km) to the ranges, 450 m below the mesh pivot
    # (placed at z 800 m -> 350 m, under the river and every map edge): from the Ma Pi Leng rim the player looked
    # past the map edge into empty haze; now a low valley floor runs out to the distant ranges.
    APRON_Z = 10000.0            # 900 m: at the height of the map edges (a 350 m floor read as a flat sea)
    apron = np.array([300000.0, 380000.0, 450000.0, 515000.0])
    # One coherent noise field over (angle, ring) so ridges continue between neighbouring vertices.
    A, J = np.meshgrid(ang, np.arange(rings))
    pts = np.stack([np.cos(A).ravel() * 2.2, np.sin(A).ravel() * 2.2, J.ravel() * 0.35], axis=1)
    ridge = wave_noise(pts, rng, octaves=4, waves=7, base=2.5, ridged=True).reshape(rings, seg)
    envelope = np.sin(np.pi * np.arange(rings) / (rings - 1)) ** 0.6
    verts, faces = [], []
    # The apron rises from the valley floor into rolling foothills (a flat one read as a lake in the haze).
    foot = wave_noise(np.stack([np.cos(ang) * 1.7, np.sin(ang) * 1.7, np.zeros_like(ang)], axis=1), rng,
                      octaves=3, waves=6, base=2.0, ridged=False)
    # Never inside the square landscape (half 2.95 km; its corners reach 4.17 km): each apron vertex stays 50 m
    # outside the map boundary along its angle, or the 900 m apron would float over the low corners of the map.
    edge_r = 296000.0 / np.maximum(np.abs(np.cos(ang)), np.abs(np.sin(ang))) + 5000.0
    for k, r in enumerate(apron):
        t = k / (len(apron) - 1)
        for i, a in enumerate(ang):
            rr = max(r, edge_r[i]) if k < len(apron) - 1 else max(r, edge_r[i] + 2000.0)
            verts.append((np.cos(a) * rr, np.sin(a) * rr, APRON_Z + t * (30000.0 + 40000.0 * foot[i])))
    for j, r in enumerate(radii):
        for i, a in enumerate(ang):
            h = (60000.0 + 150000.0 * ridge[j, i]) * envelope[j]
            if j < 4:   # rise out of the apron instead of dipping to the pivot height first
                h = max(h, (APRON_Z + 30000.0 + 40000.0 * foot[i]) * (1.0 - j / 4.0))
            verts.append((np.cos(a) * r, np.sin(a) * r, h))
    # The apron also goes out as its own mesh with a land material (with the hazy "ranges" material it read as a
    # flat lake beyond the map edge); in the ranges mesh it stays only as the skirt the ranges rise from.
    na = len(apron) * seg
    av = np.array(verts[:na])
    af = []
    for j in range(len(apron) - 1):
        for i in range(seg):
            a0, a1 = j * seg + i, j * seg + (i + 1) % seg
            b0, b1 = a0 + seg, a1 + seg
            af += [(a0, b0, b1), (a0, b1, a1)]
    write_obj(out / "SM_HG_DistantApron.obj", av, np.array(af))
    rings += len(apron)
    for j in range(rings - 1):
        for i in range(seg):
            a0, a1 = j * seg + i, j * seg + (i + 1) % seg
            b0, b1 = a0 + seg, a1 + seg
            faces += [(a0, b0, b1), (a0, b1, a1)]
    write_obj(out / "SM_HG_DistantRanges.obj", np.array(verts), np.array(faces))

    def boxes(spec):
        vs, fs, off = [], [], 0
        for cx, cy, cz, sx, sy, sz in spec:
            x0, x1, y0, y1, z0, z1 = cx - sx / 2, cx + sx / 2, cy - sy / 2, cy + sy / 2, cz - sz / 2, cz + sz / 2
            bv = np.array([(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)])
            bf = np.array([(0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7), (0, 1, 5), (0, 5, 4), (1, 2, 6), (1, 6, 5), (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7)])
            vs.append(bv); fs.append(bf + off); off += 8
        return np.vstack(vs), np.vstack(fs)

    # Curve chevron sign (black board, the white arrows are painted by UV in the material).
    v, f = boxes([(0, 0, 60, 6, 6, 140), (0, 4, 150, 60, 3, 45)])   # board on +Y as always seen in game (5B)
    uv = np.stack([(v[:, 0] + 30) / 60.0, (v[:, 2] - 127) / 45.0], axis=1)
    write_obj(out / "SM_HG_Chevron.obj", v, f, uv=uv)
    # Concrete utility pole with a cross-arm.
    v, f = boxes([(0, 0, 450, 22, 22, 960), (0, 0, 880, 160, 10, 10)])
    write_obj(out / "SM_HG_UtilityPole.obj", v, f)
    # Hmong house: stone/earth walls + low pitched tin roof (separate roof mesh -> grey/green/blue roofs).
    v, f = boxes([(0, 0, 150, 900, 600, 320)])
    write_obj(out / "SM_HG_HouseWalls.obj", v, f)
    rv = np.array([(-520, -380, 300), (520, -380, 300), (520, 380, 300), (-520, 380, 300), (-520, 0, 470), (520, 0, 470)], float)
    rf = np.array([(0, 1, 5), (0, 5, 4), (3, 4, 5), (3, 5, 2), (0, 4, 3), (1, 2, 5)])
    write_obj(out / "SM_HG_HouseRoof.obj", rv, rf)

    # Low dry-stone wall segment (4 m) for the Ma Pi Leng edge and the Mom Da fence.
    v, f = boxes([(200, 0, 35, 400, 45, 70), (200, 0, 76, 400, 52, 12)])
    write_obj(out / "SM_HG_StoneWall.obj", v, f)
    # Youth Volunteers monument at the Ma Pi Leng summit: stepped plinth + tapering obelisk.
    v, f = boxes([(0, 0, 40, 420, 420, 80), (0, 0, 110, 300, 300, 60), (0, 0, 520, 120, 120, 760), (0, 0, 930, 80, 80, 60)])
    write_obj(out / "SM_HG_Monument.obj", v, f)


def main():
    t0 = time.time()
    rng = np.random.default_rng(SEED)
    make_textures(rng)
    print(f"textures {time.time() - t0:.1f}s")
    make_meshes(rng)
    make_trees(np.random.default_rng(SEED + 7))
    make_road_furniture()
    make_set_dressing(np.random.default_rng(SEED + 11))
    print(f"meshes {time.time() - t0:.1f}s")
    make_flute(rng)
    make_sfx(rng)
    make_loops(rng)
    make_ambience(np.random.default_rng(SEED + 23))
    print(f"audio {time.time() - t0:.1f}s -> {OUT}")


if __name__ == "__main__":
    main()
