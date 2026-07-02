#ifndef WORLD3D_H
#define WORLD3D_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace world3d
{

	// Root node for the global cube-sphere world. Renders the whole Earth in ECEF
	// (Earth-Centred Earth-Fixed) double-precision coordinates and, later, hands off
	// to the local scenery3d Swiss patch by camera altitude.
	//
	// Milestone 1: an empty coordinating node that builds, registers, and loads in
	// the double-precision Godot 4.7 build. Cube-sphere geometry and LOD land in
	// later milestones.
	//
	// Named W3DWorld (not World3D) because Godot already ships a built-in World3D
	// class; reusing that name collides in ClassDB.
	class W3DWorld : public godot::Node3D
	{
		GDCLASS(W3DWorld, godot::Node3D);

	private:
		// Radius scale applied to the ellipsoid for debug/visualization. 1.0 = true
		// scale (metres). Kept as a hook for early testing before ECEF flight lands.
		double radius_scale = 1.0;

	protected:
		static void _bind_methods();

	public:
		W3DWorld();
		~W3DWorld();

		void _ready() override;

		void set_radius_scale(double p_scale);
		double get_radius_scale() const;
	};

}

#endif // WORLD3D_H
