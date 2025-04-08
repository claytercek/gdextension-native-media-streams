#include "wmf_audio_handler.hpp"

namespace godot {

WMFAudioHandler::WMFAudioHandler(IAudioMixer* mixer) 
    : audio_mixer(mixer) {
    needs_restart = true;
    audio_time = 0.0;
}

WMFAudioHandler::~WMFAudioHandler() {
    clear();
}

void WMFAudioHandler::clear() {
    audio_media_type = nullptr;
    pending_samples.clear();
    mix_buffer.resize(0);
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
    needs_restart = true;
    audio_time = 0.0;
    
    return true;
}

void WMFAudioHandler::seek(double time) {
    needs_restart = true;
    pending_samples.clear();
    audio_time = time;
}

void WMFAudioHandler::process_audio(ComPtr<IMFSourceReader>& source_reader, double current_time) {
    if (!audio_media_type || !source_reader || !audio_mixer) return;
    
    // Process pending audio samples first (audio that's ready to be played)
    while (!pending_samples.empty()) {
        AudioSample& sample = pending_samples.front();
        
        // If this sample is for the future, keep it for later
        if (sample.presentation_time > current_time) {
            break;
        }
        
        // Process this audio sample if it's time to play
        if (sample.data.size() > 0) {
            int frames = sample.data.size() / channels;
            if (frames > 0) {
                // Mix the audio using VideoStreamPlayback's mix_audio method
                mix_audio_sample(sample.data, frames);
                audio_time = sample.presentation_time;
            }
        }
        
        // Remove this sample from the queue
        pending_samples.pop_front();
    }
    
    // Check if we need to buffer more audio
    const double buffer_end_time = pending_samples.empty() ? 
        current_time : pending_samples.back().presentation_time;
    
    if (buffer_end_time < current_time + BUFFER_AHEAD_TIME) {
        // Handle restart/seek if needed
        if (needs_restart) {
            UtilityFunctions::print_verbose("Restarting audio from position: " + String::num_real(current_time));
            
            // Clear existing samples
            pending_samples.clear();
            
            // Perform a seek to current time
            PROPVARIANT var;
            PropVariantInit(&var);
            var.vt = VT_I8;
            var.hVal.QuadPart = time_to_wmf_time(current_time);
            
            HRESULT hr = source_reader->SetCurrentPosition(GUID_NULL, var);
            PropVariantClear(&var);
            
            if (FAILED(hr)) {
                UtilityFunctions::printerr("Failed to seek audio to position: " + String::num_real(current_time));
            }
            
            needs_restart = false;
        }
        
        // Read more audio samples
        const int MAX_SAMPLES_PER_READ = 5;
        int samples_read = 0;
        bool end_of_stream = false;
        bool format_changed = false;
        
        while (samples_read < MAX_SAMPLES_PER_READ && !end_of_stream && !format_changed) {
            DWORD stream_flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            
            // Read the next audio sample
            HRESULT hr = source_reader->ReadSample(
                MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                0,             // Flags
                nullptr,       // Actual stream index
                &stream_flags, // Stream flags
                &timestamp,    // Timestamp
                &sample        // Sample
            );
            
            if (FAILED(hr)) {
                UtilityFunctions::printerr("Failed to read audio sample: 0x" + String::num_int64(hr, 16));
                break;
            }
            
            // Check for end of stream
            if (stream_flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                end_of_stream = true;
                break;
            }
            
            // Check for media type changes
            if (stream_flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
                format_changed = true;
                setup_audio_stream(source_reader);
                break;
            }
            
            // Skip empty samples
            if (!sample) {
                continue;
            }
            
            // Get sample timestamp
            LONGLONG sample_time = 0;
            if (SUCCEEDED(sample->GetSampleTime(&sample_time))) {
                timestamp = sample_time;
            }
            
            double audio_sample_time = wmf_time_to_seconds(timestamp);
            
            // Skip samples that are for the past (avoid buffering old data)
            if (audio_sample_time < current_time - SYNC_TOLERANCE) {
                continue;
            }
            
            // Add the sample to our queue
            AudioSample audio_sample;
            audio_sample.presentation_time = audio_sample_time;
            
            if (extract_audio_data(sample.Get(), audio_sample.data)) {
                pending_samples.push_back(audio_sample);
                samples_read++;
                
                // Stop if we've buffered enough ahead
                if (audio_sample_time > current_time + BUFFER_AHEAD_TIME) {
                    break;
                }
            }
        }
    }
}

bool WMFAudioHandler::extract_audio_data(IMFSample* audio_sample, PackedFloat32Array& out_data) {
    if (!audio_sample) return false;
    
    // Get the buffer
    DWORD buffer_count = 0;
    HRESULT hr = audio_sample->GetBufferCount(&buffer_count);
    if (FAILED(hr) || buffer_count == 0) {
        return false;
    }
    
    ComPtr<IMFMediaBuffer> buffer;
    hr = audio_sample->GetBufferByIndex(0, &buffer);
    if (FAILED(hr)) {
        return false;
    }
    
    // Try to get 2D buffer for more efficient access
    ComPtr<IMF2DBuffer> buffer2D;
    hr = buffer.As(&buffer2D);
    
    BYTE* data = nullptr;
    DWORD current_length = 0;
    
    if (SUCCEEDED(hr) && buffer2D) {
        // Lock 2D buffer (more efficient for some hardware-accelerated formats)
        LONG pitch = 0;  // Using LONG as required by the Lock2D method
        hr = buffer2D->Lock2D(&data, &pitch);
        if (FAILED(hr) || !data) {
            return false;
        }
        
        // Get buffer length from regular buffer interface
        hr = buffer->GetCurrentLength(&current_length);
        if (FAILED(hr)) {
            buffer2D->Unlock2D();
            return false;
        }
        
        // RAII unlock
        struct Buffer2DUnlocker {
            IMF2DBuffer* buffer;
            Buffer2DUnlocker(IMF2DBuffer* b) : buffer(b) {}
            ~Buffer2DUnlocker() { if (buffer) buffer->Unlock2D(); }
        } unlocker(buffer2D.Get());
        
        // Calculate number of float samples
        size_t num_samples = current_length / sizeof(float);
        if (num_samples == 0) {
            return false;
        }
        
        // Optimize the copy operation
        out_data.resize(num_samples);
        memcpy(out_data.ptrw(), data, current_length);
        
        return true;
    } else {
        // Fall back to regular buffer
        DWORD max_length = 0;
        hr = buffer->Lock(&data, &max_length, &current_length);
        if (FAILED(hr) || !data) {
            return false;
        }
        
        // RAII unlock
        struct BufferUnlocker {
            IMFMediaBuffer* buffer;
            BufferUnlocker(IMFMediaBuffer* b) : buffer(b) {}
            ~BufferUnlocker() { if (buffer) buffer->Unlock(); }
        } unlocker(buffer.Get());
        
        // Calculate number of float samples
        size_t num_samples = current_length / sizeof(float);
        if (num_samples == 0) {
            return false;
        }
        
        // Optimize the copy operation using direct memory copy
        out_data.resize(num_samples);
        memcpy(out_data.ptrw(), data, current_length);
        
        return true;
    }
}

void WMFAudioHandler::mix_audio_sample(const PackedFloat32Array& buffer, int frame_count) {
    if (!audio_mixer || buffer.size() == 0 || frame_count <= 0) return;
    
    // Call the audio mixer interface to handle audio mixing
    audio_mixer->mix_audio(frame_count, buffer);
}

} // namespace godot