#pragma once
#include <godot_cpp/variant/packed_float32_array.hpp>

namespace godot {

/**
 * Interface for audio mixing functionality.
 * This allows decoupling the audio processing from the specific implementation.
 */
class IAudioMixer {
public:
    virtual ~IAudioMixer() = default;
    
    /**
     * Mix audio data into the Godot audio system.
     * 
     * @param frame_count Number of frames to mix
     * @param buffer Buffer containing audio data (interleaved float PCM samples)
     * @param offset Offset in the buffer to start mixing from
     */
    virtual void mix_audio(int frame_count, const PackedFloat32Array& buffer, int offset = 0) = 0;
};

} // namespace godot