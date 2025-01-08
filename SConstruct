#!/usr/bin/env python
import os
from glob import glob
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
  # Add type hints for SCons globals
  from SCons.Script.SConscript import SConsEnvironment
  from SCons.Defaults import DefaultEnvironment
  Default = SConsEnvironment(DefaultEnvironment()).Default
  Glob = DefaultEnvironment().Glob
  Alias = DefaultEnvironment().Alias
  CacheDir = DefaultEnvironment().CacheDir
  SConscript = SConsEnvironment(DefaultEnvironment()).SConscript

# TODO: Do not copy environment after godot-cpp/test is updated <https://github.com/godotengine/godot-cpp/blob/master/test/SConstruct>.
env = SConscript("godot-cpp/SConstruct")

# Add the compilation database tool for generating compile_commands.json.
env.Tool('compilation_db')
cdb = env.CompilationDatabase()
Alias('cdb', cdb)

# Add source files
env.Append(CPPPATH=["src/"])
sources = Glob("src/common/*.cpp")

# Platform-specific configurations
if env["platform"] == "macos":
    # Add AVFoundation source files (note the .mm extension)
    sources.extend(Glob("src/avf/*.mm") + Glob("src/avf/*.cpp"))

    # Add necessary frameworks
    env.Append(FRAMEWORKS=["AVFoundation", "CoreMedia", "CoreVideo"])

    # Enable Objective-C++ compilation
    env.Append(CXXFLAGS=["-ObjC++"])

# Find gdextension path even if the directory or extension is renamed (e.g. project/addons/example/example.gdextension).
(extension_path,) = glob("project/addons/*/*.gdextension")

# Get the addon path (e.g. project/addons/example).
addon_path = Path(extension_path).parent

# Get the project name from the gdextension file (e.g. example).
project_name = Path(extension_path).stem

scons_cache_path = os.environ.get("SCONS_CACHE")
if scons_cache_path != None:
    CacheDir(scons_cache_path)
    print("Scons cache enabled... (path: '" + scons_cache_path + "')")

# Create the library target (e.g. libexample.linux.debug.x86_64.so).
debug_or_release = "release" if env["target"] == "template_release" else "debug"
if env["platform"] == "macos":
    library = env.SharedLibrary(
        "{0}/bin/lib{1}.{2}.{3}.framework/{1}.{2}.{3}".format(
            addon_path,
            project_name,
            env["platform"],
            debug_or_release,
        ),
        source=sources,
    )
else:
    library = env.SharedLibrary(
        "{}/bin/lib{}.{}.{}.{}{}".format(
            addon_path,
            project_name,
            env["platform"],
            debug_or_release,
            env["arch"],
            env["SHLIBSUFFIX"],
        ),
        source=sources,
    )

Default(library)
