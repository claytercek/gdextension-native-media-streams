#pragma once
#include <godot_cpp/classes/video_stream_playback.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include "../interfaces/audio_mixer.hpp"
#include "../interfaces/media_player.hpp"
#include "../media/frame_queue.hpp"

namespace godot {

/**
 * Base class for platform-specific video stream playback implementations.
 * Provides common functionality for integrating media playback with Godot.
 */
class VideoStreamPlaybackBase : public VideoStreamPlayback, public IAudioMixer {
    GDCLASS(VideoStreamPlaybackBase, VideoStreamPlayback)

protected:
    // Media resources and state
    Ref<ImageTexture> texture;
    VideoFrameQueue video_frames;
    AudioFrameQueue audio_frames;
    
    struct PlaybackState {
        bool playing{false};
        bool paused{false};
        double engine_time{0.0};
        float playback_rate{1.0f};
    };
    
    PlaybackState state;
    
    // The platform-specific media player implementation
    std::unique_ptr<IMediaPlayer> media_player;
    String file_path;
    
    // Last processed times for tracking
    double last_video_time{0.0};
    double last_audio_time{0.0};
    
    // Buffer control constants
    static constexpr double VIDEO_BUFFER_AHEAD_TIME = 0.5;
    static constexpr double AUDIO_BUFFER_AHEAD_TIME = 1.0;
    
    // Update the texture from a video frame
    void update_texture_from_frame(const VideoFrame& frame) {
        // Create a packed byte array from frame data for Godot
        PackedByteArray pba;
        pba.resize(frame.data.size());
        if (!frame.data.empty()) {
            memcpy(pba.ptrw(), frame.data.data(), frame.data.size());
        
            // Create an image and update the texture
            Ref<Image> img = Image::create_from_data(
                frame.size.x, frame.size.y,
                false, Image::FORMAT_RGBA8,
                pba
            );
            
            if (img.is_valid()) {
                if (!texture.is_valid()) {
                    texture.instantiate();
                }
                
                if (texture->get_size() == frame.size) {
                    texture->update(img);
                } else {
                    texture->set_image(img);
                }
            }
        }
    }
    
    // Process video frames - check queue and buffer more if needed
    virtual void process_video_queue(double delta) {
        if (!media_player || !state.playing || state.paused) return;
        
        // Update current playback time
        state.engine_time += delta;
        
        // Check if we need to buffer more video frames
        if (video_frames.should_buffer_more_frames(state.engine_time, state.playback_rate)) {
            buffer_video_frames();
        }
        
        // Get the next frame to display at current time
        auto frame = video_frames.try_pop_frame_at_time(state.engine_time);
        if (frame) {
            update_texture_from_frame(*frame);
            last_video_time = frame->presentation_time;
        }
        
        // Process audio frames
        process_audio_queue();
        
        // Check for end of stream
        if (media_player->has_ended() && video_frames.empty() && audio_frames.empty()) {
            state.playing = false;
            media_player->stop();
        }
    }
    
    // Process audio frames and mix them into Godot's audio system
    virtual void process_audio_queue() {
        if (!media_player || !state.playing || state.paused) return;
        
        // Check if we need more audio frames
        if (audio_frames.should_buffer_more_frames(state.engine_time, state.playback_rate)) {
            buffer_audio_frames();
        }
        
        // Mix audio frames that are ready for playback
        while (!audio_frames.empty()) {
            auto frame = audio_frames.try_pop_frame_at_time(state.engine_time);
            if (!frame) break;
            
            // Mix this audio frame
            if (frame->data.size() > 0) {
                int num_frames = frame->get_frame_count();
                if (num_frames > 0) {
                    // Mix using VideoStreamPlayback's mix_audio method
                    VideoStreamPlayback::mix_audio(num_frames, frame->data);
                    last_audio_time = frame->presentation_time;
                }
            }
        }
    }
    
