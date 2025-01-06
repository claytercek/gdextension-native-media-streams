#include "register_types_avf.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/ref.hpp>

using namespace godot;

#include <godot_cpp/classes/resource_loader.hpp>
#include "video_stream_avf.hpp"

static Ref<ResourceFormatLoaderAVF> resource_loader_avf;

void initialize_native_video_extension_avf(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }

  GDREGISTER_CLASS(ResourceFormatLoaderAVF);
  resource_loader_avf.instantiate();
  ResourceLoader::get_singleton()->add_resource_format_loader(
      resource_loader_avf, true);
  GDREGISTER_CLASS(VideoStreamAVF);
  GDREGISTER_CLASS(VideoStreamPlaybackAVF);
}

void uninitialize_native_video_extension_avf(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }

  ResourceLoader::get_singleton()->remove_resource_format_loader(
      resource_loader_avf);
  resource_loader_avf.unref();
}

extern "C" {
// Initialization
GDExtensionBool GDE_EXPORT
native_video_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                  GDExtensionClassLibraryPtr p_library,
                  GDExtensionInitialization *r_initialization) {
  GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library,
                                          r_initialization);

  init_obj.register_initializer(initialize_native_video_extension_avf);
  init_obj.register_terminator(uninitialize_native_video_extension_avf);
  init_obj.set_minimum_library_initialization_level(
      MODULE_INITIALIZATION_LEVEL_SCENE);

  return init_obj.init();
}
}
