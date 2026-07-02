#include "w3d_globe.h"
#include "w3d_coords.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
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
	}

	void W3DGlobe::_ready()
	{
		if (Engine::get_singleton()->is_editor_hint())
		{
			return;
		}
		if (!built)
		{
			built = build_globe();
		}
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

	bool W3DGlobe::build_globe()
	{
		const String meta_path = faces_path + "/meta.json";
		Ref<FileAccess> mf = FileAccess::open(meta_path, FileAccess::READ);
		if (mf.is_null())
		{
			UtilityFunctions::push_warning("[world3d] W3DGlobe: cannot open ", meta_path, " (run tools/bake_faces.py)");
			return false;
		}
		const String meta_text = mf->get_as_text();
		mf->close();

		Variant parsed = JSON::parse_string(meta_text);
		if (parsed.get_type() != Variant::DICTIONARY)
		{
			UtilityFunctions::push_warning("[world3d] W3DGlobe: bad meta.json");
			return false;
		}
		Dictionary meta = parsed;
		const int face_size = (int)meta.get("face_size", 0);
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
			build_face(f, elev, face_size);
		}

		UtilityFunctions::print("[world3d] W3DGlobe built 6 faces (face_size=", face_size,
								", exaggeration=", vertical_exaggeration, ")");
		return true;
	}

	void W3DGlobe::build_face(int face_index, const PackedFloat32Array &elev, int F)
	{
		const int vcount = F * F;
		std::vector<Vector3> pos(vcount);

		const Vector3 base = FACE_BASE[face_index];
		const Vector3 uax = FACE_U[face_index];
		const Vector3 vax = FACE_V[face_index];

		// Positions in ECEF (metres).
		for (int j = 0; j < F; ++j)
		{
			const double v = -1.0 + 2.0 * (double)j / (double)(F - 1);
			for (int i = 0; i < F; ++i)
			{
				const double u = -1.0 + 2.0 * (double)i / (double)(F - 1);
				Vector3 p = base + uax * u + vax * v;
				Vector3 d = p.normalized();
				const double lat = Math::rad_to_deg(std::asin(CLAMP(d.z, -1.0, 1.0)));
				const double lon = Math::rad_to_deg(std::atan2(d.y, d.x));
				const double h = (double)elev[j * F + i] * vertical_exaggeration;
				pos[j * F + i] = W3DCoords::geodetic_to_ecef(lat, lon, h);
			}
		}

		PackedVector3Array vertices;
		PackedVector3Array normals;
		PackedColorArray colors;
		vertices.resize(vcount);
		normals.resize(vcount);
		colors.resize(vcount);

		for (int j = 0; j < F; ++j)
		{
			for (int i = 0; i < F; ++i)
			{
				const int idx = j * F + i;
				const int il = (i > 0) ? i - 1 : i;
				const int ir = (i < F - 1) ? i + 1 : i;
				const int jd = (j > 0) ? j - 1 : j;
				const int ju = (j < F - 1) ? j + 1 : j;
				const Vector3 du = pos[j * F + ir] - pos[j * F + il];
				const Vector3 dv = pos[ju * F + i] - pos[jd * F + i];
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
						n = -n; // orient outward
					}
				}
				vertices[idx] = pos[idx];
				normals[idx] = n;
				colors[idx] = color_for_elevation(elev[idx]);
			}
		}

		PackedInt32Array indices;
		indices.resize((F - 1) * (F - 1) * 6);
		int k = 0;
		for (int j = 0; j < F - 1; ++j)
		{
			for (int i = 0; i < F - 1; ++i)
			{
				const int a = j * F + i;
				const int b = j * F + (i + 1);
				const int c = (j + 1) * F + i;
				const int d = (j + 1) * F + (i + 1);
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
		mi->set_name("Face" + String::num_int64(face_index));
		mi->set_mesh(mesh);
		mi->set_material_override(mat);
		add_child(mi);
	}

	void W3DGlobe::set_faces_path(const String &p_path) { faces_path = p_path; }
	String W3DGlobe::get_faces_path() const { return faces_path; }
	void W3DGlobe::set_vertical_exaggeration(double p_ex) { vertical_exaggeration = p_ex; }
	double W3DGlobe::get_vertical_exaggeration() const { return vertical_exaggeration; }

}
