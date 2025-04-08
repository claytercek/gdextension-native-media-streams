#include "register_types_avf.h"
#include "avf_video_stream_playback.hpp"
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_format_loader.hpp>

namespace godot {

// Resource loader for AVF video streams
class ResourceFormatLoaderAVF : public ResourceFormatLoader {
    GDCLASS(ResourceFormatLoaderAVF, ResourceFormatLoader)

protected:
    static void _bind_methods() {}

public:
    Variant _load(const String &p_path, const String &p_original_path,
                      bool p_use_sub_threads, int32_t p_cache_mode) const override {
        Ref<VideoStreamAVF> stream;
        stream.instantiate();
        stream->set_file(p_path);
        return stream;
    }

    PackedStringArray _get_recognized_extensions() const override {
        PackedStringArray extensions;
        extensions.push_back("mp4");
        extensions.push_back("m4v");
        extensions.push_back("mov");
        return extensions;
    }

    bool _handles_type(const StringName &p_type) const override {
        return ClassDB::is_parent_class(p_type, "VideoStream");
    }

    String _get_resource_type(const String &p_path) const override {
        String extension = p_path.get_extension().to_lower();
        if (extension == "mp4" || extension == "m4v" || extension == "mov") {
            return "VideoStream";
        }
        return "";
    }
};

static Ref<ResourceFormatLoaderWMF> avf_resource_loader;

// Module initialization
void initialize_native_media_streams_avf(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    // Register classes
    ClassDB::register_class<ResourceFormatLoaderAVF>();
    ClassDB::register_class<VideoStreamAVF>();
    ClassDB::register_class<VideoStreamPlaybackAVF>();

    // Register resource loader
    avf_resource_loader.instantiate();
    ResourceLoader::get_singleton()->add_resource_format_loader(avf_resource_loader);
}

// Module cleanup
void uninitialize_native_media_streams_avf(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    // Cleanup resource loader
    ResourceLoader::get_singleton()->remove_resource_format_loader(
        avf_resource_loader);
    avf_resource_loader.unref();
}

} // namespace godot