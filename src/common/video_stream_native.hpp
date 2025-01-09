#pragma once
#include <godot_cpp/classes/video_stream_playback.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include "frame_queue.hpp"
#include "video_frame.hpp"

namespace godot {

/**
 * Base class for platform-specific video playback implementations
 */
class VideoStreamPlaybackNative : public VideoStreamPlayback {
    GDCLASS(VideoStreamPlaybackNative, VideoStreamPlayback)

protected:
    struct Dimensions {
        Vector2i frame;
        size_t aligned_width{0};
        size_t aligned_height{0};
    } dimensions;

    struct PlaybackState {
        bool playing{false};
        bool paused{false};
        bool buffering{false};
        double engine_time{0.0};
        float fps{30.0f};
    } state;

    // Common resources
    String file_name;
    Ref<ImageTexture> texture;
    FrameQueue frame_queue;

    // Utility functions
    void update_texture_from_frame(const VideoFrame& frame) {
        PackedByteArray pba;
        pba.resize(frame.data.size());
        memcpy(pba.ptrw(), frame.data.data(), frame.data.size());
        
        Ref<Image> img = Image::create_from_data(
            frame.size.x, frame.size.y,
            false, Image::FORMAT_RGBA8,
            pba
        );
        
        if (img.is_valid()) {
            if (texture->get_size() == frame.size) {
                texture->update(img);
            } else {
                texture->set_image(img);
            }
        }
    }

    void setup_dimensions(size_t width, size_t height) {
        dimensions.frame.x = width;
        dimensions.frame.y = height;
        dimensions.aligned_width = align_dimension(width);
        dimensions.aligned_height = align_dimension(height);
    }

    static size_t align_dimension(size_t dim, size_t alignment = 16) {
        return (dim + alignment - 1) & ~(alignment - 1);
    }

    static double predict_next_frame_time(double current_time, float fps) {
        return current_time + (1.0 / fps);
    }

    // virtual methods for platform implementations
    virtual void process_frame_queue() = 0;
    virtual bool check_end_of_stream() = 0;
    virtual void update_frame_queue(double p_delta) = 0;

  static void _bind_methods(){};
    
public:
    VideoStreamPlaybackNative() = default;

    virtual void _update(double p_delta) override final {
        if (!state.playing || state.paused) {
            return;
        }

        state.engine_time += p_delta;
        update_frame_queue(p_delta);
        
        // Try to get the next frame that should be displayed
        const std::optional<VideoFrame> frame = frame_queue.try_pop_next_frame(state.engine_time);
        if (frame) {
            update_texture_from_frame(*frame);
        }

        // Check for end of playback
        if (check_end_of_stream()) {
            state.playing = false;
            state.engine_time = 0.0;
            frame_queue.clear();
        }
    }
};

} // namespace godot
