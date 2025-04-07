#pragma once
#include "../common/frame_queue_video_stream.hpp"
#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/video_stream.hpp>

#include "wmf_media_source.hpp"
#include "wmf_video_decoder.hpp"
#include "wmf_audio_handler.hpp"

namespace godot {

class VideoStreamPlaybackWMF final : public FrameQueueVideoStream {
    GDCLASS(VideoStreamPlaybackWMF, FrameQueueVideoStream)

public:
    VideoStreamPlaybackWMF();
    ~VideoStreamPlaybackWMF();

    // Core interface
    void set_file(const String& file);

    // VideoStreamPlayback interface
    virtual void _play() override;
    virtual void _stop() override;
    virtual void _set_paused(bool paused) override;
    virtual void _seek(double time) override;
    virtual bool _is_playing() const override;
    virtual bool _is_paused() const override;
    virtual double _get_length() const override;
    virtual double _get_playback_position() const override;
    virtual void _set_audio_track(int idx) override;
    virtual Ref<Texture2D> _get_texture() const override;
    virtual int _get_channels() const override;
    virtual int _get_mix_rate() const override;

protected:
    static void _bind_methods();

    // FrameQueueVideoStream interface
    virtual void process_frame_queue() override;
    virtual bool check_end_of_stream() override;
    virtual void update_frame_queue(double delta) override;

private:
    // Component managers
    WMFMediaSource media_source;
    WMFVideoDecoder video_decoder;
    WMFAudioHandler audio_handler;
    
    // State variables
    bool initialization_complete{false};
    bool play_requested{false};
    double last_frame_time{0.0};
    
    // Helpers
    double get_media_time() const;
};

class VideoStreamWMF final : public VideoStream {
    GDCLASS(VideoStreamWMF, VideoStream)

protected:
    static void _bind_methods();

public:
    virtual Ref<VideoStreamPlayback> _instantiate_playback() override;
};

} // namespace godot