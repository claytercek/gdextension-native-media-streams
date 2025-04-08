#include "register_types.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

// Include platform-specific headers
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include "platform/wmf/register_types_wmf.h"
#elif defined(__APPLE__)
#include "platform/avf/register_types_avf.h"
#else
// Default to stub implementation
namespace godot {
void initialize_native_media_streams_stub(ModuleInitializationLevel p_level) {}
void uninitialize_native_media_streams_stub(ModuleInitializationLevel p_level) {}
}
#endif

// Main module initialization
void initialize_native_media_streams_module(ModuleInitializationLevel p_level) {
    // Call platform-specific initialization
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    godot::initialize_native_media_streams_wmf(p_level);
#elif defined(__APPLE__)
    godot::initialize_native_media_streams_avf(p_level);
#else
    godot::initialize_native_media_streams_stub(p_level);
#endif
}

// Main module cleanup
void uninitialize_native_media_streams_module(ModuleInitializationLevel p_level) {
    // Call platform-specific cleanup
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    godot::uninitialize_native_media_streams_wmf(p_level);
#elif defined(__APPLE__)
    godot::uninitialize_native_media_streams_avf(p_level);
#else
    godot::uninitialize_native_media_streams_stub(p_level);
#endif
}

// Entry point for the GDExtension
extern "C" {
GDExtensionBool GDE_EXPORT native_media_streams_init(
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization *r_initialization) {
    
    godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
    
    init_obj.register_initializer(initialize_native_media_streams_module);
    init_obj.register_terminator(uninitialize_native_media_streams_module);
    init_obj.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
    
    return init_obj.init();
}
}
