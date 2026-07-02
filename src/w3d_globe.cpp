#include "w3d_globe.h"
#include "w3d_coords.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
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
	W3DGlobe::~W3DGlobe()
	{
		stop_workers();
		for (int f = 0; f < 6; ++f)
		{
			free_subtree(roots[f]);
			roots[f] = nullptr;
		}
	}

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

		ClassDB::bind_method(D_METHOD("set_clamp_sea_level", "clamp"), &W3DGlobe::set_clamp_sea_level);
		ClassDB::bind_method(D_METHOD("get_clamp_sea_level"), &W3DGlobe::get_clamp_sea_level);
		ADD_PROPERTY(PropertyInfo(Variant::BOOL, "clamp_sea_level"), "set_clamp_sea_level", "get_clamp_sea_level");

		ClassDB::bind_method(D_METHOD("get_elevation", "latitude", "longitude"), &W3DGlobe::get_elevation);
		ClassDB::bind_method(D_METHOD("get_ground_ecef", "latitude", "longitude"), &W3DGlobe::get_ground_ecef);
		ClassDB::bind_method(D_METHOD("get_terrain_ecef_below", "ecef"), &W3DGlobe::get_terrain_ecef_below);
		ClassDB::bind_method(D_METHOD("get_height_above_terrain", "ecef"), &W3DGlobe::get_height_above_terrain);
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
		build_shared_indices();

		chunk_material.instantiate();
		chunk_material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		chunk_material->set_cull_mode(BaseMaterial3D::CULL_BACK);
		chunk_material->set_roughness(1.0);
		chunk_material->set_metallic(0.0);

		start_workers();
		for (int f = 0; f < 6; ++f)
		{
			roots[f] = make_node(f, 0, -1.0, -1.0, 1.0, 1.0);
			request_mesh(roots[f]);
		}
		set_process(true);
		UtilityFunctions::print("[world3d] W3DGlobe quadtree ready (face_size=", face_size,
								", chunk_res=", chunk_resolution, ", max_level=", max_level,
								", workers=", (int)workers.size(), ")");

		if (OS::get_singleton()->get_environment("W3D_ELEVTEST") != String())
		{
			struct
			{
				const char *name;
				double lat, lon;
			} pts[] = {
				{"Everest", 27.9881, 86.9250},
				{"Bern", 46.9524, 7.4396},
				{"Death Valley", 36.25, -116.82},
				{"Mid-Pacific (ocean)", 0.0, -140.0},
				{"Mariana Trench (ocean)", 11.35, 142.2},
			};
			for (auto &p : pts)
			{
				const double e = get_elevation(p.lat, p.lon);
				const Vector3 g = get_ground_ecef(p.lat, p.lon);
				const double agl = get_height_above_terrain(W3DCoords::geodetic_to_ecef(p.lat, p.lon, e + 1000.0));
				UtilityFunctions::print("[world3d] ELEV ", p.name, ": ", e, " m ASL, ground_ecef=", g, ", AGL@+1000=", agl);
			}
		}
	}

	void W3DGlobe::_exit_tree()
	{
		stop_workers();
		for (int f = 0; f < 6; ++f)
		{
			free_subtree(roots[f]);
			roots[f] = nullptr;
		}
		chunk_material.unref();
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

	void W3DGlobe::build_shared_indices()
	{
		const int R = chunk_resolution;
		shared_indices.resize((R - 1) * (R - 1) * 6);
		int k = 0;
		for (int j = 0; j < R - 1; ++j)
		{
			for (int i = 0; i < R - 1; ++i)
			{
				const int a = j * R + i;
				const int b = j * R + (i + 1);
				const int c = (j + 1) * R + i;
				const int d = (j + 1) * R + (i + 1);
				shared_indices[k++] = a;
				shared_indices[k++] = c;
				shared_indices[k++] = b;
				shared_indices[k++] = b;
				shared_indices[k++] = c;
				shared_indices[k++] = d;
			}
		}
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

	float W3DGlobe::terrain_height(float raw) const
	{
		return clamp_sea_level ? MAX(raw, 0.0f) : raw;
	}

	Vector3 W3DGlobe::ecef_at(int face, double u, double v) const
	{
		const Vector3 p = FACE_BASE[face] + FACE_U[face] * u + FACE_V[face] * v;
		const Vector3 dir = p.normalized();
		const double lat = Math::rad_to_deg(std::asin(CLAMP(dir.z, -1.0, 1.0)));
		const double lon = Math::rad_to_deg(std::atan2(dir.y, dir.x));
		const double h = (double)terrain_height(sample_face(face, u, v)) * vertical_exaggeration;
		return W3DCoords::geodetic_to_ecef(lat, lon, h);
	}

	// Pure compute — safe to run on a worker thread (reads immutable face grids).
	void W3DGlobe::compute_chunk(const ChunkRequest &req, ChunkResult &out) const
	{
		const int R = chunk_resolution;
		const int vcount = R * R;
		const Vector3 origin = req.origin;

		std::vector<Vector3> pos(vcount);
		std::vector<float> elev(vcount);
		for (int j = 0; j < R; ++j)
		{
			const double tv = (double)j / (double)(R - 1);
			const double v = req.v0 + (req.v1 - req.v0) * tv;
			for (int i = 0; i < R; ++i)
			{
				const double tu = (double)i / (double)(R - 1);
				const double u = req.u0 + (req.u1 - req.u0) * tu;
				const Vector3 p = FACE_BASE[req.face] + FACE_U[req.face] * u + FACE_V[req.face] * v;
				const Vector3 dir = p.normalized();
				const double lat = Math::rad_to_deg(std::asin(CLAMP(dir.z, -1.0, 1.0)));
				const double lon = Math::rad_to_deg(std::atan2(dir.y, dir.x));
				const float e = sample_face(req.face, u, v);
				elev[j * R + i] = e;
				pos[j * R + i] = W3DCoords::geodetic_to_ecef(lat, lon, (double)terrain_height(e) * vertical_exaggeration);
			}
		}

		out.id = req.id;
		out.vertices.resize(vcount);
		out.normals.resize(vcount);
		out.colors.resize(vcount);
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
				out.vertices[idx] = pos[idx] - origin; // local to chunk centre
				out.normals[idx] = n;
				out.colors[idx] = color_for_elevation(elev[idx]);
			}
		}
	}

	void W3DGlobe::worker_loop()
	{
		for (;;)
		{
			ChunkRequest req;
			{
				std::unique_lock<std::mutex> lock(req_mutex);
				req_cv.wait(lock, [this]
							{ return stop_threads.load() || !requests.empty(); });
				if (stop_threads.load() && requests.empty())
				{
					return;
				}
				req = requests.front();
				requests.pop_front();
			}
			ChunkResult res;
			compute_chunk(req, res);
			{
				std::lock_guard<std::mutex> lock(res_mutex);
				results.push_back(std::move(res));
			}
		}
	}

	void W3DGlobe::start_workers()
	{
		if (!workers.empty())
		{
			return;
		}
		unsigned hc = std::thread::hardware_concurrency();
		int n = (int)(hc > 2 ? hc - 1 : 2);
		if (n > 4)
		{
			n = 4;
		}
		stop_threads.store(false);
		for (int i = 0; i < n; ++i)
		{
			workers.emplace_back(&W3DGlobe::worker_loop, this);
		}
	}

	void W3DGlobe::stop_workers()
	{
		if (workers.empty())
		{
			return;
		}
		stop_threads.store(true);
		req_cv.notify_all();
		for (std::thread &t : workers)
		{
			if (t.joinable())
			{
				t.join();
			}
		}
		workers.clear();
		{
			std::lock_guard<std::mutex> lock(req_mutex);
			requests.clear();
		}
		{
			std::lock_guard<std::mutex> lock(res_mutex);
			results.clear();
		}
	}

	W3DGlobe::QuadNode *W3DGlobe::make_node(int face, int level, double u0, double v0, double u1, double v1)
	{
		QuadNode *n = new QuadNode();
		n->id = next_id++;
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
		live_nodes[n->id] = n;
		return n;
	}

	void W3DGlobe::request_mesh(QuadNode *node)
	{
		if (node->mesh || node->mesh_requested)
		{
			return;
		}
		node->mesh_requested = true;
		ChunkRequest req;
		req.id = node->id;
		req.face = node->face;
		req.u0 = node->u0;
		req.v0 = node->v0;
		req.u1 = node->u1;
		req.v1 = node->v1;
		req.origin = node->center_ecef;
		{
			std::lock_guard<std::mutex> lock(req_mutex);
			requests.push_back(req);
		}
		req_cv.notify_one();
	}

	void W3DGlobe::commit_result(const ChunkResult &res)
	{
		auto it = live_nodes.find(res.id);
		if (it == live_nodes.end())
		{
			return; // node was freed while its mesh was in flight
		}
		QuadNode *node = it->second;
		if (node->mesh)
		{
			return;
		}

		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = res.vertices;
		arrays[Mesh::ARRAY_NORMAL] = res.normals;
		arrays[Mesh::ARRAY_COLOR] = res.colors;
		arrays[Mesh::ARRAY_INDEX] = shared_indices;

		Ref<ArrayMesh> mesh;
		mesh.instantiate();
		mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

		MeshInstance3D *mi = memnew(MeshInstance3D);
		mi->set_name("F" + String::num_int64(node->face) + "L" + String::num_int64(node->level));
		mi->set_mesh(mesh);
		mi->set_material_override(chunk_material);
		mi->set_visible(false); // visibility decided by the LOD traversal
		add_child(mi);
		mi->set_position(node->center_ecef); // double-precision chunk origin
		node->mesh = mi;
		node->mesh_requested = false;
	}

	void W3DGlobe::drain_results()
	{
		int committed = 0;
		while (committed < commit_budget)
		{
			ChunkResult res;
			{
				std::lock_guard<std::mutex> lock(res_mutex);
				if (results.empty())
				{
					break;
				}
				res = std::move(results.front());
				results.pop_front();
			}
			commit_result(res);
			committed++;
		}
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
		live_nodes.erase(node->id);
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
			request_mesh(node->children[i]);
		}
		node->has_children = true;
	}

	bool W3DGlobe::beyond_horizon(const QuadNode *node, const Vector3 &cam_ecef) const
	{
		const double R = 6371000.0; // mean Earth radius
		const double cam_r = cam_ecef.length();
		if (cam_r <= R * 1.001)
		{
			return false; // near/at surface: nothing culled by horizon
		}
		const Vector3 cam_hat = cam_ecef / cam_r;
		const Vector3 c_hat = node->center_ecef.normalized();
		const double cap = std::acos(CLAMP(R / cam_r, -1.0, 1.0)); // visible cap half-angle
		const double ang = std::acos(CLAMP(cam_hat.dot(c_hat), -1.0, 1.0));
		const double node_ang = node->bound_radius / R + 0.08; // generous margin
		return (ang - node_ang) > cap;
	}

	bool W3DGlobe::children_ready(QuadNode *node) const
	{
		// A child counts as ready if it has a committed mesh. (Horizon-culled
		// children are handled during recursion, so we still require their mesh
		// only if they might be shown; here we simply require all four meshes.)
		for (int i = 0; i < 4; ++i)
		{
			if (!node->children[i]->mesh)
			{
				return false;
			}
		}
		return true;
	}

	void W3DGlobe::update_node(QuadNode *node, const Vector3 &cam_ecef)
	{
		if (beyond_horizon(node, cam_ecef))
		{
			if (node->has_children)
			{
				collapse(node);
			}
			if (node->mesh)
			{
				node->mesh->set_visible(false);
			}
			return;
		}

		request_mesh(node); // ensure this node has (or is building) its own mesh

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
				split(node);
			}
			if (children_ready(node))
			{
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
				// Seamless: keep the parent visible until every child is ready.
				if (node->mesh)
				{
					node->mesh->set_visible(true);
				}
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
		drain_results(); // commit finished meshes (budgeted GPU upload)

		Viewport *vp = get_viewport();
		Camera3D *cam = vp ? vp->get_camera_3d() : nullptr;
		if (!cam)
		{
			return;
		}
		const Vector3 cam_ecef = to_local(cam->get_global_position());
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
	void W3DGlobe::set_clamp_sea_level(bool p_clamp) { clamp_sea_level = p_clamp; }
	bool W3DGlobe::get_clamp_sea_level() const { return clamp_sea_level; }

	double W3DGlobe::get_elevation(double latitude, double longitude) const
	{
		if (face_size < 2)
		{
			return 0.0;
		}
		const double la = Math::deg_to_rad(latitude);
		const double lo = Math::deg_to_rad(longitude);
		const double cla = std::cos(la);
		// Geocentric unit direction (matches the baker's forward mapping).
		const Vector3 dir(cla * std::cos(lo), cla * std::sin(lo), std::sin(la));

		// Inverse cube map: the face is the one whose base axis dir points most
		// toward; then u,v are the projections onto that face's axes.
		int face = 0;
		double best = -2.0;
		for (int f = 0; f < 6; ++f)
		{
			const double d = dir.dot(FACE_BASE[f]);
			if (d > best)
			{
				best = d;
				face = f;
			}
		}
		if (best <= 0.0)
		{
			return 0.0;
		}
		const double u = dir.dot(FACE_U[face]) / best;
		const double v = dir.dot(FACE_V[face]) / best;
		return (double)terrain_height(sample_face(face, u, v)) * vertical_exaggeration;
	}

	Vector3 W3DGlobe::get_ground_ecef(double latitude, double longitude) const
	{
		return W3DCoords::geodetic_to_ecef(latitude, longitude, get_elevation(latitude, longitude));
	}

	Vector3 W3DGlobe::get_terrain_ecef_below(Vector3 ecef) const
	{
		const Vector3 g = W3DCoords::ecef_to_geodetic(ecef); // (lat, lon, height)
		return get_ground_ecef(g.x, g.y);
	}

	double W3DGlobe::get_height_above_terrain(Vector3 ecef) const
	{
		const Vector3 g = W3DCoords::ecef_to_geodetic(ecef); // (lat, lon, height)
		return g.z - get_elevation(g.x, g.y);
	}

}
