#pragma once
#include <godot_cpp/variant/packed_float32_array.hpp>

namespace godot {

/**
 * Platform-agnostic representation of an audio frame.
 * Contains PCM audio data and metadata needed for playback.
 */
struct AudioFrame {
    // Audio sample data (interleaved float PCM samples)
    PackedFloat32Array data;
    
    // Presentation timestamp in seconds
    double presentation_time{0.0};
    
    // Audio format information
    int channels{2};
    int sample_rate{44100};
    
    // Calculate the duration of this audio frame
    double get_duration() const {
        // Duration = number of samples / (sample rate * channels)
        return data.size() > 0 ? static_cast<double>(data.size()) / (sample_rate * channels) : 0.0;
    }
    
    // Get number of audio frames (samples per channel)
    int get_frame_count() const {
        return channels > 0 ? data.size() / channels : 0;
    }
    
    // Default constructor
    AudioFrame() = default;
    
    // Constructor with timestamp
    explicit AudioFrame(double time) : presentation_time(time) {}
};

} // namespace godot