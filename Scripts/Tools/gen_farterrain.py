#!/usr/bin/env python3
"""Real terrain beyond the map edge (Round 3B): Generated/Assets/Meshes/SM_HG_FarTerrain.obj

The 5.9 km landscape ended in a flat foothill apron that read as a hazy lake from the Ma Pi Leng rim. The
Copernicus tile covers far more than the frame, so the real mountains around it (out to 12 km, 100 m grid)
become one mesh, absolute altitude (Z cm = ASL m * 100), spawned at the origin. Inside the map square it is cut
away; within 400 m of the map edge it is tucked up to 25 m under the landscape so the seam never shows.
The synthetic ridge layers (gen_ranges.py) stand beyond it.

Usage: .venv/bin/python Scripts/Tools/gen_farterrain.py
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_heightmap as G  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Generated" / "Assets" / "Meshes"
R_M = 12000.0
STEP_M = 100.0


def main():
    dem = G.load_dem()
    xs = np.arange(-R_M, R_M + 1e-3, STEP_M)
    ys = np.arange(-R_M, R_M + 1e-3, STEP_M)
    lats = G.LAT0 - ys / G.M_PER_DEG_LAT
    lons = G.LON0 + xs / G.m_per_deg_lon(G.LAT0)
    h = G.sample_dem(dem, lats, lons).astype(np.float64)            # rows = y, cols = x
    X, Y = np.meshgrid(xs, ys)
    half = G.HALF_M
    outside = np.maximum(np.abs(X), np.abs(Y)) - half                # m outside the map square (<0 inside)
    h -= 25.0 * np.clip(1.0 - outside / 400.0, 0.0, 1.0)             # tuck under the landscape near the seam
    inner = outside < -150.0                                         # vertices well inside the map
    n = len(xs)
    idx = -np.ones((n, n), np.int64)
    keep_v = ~inner
    # keep vertices of any quad that is not fully inside
    quad_in = inner[:-1, :-1] & inner[1:, :-1] & inner[:-1, 1:] & inner[1:, 1:]
    used = np.zeros((n, n), bool)
    used[:-1, :-1] |= ~quad_in
    used[1:, :-1] |= ~quad_in
    used[:-1, 1:] |= ~quad_in
    used[1:, 1:] |= ~quad_in
    del keep_v
    vid = np.argwhere(used)
    idx[used] = np.arange(len(vid))
    verts = np.stack([X[used] * 100.0, Y[used] * 100.0, h[used] * 100.0], axis=1)
    faces = []
    for r in range(n - 1):
        for c in range(n - 1):
            if quad_in[r, c]:
                continue
            a, b, cc, d = idx[r, c], idx[r, c + 1], idx[r + 1, c + 1], idx[r + 1, c]
            faces.append((a, cc, b))
            faces.append((a, d, cc))
    f = np.array(faces)
    fn = np.cross(verts[f[:, 1]] - verts[f[:, 0]], verts[f[:, 2]] - verts[f[:, 0]])
    flip = fn[:, 2] < 0
    fn[flip] *= -1
    f[flip] = f[flip][:, [0, 2, 1]]
    # Round 4B: Unreal culls these (counter-clockwise seen from above) faces as back faces - in game only the
    # slopes above eye level showed (seen from below): an "arch" over a void past Ma Pi Leng and terrain slivers
    # floating in the sky. Wind them clockwise; the vertex normals stay up.
    f = f[:, [0, 2, 1]]
    nrm = np.zeros_like(verts)
    for k in range(3):
        np.add.at(nrm, f[:, k], fn)
    nrm /= np.linalg.norm(nrm, axis=1, keepdims=True) + 1e-9
    OUT.mkdir(parents=True, exist_ok=True)
    with open(OUT / "SM_HG_FarTerrain.obj", "w") as fh:
        fh.write("# Ha Giang terrain beyond the map edge (gen_farterrain.py)\n")
        for p in verts:
            fh.write(f"v {p[0]:.0f} {p[1]:.0f} {p[2]:.0f}\n")
        for q in nrm:
            fh.write(f"vn {q[0]:.4f} {q[1]:.4f} {q[2]:.4f}\n")
        for t in f:
            fh.write(f"f {t[0] + 1}//{t[0] + 1} {t[1] + 1}//{t[1] + 1} {t[2] + 1}//{t[2] + 1}\n")
    print(f"SM_HG_FarTerrain: {len(verts)} verts, {len(f)} tris, {h[used].min():.0f}..{h[used].max():.0f} m")


if __name__ == "__main__":
    main()
