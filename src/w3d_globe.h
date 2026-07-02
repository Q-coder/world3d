#ifndef W3D_GLOBE_H
#define W3D_GLOBE_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>

namespace world3d
{

	// Static cube-sphere Earth (milestone 2). On _ready it loads the six baked
	// ETOPO face heightmaps (tools/bake_faces.py) and builds one ArrayMesh per
	// face directly in ECEF metres, coloured green (land) / blue (sea) by the sign
	// of the sampled elevation. No streaming or LOD yet.
	//
	// The cube mapping (FACE_BASE/FACE_U/FACE_V) MUST match the Python baker.
	class W3DGlobe : public godot::Node3D
	{
		GDCLASS(W3DGlobe, godot::Node3D);

	private:
		godot::String faces_path = "res://faces";
		double vertical_exaggeration = 1.0;
		bool built = false;

		bool build_globe();
		void build_face(int face_index, const godot::PackedFloat32Array &elev, int face_size);

	protected:
		static void _bind_methods();

	public:
		W3DGlobe();
		~W3DGlobe();

		void _ready() override;

		void set_faces_path(const godot::String &p_path);
		godot::String get_faces_path() const;

		void set_vertical_exaggeration(double p_ex);
		double get_vertical_exaggeration() const;
	};

}

#endif // W3D_GLOBE_H
