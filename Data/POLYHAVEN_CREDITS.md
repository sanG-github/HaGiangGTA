# Poly Haven assets (CC0)

The scanned rocks, cliffs, plants and textures under `Data/PolyHaven/` come from Poly Haven (https://polyhaven.com).
They are released under CC0 1.0 Universal (public domain, https://polyhaven.com/license): no attribution is required,
but we credit Poly Haven and its artists and scanners anyway.

The files are not committed. Run `python3 Scripts/Tools/fetch_polyhaven.py` to download them. The script is
idempotent and checks md5s. `Data/polyhaven_manifest.json` lists each asset id, its resolution, format, files and
bytes, and its source page.
