#pragma once
#include "../common/frame_queue_video_stream.hpp"
#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/video_stream.hpp>
#include <vector>

// Windows and WMF includes
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wrl/client.h>

// DirectX for texture handling
#include <d3d11.h>

// Use smart COM pointers for better memory management
using Microsoft::WRL::ComPtr;

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
    struct AudioTrack {
        int index;
        String language;
        String name;
    };

    // WMF resources
    ComPtr<IMFSourceReader> source_reader;
    ComPtr<IMFMediaType> video_media_type;
    ComPtr<IMFMediaType> audio_media_type;
    
    // DirectX resources for hardware acceleration
    ComPtr<ID3D11Device> d3d_device;
    ComPtr<ID3D11DeviceContext> d3d_context;
    
    // Audio mixing
    PackedFloat32Array mix_buffer;
    int mix_rate{44100};
    int channels{2};
    double audio_frame_time{0.0};
    LONGLONG timestamp_offset{0};
    
    // Buffering
    Vector<uint8_t> frame_buffer;
    std::vector<AudioTrack> audio_tracks;
    LONGLONG duration{0};
    
    bool initialization_complete{false};
    bool play_requested{false};
    int audio_track{0};
    double last_frame_time{0.0};

    // Private helpers
    bool setup_wmf_pipeline(const String& file);
    void clear_wmf_objects();
    void detect_framerate();
    bool setup_video_stream();
    bool setup_audio_stream();
    void ensure_frame_buffer(size_t width, size_t height);
    double get_media_time() const;
    void process_audio_samples_for_sync();
    void process_audio_sample(IMFSample* audio_sample, double sample_time);
    LONGLONG time_to_wmf_time(double time) const;
    double wmf_time_to_seconds(LONGLONG wmf_time) const;
};

class VideoStreamWMF final : public VideoStream {
    GDCLASS(VideoStreamWMF, VideoStream)

protected:
    static void _bind_methods();

public:
    virtual Ref<VideoStreamPlayback> _instantiate_playback() override;
};

class ResourceFormatLoaderWMF : public ResourceFormatLoader {
    GDCLASS(ResourceFormatLoaderWMF, ResourceFormatLoader)

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
