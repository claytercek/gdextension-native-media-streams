#pragma once

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

namespace godot {

// AVFoundation platform specific initialization
void initialize_native_media_streams_avf(ModuleInitializationLevel p_level);
void uninitialize_native_media_streams_avf(ModuleInitializationLevel p_level);

} // namespace godot