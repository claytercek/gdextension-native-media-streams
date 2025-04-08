#pragma once
#include <godot_cpp/classes/video_stream.hpp>
#include "video_stream_playback_base.hpp"

namespace godot {

/**
 * Base VideoStream class for all platform implementations.
 * Handles basic functionality common to all video streams.
 */
class VideoStreamBase : public VideoStream {
    GDCLASS(VideoStreamBase, VideoStream)

protected:
    String file_path;

public:
    VideoStreamBase() = default;
    virtual ~VideoStreamBase() = default;
    
    void set_file(const String& p_file) {
        file_path = p_file;
    }
    
    String get_file() const {
        return file_path;
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_file", "file"), &VideoStreamBase::set_file);
        ClassDB::bind_method(D_METHOD("get_file"), &VideoStreamBase::get_file);
        
        ADD_PROPERTY(PropertyInfo(Variant::STRING, "file"), "set_file", "get_file");
    }
};

} // namespace godot