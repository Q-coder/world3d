#ifndef W3D_TANGENT_ANCHOR_H
#define W3D_TANGENT_ANCHOR_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace world3d
{

	// Mounts a flat local tile system (e.g. the scenery3d Swiss LV95 patch) onto the
	// curved global shell. On _ready it sets its own Transform3D to the ECEF tangent
	// frame at the configured geodetic origin, so any child placed in the local Y-up
	// convention lands correctly on the ellipsoid. Parent a Scenery3D node under this.
	//
	// It also drives the near/far handoff: when the camera is within active_radius of
	// the origin the anchor (and its mounted content) is shown; otherwise it hides,
	// letting the global shell take over. A W3DWorld coordinator reads is_active() to
	// hide the coarse globe while the detailed patch is driving the scene.
	class W3DTangentAnchor : public godot::Node3D
	{
		GDCLASS(W3DTangentAnchor, godot::Node3D);

	private:
		// Default origin: WGS84 of the Swiss LV95 fundamental point (E2600000/N1200000).
		double origin_latitude = 46.95240555555556;
		double origin_longitude = 7.439583333333333;
		double origin_height = 0.0;
		bool x_axis_west = true;       // scenery3d convention (+X=West, +Z=North)
		double active_radius = 350000.0; // metres from origin at which the patch activates
		bool force_active = false;       // debug/test override

		godot::Vector3 origin_ecef;
		bool active = false;

		void apply_transform();

	protected:
		static void _bind_methods();

	public:
		W3DTangentAnchor();
		~W3DTangentAnchor();

		void _ready() override;
		void _process(double delta) override;

		godot::Vector3 get_origin_ecef() const;
		bool is_active() const;

		void set_origin_latitude(double v);
		double get_origin_latitude() const;
		void set_origin_longitude(double v);
		double get_origin_longitude() const;
		void set_origin_height(double v);
		double get_origin_height() const;
		void set_x_axis_west(bool v);
		bool get_x_axis_west() const;
		void set_active_radius(double v);
		double get_active_radius() const;
	};

}

#endif // W3D_TANGENT_ANCHOR_H
