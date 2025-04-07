#include "wmf_audio_handler.hpp"

namespace godot {

WMFAudioHandler::WMFAudioHandler() {
    // Initialize audio variables
    audio_needs_restart = true;
    audio_frame_time = 0.0;
    audio_read_position = 0.0;
}

WMFAudioHandler::~WMFAudioHandler() {
    clear();
}

void WMFAudioHandler::clear() {
    audio_media_type = nullptr;
    clear_audio_sample_queue();
}

LONGLONG WMFAudioHandler::time_to_wmf_time(double time) {
    // Convert seconds to 100-nanosecond units
    return static_cast<LONGLONG>(time * 10000000);
}

double WMFAudioHandler::wmf_time_to_seconds(LONGLONG wmf_time) {
    // Convert 100-nanosecond units to seconds
    return static_cast<double>(wmf_time) / 10000000.0;
}

bool WMFAudioHandler::setup_audio_stream(ComPtr<IMFSourceReader>& source_reader) {
    if (!source_reader) return false;
    
    // Select the first audio stream
    HRESULT hr = source_reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    if (FAILED(hr)) {
        UtilityFunctions::print_verbose("No audio stream available in the media file");
        return false;
    }
    
    // Get native media type
    ComPtr<IMFMediaType> native_type;
    hr = source_reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &native_type);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to get native audio media type");
        return false;
    }
    
    // Get native audio parameters
    UINT32 native_channels = 0;
    UINT32 native_sample_rate = 0;
    
    hr = native_type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &native_channels);
    if (SUCCEEDED(hr) && native_channels > 0) {
        channels = native_channels;
    }
    
    hr = native_type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &native_sample_rate);
    if (SUCCEEDED(hr) && native_sample_rate > 0) {
        mix_rate = native_sample_rate;
    }
    
    UtilityFunctions::print("Audio stream detected: " + itos(channels) + " channels at " + 
                               itos(mix_rate) + " Hz");
    
    // Create a media type for PCM float audio output
    hr = MFCreateMediaType(&audio_media_type);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to create audio media type");
        return false;
    }
    
    hr = audio_media_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set audio major type");
        return false;
    }
    
    // Setting up PCM float format which is what Godot expects
    hr = audio_media_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set audio subtype to float");
        return false;
    }
    
    hr = audio_media_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set audio bits per sample");
        return false;
    }
    
    hr = audio_media_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, mix_rate);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set audio sample rate");
        return false;
    }
    
    hr = audio_media_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set audio channels");
        return false;
    }
    
    // Important PCM format details
    hr = audio_media_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 4); // 4 bytes per float sample
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set audio block alignment");
        // Not critical, continue
    }
    
    hr = audio_media_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, channels * 4 * mix_rate);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set audio bytes per second");
        // Not critical, continue
    }
    
    // Set the format explicitly as uncompressed PCM
    hr = audio_media_type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set independent samples flag");
        // Not critical, continue
    }
    
    // Set the media type on the source reader
    hr = source_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, audio_media_type.Get());
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set audio media type on reader: 0x" + String::num_int64(hr, 16));
        return false;
    }
    
    // Verify the media type was actually set correctly
    ComPtr<IMFMediaType> actual_type;
    hr = source_reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual_type);
    if (SUCCEEDED(hr)) {
        GUID actual_subtype;
        hr = actual_type->GetGUID(MF_MT_SUBTYPE, &actual_subtype);
        if (SUCCEEDED(hr)) {
            bool is_float = (actual_subtype == MFAudioFormat_Float);
            UtilityFunctions::print("Audio format set as float PCM: " + String(is_float ? "YES" : "NO"));
        }
        
        UINT32 actual_channels;
        hr = actual_type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &actual_channels);
        if (SUCCEEDED(hr)) {
            UtilityFunctions::print("Actual audio channels: " + itos(actual_channels));
        }
    }
    
    // Initialize audio variables
    audio_needs_restart = true;
    audio_frame_time = 0.0;
    audio_read_position = 0.0;
    
    return true;
}

