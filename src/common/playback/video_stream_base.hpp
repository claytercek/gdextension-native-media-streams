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
    bool use_threaded_decoding = true;

public:
    VideoStreamBase() = default;

    virtual ~VideoStreamBase() = default;
    
    // Set whether to use threaded decoding (can improve performance)
    void set_threaded_decoding(bool enabled) {
        use_threaded_decoding = enabled;
    }

    // Get whether threaded decoding is enabled
    bool get_threaded_decoding() const {
        return use_threaded_decoding;
    }
    
    // Override to allow controlling threading in playback instance
    Ref<VideoStreamPlayback> _instantiate_playback() override {
        Ref<VideoStreamPlaybackBase> playback = _create_playback_instance();
        if (playback.is_valid()) {
            playback->set_threaded_decoding(use_threaded_decoding);
        }
        return playback;
    }
    
    // Platform implementations must override this to create their playback instance
    virtual Ref<VideoStreamPlaybackBase> _create_playback_instance() {
        return nullptr;
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_threaded_decoding", "enabled"), &VideoStreamBase::set_threaded_decoding);
        ClassDB::bind_method(D_METHOD("get_threaded_decoding"), &VideoStreamBase::get_threaded_decoding);
        
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "threaded_decoding"), 
            "set_threaded_decoding", "get_threaded_decoding");
    }
};

} // namespace godot