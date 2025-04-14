#pragma once
#include "../../common/playback/video_stream_playback_base.hpp"
#include "../../common/playback/video_stream_base.hpp"
#include "wmf_player.hpp"

namespace godot {

/**
 * Windows Media Foundation implementation of VideoStreamPlayback.
 * Uses the WMFPlayer for platform-specific functionality.
 */
class VideoStreamPlaybackWMF : public VideoStreamPlaybackBase {
    GDCLASS(VideoStreamPlaybackWMF, VideoStreamPlaybackBase)

private:
    // Hardware acceleration flag
    bool hardware_acceleration_enabled = true;

public:
    VideoStreamPlaybackWMF();
    virtual ~VideoStreamPlaybackWMF();
    
    // Initialize with file path
    void initialize(const String& p_file);
    
    // Hardware acceleration control
    void set_hardware_acceleration(bool p_enabled);
    bool is_hardware_acceleration_enabled() const;
    bool is_hardware_acceleration_active() const;

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
    virtual Ref<VideoStreamPlaybackBase> _create_playback_instance() override;
};

} // namespace godot