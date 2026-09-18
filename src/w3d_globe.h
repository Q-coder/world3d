#ifndef W3D_GLOBE_H
#define W3D_GLOBE_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <cstdint>

namespace godot
{
	class MeshInstance3D;
}

namespace world3d
{

	// Cube-sphere quadtree Earth. Each cube face is the root of a quadtree; a node
	// splits into four children when the camera gets close relative to the node's
	// edge length, and collapses when it moves away.
	//
	// Each chunk mesh is built with its vertices RELATIVE to the chunk's ECEF
	// centre, and the owning MeshInstance3D carries that centre in its (double)
	// transform. This keeps per-vertex magnitudes small so the float32 GPU vertex
	// buffer stays precise right down at the surface — the prerequisite for
	// close-up ECEF flight.
	//
	// For smooth flight the heavy per-vertex maths runs on a background worker
	// pool; the main thread only commits finished vertex buffers to the GPU (a few
	// per frame) and toggles visibility. Splits keep the parent visible until all
	// four children are ready, so the surface never shows a hole.
	//
	// Elevation is sampled from the six baked ETOPO face grids held in memory.
	// The cube mapping must match tools/bake_faces.py.
	class W3DGlobe : public godot::Node3D
	{
		GDCLASS(W3DGlobe, godot::Node3D);

	private:
		struct QuadNode
		{
			uint64_t id = 0;
			int face = 0;
			int level = 0;
			double u0 = -1.0, v0 = -1.0, u1 = 1.0, v1 = 1.0;
			godot::Vector3 center_ecef; // chunk centre (double, metres)
			double edge_len = 0.0;      // approx ECEF edge length (metres)
			double bound_radius = 0.0;  // bounding-sphere radius about centre
			godot::MeshInstance3D *mesh = nullptr;
			bool mesh_requested = false;
			QuadNode *children[4] = {nullptr, nullptr, nullptr, nullptr};
			bool has_children = false;
		};

		// A meshing job sent to a worker thread (carries only plain data).
		struct ChunkRequest
		{
			uint64_t id = 0;
			int face = 0;
			double u0 = 0, v0 = 0, u1 = 0, v1 = 0;
			godot::Vector3 origin;
		};

		// A finished vertex buffer returned by a worker thread.
		struct ChunkResult
		{
			uint64_t id = 0;
			godot::PackedVector3Array vertices;
			godot::PackedVector3Array normals;
			godot::PackedColorArray colors;
		};

		godot::String faces_path = "res://faces";
		double vertical_exaggeration = 1.0;
		bool clamp_sea_level = true;   // render/query ocean as a flat shell at 0 m
		int chunk_resolution = 33;     // vertices per chunk edge
		int max_level = 6;
		double lod_split_factor = 2.5; // split when dist < edge_len * factor
		int commit_budget = 6;         // finished meshes committed to GPU per frame

