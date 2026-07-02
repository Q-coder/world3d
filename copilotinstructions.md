Create world3D in C++.
World3D is used in ProVPilot for scenery generation. It contains a full world model.

namespace in C++ is world3d

ProVPilot is compiled with Godot 4.7 in dobule precision.
For reference the ProVPilot is located here : /Users/gery/code/provpilot
World3D will replace the currently existing Scenery3D for scenery display which is submodules in Git here : /Users/gery/code/provpilot/provpilot/addons/scenery3d

Some premade thoughts to incorporate :
The architecture: a cube-sphere quadtree on the WGS84 ellipsoid

The standard approach, and the one that fits your stack best, is a cube-sphere with per-face chunked quadtree LOD, rendered in ECEF (Earth-Centred Earth-Fixed) coordinates. You're already running the double-precision Godot fork — that's the single biggest prerequisite, because ECEF positions reach 6.4 million metres and 32-bit floats would shred your vertex precision near the surface.

There's a directly relevant Godot reference to crib from: the cuberact godot-cuberact-planet-chunked-lod project. It starts from a unit cube, makes each of the 6 cube faces the root of a quadtree, with 17×17 vertex chunks, and splits a chunk when its angular size on screen exceeds a threshold (default 10°). GDScript handles the geometry and LOD decisions while shaders displace the flat sphere patches and render atmosphere. Your version replaces the procedural noise with sampled real elevation. There's also mathis-s/PlanetaryTerrain (an open-sourced Unity asset, MIT) that's a good algorithm reference even though it's not Godot, and the cube-corner-stretching caveat from dexyfex's writeup is worth reading before you commit to a cube vs. icosphere base. GitHubGitHub

Here's how the two layers relate geometrically — this is the crux:

Here's the geometric crux — a flat, detailed scenery3d patch sitting tangent on a curved, low-detail global shell, both living in the same double-precision ECEF space:

V visualize V

visualize show_widget

Now the practical parts: data, pipeline, and how the two layers meet.
Data sources for the rough world

You need two things — elevation (for the height model) and coastlines (for the continent outlines) — and you can get the coastlines almost for free from the elevation.

For elevation, the three real options, all in WGS84 lat/lon:

    ETOPO 2022 (NOAA) is the one I'd pick. It's a seamless topographic–bathymetric dataset at 15 arc-second global resolution, and crucially it's available in GeoTIFF and NetCDF, in 15×15° tiles plus global tiles at 30 and 60 arc-second resolutions. The bathymetry matters: because ETOPO includes ocean depth, every place the elevation crosses zero is a coastline. You get continent outlines for free from the sign of the height field — no vector data needed for a first cut. Its ocean bathymetry largely comes from GEBCO, which shares the same 15-arc-second grid. NOAA + 2
    GEBCO 2024 is essentially the same bathymetry-inclusive global grid at 15 arc-second resolution, a continuous global terrain model for ocean and land. Interchangeable with ETOPO for your purposes. Nature
    GMTED2010 (USGS/NGA) is land-only — no oceans — at 30-, 15-, and 7.5-arc-second resolutions, replacing GTOPO30 as the elevation dataset of choice for global and continental-scale applications. Use this only if you go the vector-coastline route instead of sign-of-bathymetry. Note it covers 84°N to 56°S for most products, so Antarctica is incomplete. usgsusgs

For "rough," don't reach for 15 arc-second. At 60 arc-second the whole earth is a single 21600×10800 grid (~233M cells, ~466 MB as int16); at 30 arc-second it's 43200×21600 (~1.9 GB int16). Either fits comfortably and gives you ~1–2 km horizontal posting, which is plenty when the camera is at altitude. ETOPO1's resolution for comparison: 1 arc-minute spacing, about 1.85 km at the equator. psu

For crisp coastlines (if the bathymetry zero-crossing looks too ragged), use Natural Earth. It's WGS84, shapefile format, with carefully generalized linework at 1:10m, 1:50m, and 1:110m scales. The 1:110m coastline file is ~83 KB — trivially small and gives recognizable continents; 1:50m is the sweet spot for a globe you fly over. The matching 1:10m raster (shaded relief, land cover, ocean) ships at 21,600 × 10,800 px and registers precisely with the vectors if you later want a base colour texture instead of colour-by-elevation. It's free for any use. Natural EarthNatural Earth