void WMFAudioHandler::process_audio_samples(ComPtr<IMFSourceReader>& source_reader, double current_time) {
    if (!audio_media_type || !source_reader) return;
    
    // Determine the current video time we're targeting for audio
    double target_time = current_time;
    double audio_window_start = target_time - 0.05; // 50ms before
    double audio_window_end = target_time + AUDIO_BUFFER_AHEAD_TIME;
    
    // If we need to restart audio from a specific position
    if (audio_needs_restart) {
        UtilityFunctions::print("Restarting audio from position: " + String::num_real(target_time));
        
        // Clear any existing audio samples
        clear_audio_sample_queue();
        audio_read_position = target_time;
        audio_needs_restart = false;
        
        // Perform a seek specifically for the audio stream
        PROPVARIANT var;
        PropVariantInit(&var);
        var.vt = VT_I8;
        var.hVal.QuadPart = time_to_wmf_time(target_time);
        
        HRESULT hr = source_reader->SetCurrentPosition(GUID_NULL, var);
        PropVariantClear(&var);
        
        if (FAILED(hr)) {
            UtilityFunctions::printerr("Failed to seek audio to position: " + String::num_real(target_time));
        }
    }
    
    // Debug the buffering window
    UtilityFunctions::print_verbose("Audio buffering window: " + String::num_real(audio_window_start) + 
                                  " to " + String::num_real(audio_window_end));
    
    int samples_read = 0;
    bool buffer_full = false;
    
    // Process audio samples until we've buffered enough ahead or hit end of stream
    while (!buffer_full) {
        DWORD stream_flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        
        HRESULT hr = source_reader->ReadSample(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            0,                     // Flags
            nullptr,               // Actual stream index
            &stream_flags,         // Stream flags
            &timestamp,            // Timestamp
            &sample                // Sample
        );
        
        if (FAILED(hr)) {
            UtilityFunctions::printerr("Failed to read audio sample: 0x" + String::num_int64(hr, 16));
            break;
        }
        
        if (stream_flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            UtilityFunctions::print_verbose("End of audio stream reached");
            break;
        }
        
        if (stream_flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            UtilityFunctions::print("Audio media type changed, reconfiguring...");
            // Reconfigure audio when media type changes
            if (!setup_audio_stream(source_reader)) {
                UtilityFunctions::printerr("Failed to reconfigure audio after format change");
                break;
            }
            continue;
        }
        
        if (!sample) {
            UtilityFunctions::print_verbose("Null audio sample received");
            continue;
        }
        
        // Get the sample timestamp for accurate sync
        LONGLONG sample_time = 0;
        hr = sample->GetSampleTime(&sample_time);
        if (SUCCEEDED(hr)) {
            timestamp = sample_time;
        }
        
        double audio_time = wmf_time_to_seconds(timestamp);
        audio_read_position = audio_time;
        
        UtilityFunctions::print_verbose("Read audio sample at time=" + String::num_real(audio_time));
        
        // Only process audio that's in our sync window
        if (audio_time >= audio_window_start && audio_time <= audio_window_end) {
            // Process this audio sample and store in our queue
            AudioSample audio_sample;
            audio_sample.presentation_time = audio_time;
            
            if (extract_audio_data(sample.Get(), audio_sample.data)) {
                // Store the audio sample in our queue
                audio_sample_queue.push_back(audio_sample);
                samples_read++;
                
                UtilityFunctions::print_verbose("Buffered audio: time=" + String::num_real(audio_time) +
                                              ", samples=" + itos(audio_sample.data.size()) +
                                              ", queue_size=" + itos(audio_sample_queue.size()));
            } else {
                UtilityFunctions::printerr("Failed to extract audio data at time=" + String::num_real(audio_time));
            }
        } else if (audio_time > audio_window_end) {
            // We've read past our buffer window
            buffer_full = true;
            UtilityFunctions::print_verbose("Audio buffer window full, stopping at time=" + String::num_real(audio_time));
            break;
        } else {
            // This audio is too old, skip it
            UtilityFunctions::print_verbose("Skipping old audio sample at time=" + String::num_real(audio_time));
            continue;
        }
        
        // If we've buffered enough samples or reached the end of our window, stop
        if (samples_read >= 10 || audio_read_position >= audio_window_end) {
            buffer_full = true;
            break;
        }
    }
    
    UtilityFunctions::print_verbose("Audio buffering complete: read " + itos(samples_read) + 
                                  " samples, queue size=" + itos(audio_sample_queue.size()));
}

void WMFAudioHandler::update_audio_sync(double current_time) {
    if (!audio_media_type) return;
    
    // Check if we need to resync audio
    if (needs_audio_resync(current_time)) {
        UtilityFunctions::print("Audio needs resync at time=" + String::num_real(current_time));
        audio_needs_restart = true;
        return;
    }
    
    // Process audio samples at the current time
    bool audio_mixed = false;
    
    // Find audio samples that should be played at this time
    while (!audio_sample_queue.empty()) {
        AudioSample& sample = audio_sample_queue.front();
        
        // If this sample is for the future, keep it
        if (sample.presentation_time > current_time + 0.01) { // Small lookahead
            break;
        }
        
        // If this sample is for now or the past, play it
        if (sample.data.size() > 0) {
            // Get the actual number of frames (divide by channels)
            int frames = sample.data.size() / channels;
            if (frames > 0) {
                // Debug info to track audio playback
                UtilityFunctions::print_verbose("Mixing audio: time=" + String::num_real(sample.presentation_time) + 
                                              ", frames=" + itos(frames) + 
                                              ", current_time=" + String::num_real(current_time));
                
                // Mix the audio
                mix_audio(frames, sample.data);
                audio_mixed = true;
                audio_frame_time = sample.presentation_time;
            }
        }
        
        // Remove this sample from the queue
        audio_sample_queue.pop_front();
    }
    
    // Debug if no audio was mixed
    if (!audio_mixed && !audio_sample_queue.empty()) {
        const AudioSample& next = audio_sample_queue.front();
        UtilityFunctions::print_verbose("No audio mixed at time=" + String::num_real(current_time) + 
                                      ", next sample at=" + String::num_real(next.presentation_time));
    }
}

