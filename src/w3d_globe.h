#ifndef W3D_GLOBE_H
#define W3D_GLOBE_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <vector>

namespace godot
{
	class MeshInstance3D;
}

namespace world3d
{

	// Cube-sphere quadtree Earth (milestone 3). Each cube face is the root of a
	// quadtree; a node splits into four children when the camera gets close
	// relative to the node's edge length, and collapses when it moves away.
	//
	// Each chunk mesh is built with its vertices RELATIVE to the chunk's ECEF
	// centre, and the owning MeshInstance3D carries that centre in its (double)
	// transform. This keeps per-vertex magnitudes small so the float32 GPU vertex
	// buffer stays precise right down at the surface — the prerequisite for
	// close-up ECEF flight.
	//
	// Elevation is sampled from the six baked ETOPO face grids held in memory.
	// The cube mapping must match tools/bake_faces.py.
	class W3DGlobe : public godot::Node3D
	{
		GDCLASS(W3DGlobe, godot::Node3D);

	private:
		struct QuadNode
		{
			int face = 0;
			int level = 0;
			double u0 = -1.0, v0 = -1.0, u1 = 1.0, v1 = 1.0;
			godot::Vector3 center_ecef; // chunk centre (double, metres)
			double edge_len = 0.0;      // approx ECEF edge length (metres)
			double bound_radius = 0.0;  // bounding-sphere radius about centre
			godot::MeshInstance3D *mesh = nullptr;
			QuadNode *children[4] = {nullptr, nullptr, nullptr, nullptr};
			bool has_children = false;
		};

		godot::String faces_path = "res://faces";
		double vertical_exaggeration = 1.0;
		int chunk_resolution = 33; // vertices per chunk edge
		int max_level = 6;
		double lod_split_factor = 2.5; // split when dist < edge_len * factor
		int build_budget = 12;         // max chunk builds per frame

		int face_size = 0;
		std::vector<float> face_grid[6];
		QuadNode *roots[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
		bool ready = false;
		int builds_this_frame = 0;

		bool load_faces();
		float sample_face(int face, double u, double v) const;
		godot::Vector3 ecef_at(int face, double u, double v) const;

		QuadNode *make_node(int face, int level, double u0, double v0, double u1, double v1);
		void build_mesh(QuadNode *node);
		void free_subtree(QuadNode *node);
		void collapse(QuadNode *node);
		void split(QuadNode *node);
		void set_subtree_visible(QuadNode *node, bool visible);
		void update_node(QuadNode *node, const godot::Vector3 &cam_ecef);

	protected:
		static void _bind_methods();

	public:
		W3DGlobe();
		~W3DGlobe();

		void _ready() override;
		void _process(double delta) override;

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
	};

}

#endif // W3D_GLOBE_H
