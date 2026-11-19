#!/usr/bin/env python3
"""Layered distant karst ranges (Round 3B): Generated/Assets/Meshes/SM_HG_RangeLayer_0..3.obj

Replaces the single flat ring (SM_HG_DistantRanges) that read as one pale blue band on the horizon.
Three rings at 14 / 17.5 / 21.5 km from the map centre (beyond the real-terrain mesh), each a ridge silhouette with sloped, gullied faces
(so the sun gives every layer lit and shaded flanks) and peaks that rise with distance so each layer shows
above the one in front. Absolute world Z (cm = altitude ASL * 100); spawn at the origin.

Usage: .venv/bin/python Scripts/Tools/gen_ranges.py
"""
from __future__ import annotations

import math
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Generated" / "Assets" / "Meshes"

LAYERS = [
    # radius m, ridge base ASL m, ridge amplitude m, seed
    # Beyond the real DEM terrain mesh (gen_farterrain.py, out to 12 km).
    # Round 4B: the 26 km ring dropped (its spikes floated as slivers above the nearer layers).
    (14000.0, 1450.0, 600.0, 11),
    (17500.0, 1600.0, 650.0, 23),
    (21500.0, 1750.0, 700.0, 37),
]
SEG = 420          # around the ring (~90-200 m per segment)
ROWS = 8           # foot -> ridge
FOOT_ASL = 250.0   # below every horizon seen from the map


def ridge_profile(a, seed):
    """Ridged multi-frequency profile in [0, 1] around the ring, with sharp karst peaks."""
    rng = np.random.default_rng(seed)
    h = np.zeros_like(a)
    norm = 0.0
    for k, w in ((3, 1.0), (7, 0.6), (13, 0.38), (29, 0.22), (61, 0.12)):
        ph = rng.uniform(0, 2 * math.pi, 2)
        v = 1.0 - np.abs(np.sin(a * k + ph[0]) * 0.7 + np.sin(a * (k * 1.37) + ph[1]) * 0.3)
        h += w * v ** 2
        norm += w
    h /= norm
    peaks = rng.uniform(0, 2 * math.pi, 9)
    for p in peaks:
        d = np.angle(np.exp(1j * (a - p)))
        h += 0.22 * np.exp(-(d / 0.07) ** 2)      # broad karst summits, no needle spikes
    return np.clip((h - h.min()) / (h.max() - h.min()), 0, 1)


def gully(a, t, seed):
    rng = np.random.default_rng(seed + 1000)
    g = np.zeros_like(a)
    for k, w in ((41, 1.0), (97, 0.5), (173, 0.3)):
        ph = rng.uniform(0, 2 * math.pi)
        g += w * np.sin(a * k + ph + t * 2.0)
    return g / 1.8


def write_obj(path, v, f, n):
    with open(path, "w") as fh:
        fh.write("# Ha Giang distant range layer (gen_ranges.py)\n")
        for p in v:
            fh.write(f"v {p[0]:.1f} {p[1]:.1f} {p[2]:.1f}\n")
        for q in n:
            fh.write(f"vn {q[0]:.4f} {q[1]:.4f} {q[2]:.4f}\n")
        for t in f:
            fh.write(f"f {t[0] + 1}//{t[0] + 1} {t[1] + 1}//{t[1] + 1} {t[2] + 1}//{t[2] + 1}\n")


def build_layer(radius_m, base_m, amp_m, seed):
    a = np.linspace(0, 2 * math.pi, SEG, endpoint=False)
    ridge = base_m + amp_m * ridge_profile(a, seed)                       # ASL m
    verts = []
    prev = None
    for r in range(ROWS):
        t = r / (ROWS - 1)                                                 # 0 foot .. 1 ridge
        z = FOOT_ASL + (ridge - FOOT_ASL) * (1.0 - (1.0 - t) ** 1.6)       # concave slope, steep near the top
        # Foot ~900 m nearer than the ridge: faces slope back like real mountain flanks. Gullies keep one phase
        # per column and the radius only grows upwards (Round 4B: folded rows read as arches / floating slivers).
        rad = radius_m - (1.0 - t) ** 1.3 * 900.0 + gully(a, 0.0, seed) * 120.0 * (1.0 - t)
        if prev is not None:
            rad = np.maximum(rad, prev + 20.0)
        prev = rad
        for i in range(SEG):
            verts.append((math.cos(a[i]) * rad[i] * 100.0, math.sin(a[i]) * rad[i] * 100.0, z[i] * 100.0))
    v = np.array(verts)
    faces = []
    for r in range(ROWS - 1):
        for i in range(SEG):
            a0, a1 = r * SEG + i, r * SEG + (i + 1) % SEG
            b0, b1 = a0 + SEG, a1 + SEG
            faces += [(a0, b1, b0), (a0, a1, b1)]
    f = np.array(faces)
    # Vertex normals; faces point towards the map centre (the viewer).
    fn = np.cross(v[f[:, 1]] - v[f[:, 0]], v[f[:, 2]] - v[f[:, 0]])
    centre_dir = -(v[f[:, 0]] * np.array([1, 1, 0]))
    flip = (fn * centre_dir).sum(axis=1) < 0
    fn[flip] *= -1
    f[flip] = f[flip][:, [0, 2, 1]]
    n = np.zeros_like(v)
    for k in range(3):
        np.add.at(n, f[:, k], fn)
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-9
    return v, f, n


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for i, (radius, base, amp, seed) in enumerate(LAYERS):
        v, f, n = build_layer(radius, base, amp, seed)
        write_obj(OUT / f"SM_HG_RangeLayer_{i}.obj", v, f, n)
        print(f"SM_HG_RangeLayer_{i}: {len(v)} verts, {len(f)} tris, ridge {base:.0f}-{base + amp:.0f} m at {radius / 1000:.1f} km")


if __name__ == "__main__":
    main()
