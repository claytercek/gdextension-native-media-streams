#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include "wmf/register_types_wmf.h"
using InitFunc = decltype(&initialize_native_video_extension_wmf);
using UninitFunc = decltype(&uninitialize_native_video_extension_wmf);
#elif defined(__APPLE__)
#include "avf/register_types_avf.h"
using InitFunc = decltype(&initialize_native_video_extension_avf);
using UninitFunc = decltype(&uninitialize_native_video_extension_avf);
#else
// Default to stub implementation
void initialize_native_video_extension_stub(godot::ModuleInitializationLevel p_level) {}
void uninitialize_native_video_extension_stub(godot::ModuleInitializationLevel p_level) {}
using InitFunc = decltype(&initialize_native_video_extension_stub);
using UninitFunc = decltype(&uninitialize_native_video_extension_stub);
#endif

static InitFunc initialize_func = nullptr;
static UninitFunc uninitialize_func = nullptr;

static void setup_platform_specific_funcs() {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    initialize_func = initialize_native_video_extension_wmf;
    uninitialize_func = uninitialize_native_video_extension_wmf;
#elif defined(__APPLE__)
    initialize_func = initialize_native_video_extension_avf;
    uninitialize_func = uninitialize_native_video_extension_avf;
#else
    initialize_func = initialize_native_video_extension_stub;
    uninitialize_func = uninitialize_native_video_extension_stub;
#endif
}

extern "C" {
GDExtensionBool GDE_EXPORT native_video_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                                           GDExtensionClassLibraryPtr p_library,
                                           GDExtensionInitialization *r_initialization) {
    
    godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
    
    setup_platform_specific_funcs();
    
    init_obj.register_initializer(initialize_func);
    init_obj.register_terminator(uninitialize_func);
    init_obj.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
    
    return init_obj.init();
}
}
