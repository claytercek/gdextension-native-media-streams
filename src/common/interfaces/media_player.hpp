#pragma once
#include "../media/video_frame.hpp"
#include "../media/audio_frame.hpp"
#include <string>

namespace godot {

/**
 * Interface for platform-specific media players.
 * Defines the common API for interacting with different media backends.
 */
class IMediaPlayer {
public:
    // Media properties
    struct MediaInfo {
        double duration{0.0};         // Duration in seconds
        int width{0};                 // Video width in pixels
        int height{0};                // Video height in pixels
        float framerate{30.0f};       // Video framerate
        int audio_channels{0};        // Number of audio channels (0 = no audio)
        int audio_sample_rate{0};     // Audio sample rate in Hz
        int audio_track_count{0};     // Number of available audio tracks
    };
    
    // Track selection info
    struct TrackInfo {
        int index{0};                 // Track index
        std::string language;         // Language code (if available)
        std::string name;             // Track name (if available)
    };
    
    // Playback state
    enum class State {
        STOPPED,
        PLAYING,
        PAUSED,
        ERROR
    };
    
    virtual ~IMediaPlayer() = default;
    
    // Core media operations
    virtual bool open(const std::string& file_path) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    
    // Playback controls
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void seek(double time_sec) = 0;
    
    // State queries
    virtual State get_state() const = 0;
    virtual bool is_playing() const = 0;
    virtual bool is_paused() const = 0;
    virtual bool has_ended() const = 0;
    
    // Media information
    virtual MediaInfo get_media_info() const = 0;
    virtual double get_position() const = 0;
    
    // Frame handling
    virtual bool read_video_frame(VideoFrame& frame) = 0;
    virtual bool read_audio_frame(AudioFrame& frame, double current_time) = 0;
    
    // Audio track management
    virtual int get_audio_track_count() const = 0;
    virtual TrackInfo get_audio_track_info(int track_index) const = 0;
    virtual void set_audio_track(int track_index) = 0;
    virtual int get_current_audio_track() const = 0;
};

} // namespace godot