# World3D

A global cube-sphere world model for **ProVPilot**, built as a C++ GDExtension
(`namespace world3d`). World3D renders the whole Earth in **ECEF** (Earth-Centred
Earth-Fixed) double-precision coordinates and replaces `scenery3d` for global
scenery display, handing off to the detailed Swiss LV95 patch by camera altitude.

Requires the **double-precision Godot 4.7** build used by ProVPilot — ECEF
positions near the surface reach ~6.4 million metres, which 32-bit floats cannot
represent without shredding vertex precision.

## Architecture

Cube-sphere with per-face chunked quadtree LOD on the WGS84 ellipsoid:

- Six cube faces are the roots of six quadtrees; chunks are 17×17 vertex patches.
- A chunk splits when its on-screen angular size exceeds a threshold (~10°),
  and by node **size** rather than tree level to handle cube-corner stretching.
- Every vertex is `(R + elevation) · surface_normal`, in metres, rendered in ECEF.
- Elevation comes from **ETOPO 2022** (60/30 arc-second); coastlines are derived
  for free from the sign of the bathymetry-inclusive height field.
- The Swiss `scenery3d` patch is mounted on an **ENU tangent frame** at the Bern
  origin (E=2,600,000 / N=1,200,000) and takes over below an altitude threshold.

```
World3D (Node3D)          — Root coordinator, ECEF, altitude handoff
└── W3DCoords (RefCounted)— WGS84 geodetic ↔ ECEF + ENU tangent frame
```

More managers (cube-sphere quadtree, chunk streamer, Swiss handoff) land in later
milestones.

## Coordinate conventions

- **Global shell:** ECEF metres, `X`/`Y`/`Z` per the WGS84 ellipsoid
  (a = 6,378,137 m, f = 1/298.257223563).
- **Local Swiss patch:** existing `scenery3d` LV95 → Godot convention, hung off an
  ENU-to-ECEF transform (`+X = East`, `+Y = Up`, `+Z = North`).

## Building

Mirrors `scenery3d`. Requires SCons and the pinned `godot-cpp` submodule.

```sh
git submodule update --init --recursive
scons target=template_debug precision=double
```

The shared library is written to `demo/bin/`, where `world3d.gdextension`
references it.

## Data pipeline

`tools/` (added in a later milestone) downloads ETOPO 2022 and bakes per-node
heightmaps keyed by `(face, level, x, y)` into a headerless raw format mirroring
the `scenery3d` tile convention, so the runtime loader stays dumb and fast.

## Milestones

1. **Scaffold** — GDExtension builds, registers, and loads. ✅
2. **Static cube-sphere** — 6 faces from 60″ ETOPO, land/sea colour by elevation sign.
3. **Data pipeline** — Python ETOPO baker (rasterio/numpy).
4. **Quadtree LOD streaming** — background chunk loader, split/merge by angular size.
5. **Swiss handoff** — ECEF/ENU mount of `scenery3d`, altitude-based near/far switch.
