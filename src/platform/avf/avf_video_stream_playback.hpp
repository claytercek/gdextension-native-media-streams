#pragma once
#include "../../common/playback/video_stream_playback_base.hpp"
#include "../../common/playback/video_stream_base.hpp"
#include "avf_player.hpp"

namespace godot {

/**
 * AVFoundation implementation of VideoStreamPlayback.
 * Uses the AVFPlayer for platform-specific functionality.
 */
class VideoStreamPlaybackAVF : public VideoStreamPlaybackBase {
    GDCLASS(VideoStreamPlaybackAVF, VideoStreamPlaybackBase)

public:
    VideoStreamPlaybackAVF();
    virtual ~VideoStreamPlaybackAVF();
    
    // Initialize with file path
    void initialize(const String& p_file);

protected:
    static void _bind_methods();
};

/**
 * AVFoundation implementation of VideoStream.
 */
class VideoStreamAVF : public VideoStreamBase {
    GDCLASS(VideoStreamAVF, VideoStreamBase)

protected:
    static void _bind_methods();

public:
    virtual Ref<VideoStreamPlayback> _instantiate_playback() override;
};

} // namespace godot