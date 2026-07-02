#include "register_types.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "world3d.h"
#include "w3d_coords.h"
#include "w3d_globe.h"

using namespace godot;

void initialize_world3d_module(godot::ModuleInitializationLevel p_level)
{
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
	{
		return;
	}
	ClassDB::register_class<world3d::W3DWorld>();
	ClassDB::register_class<world3d::W3DCoords>();
	ClassDB::register_class<world3d::W3DGlobe>();
}

void uninitialize_world3d_module(godot::ModuleInitializationLevel p_level)
{
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
	{
		return;
	}
}

extern "C"
{
	GDExtensionBool GDE_EXPORT world3d_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization)
	{
		godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

		init_obj.register_initializer(initialize_world3d_module);
		init_obj.register_terminator(uninitialize_world3d_module);
		init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

		return init_obj.init();
	}
}
