#include "w3d_globe.h"
#include "w3d_coords.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <cmath>
#include <vector>

using namespace godot;

namespace world3d
{

	// Cube mapping — must match tools/bake_faces.py.
	static const Vector3 FACE_BASE[6] = {
		Vector3(1, 0, 0), Vector3(-1, 0, 0),
		Vector3(0, 1, 0), Vector3(0, -1, 0),
		Vector3(0, 0, 1), Vector3(0, 0, -1)};
	static const Vector3 FACE_U[6] = {
		Vector3(0, 1, 0), Vector3(0, -1, 0),
		Vector3(1, 0, 0), Vector3(1, 0, 0),
		Vector3(1, 0, 0), Vector3(-1, 0, 0)};
	static const Vector3 FACE_V[6] = {
		Vector3(0, 0, 1), Vector3(0, 0, 1),
		Vector3(0, 0, -1), Vector3(0, 0, 1),
		Vector3(0, 1, 0), Vector3(0, 1, 0)};

	W3DGlobe::W3DGlobe() {}
	W3DGlobe::~W3DGlobe() {}

	void W3DGlobe::_bind_methods()
	{
		ClassDB::bind_method(D_METHOD("set_faces_path", "path"), &W3DGlobe::set_faces_path);
		ClassDB::bind_method(D_METHOD("get_faces_path"), &W3DGlobe::get_faces_path);
		ADD_PROPERTY(PropertyInfo(Variant::STRING, "faces_path"), "set_faces_path", "get_faces_path");

		ClassDB::bind_method(D_METHOD("set_vertical_exaggeration", "ex"), &W3DGlobe::set_vertical_exaggeration);
		ClassDB::bind_method(D_METHOD("get_vertical_exaggeration"), &W3DGlobe::get_vertical_exaggeration);
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "vertical_exaggeration"), "set_vertical_exaggeration", "get_vertical_exaggeration");

		ClassDB::bind_method(D_METHOD("set_chunk_resolution", "res"), &W3DGlobe::set_chunk_resolution);
		ClassDB::bind_method(D_METHOD("get_chunk_resolution"), &W3DGlobe::get_chunk_resolution);
		ADD_PROPERTY(PropertyInfo(Variant::INT, "chunk_resolution"), "set_chunk_resolution", "get_chunk_resolution");

		ClassDB::bind_method(D_METHOD("set_max_level", "level"), &W3DGlobe::set_max_level);
		ClassDB::bind_method(D_METHOD("get_max_level"), &W3DGlobe::get_max_level);
		ADD_PROPERTY(PropertyInfo(Variant::INT, "max_level"), "set_max_level", "get_max_level");

		ClassDB::bind_method(D_METHOD("set_lod_split_factor", "factor"), &W3DGlobe::set_lod_split_factor);
		ClassDB::bind_method(D_METHOD("get_lod_split_factor"), &W3DGlobe::get_lod_split_factor);
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_split_factor"), "set_lod_split_factor", "get_lod_split_factor");
	}

	void W3DGlobe::_ready()
	{
		if (Engine::get_singleton()->is_editor_hint())
		{
			return;
		}
		ready = load_faces();
		if (!ready)
		{
			return;
		}
		for (int f = 0; f < 6; ++f)
		{
			roots[f] = make_node(f, 0, -1.0, -1.0, 1.0, 1.0);
			build_mesh(roots[f]);
		}
		set_process(true);
		UtilityFunctions::print("[world3d] W3DGlobe quadtree ready (face_size=", face_size,
								", chunk_res=", chunk_resolution, ", max_level=", max_level, ")");
	}

	static Color color_for_elevation(float e)
	{
		if (e < 0.0f)
		{
			// Sea: deep -> shallow blue.
			float t = CLAMP(1.0f + e / 6000.0f, 0.0f, 1.0f); // 0 at -6000m, 1 at 0
			return Color(0.02f + 0.08f * t, 0.05f + 0.25f * t, 0.20f + 0.40f * t);
		}
		// Land: green -> brown -> snow.
		if (e < 1500.0f)
		{
			float t = e / 1500.0f;
			return Color(0.20f + 0.25f * t, 0.45f - 0.10f * t, 0.18f + 0.05f * t);
		}
		float t = CLAMP((e - 1500.0f) / 3500.0f, 0.0f, 1.0f);
		return Color(0.45f + 0.50f * t, 0.35f + 0.60f * t, 0.23f + 0.72f * t);
	}

	bool W3DGlobe::load_faces()
	{
		const String meta_path = faces_path + "/meta.json";
		Ref<FileAccess> mf = FileAccess::open(meta_path, FileAccess::READ);
		if (mf.is_null())
		{
			UtilityFunctions::push_warning("[world3d] W3DGlobe: cannot open ", meta_path, " (run tools/bake_faces.py)");
			return false;
		}
		Variant parsed = JSON::parse_string(mf->get_as_text());
		mf->close();
		if (parsed.get_type() != Variant::DICTIONARY)
		{
			UtilityFunctions::push_warning("[world3d] W3DGlobe: bad meta.json");
			return false;
		}
		Dictionary meta = parsed;
		face_size = (int)meta.get("face_size", 0);
		if (face_size < 2)
		{
			UtilityFunctions::push_warning("[world3d] W3DGlobe: invalid face_size");
			return false;
		}

		for (int f = 0; f < 6; ++f)
		{
			const String fp = faces_path + "/face" + String::num_int64(f) + ".f32";
			Ref<FileAccess> ff = FileAccess::open(fp, FileAccess::READ);
			if (ff.is_null())
			{
				UtilityFunctions::push_warning("[world3d] W3DGlobe: cannot open ", fp);
				return false;
			}
			PackedByteArray bytes = ff->get_buffer(ff->get_length());
			ff->close();
			PackedFloat32Array elev = bytes.to_float32_array();
			if (elev.size() != face_size * face_size)
			{
				UtilityFunctions::push_warning("[world3d] W3DGlobe: face", f, " size mismatch");
				return false;
			}
			face_grid[f].resize((size_t)face_size * face_size);
			for (int i = 0; i < elev.size(); ++i)
			{
				face_grid[f][i] = elev[i];
			}
		}
		return true;
	}

	float W3DGlobe::sample_face(int face, double u, double v) const
	{
		// (u,v) in [-1,1] -> grid coords. i indexes u, j indexes v (matches baker).
		const double gx = (u * 0.5 + 0.5) * (double)(face_size - 1);
		const double gy = (v * 0.5 + 0.5) * (double)(face_size - 1);
		const double cx = CLAMP(gx, 0.0, (double)(face_size - 1));
		const double cy = CLAMP(gy, 0.0, (double)(face_size - 1));
		const int i0 = (int)std::floor(cx);
		const int j0 = (int)std::floor(cy);
		const int i1 = (i0 < face_size - 1) ? i0 + 1 : i0;
		const int j1 = (j0 < face_size - 1) ? j0 + 1 : j0;
		const double fx = cx - i0;
		const double fy = cy - j0;
		const std::vector<float> &g = face_grid[face];
		const double a = g[j0 * face_size + i0];
		const double b = g[j0 * face_size + i1];
		const double c = g[j1 * face_size + i0];
		const double d = g[j1 * face_size + i1];
		const double top = a * (1 - fx) + b * fx;
		const double bot = c * (1 - fx) + d * fx;
		return (float)(top * (1 - fy) + bot * fy);
	}

	Vector3 W3DGlobe::ecef_at(int face, double u, double v) const
	{
		const Vector3 p = FACE_BASE[face] + FACE_U[face] * u + FACE_V[face] * v;
		const Vector3 dir = p.normalized();
		const double lat = Math::rad_to_deg(std::asin(CLAMP(dir.z, -1.0, 1.0)));
		const double lon = Math::rad_to_deg(std::atan2(dir.y, dir.x));
		const double h = (double)sample_face(face, u, v) * vertical_exaggeration;
		return W3DCoords::geodetic_to_ecef(lat, lon, h);
	}

	W3DGlobe::QuadNode *W3DGlobe::make_node(int face, int level, double u0, double v0, double u1, double v1)
	{
		QuadNode *n = new QuadNode();
		n->face = face;
		n->level = level;
		n->u0 = u0;
		n->v0 = v0;
		n->u1 = u1;
		n->v1 = v1;
		n->center_ecef = ecef_at(face, 0.5 * (u0 + u1), 0.5 * (v0 + v1));
		const Vector3 c00 = ecef_at(face, u0, v0);
		const Vector3 c10 = ecef_at(face, u1, v0);
		const Vector3 c11 = ecef_at(face, u1, v1);
		n->edge_len = c00.distance_to(c10);
		n->bound_radius = MAX(n->center_ecef.distance_to(c00), n->center_ecef.distance_to(c11));
		return n;
	}

	void W3DGlobe::build_mesh(QuadNode *node)
	{
		const int R = chunk_resolution;
		const int vcount = R * R;
		const Vector3 origin = node->center_ecef;

		std::vector<Vector3> pos(vcount);
		std::vector<float> elev(vcount);
		for (int j = 0; j < R; ++j)
		{
			const double tv = (double)j / (double)(R - 1);
			const double v = node->v0 + (node->v1 - node->v0) * tv;
			for (int i = 0; i < R; ++i)
			{
				const double tu = (double)i / (double)(R - 1);
				const double u = node->u0 + (node->u1 - node->u0) * tu;
				const Vector3 p = FACE_BASE[node->face] + FACE_U[node->face] * u + FACE_V[node->face] * v;
				const Vector3 dir = p.normalized();
				const double lat = Math::rad_to_deg(std::asin(CLAMP(dir.z, -1.0, 1.0)));
				const double lon = Math::rad_to_deg(std::atan2(dir.y, dir.x));
				const float e = sample_face(node->face, u, v);
				elev[j * R + i] = e;
				pos[j * R + i] = W3DCoords::geodetic_to_ecef(lat, lon, (double)e * vertical_exaggeration);
			}
		}

		PackedVector3Array vertices;
		PackedVector3Array normals;
		PackedColorArray colors;
		vertices.resize(vcount);
		normals.resize(vcount);
		colors.resize(vcount);

		for (int j = 0; j < R; ++j)
		{
			for (int i = 0; i < R; ++i)
			{
				const int idx = j * R + i;
				const int il = (i > 0) ? i - 1 : i;
				const int ir = (i < R - 1) ? i + 1 : i;
				const int jd = (j > 0) ? j - 1 : j;
				const int ju = (j < R - 1) ? j + 1 : j;
				const Vector3 du = pos[j * R + ir] - pos[j * R + il];
				const Vector3 dv = pos[ju * R + i] - pos[jd * R + i];
				Vector3 n = dv.cross(du);
				if (n.length_squared() <= 0.0)
				{
					n = pos[idx].normalized();
				}
				else
				{
					n = n.normalized();
					if (n.dot(pos[idx]) < 0.0)
					{
						n = -n;
					}
				}
				vertices[idx] = pos[idx] - origin; // local to chunk centre
				normals[idx] = n;
				colors[idx] = color_for_elevation(elev[idx]);
			}
		}

		PackedInt32Array indices;
		indices.resize((R - 1) * (R - 1) * 6);
		int k = 0;
		for (int j = 0; j < R - 1; ++j)
		{
			for (int i = 0; i < R - 1; ++i)
			{
				const int a = j * R + i;
				const int b = j * R + (i + 1);
				const int c = (j + 1) * R + i;
				const int d = (j + 1) * R + (i + 1);
				indices[k++] = a;
				indices[k++] = c;
				indices[k++] = b;
				indices[k++] = b;
				indices[k++] = c;
				indices[k++] = d;
			}
		}

		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = vertices;
		arrays[Mesh::ARRAY_NORMAL] = normals;
		arrays[Mesh::ARRAY_COLOR] = colors;
		arrays[Mesh::ARRAY_INDEX] = indices;

		Ref<ArrayMesh> mesh;
		mesh.instantiate();
		mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

		Ref<StandardMaterial3D> mat;
		mat.instantiate();
		mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		mat->set_cull_mode(BaseMaterial3D::CULL_BACK);
		mat->set_roughness(1.0);
		mat->set_metallic(0.0);

		MeshInstance3D *mi = memnew(MeshInstance3D);
		mi->set_name("F" + String::num_int64(node->face) + "L" + String::num_int64(node->level));
		mi->set_mesh(mesh);
		mi->set_material_override(mat);
		add_child(mi);
		mi->set_position(origin); // double-precision chunk origin
		node->mesh = mi;
		builds_this_frame++;
	}

	void W3DGlobe::free_subtree(QuadNode *node)
	{
		if (!node)
		{
			return;
		}
		for (int i = 0; i < 4; ++i)
		{
			free_subtree(node->children[i]);
			node->children[i] = nullptr;
		}
		if (node->mesh)
		{
			node->mesh->queue_free();
			node->mesh = nullptr;
		}
		delete node;
	}

	void W3DGlobe::collapse(QuadNode *node)
	{
		if (!node->has_children)
		{
			return;
		}
		for (int i = 0; i < 4; ++i)
		{
			free_subtree(node->children[i]);
			node->children[i] = nullptr;
		}
		node->has_children = false;
	}

	void W3DGlobe::split(QuadNode *node)
	{
		const double um = 0.5 * (node->u0 + node->u1);
		const double vm = 0.5 * (node->v0 + node->v1);
		node->children[0] = make_node(node->face, node->level + 1, node->u0, node->v0, um, vm);
		node->children[1] = make_node(node->face, node->level + 1, um, node->v0, node->u1, vm);
		node->children[2] = make_node(node->face, node->level + 1, node->u0, vm, um, node->v1);
		node->children[3] = make_node(node->face, node->level + 1, um, vm, node->u1, node->v1);
		for (int i = 0; i < 4; ++i)
		{
			build_mesh(node->children[i]);
		}
		node->has_children = true;
	}

	void W3DGlobe::set_subtree_visible(QuadNode *node, bool visible)
	{
		if (!node)
		{
			return;
		}
		if (node->mesh)
		{
			node->mesh->set_visible(visible);
		}
	}

	void W3DGlobe::update_node(QuadNode *node, const Vector3 &cam_ecef)
	{
		double dist = cam_ecef.distance_to(node->center_ecef) - node->bound_radius;
		if (dist < 1.0)
		{
			dist = 1.0;
		}
		const bool want_split = (node->level < max_level) && (dist < node->edge_len * lod_split_factor);

		if (want_split)
		{
			if (!node->has_children)
			{
				if (builds_this_frame >= build_budget)
				{
					// Budget exhausted: keep this node visible this frame.
					if (node->mesh)
					{
						node->mesh->set_visible(true);
					}
					return;
				}
				split(node);
			}
			if (node->mesh)
			{
				node->mesh->set_visible(false);
			}
			for (int i = 0; i < 4; ++i)
			{
				update_node(node->children[i], cam_ecef);
			}
		}
		else
		{
			if (node->has_children)
			{
				collapse(node);
			}
			if (node->mesh)
			{
				node->mesh->set_visible(true);
			}
		}
	}

	void W3DGlobe::_process(double)
	{
		if (!ready)
		{
			return;
		}
		Viewport *vp = get_viewport();
		Camera3D *cam = vp ? vp->get_camera_3d() : nullptr;
		if (!cam)
		{
			return;
		}
		const Vector3 cam_ecef = to_local(cam->get_global_position());
		builds_this_frame = 0;
		for (int f = 0; f < 6; ++f)
		{
			if (roots[f])
			{
				update_node(roots[f], cam_ecef);
			}
		}
	}

	void W3DGlobe::set_faces_path(const String &p_path) { faces_path = p_path; }
	String W3DGlobe::get_faces_path() const { return faces_path; }
	void W3DGlobe::set_vertical_exaggeration(double p_ex) { vertical_exaggeration = p_ex; }
	double W3DGlobe::get_vertical_exaggeration() const { return vertical_exaggeration; }
	void W3DGlobe::set_chunk_resolution(int p_res) { chunk_resolution = MAX(2, p_res); }
	int W3DGlobe::get_chunk_resolution() const { return chunk_resolution; }
	void W3DGlobe::set_max_level(int p_level) { max_level = CLAMP(p_level, 0, 12); }
	int W3DGlobe::get_max_level() const { return max_level; }
	void W3DGlobe::set_lod_split_factor(double p_factor) { lod_split_factor = MAX(0.1, p_factor); }
	double W3DGlobe::get_lod_split_factor() const { return lod_split_factor; }

}
