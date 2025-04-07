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

class WMFAudioHandler {
public:
    WMFAudioHandler();
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

    // Audio synchronization methods
    void process_audio_samples(ComPtr<IMFSourceReader>& source_reader, double current_time);
    void update_audio_sync(double current_time);
    bool needs_audio_resync(double video_time) const;
    void clear_audio_sample_queue();
    double get_audio_frame_time() const { return audio_frame_time; }
    
    // Time conversion utilities
    static LONGLONG time_to_wmf_time(double time);
    static double wmf_time_to_seconds(LONGLONG wmf_time);
    
    // Audio mixing
    void mix_audio(int p_frames, const PackedFloat32Array& p_buffer);

private:
    struct AudioTrack {
        int index;
        String language;
        String name;
    };
    
    struct AudioSample {
        PackedFloat32Array data;
        double presentation_time{0.0};
    };
    
    // Audio processing methods
    bool extract_audio_data(IMFSample* audio_sample, PackedFloat32Array& out_data);
    void process_audio_sample(IMFSample* audio_sample, double sample_time);
    
    // Audio state variables
    ComPtr<IMFMediaType> audio_media_type;
    int mix_rate{44100};
    int channels{2};
    double audio_frame_time{0.0};
    std::list<AudioSample> audio_sample_queue;
    double audio_read_position{0.0};
    bool audio_needs_restart{false};
    int audio_track{0};
    std::vector<AudioTrack> audio_tracks;
    
    // Constants
    const double AUDIO_BUFFER_AHEAD_TIME{0.5}; // Buffer 500ms ahead
    const double AUDIO_SYNC_TOLERANCE{0.1};     // 100ms tolerance before resyncing
};

} // namespace godot