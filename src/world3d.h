#ifndef WORLD3D_H
#define WORLD3D_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <vector>

namespace world3d
{

	class W3DGlobe;
	class W3DTangentAnchor;

	// Root node for the global cube-sphere world. Renders the whole Earth in ECEF
	// (Earth-Centred Earth-Fixed) double-precision coordinates and hands off to a
	// local tangent patch (e.g. the scenery3d Swiss LV95 tiles) by camera proximity.
	//
	// It scans its descendants for a W3DGlobe and any W3DTangentAnchor nodes, and
	// each frame hides the coarse globe while a patch is active — the near/far
	// handoff described in the design.
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
		bool hide_globe_when_local = true;

		W3DGlobe *globe = nullptr;
		std::vector<W3DTangentAnchor *> anchors;
		bool globe_hidden = false;

		void gather(godot::Node *node);

	protected:
		static void _bind_methods();

	public:
		W3DWorld();
		~W3DWorld();

		void _ready() override;
		void _process(double delta) override;

		void set_radius_scale(double p_scale);
		double get_radius_scale() const;

		void set_hide_globe_when_local(bool p_hide);
		bool get_hide_globe_when_local() const;
	};

}

#endif // WORLD3D_H
