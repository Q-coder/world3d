#!/usr/bin/env python3
"""
ETOPO 2022 -> world3d cube-face heightmap baker.

Downloads the global ETOPO 2022 60 arc-second surface-elevation GeoTIFF (once,
cached) and bakes it into six square heightmaps, one per cube-sphere face. Each
face maps its (u, v) grid -> cube point -> unit sphere direction -> geodetic
lat/lon, then bilinearly samples ETOPO for elevation (metres, bathymetry
included, so the sign of the value is the land/sea mask).

Output (headerless, mirrors the scenery3d raw-tile convention):
  faces/face{0..5}.f32   float32 little-endian, row-major, FACE_SIZE x FACE_SIZE
  faces/meta.json        face size, ordering, and the cube mapping used

The cube mapping here MUST stay in lock-step with the C++ renderer
(src/w3d_globe.cpp, cube_face_direction()).

Requirements: numpy, rasterio, requests
Usage:
  python3 tools/bake_faces.py                 # 513 grid, default paths
  python3 tools/bake_faces.py --face-size 1025
"""

import argparse
import json
import sys
from pathlib import Path

import numpy as np

try:
    import rasterio
except ImportError:
    print("Error: rasterio is required (pip install rasterio)", file=sys.stderr)
    sys.exit(1)

ETOPO_URL = (
    "https://www.ngdc.noaa.gov/mgg/global/relief/ETOPO2022/data/"
    "60s/60s_surface_elev_gtif/ETOPO_2022_v1_60s_N90W180_surface.tif"
)
ETOPO_FILENAME = "ETOPO_2022_v1_60s_N90W180_surface.tif"

# Face order and names. Index == the value written to face{index}.f32.
FACE_NAMES = ["+X", "-X", "+Y", "-Y", "+Z", "-Z"]

# Cube mapping: for face f and (u, v) in [-1, 1], the 3D cube point is
#   base[f] + u * uaxis[f] + v * vaxis[f]
# normalized to a unit sphere direction. ECEF axes: +X lon=0/lat=0,
# +Y lon=90E, +Z north pole. Keep identical to the C++ side.
FACE_BASE = np.array([
    [ 1.0,  0.0,  0.0],  # +X
    [-1.0,  0.0,  0.0],  # -X
    [ 0.0,  1.0,  0.0],  # +Y
    [ 0.0, -1.0,  0.0],  # -Y
    [ 0.0,  0.0,  1.0],  # +Z
    [ 0.0,  0.0, -1.0],  # -Z
])
FACE_U = np.array([
    [ 0.0,  1.0,  0.0],  # +X: u -> +Y
    [ 0.0, -1.0,  0.0],  # -X: u -> -Y
    [ 1.0,  0.0,  0.0],  # +Y: u -> +X
    [ 1.0,  0.0,  0.0],  # -Y: u -> +X
    [ 1.0,  0.0,  0.0],  # +Z: u -> +X
    [-1.0,  0.0,  0.0],  # -Z: u -> -X
])
FACE_V = np.array([
    [ 0.0,  0.0,  1.0],  # +X: v -> +Z
    [ 0.0,  0.0,  1.0],  # -X: v -> +Z
    [ 0.0,  0.0, -1.0],  # +Y: v -> -Z
    [ 0.0,  0.0,  1.0],  # -Y: v -> +Z
    [ 0.0,  1.0,  0.0],  # +Z: v -> +Y
    [ 0.0,  1.0,  0.0],  # -Z: v -> +Y
])


def download_etopo(dest: Path) -> Path:
    tif = dest / ETOPO_FILENAME
    if tif.exists() and tif.stat().st_size > 400_000_000:
        print(f"ETOPO already cached: {tif} ({tif.stat().st_size/1e6:.0f} MB)")
        return tif

    import requests

    dest.mkdir(parents=True, exist_ok=True)
    tmp = tif.with_suffix(".tif.part")
    print(f"Downloading ETOPO 2022 60\" (~444 MB) -> {tif}")
    with requests.get(ETOPO_URL, stream=True, timeout=120) as r:
        r.raise_for_status()
        total = int(r.headers.get("content-length", 0))
        done = 0
        with open(tmp, "wb") as fh:
            for chunk in r.iter_content(chunk_size=1 << 20):
                fh.write(chunk)
                done += len(chunk)
                if total:
                    pct = 100.0 * done / total
                    print(f"\r  {done/1e6:7.0f} / {total/1e6:.0f} MB ({pct:5.1f}%)",
                          end="", flush=True)
        print()
    tmp.rename(tif)
    return tif


