#include "w3d_tangent_anchor.h"
#include "w3d_coords.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace world3d
{

	W3DTangentAnchor::W3DTangentAnchor() {}
	W3DTangentAnchor::~W3DTangentAnchor() {}

	void W3DTangentAnchor::_bind_methods()
	{
		ClassDB::bind_method(D_METHOD("get_origin_ecef"), &W3DTangentAnchor::get_origin_ecef);
		ClassDB::bind_method(D_METHOD("is_active"), &W3DTangentAnchor::is_active);

		ClassDB::bind_method(D_METHOD("set_origin_latitude", "v"), &W3DTangentAnchor::set_origin_latitude);
		ClassDB::bind_method(D_METHOD("get_origin_latitude"), &W3DTangentAnchor::get_origin_latitude);
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "origin_latitude"), "set_origin_latitude", "get_origin_latitude");

		ClassDB::bind_method(D_METHOD("set_origin_longitude", "v"), &W3DTangentAnchor::set_origin_longitude);
		ClassDB::bind_method(D_METHOD("get_origin_longitude"), &W3DTangentAnchor::get_origin_longitude);
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "origin_longitude"), "set_origin_longitude", "get_origin_longitude");

		ClassDB::bind_method(D_METHOD("set_origin_height", "v"), &W3DTangentAnchor::set_origin_height);
		ClassDB::bind_method(D_METHOD("get_origin_height"), &W3DTangentAnchor::get_origin_height);
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "origin_height"), "set_origin_height", "get_origin_height");

		ClassDB::bind_method(D_METHOD("set_x_axis_west", "v"), &W3DTangentAnchor::set_x_axis_west);
		ClassDB::bind_method(D_METHOD("get_x_axis_west"), &W3DTangentAnchor::get_x_axis_west);
		ADD_PROPERTY(PropertyInfo(Variant::BOOL, "x_axis_west"), "set_x_axis_west", "get_x_axis_west");

		ClassDB::bind_method(D_METHOD("set_active_radius", "v"), &W3DTangentAnchor::set_active_radius);
		ClassDB::bind_method(D_METHOD("get_active_radius"), &W3DTangentAnchor::get_active_radius);
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "active_radius"), "set_active_radius", "get_active_radius");
	}

	void W3DTangentAnchor::apply_transform()
	{
		const Transform3D t = W3DCoords::enu_to_ecef_transform(origin_latitude, origin_longitude, origin_height, x_axis_west);
		set_transform(t);
		origin_ecef = t.origin;
	}

	void W3DTangentAnchor::_ready()
	{
		apply_transform();
		if (Engine::get_singleton()->is_editor_hint())
		{
			return;
		}
		if (OS::get_singleton()->get_environment("W3D_ANCHOR_ALWAYS") != String())
		{
			force_active = true;
		}
		active = force_active;
		set_visible(active);
		set_process(true);
		UtilityFunctions::print("[world3d] W3DTangentAnchor at lat=", origin_latitude,
								" lon=", origin_longitude, " -> ECEF ", origin_ecef,
								" (active_radius=", active_radius, ")");
	}

	void W3DTangentAnchor::_process(double)
	{
		if (force_active)
		{
			if (!active)
			{
				active = true;
				set_visible(true);
			}
			return;
		}
		Viewport *vp = get_viewport();
		Camera3D *cam = vp ? vp->get_camera_3d() : nullptr;
		if (!cam)
		{
			return;
		}
		const double dist = get_global_position().distance_to(cam->get_global_position());
		const bool want = dist < active_radius;
		if (want != active)
		{
			active = want;
			set_visible(active);
		}
	}

	Vector3 W3DTangentAnchor::get_origin_ecef() const { return origin_ecef; }
	bool W3DTangentAnchor::is_active() const { return active; }

	void W3DTangentAnchor::set_origin_latitude(double v)
	{
		origin_latitude = v;
		if (is_inside_tree())
		{
			apply_transform();
		}
	}
	double W3DTangentAnchor::get_origin_latitude() const { return origin_latitude; }

	void W3DTangentAnchor::set_origin_longitude(double v)
	{
		origin_longitude = v;
		if (is_inside_tree())
		{
			apply_transform();
		}
	}
	double W3DTangentAnchor::get_origin_longitude() const { return origin_longitude; }

	void W3DTangentAnchor::set_origin_height(double v)
	{
		origin_height = v;
		if (is_inside_tree())
		{
			apply_transform();
		}
	}
	double W3DTangentAnchor::get_origin_height() const { return origin_height; }

	void W3DTangentAnchor::set_x_axis_west(bool v)
	{
		x_axis_west = v;
		if (is_inside_tree())
		{
			apply_transform();
		}
	}
	bool W3DTangentAnchor::get_x_axis_west() const { return x_axis_west; }

	void W3DTangentAnchor::set_active_radius(double v) { active_radius = v; }
	double W3DTangentAnchor::get_active_radius() const { return active_radius; }

}
