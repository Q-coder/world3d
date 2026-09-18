# World3D

A global cube-sphere world model for **ProVPilot**, built as a C++ GDExtension
(`namespace world3d`). World3D renders the whole Earth in **ECEF** (Earth-Centred
Earth-Fixed) double-precision coordinates and provides fast whole-planet terrain
queries for flight dynamics. It complements `scenery3d`: the global shell is the
far/low-detail backdrop, and detailed local patches (the Swiss LV95 tiles, or
Baden-Württemberg) are mounted on it via a tangent frame with a near/far handoff.

Requires the **double-precision Godot 4.7** build used by ProVPilot — ECEF
positions near the surface reach ~6.4 million metres, which 32-bit floats cannot
represent without shredding vertex precision.

## Status

All of the initial milestones are implemented and verified:

1. **Scaffold** — GDExtension builds, registers, and loads.
2. **Static cube-sphere** — six faces from 60″ ETOPO, land/sea colour by elevation.
3. **Data pipeline** — Python ETOPO baker (`tools/bake_faces.py`).
4. **Quadtree LOD streaming** — background worker pool, seamless split/merge,
   horizon culling, per-chunk local ECEF origins for surface-precise close-ups.
5. **Swiss handoff** — ECEF/ENU tangent mount + altitude-based near/far switch.
6. **Terrain queries** — O(1) whole-planet elevation / AGL for ground contact.

## Architecture

Cube-sphere with per-face chunked quadtree LOD on the WGS84 ellipsoid, rendered
in ECEF:

- Six cube faces are the roots of six quadtrees; each chunk is a `chunk_resolution`
  (default 33) vertex patch.
- A node splits when the camera is closer than `edge_len × lod_split_factor` and
  collapses when it moves away, capped at `max_level`.
- Heavy per-vertex maths (geodetic→ECEF, normals) runs on a **background worker
  pool**; the main thread only commits a budgeted number of finished meshes to the
  GPU per frame, so motion never stalls on chunk splits.
- Splits are **seamless**: a parent stays visible until all four children are
  committed, so the surface never shows a hole. **Horizon culling** skips chunks
  beyond the visible cap.
- Each chunk's vertices are stored relative to its ECEF centre, with the
  `MeshInstance3D` carrying the centre in its double transform — so the float32 GPU
  vertex buffer stays precise right down at the surface.
- Elevation comes from **ETOPO 2022** (60 arc-second); coastlines fall out of the
  sign of the bathymetry-inclusive height field. Oceans render as a flat shell at
  0 m (`clamp_sea_level`).

```
W3DWorld (Node3D)            — Root coordinator; scans for the globe + anchors,
│                              performs the near/far handoff.
├── W3DGlobe (Node3D)        — Cube-sphere quadtree; streaming; terrain queries.
└── W3DTangentAnchor (Node3D)— Mounts a flat local patch (scenery3d) on the
                               ellipsoid at a geodetic origin; activates by range.

W3DCoords (RefCounted)       — WGS84 geodetic ↔ ECEF + ENU/WUN tangent frames.
```

## Coordinate conventions

- **Global shell:** ECEF metres per the WGS84 ellipsoid (a = 6,378,137 m,
  f = 1/298.257223563). `+Z` is the true north pole.
- **Local patch:** the `scenery3d` LV95 → Godot convention (`+X = West`, `+Y = Up`,
  `+Z = North`) is a proper right-handed frame; `W3DTangentAnchor` mounts it with
  `x_axis_west = true`. (A naïve `East/Up/North` basis is left-handed and would
  mirror mounted geometry, so use the provided helper.)

## Building

Mirrors `scenery3d`. Requires SCons and the pinned `godot-cpp` submodule.

```sh
git submodule update --init --recursive
scons target=template_debug precision=double
```

The shared library is written to `demo/bin/`, where `world3d.gdextension`
references it. The build defaults to the double-precision API file in
`gdextension/extension_api.double.json` when `precision=double`.

## Data pipeline

`tools/bake_faces.py` downloads the global ETOPO 2022 60″ surface GeoTIFF (~444 MB,
cached under `data/`) and bakes it into six cube-face heightmaps under
`demo/faces/` (`face{0..5}.f32` float32 + `meta.json`). Requires
`numpy`, `rasterio`, `requests`.

