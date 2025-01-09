#pragma once
#include <godot_cpp/classes/video_stream_playback.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include "frame_queue.hpp"
#include "video_frame.hpp"

namespace godot {

class VideoStreamPlaybackNative : public VideoStreamPlayback {
    GDCLASS(VideoStreamPlaybackNative, VideoStreamPlayback)

protected:
    struct Dimensions {
        Vector2i frame;
        size_t aligned_width{0};
        size_t aligned_height{0};
    };

    struct PlaybackState {
        bool playing{false};
        bool paused{false};
        bool buffering{false};
        double engine_time{0.0};
        float fps{30.0f};
    };

    Dimensions dimensions;
    PlaybackState state;
    String file_name;
    Ref<ImageTexture> texture;
    FrameQueue frame_queue;

    // Platform-specific interface
    virtual void process_frame_queue() = 0;
    virtual bool check_end_of_stream() = 0;
    virtual void update_frame_queue(double delta) = 0;

    // Common utility functions
    void update_texture_from_frame(const VideoFrame& frame);
    void setup_dimensions(size_t width, size_t height);
    static size_t align_dimension(size_t dim, size_t alignment = 16);
    static double predict_next_frame_time(double current_time, float fps);

    static void _bind_methods() {}

public:
    VideoStreamPlaybackNative() = default;
    virtual ~VideoStreamPlaybackNative() = default;

    // Common implementation of update that delegates to platform-specific code
    virtual void _update(double delta) override;
};

} // namespace godot
