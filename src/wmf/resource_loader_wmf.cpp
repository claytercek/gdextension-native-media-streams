#include "resource_loader_wmf.hpp"
#include "video_stream_wmf.hpp"
#include <godot_cpp/core/class_db.hpp>

namespace godot {

Variant ResourceFormatLoaderWMF::_load(const String &p_path,
                                       const String &p_original_path,
                                       bool p_use_sub_threads,
                                       int32_t p_cache_mode) const {
    VideoStreamWMF *stream = memnew(VideoStreamWMF);
    stream->set_file(p_path);
    
    Ref<VideoStreamWMF> wmf_stream = Ref<VideoStreamWMF>(stream);
    
    return {wmf_stream};
}

PackedStringArray ResourceFormatLoaderWMF::_get_recognized_extensions() const {
    PackedStringArray arr;
    
    arr.push_back("mp4");
    arr.push_back("wmv");
    arr.push_back("avi");
    arr.push_back("mov");
    arr.push_back("mkv");
    
    return arr;
}

bool ResourceFormatLoaderWMF::_handles_type(const StringName &p_type) const {
    return ClassDB::is_parent_class(p_type, "VideoStream");
}

String ResourceFormatLoaderWMF::_get_resource_type(const String &p_path) const {
    String ext = p_path.get_extension().to_lower();
    if (ext == "mp4" || ext == "wmv" || ext == "avi" || ext == "mov" || ext == "mkv") {
        return "VideoStreamWMF";
    }
    
    return "";
}

} // namespace godot