#include "register_types_wmf.h"
#include "wmf_video_stream_playback.hpp"
#include "wmf_hardware_helper.hpp"
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

// Resource loader for WMF video streams
class ResourceFormatLoaderWMF : public ResourceFormatLoader {
    GDCLASS(ResourceFormatLoaderWMF, ResourceFormatLoader)

protected:
    static void _bind_methods() {}

public:
    Variant _load(const String &p_path, const String &p_original_path,
                      bool p_use_sub_threads, int32_t p_cache_mode) const override {
        Ref<VideoStreamWMF> stream;
        stream.instantiate();
        stream->set_file(p_path);
        return stream;
    }

    PackedStringArray _get_recognized_extensions() const override {
        PackedStringArray extensions;
        extensions.push_back("mp4");
        extensions.push_back("m4v");
        extensions.push_back("mov");
        extensions.push_back("wmv");
        extensions.push_back("mkv");
        extensions.push_back("avi");
        extensions.push_back("webm");
        return extensions;
    }

    bool _handles_type(const StringName &p_type) const override {
        return ClassDB::is_parent_class(p_type, "VideoStream");
    }

    String _get_resource_type(const String &p_path) const override {
        String extension = p_path.get_extension().to_lower();
        if (extension == "mp4" || extension == "m4v" || extension == "mov" || 
            extension == "wmv" || extension == "mkv" || extension == "avi" ||
            extension == "webm") {
            return "VideoStream";
        }
        return "";
    }
};

static Ref<ResourceFormatLoaderWMF> wmf_resource_loader;

// Module initialization
void initialize_native_media_streams_wmf(ModuleInitializationLevel p_level) {
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        // Register WMF-specific classes
        ClassDB::register_class<ResourceFormatLoaderWMF>();
        ClassDB::register_class<VideoStreamWMF>();
        ClassDB::register_class<VideoStreamPlaybackWMF>();

        // Register resource loader
        wmf_resource_loader.instantiate();
        ResourceLoader::get_singleton()->add_resource_format_loader(wmf_resource_loader);

        // Log initialization
        UtilityFunctions::print("Windows Media Foundation video backend initialized");

        // Initialize hardware acceleration settings in project settings
        // We let the VideoStreamWMF class handle the project settings registration
    }
}

// Module cleanup
void uninitialize_native_media_streams_wmf(ModuleInitializationLevel p_level) {
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        // Cleanup resource loader
        ResourceLoader::get_singleton()->remove_resource_format_loader(
            wmf_resource_loader);
        wmf_resource_loader.unref();
    }
}

} // namespace godot