#pragma once

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

namespace godot {

// Windows Media Foundation platform specific initialization
void initialize_native_media_streams_wmf(ModuleInitializationLevel p_level);
void uninitialize_native_media_streams_wmf(ModuleInitializationLevel p_level);

} // namespace godot