    // Buffer more video frames from the media player
    virtual void buffer_video_frames() {
        if (!media_player) return;
        
        int frames_to_buffer = VideoFrameQueue::DEFAULT_MAX_SIZE - video_frames.size();
        for (int i = 0; i < frames_to_buffer; i++) {
            VideoFrame frame;
            if (media_player->read_video_frame(frame)) {
                video_frames.push(std::move(frame));
            } else {
                break; // No more frames available now
            }
        }
    }
    
    // Buffer more audio frames from the media player
    virtual void buffer_audio_frames() {
        if (!media_player) return;
        
        int frames_to_buffer = AudioFrameQueue::DEFAULT_MAX_SIZE - audio_frames.size();
        for (int i = 0; i < frames_to_buffer; i++) {
            AudioFrame frame;
            if (media_player->read_audio_frame(frame, state.engine_time)) {
                audio_frames.push(std::move(frame));
            } else {
                break; // No more frames available now
            }
        }
    }

public:
    VideoStreamPlaybackBase() {
        texture.instantiate();
    }
    
    virtual ~VideoStreamPlaybackBase() {
        // Ensure media player is cleaned up
        if (media_player) {
            media_player->close();
            media_player.reset();
        }
    }
    
    // IAudioMixer implementation
    virtual void mix_audio(int frame_count, const PackedFloat32Array& buffer, int offset = 0) override {
        // Call the godot video stream playback mix_audio method
        VideoStreamPlayback::mix_audio(frame_count, buffer, offset);
    }
    
    // VideoStreamPlayback interface implementation
    virtual void _play() override {
        if (!media_player) return;
        
        if (!state.playing) {
            // Starting playback from beginning or after stop
            state.engine_time = 0.0;
            last_video_time = 0.0;
            last_audio_time = 0.0;
            
            // Clear queues
            video_frames.clear();
            audio_frames.clear();
            
            // Start media player
            media_player->seek(0.0);
            media_player->play();
            
            // Pre-buffer some frames
            buffer_video_frames();
            buffer_audio_frames();
        } else if (state.paused) {
            // Resuming from pause
            media_player->play();
        }
        
        state.playing = true;
        state.paused = false;
    }
    
    virtual void _stop() override {
        if (!media_player) return;
        
        media_player->stop();
        state.playing = false;
        state.paused = false;
        state.engine_time = 0.0;
        
        // Clear queues
        video_frames.clear();
        audio_frames.clear();
        
        // Reset tracking times
        last_video_time = 0.0;
        last_audio_time = 0.0;
    }
    
    virtual void _set_paused(bool p_paused) override {
        if (!media_player) return;
        
        if (state.paused != p_paused) {
            state.paused = p_paused;
            
            if (p_paused) {
                media_player->pause();
            } else {
                media_player->play();
            }
        }
    }
    
    virtual bool _is_playing() const override {
        return state.playing;
    }
    
    virtual bool _is_paused() const override {
        return state.paused;
    }
    
    virtual void _seek(double p_time) override {
        if (!media_player) return;
        
        // Clear frame queues
        video_frames.clear();
        audio_frames.clear();
        
        // Update internal time
        state.engine_time = p_time;
        last_video_time = p_time;
        last_audio_time = p_time;
        
        // Seek in media player
        media_player->seek(p_time);
        
        // Buffer new frames at the seek position
        buffer_video_frames();
        buffer_audio_frames();
    }
    
    virtual void _update(double delta) override {
        process_video_queue(delta);
    }
    
    virtual double _get_playback_position() const override {
        return state.engine_time;
    }
    
    virtual void _set_audio_track(int idx) override {
        if (media_player) {
            media_player->set_audio_track(idx);
        }
    }
    
    virtual Ref<Texture2D> _get_texture() const override {
        return texture;
    }
    
    virtual double _get_length() const override {
        return media_player ? media_player->get_media_info().duration : 0.0;
    }
    
    virtual int _get_channels() const override {
        return media_player ? media_player->get_media_info().audio_channels : 0;
    }
    
    virtual int _get_mix_rate() const override {
        return media_player ? media_player->get_media_info().audio_sample_rate : 0;
    }
    
protected:
    static void _bind_methods() {
        // No additional bindings needed
    }
};

} // namespace godot