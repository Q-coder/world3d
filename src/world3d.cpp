#include "world3d.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace world3d
{

	W3DWorld::W3DWorld()
	{
	}

	W3DWorld::~W3DWorld()
	{
	}

	void W3DWorld::_bind_methods()
	{
		ClassDB::bind_method(D_METHOD("set_radius_scale", "scale"), &W3DWorld::set_radius_scale);
		ClassDB::bind_method(D_METHOD("get_radius_scale"), &W3DWorld::get_radius_scale);
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius_scale"), "set_radius_scale", "get_radius_scale");
	}

	void W3DWorld::_ready()
	{
		if (Engine::get_singleton()->is_editor_hint())
		{
			return;
		}
		UtilityFunctions::print("[world3d] W3DWorld ready (radius_scale=", radius_scale, ")");
	}

	void W3DWorld::set_radius_scale(double p_scale)
	{
		radius_scale = p_scale;
	}

	double W3DWorld::get_radius_scale() const
	{
		return radius_scale;
	}

}
