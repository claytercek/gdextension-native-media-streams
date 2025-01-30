#pragma once
#include "../common/frame_queue_video_stream.hpp"
#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/video_stream.hpp>
#include <vector>
#include <AVFoundation/AVFoundation.h>

namespace godot {

class VideoStreamPlaybackAVF final : public FrameQueueVideoStream {
    GDCLASS(VideoStreamPlaybackAVF, FrameQueueVideoStream)

public:
    VideoStreamPlaybackAVF();
    ~VideoStreamPlaybackAVF();

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
    struct AudioTrack {
        int index;
        String language;
        String name;
    };

    // AVFoundation resources
    AVPlayer* player{nullptr};
    AVPlayerItem* player_item{nullptr};
    AVPlayerItemVideoOutput* video_output{nullptr};
    std::vector<AudioTrack> audio_tracks;
    Vector<uint8_t> frame_buffer;
    
    bool initialization_complete{false};
    bool play_requested{false};
    int audio_track{0};

    // Private helpers
    bool setup_video_pipeline(const String& file);
    void clear_avf_objects();
    void detect_framerate();
    void setup_aligned_dimensions();
    double get_media_time() const;
    void ensure_frame_buffer(size_t width, size_t height);
    static void convert_bgra_to_rgba_simd(const uint8_t* src, uint8_t* dst, size_t pixel_count);
};

class VideoStreamAVF final : public VideoStream {
    GDCLASS(VideoStreamAVF, VideoStream)

protected:
    static void _bind_methods();

public:
    virtual Ref<VideoStreamPlayback> _instantiate_playback() override;
};

class ResourceFormatLoaderAVF : public ResourceFormatLoader {
    GDCLASS(ResourceFormatLoaderAVF, ResourceFormatLoader)

protected:
    static void _bind_methods() {}

public:
    Variant _load(const String& p_path, const String& p_original_path,
                         bool p_use_sub_threads, int32_t p_cache_mode) const override;
    PackedStringArray _get_recognized_extensions() const override;
    bool _handles_type(const StringName& p_type) const override;
    String _get_resource_type(const String& p_path) const override;
};

} // namespace godot