```sh
python3 tools/bake_faces.py                # 513-vertex faces
python3 tools/bake_faces.py --face-size 1025
```

The cube mapping in the baker and in `src/w3d_globe.cpp` must stay in lock-step.
`data/` and `demo/faces/` are gitignored, so regenerate the faces after a fresh
clone before running.

## Running the demo

```sh
GODOT=/path/to/godot.macos.editor.double.arm64   # ProVPilot's double-precision build

$GODOT --path demo --headless --import           # once, or after editing scenes
$GODOT --path demo --rendering-driver metal      # run (drag to orbit, wheel to zoom)
```

Environment test hooks (prefix the run command):

| Var | Effect |
|-----|--------|
| `W3D_MASKTEST=1` | Upload a test coverage mask in ECEF XZ (checkerboard around Bern) — the globe must show holes there. |
| `W3D_MASKTEST=2` | Same, but the mask is in **LV95 grid metres** (`set_coverage_mask_frame(1)`) — holes aligned with the Swiss grid. |
| `W3D_DIST=<m>` | Camera distance from Earth centre. |
| `W3D_VIEW=bern` | Look straight down over Switzerland, north up. |
| `W3D_SHOT=<png>` | Render one frame to a PNG, then quit. |
| `W3D_FLY=1` | Fly from orbit to the surface and log frame-time / jank stats. |
| `W3D_ANCHOR_ALWAYS=1` | Force the tangent patch active regardless of range. |
| `W3D_KEEPGLOBE=1` | Keep the globe visible even when a patch is active. |
| `W3D_ELEVTEST=1` | Print elevation/AGL queries at a few known locations. |

## API reference

### `W3DGlobe` (Node3D)

Properties: `faces_path`, `vertical_exaggeration`, `clamp_sea_level`,
`chunk_resolution`, `max_level`, `lod_split_factor`.

Terrain queries — O(1), thread-safe, whole-planet (sampled from the in-memory
face grids, independent of which chunks are meshed):

| Method | Returns |
|--------|---------|
| `get_elevation(lat, lon)` | terrain height in metres ASL (matches the rendered surface) |
| `get_ground_ecef(lat, lon)` | ECEF point on the terrain surface |
| `get_terrain_ecef_below(ecef)` | ECEF surface point directly under an ECEF position |
| `get_height_above_terrain(ecef)` | height above ground (AGL) in metres |

Patch suppression — a **per-fragment coverage mask**. The host uploads one
byte per cell of a square grid (nonzero = detailed scenery geometry is
resident in that cell) together with the mapping from the globe's parent
frame XZ (the frame the globe is mounted into) to cell coordinates
(`cell = floor((xz - origin) / step)`, a negative step is fine when the host's
axis runs opposite to the cell index). The chunk shader discards fragments in
covered cells, so the coarse globe is hidden exactly under resident detailed
geometry regardless of globe chunk size or LOD level, and stays visible as
the fallback surface in every other cell (unloaded cells while the streamer
catches up, sea, data gaps) — a hole to space cannot exist. ProVPilot feeds
Scenery3D's **live tile/chunk residency** mask straight into it (one byte
per 1 km tile around the camera, ~421² cells) whenever a tile or chunk
appears or disappears or the camera moves. Chunk-level culling by rectangles was the
previous mechanism and was dropped: a max-level chunk is 130–170 km wide and
could never be contained inside a ~220 km region rectangle reliably.

| Method | Effect |
|--------|--------|
| `set_coverage_mask(cells, n, origin_x, origin_z, step_x, step_z)` | replace the mask (`cells` = n×n bytes, row = second-axis cell, column = first-axis cell) |
| `set_coverage_mask_frame(frame)` | `0` (default): mask plane = the globe's parent-frame XZ; `1`: parent frame is ECEF and the shader projects each fragment ECEF → WGS84 → CH1903+ → **LV95 E/N** (rigorous Swiss oblique Mercator, vendored `s3d_proj.h`), so the mask is in LV95 grid metres while the globe sits at the ECEF origin |
| `clear_coverage_mask()` | no mask — the globe renders everywhere |
| `get_coverage_mask_size()` | n (0 when no mask) |
| `suppresses_world_point(x, z)` | CPU mirror of the shader test in the mask plane (for host/tests) |
| `suppresses_ecef_point(ecef)` | CPU mirror for frame 1 (ECEF point → LV95 → mask) |