So the minimal data shopping list is just one file: ETOPO 2022 at 30 or 60 arc-second, bedrock or ice-surface, GeoTIFF. Add Natural Earth 1:50m coastline only if you want cleaner shores.
Processing pipeline

You already use GDAL heavily in scenery3d's tools (the /vsizip/ streaming, the convert_lgl_bw.py reprojection), so this is familiar ground. The flow:

    Download the ETOPO global GeoTIFF (single file at 30/60 arc-sec).
    For a cube-sphere, you don't reproject to a planar CRS — you sample the lat/lon grid directly. For each quadtree node on a cube face, convert its grid of (u,v) → cube point → normalized sphere direction → geodetic lat/lon, then bilinear-sample the ETOPO grid at that lat/lon for elevation. gdallocationinfo-style sampling, but you'll want to do it in bulk in Python with rasterio/numpy (read the whole grid into RAM once — it fits — then index).
    Bake per-node heightmaps offline into a simple raw format mirroring scenery3d's own convention (headerless float32 or int16), keyed by (face, level, x, y). This keeps the runtime loader dumb and fast, exactly like the existing .raw tiles.
    Clamp ocean: for a flight sim you usually render sea as a flat shell at elevation 0 and only keep positive elevations as terrain, so the bathymetry is used purely to detect the coastline (sign change), then discarded.

If you'd rather not write the cube-sphere sampler from scratch, GDAL can also pre-warp ETOPO into the six cube faces (gnomonic projection per face), giving you six square rasters you tile with a flat quadtree per face — that reuses scenery3d's planar tile logic almost verbatim, just six times.
Handing off to the Swiss scenery3d patch

This is the part that ties into what you already have. scenery3d's S3DCoords already does WGS84 ↔ LV95 ↔ Godot world. To unify the two layers, extend it (or add a sibling) with a geodetic → ECEF conversion using the standard WGS84 ellipsoid constants (semi-major axis a = 6,378,137 m, flattening f = 1/298.257223563). Then:

    The global shell renders directly in ECEF: every vertex is (R + elevation) · the sphere direction, in metres, which is why you need the double-precision fork — ECEF coordinates near the surface are ~6.4 million metres.
    The Swiss patch stays in its existing flat LV95 → Godot convention, but you place that whole local coordinate frame onto the globe by computing the tangent plane (ENU frame) at the LV95 origin (E=2,600,000 / N=1,200,000, the Bern point scenery3d already uses). That ENU-to-ECEF rotation+translation is a fixed transform you compute once; the entire Swiss tile system then hangs off it as a child node.
    Switch between them by camera altitude/distance: below some threshold the detailed LV95 tiles drive the scene; above it, or outside Switzerland, the global shell takes over. This is the same near/far handoff philosophy scenery3d already uses internally between individual tiles and the 8×8 far-terrain chunks — you're just adding one more, coarser tier on top.

The honest caveat: over Switzerland the flat patch and the curved shell won't perfectly agree at the edges (the patch is a plane, the shell is curved), but over ~300 km the discrepancy is small and a blend/skirt at the boundary hides it. This is exactly the compromise X-Plane and FlightGear make.
A reference to crib from, and a minimal first version

For the globe code itself, the closest Godot-native reference is cuberact/godot-cuberact-planet-chunked-lod, which does six cube faces as quadtree roots, 17×17-vertex chunks on the ideal sphere, splitting by on-screen angular size, with GDScript handling LOD and shaders doing displacement — you swap its procedural noise for ETOPO sampling. mathis-s/PlanetaryTerrain (MIT, Unity) is a solid algorithm reference and explicitly built to make the entirety of Earth your game map, to scale. Worth reading dexyfex's writeup first for the cube-corner stretching gotcha — nodes near cube corners get stretched and can be much smaller than same-level nodes mid-face, which you handle by splitting on node size rather than tree level. GitHub + 2

If I were you, the smallest satisfying first milestone is: render the whole earth as a single static cube-sphere (no streaming yet) at 60 arc-second ETOPO baked into six 1025×1025 face heightmaps, colour land green / sea blue by elevation sign, in your existing double-precision Godot. That gives you a recognizable spinning earth with all continents in an afternoon, entirely decoupled from scenery3d. Add quadtree LOD second, ECEF-correct flight third, and the Swiss tangent-patch handoff last.

One note on "five continents": that's the cartographic/Olympic grouping — the data doesn't know about it. ETOPO and Natural Earth give you all landmasses, and the familiar continents simply emerge from the coastline. There's nothing to configure; render the land and they're all there. 