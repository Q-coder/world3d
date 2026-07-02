#!/usr/bin/env python
import os
from SCons.Script import ARGUMENTS

# The bundled godot-cpp ships a single-precision extension_api.json. ProVPilot runs
# a double-precision Godot 4.7 build, so default to our double-precision API file
# (generated from that editor) whenever precision=double and no override is given.
if ARGUMENTS.get("precision", "single") == "double" and "custom_api_file" not in ARGUMENTS:
    ARGUMENTS["custom_api_file"] = os.path.abspath("gdextension/extension_api.double.json")

env = SConscript('godot-cpp/SConstruct')

# Paths
godot_headers_path = "godot-cpp/godot_headers/"
cpp_bindings_path = "godot-cpp/"

env.Append(CPPPATH="src/")
env.Append(CPPPATH="godot-cpp/include/")
env.Append(CPPPATH=['.', godot_headers_path, cpp_bindings_path + 'include/', cpp_bindings_path + 'include/core/', cpp_bindings_path + 'include/gen/'])
env.Append(LIBPATH=[cpp_bindings_path + 'bin/'])

src = Glob("src/*.cpp")

libpath = 'demo/bin/libworld3d{}{}'.format(env['suffix'], env['SHLIBSUFFIX'])
sharedlib = env.SharedLibrary(libpath, src)
Default(sharedlib)
