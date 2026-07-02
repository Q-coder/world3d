#include "world3d.h"
#include "w3d_globe.h"
#include "w3d_tangent_anchor.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
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

		ClassDB::bind_method(D_METHOD("set_hide_globe_when_local", "hide"), &W3DWorld::set_hide_globe_when_local);
		ClassDB::bind_method(D_METHOD("get_hide_globe_when_local"), &W3DWorld::get_hide_globe_when_local);
		ADD_PROPERTY(PropertyInfo(Variant::BOOL, "hide_globe_when_local"), "set_hide_globe_when_local", "get_hide_globe_when_local");
	}

	void W3DWorld::gather(Node *node)
	{
		if (W3DGlobe *g = Object::cast_to<W3DGlobe>(node))
		{
			if (!globe)
			{
				globe = g;
			}
		}
		if (W3DTangentAnchor *a = Object::cast_to<W3DTangentAnchor>(node))
		{
			anchors.push_back(a);
		}
		const int n = node->get_child_count();
		for (int i = 0; i < n; ++i)
		{
			gather(node->get_child(i));
		}
	}

	void W3DWorld::_ready()
	{
		if (Engine::get_singleton()->is_editor_hint())
		{
			return;
		}
		if (OS::get_singleton()->get_environment("W3D_KEEPGLOBE") != String())
		{
			hide_globe_when_local = false;
		}
		gather(this);
		set_process(true);
		UtilityFunctions::print("[world3d] W3DWorld ready (globe=", globe != nullptr,
								", anchors=", (int)anchors.size(),
								", hide_globe_when_local=", hide_globe_when_local, ")");
	}

	void W3DWorld::_process(double)
	{
		if (anchors.empty() || !globe || !hide_globe_when_local)
		{
			return;
		}
		bool any_active = false;
		for (W3DTangentAnchor *a : anchors)
		{
			if (a->is_active())
			{
				any_active = true;
				break;
			}
		}
		if (any_active != globe_hidden)
		{
			globe_hidden = any_active;
			globe->set_visible(!any_active);
		}
	}

	void W3DWorld::set_radius_scale(double p_scale)
	{
		radius_scale = p_scale;
	}

	double W3DWorld::get_radius_scale() const
	{
		return radius_scale;
	}

	void W3DWorld::set_hide_globe_when_local(bool p_hide)
	{
		hide_globe_when_local = p_hide;
	}

	bool W3DWorld::get_hide_globe_when_local() const
	{
		return hide_globe_when_local;
	}

}
