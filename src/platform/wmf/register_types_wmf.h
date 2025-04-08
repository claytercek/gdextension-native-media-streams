#pragma once

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

namespace godot {

// Windows Media Foundation platform specific initialization
void initialize_native_media_streams_wmf(ModuleInitializationLevel p_level);
void uninitialize_native_media_streams_wmf(ModuleInitializationLevel p_level);

// Legacy function names for compatibility with old code
inline void initialize_native_video_extension_wmf(ModuleInitializationLevel p_level) {
    initialize_native_media_streams_wmf(p_level);
}

inline void uninitialize_native_video_extension_wmf(ModuleInitializationLevel p_level) {
    uninitialize_native_media_streams_wmf(p_level);
}

} // namespace godot