bool WMFAudioHandler::needs_audio_resync(double video_time) const {
    // If the audio queue is empty, we need to resync
    if (audio_sample_queue.empty()) {
        return true;
    }
    
    // Get the earliest and latest audio samples in the queue
    const AudioSample& first_sample = audio_sample_queue.front();
    const AudioSample& last_sample = audio_sample_queue.back();
    
    // If the current video time is outside our buffered audio range with some tolerance,
    // we need to resync
    return (video_time < first_sample.presentation_time - AUDIO_SYNC_TOLERANCE || 
            video_time > last_sample.presentation_time + AUDIO_SYNC_TOLERANCE);
}

void WMFAudioHandler::clear_audio_sample_queue() {
    audio_sample_queue.clear();
}

bool WMFAudioHandler::extract_audio_data(IMFSample* audio_sample, PackedFloat32Array& out_data) {
    if (!audio_sample) return false;
    
    // Get sample duration for debugging
    LONGLONG sample_duration = 0;
    HRESULT hr = audio_sample->GetSampleDuration(&sample_duration);
    if (SUCCEEDED(hr)) {
        double duration_sec = wmf_time_to_seconds(sample_duration);
        UtilityFunctions::print_verbose("Audio sample duration: " + String::num_real(duration_sec) + " seconds");
    }
    
    // Get sample buffer count
    DWORD buffer_count = 0;
    hr = audio_sample->GetBufferCount(&buffer_count);
    if (FAILED(hr) || buffer_count == 0) {
        UtilityFunctions::printerr("No audio buffers in sample");
        return false;
    }
    
    UtilityFunctions::print_verbose("Audio sample contains " + itos(buffer_count) + " buffers");
    
    // Get the buffer
    ComPtr<IMFMediaBuffer> buffer;
    hr = audio_sample->GetBufferByIndex(0, &buffer);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to get audio buffer by index");
        return false;
    }
    
    // Lock the buffer
    BYTE* data = nullptr;
    DWORD max_length = 0;
    DWORD current_length = 0;
    
    hr = buffer->Lock(&data, &max_length, &current_length);
    if (FAILED(hr) || data == nullptr) {
        UtilityFunctions::printerr("Failed to lock audio buffer");
        return false;
    }
    
    // Setup RAII unlock
    struct BufferUnlocker {
        IMFMediaBuffer* buffer;
        BufferUnlocker(IMFMediaBuffer* b) : buffer(b) {}
        ~BufferUnlocker() { if (buffer) buffer->Unlock(); }
    } unlocker(buffer.Get());
    
    // Calculate number of samples
    size_t num_samples = current_length / sizeof(float);
    if (num_samples == 0) {
        UtilityFunctions::printerr("Audio buffer contains no samples");
        return false;
    }
    
    UtilityFunctions::print_verbose("Audio buffer contains " + itos(num_samples) + 
                                    " float samples (" + itos(current_length) + " bytes)");
    
    // Resize output array
    out_data.resize(num_samples);
    
    // Copy audio data
    float* src = reinterpret_cast<float*>(data);
    
    // Check for valid audio data - detect potential silence or garbage
    bool has_valid_audio = false;
    float max_sample = 0.0f;
    
    for (size_t i = 0; i < num_samples; i++) {
        float sample = src[i];
        out_data.set(i, sample);
        
        // Track max amplitude
        float abs_sample = std::abs(sample);
        if (abs_sample > max_sample) {
            max_sample = abs_sample;
        }
        
        if (abs_sample > 0.01f) { // Consider non-silent if above 1% amplitude
            has_valid_audio = true;
        }
    }
    
    if (!has_valid_audio) {
        UtilityFunctions::print_verbose("Audio buffer appears to be silent (max amplitude: " + 
                                       String::num_real(max_sample) + ")");
    } else {
        UtilityFunctions::print_verbose("Audio buffer has valid signal (max amplitude: " + 
                                       String::num_real(max_sample) + ")");
    }
    
    // Return success even if silent, as silence is valid audio
    return true;
}

void WMFAudioHandler::process_audio_sample(IMFSample* audio_sample, double sample_time) {
    if (!audio_sample) return;
    
    PackedFloat32Array audio_data;
    if (!extract_audio_data(audio_sample, audio_data)) {
        return;
    }
    
    // Debug output
    UtilityFunctions::print_verbose("Direct processing audio: time=" + String::num_real(sample_time) +
                                   ", samples=" + itos(audio_data.size()));
    
    // Mix the audio
    mix_audio(audio_data.size() / channels, audio_data);
    audio_frame_time = sample_time;
}

// This would typically be called from the video playback class
void WMFAudioHandler::mix_audio(int p_frames, const PackedFloat32Array& p_buffer) {
    // This is a placeholder - in the real implementation, this would call into the Godot audio system
    // For now we'll just log that we received audio data
    UtilityFunctions::print_verbose("Mixing " + itos(p_frames) + " audio frames with " + 
                                  itos(channels) + " channels");
}

} // namespace godot