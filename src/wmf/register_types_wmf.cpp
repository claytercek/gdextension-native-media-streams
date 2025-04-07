#include "register_types_wmf.h"
#include "video_stream_wmf.hpp"
#include "../common/frame_queue_video_stream.hpp"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource_loader.hpp>

using namespace godot;

static Ref<ResourceFormatLoaderWMF> resource_loader_wmf;

void initialize_native_video_extension_wmf(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }

  // Initialize WMF
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(hr)) {
    ERR_PRINT("Failed to initialize COM library");
    return;
  }

  hr = MFStartup(MF_VERSION);
  if (FAILED(hr)) {
    ERR_PRINT("Failed to initialize Windows Media Foundation");
    CoUninitialize();
    return;
  }

  GDREGISTER_CLASS(ResourceFormatLoaderWMF);
  resource_loader_wmf.instantiate();
  ResourceLoader::get_singleton()->add_resource_format_loader(
      resource_loader_wmf, true);
  GDREGISTER_ABSTRACT_CLASS(FrameQueueVideoStream);
  GDREGISTER_CLASS(VideoStreamWMF);
  GDREGISTER_CLASS(VideoStreamPlaybackWMF);
}

void uninitialize_native_video_extension_wmf(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }

  ResourceLoader::get_singleton()->remove_resource_format_loader(
      resource_loader_wmf);
  resource_loader_wmf.unref();

  // Shutdown WMF
  MFShutdown();
  CoUninitialize();
}

// The entry point function is now in src/register_types.cpp
// and will call initialize_native_video_extension_wmf and
// uninitialize_native_video_extension_wmf as needed
