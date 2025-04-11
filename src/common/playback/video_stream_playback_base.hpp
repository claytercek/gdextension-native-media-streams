#pragma once
#include <godot_cpp/classes/video_stream_playback.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include "../interfaces/audio_mixer.hpp"
#include "../interfaces/media_player.hpp"
#include "../media/frame_queue.hpp"
#include "frame_decoder.hpp"

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
    
    // Frame decoder - abstracts threading details
    std::unique_ptr<FrameDecoder> decoder;
    
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
    
    // Performance settings
    bool use_threading = true;
    
    // Update the texture from a video frame
    void update_texture_from_frame(const VideoFrame& frame) {
        // Safety checks
        if (frame.data.empty() || frame.size.x <= 0 || frame.size.y <= 0) {
            UtilityFunctions::printerr("Invalid video frame data or dimensions");
            return;
        }
        
        // Check that the data size matches expected dimensions
        size_t expected_size = frame.size.x * frame.size.y * 4; // RGBA8 = 4 bytes per pixel
        if (frame.data.size() != expected_size) {
            UtilityFunctions::printerr("Frame data size mismatch: got " + 
                String::num_int64(frame.data.size()) + " bytes, expected " + 
                String::num_int64(expected_size) + " for size " + 
                String::num_int64(frame.size.x) + "x" + String::num_int64(frame.size.y));
            return;
        }
        
        // Create a packed byte array from frame data for Godot
        PackedByteArray pba;
        pba.resize(frame.data.size());
        memcpy(pba.ptrw(), frame.data.data(), frame.data.size());
    
        // Create an image and update the texture
        Ref<Image> img;
        
        try {
            img = Image::create_from_data(
                frame.size.x, frame.size.y,
                false, Image::FORMAT_RGBA8,
                pba
            );
        } catch (const std::exception& e) {
            UtilityFunctions::printerr("Exception creating image: " + String(e.what()));
            return;
        } catch (...) {
            UtilityFunctions::printerr("Unknown exception creating image");
            return;
        }
        
        if (img.is_valid()) {
            if (!texture.is_valid()) {
                texture.instantiate();
            }
            
            try {
                if (texture->get_size() == frame.size) {
                    texture->update(img);
                } else {
                    texture->set_image(img);
                }
            } catch (const std::exception& e) {
                UtilityFunctions::printerr("Exception setting texture image: " + String(e.what()));
            } catch (...) {
                UtilityFunctions::printerr("Unknown exception setting texture image");
            }
        } else {
            UtilityFunctions::printerr("Failed to create valid image from video frame");
        }
    }
    
    // Process video frames
    virtual void process_video_queue() {
        if (!media_player || !state.playing || state.paused) return;
        
        // Get the next frame to display at current time
        auto frame = video_frames.try_pop_frame_at_time(state.engine_time);
        if (frame) {
            update_texture_from_frame(*frame);
            last_video_time = frame->presentation_time;
        }
        
        // Check for end of stream
        if (media_player->has_ended() && video_frames.empty() && audio_frames.empty()) {
            state.playing = false;
            media_player->stop();
            if (decoder) {
                decoder->stop();
            }
        }
    }
    
    // Process audio frames and mix them into Godot's audio system
    virtual void process_audio_queue() {
        if (!media_player || !state.playing || state.paused) return;
        
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
    
    // Initialize decoder with appropriate implementation
    void setup_decoder() {
        decoder = FrameDecoder::create(media_player, video_frames, audio_frames, use_threading);
    }

public:
    VideoStreamPlaybackBase() {
        texture.instantiate();
    }
    
    virtual ~VideoStreamPlaybackBase() {
        // Stop decoder first
        if (decoder) {
            decoder->stop();
            decoder.reset();
        }
        
        // Clean up media player
        if (media_player) {
            media_player->close();
            media_player.reset();
        }
    }
    
    // Enable or disable threaded decoding
    void set_threaded_decoding(bool enabled) {
        if (use_threading == enabled) return;
        
        use_threading = enabled;
        
        // If we're currently playing, restart with new decoder type
        if (state.playing && !state.paused) {
            if (decoder) {
                decoder->stop();
                decoder.reset();
            }
            
            setup_decoder();
            decoder->start();
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
            
            // Start appropriate decoder
            setup_decoder();
            decoder->start();
        } else if (state.paused) {
            // Resuming from pause
            media_player->play();
            
            // Resume decoder
            if (decoder) {
                decoder->resume();
            }
        }
        
        state.playing = true;
        state.paused = false;
    }
    
    virtual void _stop() override {
        if (!media_player) return;
        
        // Stop the decoder
        if (decoder) {
            decoder->stop();
            decoder.reset();
        }
        
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
                // Pause decoder
                if (decoder) {
                    decoder->pause();
                }
            } else {
                media_player->play();
                // Resume decoder
                if (decoder) {
                    decoder->resume();
                }
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
        
        // Pause decoder during seeking
        if (decoder) {
            decoder->pause();
        }
        
        // Clear frame queues
        video_frames.clear();
        audio_frames.clear();
        
        // Update internal time
        state.engine_time = p_time;
        last_video_time = p_time;
        last_audio_time = p_time;
        
        // Seek in media player
        media_player->seek(p_time);
        
        // Resume decoder after seek
        if (decoder) {
            decoder->resume();
        }
    }
    
    virtual void _update(double delta) override {
        // Update current playback time
        state.engine_time += delta;

        // If using synchronous decoder, explicitly decode frames
        if (!use_threading && decoder && decoder->is_running()) {
            static_cast<SyncFrameDecoder*>(decoder.get())->decode_frames();
        }

        process_video_queue();
        process_audio_queue();
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
        if (!texture.is_valid()) {
            // Create a 1x1 black texture as fallback for audio-only streams
            Ref<Image> img = Image::create(1, 1, false, Image::FORMAT_RGBA8);
            img->fill(Color(0, 0, 0, 1)); // Black, opaque
            
            Ref<ImageTexture> fallback;
            fallback.instantiate();
            fallback->set_image(img);
            
            return fallback;
        }
        
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