		int face_size = 0;
		std::vector<float> face_grid[6];
		QuadNode *roots[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
		bool ready = false;

		// Coverage mask: where a detailed scenery patch is RESIDENT, the coarse
		// globe must not show through. The host uploads one byte per Scenery3D
		// chunk cell (255 = covered) on a square n×n grid, plus the mapping
		// from the globe's parent ("world", i.e. the host's flat frame) XZ to
		// cell coordinates: cell = (world_xz - origin) / step (floor). The
		// chunk shader discards fragments in covered cells, so suppression is
		// per FRAGMENT and independent of globe chunk size or LOD level — the
		// globe ends exactly at the resident-geometry boundary and stays
		// visible as the fallback surface in unloaded / no-data cells.
		godot::PackedByteArray mask_cells;
		int mask_n = 0;
		double mask_origin_x = 0.0, mask_origin_z = 0.0;
		double mask_step_x = 1.0, mask_step_z = 1.0;
		// Mask frame: how a fragment position maps to the mask's 2D plane.
		//   0 = FLAT: the globe's parent-frame XZ (host mounts the globe into a
		//       flat tangent frame; design §9 pre-step-C).
		//   1 = LV95: the parent frame IS ECEF; fragment ECEF -> WGS84 geodetic
		//       -> CH1903+ -> Swiss oblique Mercator E/N in the shader, so the
		//       mask is in LV95 grid metres (Scenery3D's tile grid) while the
		//       globe sits at the ECEF origin (step C).
		int mask_frame = 0;
		godot::Ref<godot::ImageTexture> mask_texture;

		// Shared triangle topology (identical for every chunk of this resolution).
		godot::PackedInt32Array shared_indices;
		// Shared material for every chunk (released in _exit_tree).
		godot::Ref<godot::Shader> chunk_shader;
		godot::Ref<godot::ShaderMaterial> chunk_material;

		// Background meshing pool.
		std::vector<std::thread> workers;
		std::mutex req_mutex;
		std::condition_variable req_cv;
		std::deque<ChunkRequest> requests;
		std::mutex res_mutex;
		std::deque<ChunkResult> results;
		std::atomic<bool> stop_threads{false};
		std::unordered_map<uint64_t, QuadNode *> live_nodes;
		uint64_t next_id = 1;

		bool load_faces();
		void build_shared_indices();
		float sample_face(int face, double u, double v) const;
		float terrain_height(float raw) const; // applies sea-level clamp
		godot::Vector3 ecef_at(int face, double u, double v) const;
		void compute_chunk(const ChunkRequest &req, ChunkResult &out) const;
		void worker_loop();
		void start_workers();
		void stop_workers();

		QuadNode *make_node(int face, int level, double u0, double v0, double u1, double v1);
		void request_mesh(QuadNode *node);
		void commit_result(const ChunkResult &res);
		void drain_results();
		void free_subtree(QuadNode *node);
		void collapse(QuadNode *node);
		void split(QuadNode *node);
		bool children_ready(QuadNode *node) const;
		bool beyond_horizon(const QuadNode *node, const godot::Vector3 &cam_ecef) const;
		void push_mask_uniforms();
		void update_node(QuadNode *node, const godot::Vector3 &cam_ecef);

	protected:
		static void _bind_methods();

	public:
		W3DGlobe();
		~W3DGlobe();

		void _ready() override;
		void _process(double delta) override;
		void _exit_tree() override;

		void set_faces_path(const godot::String &p_path);
		godot::String get_faces_path() const;

		void set_vertical_exaggeration(double p_ex);
		double get_vertical_exaggeration() const;

		void set_chunk_resolution(int p_res);
		int get_chunk_resolution() const;

		void set_max_level(int p_level);
		int get_max_level() const;

		void set_lod_split_factor(double p_factor);
		double get_lod_split_factor() const;

		void set_clamp_sea_level(bool p_clamp);
		bool get_clamp_sea_level() const;

		// --- Patch suppression (per-fragment coverage mask) ---
		// cells: n*n bytes, row-major, row = Z cell, column = X cell, nonzero =
		// covered (detailed geometry resident there). origin/step map the
		// globe's parent-frame XZ to cell coordinates (step may be negative
		// when the host's axis runs opposite to the cell index). Replaces the
		// mask entirely; call again whenever the host's coverage changes.
		void set_coverage_mask(const godot::PackedByteArray &cells, int n,
			double origin_x, double origin_z, double step_x, double step_z);
		void clear_coverage_mask();
		int get_coverage_mask_size() const;
		// 0 = flat parent-frame XZ (default), 1 = LV95 E/N computed from ECEF.
		void set_coverage_mask_frame(int p_frame);
		int get_coverage_mask_frame() const;
		// CPU mirrors of the shader test (for the host / smoke tests): a point
		// in the mask plane (flat XZ or LV95 E/N depending on the frame), or an
		// ECEF point (frame 1 only).
		bool suppresses_world_point(double x, double z) const;
		bool suppresses_ecef_point(godot::Vector3 ecef) const;

		// --- Global terrain queries (for flight dynamics / ground contact) ---
		// All work anywhere on the planet from the in-memory face grids, regardless
		// of which chunks are currently meshed, and are O(1) and thread-safe.
		//
		// Terrain elevation (metres, incl. vertical_exaggeration and the sea-level
		// clamp) at a WGS84 geodetic point. Matches the rendered surface exactly.
		double get_elevation(double latitude, double longitude) const;
		// ECEF point on the terrain surface at the given lat/lon.
		godot::Vector3 get_ground_ecef(double latitude, double longitude) const;
		// ECEF terrain point directly below an ECEF position (same lat/lon).
		godot::Vector3 get_terrain_ecef_below(godot::Vector3 ecef) const;
		// Height of an ECEF position above the terrain surface (AGL, metres).
		double get_height_above_terrain(godot::Vector3 ecef) const;
	};

}

#endif // W3D_GLOBE_H
