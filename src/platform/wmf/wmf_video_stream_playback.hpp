#pragma once
#include "../../common/playback/video_stream_playback_base.hpp"
#include "wmf_player.hpp"

namespace godot {

/**
 * Windows Media Foundation implementation of VideoStreamPlayback.
 * Uses the WMFPlayer for platform-specific functionality.
 */
class VideoStreamPlaybackWMF : public VideoStreamPlaybackBase {
    GDCLASS(VideoStreamPlaybackWMF, VideoStreamPlaybackBase)

public:
    VideoStreamPlaybackWMF();
    virtual ~VideoStreamPlaybackWMF();
    
    // Initialize with file path
    void initialize(const String& p_file);

protected:
    static void _bind_methods();
};

/**
 * Windows Media Foundation implementation of VideoStream.
 */
class VideoStreamWMF : public VideoStreamBase {
    GDCLASS(VideoStreamWMF, VideoStreamBase)

protected:
    static void _bind_methods();

public:
    virtual Ref<VideoStreamPlayback> _instantiate_playback() override;
};

} // namespace godot