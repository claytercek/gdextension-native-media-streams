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

public:
    VideoStreamBase() = default;
    virtual ~VideoStreamBase() = default;

protected:
    static void _bind_methods() {
    }
};

} // namespace godot