#include "register_types.h"

using namespace godot;

#include <godot_cpp/classes/resource_loader.hpp>

#ifdef __APPLE__
#include "avf/video_stream_avf.hpp"
#endif

#ifdef __APPLE__
static Ref<ResourceFormatLoaderAVF> resource_loader_avf;
#endif

void initialize_native_video_extension(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }

#ifdef __APPLE__
  GDREGISTER_CLASS(ResourceFormatLoaderAVF);
  resource_loader_avf.instantiate();
  ResourceLoader::get_singleton()->add_resource_format_loader(
      resource_loader_avf, true);
  GDREGISTER_CLASS(VideoStreamAVF);
  GDREGISTER_CLASS(VideoStreamPlaybackAVF);
#endif
}

void uninitialize_native_video_extension(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }

#ifdef __APPLE__
  ResourceLoader::get_singleton()->remove_resource_format_loader(
      resource_loader_avf);
  resource_loader_avf.unref();
#endif
}

extern "C" {
// Initialization
GDExtensionBool GDE_EXPORT
native_video_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                  GDExtensionClassLibraryPtr p_library,
                  GDExtensionInitialization *r_initialization) {
  GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library,
                                          r_initialization);

  init_obj.register_initializer(initialize_native_video_extension);
  init_obj.register_terminator(uninitialize_native_video_extension);
  init_obj.set_minimum_library_initialization_level(
      MODULE_INITIALIZATION_LEVEL_SCENE);

  return init_obj.init();
}
}