### `W3DTangentAnchor` (Node3D)

Mounts a flat local patch on the ellipsoid. Properties: `origin_latitude`,
`origin_longitude`, `origin_height`, `x_axis_west` (default `true`, scenery3d
convention), `active_radius`. Methods: `get_origin_ecef()`, `is_active()`.

### `W3DWorld` (Node3D)

Root coordinator. Property: `hide_globe_when_local` (default `true`) — hides the
coarse globe while any child anchor is active.

### `W3DCoords` (RefCounted, static)

| Method | Purpose |
|--------|---------|
| `geodetic_to_ecef(lat, lon, h)` | WGS84 → ECEF metres |
| `ecef_to_geodetic(ecef)` | ECEF → `(lat, lon, height)` |
| `geodetic_normal(lat, lon)` | outward ellipsoid normal |
| `enu_to_ecef_transform(lat, lon, h, x_axis_west=false)` | local tangent frame → ECEF (proper right-handed) |

## Using it in ProVPilot

ProVPilot's Godot world **is ECEF** (design §9.5 in its `WORLD_SCENERY_DESIGN.md`):
the globe sits at the origin (`GlobeMount` = identity), Scenery3D places its
regional tiles on the ellipsoid (`frame_mode = 1`), and the aircraft node
carries an ECEF position with a body-to-ECEF basis from JSBSim.

```
node_3d
├── GlobeMount (identity)
│   └── W3DGlobe            (faces_path = "res://faces", coverage mask frame 1)
├── Scenery3D (frame_mode 1)
└── AC                      (ECEF pose; cameras as children)
```

Place anything at a geodetic point with `W3DCoords`:

```gdscript
aircraft.global_position = W3DCoords.geodetic_to_ecef(lat, lon, altitude_m)
# Local West-Up-North frame there (scenery3d convention):
var wun := W3DCoords.enu_to_ecef_transform(lat, lon, altitude_m, true)
```

Double precision keeps positions stable at ~6.4 M m; no floating-origin
rebasing is needed. Do not use `Node3D.look_at()` / `is_equal_approx()` on
world positions: their relative epsilon is ~64 m at these magnitudes.

### Ground contact / collision

ProVPilot does terrain collision the flight-sim way — a fast elevation query in the
flight dynamics, the same pattern as `scenery3d`'s `S3DElevationDB` — not physics
shapes. Each tick:

```gdscript
var agl    := globe.get_height_above_terrain(aircraft.global_position)  # AGL metres
var ground := globe.get_terrain_ecef_below(aircraft.global_position)    # ECEF under you
```

`get_elevation` / `get_height_above_terrain` are O(1), thread-safe, and cover the
whole planet regardless of which LOD chunks are currently loaded — so they are
safe to call from the physics thread. ProVPilot prefers the Scenery3D value
where a region is resident and falls back to the globe elsewhere.

### Hiding the globe under regional scenery

The host uploads Scenery3D's live tile-resolution coverage mask
(`set_coverage_mask`, mask frame 1 = LV95 grid metres); the chunk shader
projects each fragment ECEF → LV95 and discards it where detailed geometry is
resident, so the globe is the fallback surface everywhere else.
`W3DTangentAnchor` / `W3DWorld` remain for hosts that mount a flat patch.

## Notes and limitations

- Global terrain is 60″ ETOPO (~2 km posting), so sharp peaks are smoothed
  (Everest reads ~5.9 km). Fine near-surface detail is what the local patches add;
  a finer offline bake is possible if needed.
- Chunks are visual-only (no Godot physics `CollisionShape3D`). For aircraft this
  is correct — use the elevation queries. Rigid-body terrain collision (e.g. for
  ground vehicles) would need `StaticBody3D` + `ConcavePolygonShape3D` on near
  chunks; not implemented.
- Still open (polish): T-junction skirts between LOD levels, and frustum
  culling. The keep-globe-as-backdrop refinement is the `set_coverage_mask()`
  API above (per-fragment discard in the chunk shader).
