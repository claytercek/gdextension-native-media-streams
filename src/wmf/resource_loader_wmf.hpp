#pragma once
#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/video_stream.hpp>

namespace godot {

class ResourceFormatLoaderWMF : public ResourceFormatLoader {
    GDCLASS(ResourceFormatLoaderWMF, ResourceFormatLoader)

protected:
    static void _bind_methods() {}

public:
    Variant _load(const String& p_path, const String& p_original_path,
                  bool p_use_sub_threads, int32_t p_cache_mode) const override;
    PackedStringArray _get_recognized_extensions() const override;
    bool _handles_type(const StringName& p_type) const override;
    String _get_resource_type(const String& p_path) const override;
};

} // namespace godot