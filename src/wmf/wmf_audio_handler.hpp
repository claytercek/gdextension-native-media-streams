#pragma once
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <list>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace godot {

// Interface for audio mixing functionality
class IAudioMixer {
public:
    virtual ~IAudioMixer() = default;
    virtual void mix_audio(int frame_count, const PackedFloat32Array& buffer) = 0;
};

class WMFAudioHandler {
public:
    WMFAudioHandler(IAudioMixer* mixer);
    ~WMFAudioHandler();

    // Setup methods
    bool setup_audio_stream(ComPtr<IMFSourceReader>& source_reader);
    void clear();
    
    // Getters
    int get_channels() const { return channels; }
    int get_mix_rate() const { return mix_rate; }
    
    // Audio track selection
    void set_audio_track(int idx) { audio_track = idx; }
    int get_audio_track() const { return audio_track; }

    // Audio processing methods
    void process_audio(ComPtr<IMFSourceReader>& source_reader, double current_time);
    void seek(double time);
    
    // Time conversion utilities
    static LONGLONG time_to_wmf_time(double time);
    static double wmf_time_to_seconds(LONGLONG wmf_time);

private:
    struct AudioSample {
        PackedFloat32Array data;
        double presentation_time{0.0};
    };
    
    // Core audio processing
    bool extract_audio_data(IMFSample* audio_sample, PackedFloat32Array& out_data);
    void mix_audio_sample(const PackedFloat32Array& buffer, int frame_count);
    
    // Interface for audio mixing
    IAudioMixer* audio_mixer;
    
    // Audio state
    ComPtr<IMFMediaType> audio_media_type;
    PackedFloat32Array mix_buffer;
    int mix_rate{44100};
    int channels{2};
    double audio_time{0.0};
    std::list<AudioSample> pending_samples;
    bool needs_restart{false};
    int audio_track{0};
    
    // Constants
    static constexpr double BUFFER_AHEAD_TIME = 0.5;  // Buffer 500ms ahead
    static constexpr double SYNC_TOLERANCE = 0.1;     // 100ms tolerance
};

} // namespace godot