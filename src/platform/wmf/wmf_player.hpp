#pragma once
#include "../../common/interfaces/media_player.hpp"
#include "../../common/media/media_time.hpp"
#include <wrl/client.h>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <memory>
#include <string>

using Microsoft::WRL::ComPtr;

namespace godot {

/**
 * Windows Media Foundation implementation of the IMediaPlayer interface.
 * Handles all WMF-specific media operations.
 */
class WMFPlayer : public IMediaPlayer {
private:
    // WMF resources
    ComPtr<IMFSourceReader> source_reader;
    
    // Media information
    MediaInfo media_info;
    State current_state{State::STOPPED};
    int current_audio_track{0};
    
    // Last read positions
    double last_video_position{0.0};
    double last_audio_position{0.0};
    
    // Helper methods
    bool configure_source_reader(const std::string& file_path);
    bool configure_video_stream();
    bool configure_audio_stream();
    bool extract_video_data(IMFSample* sample, VideoFrame& frame);
    bool extract_audio_data(IMFSample* sample, AudioFrame& frame);

public:
    WMFPlayer();
    ~WMFPlayer();
    
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