def bilinear_sample(grid: np.ndarray, lat_deg: np.ndarray, lon_deg: np.ndarray,
                    top: float, left: float, res_lat: float, res_lon: float) -> np.ndarray:
    """Bilinear sample a (rows=lat, cols=lon) grid. Longitude wraps."""
    h, w = grid.shape
    # Fractional pixel coordinates. Row 0 = north (top), col 0 = west (left).
    fr = (top - lat_deg) / res_lat
    fc = (lon_deg - left) / res_lon

    fr = np.clip(fr, 0.0, h - 1.0000001)
    fc = np.mod(fc, w)  # wrap longitude

    r0 = np.floor(fr).astype(np.int64)
    c0 = np.floor(fc).astype(np.int64)
    r1 = np.minimum(r0 + 1, h - 1)
    c1 = np.mod(c0 + 1, w)
    dr = fr - r0
    dc = fc - c0

    v00 = grid[r0, c0]
    v01 = grid[r0, c1]
    v10 = grid[r1, c0]
    v11 = grid[r1, c1]
    top_i = v00 * (1 - dc) + v01 * dc
    bot_i = v10 * (1 - dc) + v11 * dc
    return top_i * (1 - dr) + bot_i * dr


def bake(tif: Path, out_dir: Path, face_size: int) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"Reading {tif} into memory ...")
    with rasterio.open(tif) as src:
        grid = src.read(1).astype(np.float32)
        b = src.bounds
        top, left = b.top, b.left
        res_lon, res_lat = src.res[0], src.res[1]
    print(f"  grid {grid.shape}, bounds L{left} R{b.right} T{top} B{b.bottom}, "
          f"res {res_lon}x{res_lat} deg")

    # Grid of (u, v) in [-1, 1].
    lin = np.linspace(-1.0, 1.0, face_size, dtype=np.float64)
    uu, vv = np.meshgrid(lin, lin)  # shape (face_size, face_size)

    for f in range(6):
        p = (FACE_BASE[f][None, None, :]
             + uu[..., None] * FACE_U[f][None, None, :]
             + vv[..., None] * FACE_V[f][None, None, :])
        norm = np.linalg.norm(p, axis=2, keepdims=True)
        d = p / norm  # unit sphere direction, shape (F, F, 3)

        dx, dy, dz = d[..., 0], d[..., 1], d[..., 2]
        lat = np.degrees(np.arcsin(np.clip(dz, -1.0, 1.0)))
        lon = np.degrees(np.arctan2(dy, dx))

        elev = bilinear_sample(grid, lat, lon, top, left, res_lat, res_lon)
        elev = elev.astype("<f4")
        out = out_dir / f"face{f}.f32"
        elev.tofile(out)
        emin, emax = float(elev.min()), float(elev.max())
        print(f"  face{f} {FACE_NAMES[f]}: {out.name} "
              f"elev[{emin:.0f}, {emax:.0f}] m")

    meta = {
        "face_size": face_size,
        "faces": FACE_NAMES,
        "elevation_units": "meters",
        "format": "float32_le_rowmajor",
        "source": "ETOPO_2022_v1_60s_surface",
        "cube_mapping": {
            "base": FACE_BASE.tolist(),
            "uaxis": FACE_U.tolist(),
            "vaxis": FACE_V.tolist(),
        },
    }
    with open(out_dir / "meta.json", "w") as fh:
        json.dump(meta, fh, indent=2)
    print(f"Wrote {out_dir/'meta.json'}")


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(description="Bake ETOPO into cube-face heightmaps")
    ap.add_argument("--face-size", type=int, default=513,
                    help="vertices per face edge (default 513)")
    ap.add_argument("--etopo-dir", type=Path, default=root / "data" / "etopo",
                    help="download cache directory")
    ap.add_argument("--out-dir", type=Path, default=root / "demo" / "faces",
                    help="output directory for baked faces")
    args = ap.parse_args()

    tif = download_etopo(args.etopo_dir)
    bake(tif, args.out_dir, args.face_size)
    print("Done.")


if __name__ == "__main__":
    main()
