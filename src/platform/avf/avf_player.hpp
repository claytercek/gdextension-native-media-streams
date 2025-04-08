#pragma once
#include "../../common/interfaces/media_player.hpp"
#include "../../common/media/media_time.hpp"
#include <memory>
#include <string>

// Forward declare AVFoundation classes to avoid including Objective-C headers
// in this header file
class AVPlayerItemVideoOutputImpl;
class AVPlayerImpl;
class AVAssetReaderImpl;

namespace godot {

/**
 * AVFoundation implementation of the IMediaPlayer interface.
 * Handles all AVF-specific media operations.
 */
class AVFPlayer : public IMediaPlayer {
private:
    // PIMPL pattern to hide Objective-C implementation details
    std::unique_ptr<AVPlayerImpl> player;
    std::unique_ptr<AVPlayerItemVideoOutputImpl> video_output;
    std::unique_ptr<AVAssetReaderImpl> audio_reader;
    
    // Media information
    MediaInfo media_info;
    State current_state{State::STOPPED};
    int current_audio_track{0};
    
    // Last read positions
    double last_video_position{0.0};
    double last_audio_position{0.0};
    
    // Internal buffer for frame conversion
    std::vector<uint8_t> frame_buffer;
    
    // Helper methods
    void detect_media_info();
    void setup_audio_reader();
    void convert_frame_data(void* src_data, size_t src_stride, 
                           size_t width, size_t height, VideoFrame& frame);

public:
    AVFPlayer();
    ~AVFPlayer();
    
    // IMediaPlayer implementation
    bool open(const std::string& file_path) override;
    void close() override;
    bool is_open() const override;
    
    void play() override;
    void pause() override;
    void stop() override;
    void seek(double time_sec) override;
    
    State get_state() const override;
    bool is_playing() const override;
    bool is_paused() const override;
    bool has_ended() const override;
    
    MediaInfo get_media_info() const override;
    double get_position() const override;
    
    bool read_video_frame(VideoFrame& frame) override;
    bool read_audio_frame(AudioFrame& frame, double current_time) override;
    
    int get_audio_track_count() const override;
    TrackInfo get_audio_track_info(int track_index) const override;
    void set_audio_track(int track_index) override;
    int get_current_audio_track() const override;
};

} // namespace godot