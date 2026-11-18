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


def main():
    t0 = time.time()
    rng = np.random.default_rng(SEED)
    make_textures(rng)
    print(f"textures {time.time() - t0:.1f}s")
    make_meshes(rng)
    print(f"meshes {time.time() - t0:.1f}s")
    make_flute(rng)
    make_sfx(rng)
    make_loops(rng)
    print(f"audio {time.time() - t0:.1f}s -> {OUT}")


if __name__ == "__main__":
    main()
