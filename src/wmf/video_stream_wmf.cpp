#include "video_stream_wmf.hpp"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// -----------------------------------------------------------------------------
// VideoStreamWMF Implementation
// -----------------------------------------------------------------------------
void VideoStreamWMF::_bind_methods() {
  // No additional bindings needed for the stream class
}

Ref<VideoStreamPlayback> VideoStreamWMF::_instantiate_playback() {
    Ref<VideoStreamPlaybackWMF> playback;
    playback.instantiate();
    playback->set_file(get_file());
    return playback;
}

// -----------------------------------------------------------------------------
// VideoStreamPlaybackWMF Implementation
// -----------------------------------------------------------------------------
void VideoStreamPlaybackWMF::_bind_methods() {
  // No additional bindings needed as we're implementing VideoStreamPlayback interface
}

VideoStreamPlaybackWMF::VideoStreamPlaybackWMF() { 
    texture.instantiate();
}

VideoStreamPlaybackWMF::~VideoStreamPlaybackWMF() { 
    clear_wmf_objects();
}

// -----------------------------------------------------------------------------
// Resource Management
// -----------------------------------------------------------------------------
void VideoStreamPlaybackWMF::clear_wmf_objects() {
    source_reader = nullptr;
    video_media_type = nullptr;
    audio_media_type = nullptr;
    d3d_device = nullptr;
    d3d_context = nullptr;
    
    state.playing = false;
    frame_queue.clear();
}

LONGLONG VideoStreamPlaybackWMF::time_to_wmf_time(double time) const {
    // Convert seconds to 100-nanosecond units
    return static_cast<LONGLONG>(time * 10000000);
}

double VideoStreamPlaybackWMF::wmf_time_to_seconds(LONGLONG wmf_time) const {
    // Convert 100-nanosecond units to seconds
    return static_cast<double>(wmf_time) / 10000000.0;
}

bool VideoStreamPlaybackWMF::setup_wmf_pipeline(const String &p_file) {
    UtilityFunctions::print_verbose("VideoStreamPlaybackWMF::setup_wmf_pipeline() called for file: " + p_file);
    
    // Convert file path to Windows format
    wchar_t filepath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, p_file.utf8().get_data(), -1, filepath, MAX_PATH);
    
    // Create source attributes
    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to create source reader attributes");
        return false;
    }
    
    // Set up async callback if needed
    // hr = attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback);
    
    // Enable hardware decoding
    hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to enable hardware video processing");
        // Continue anyway, hardware acceleration is optional
    }
    
    // Create the source reader
    hr = MFCreateSourceReaderFromURL(filepath, attributes.Get(), &source_reader);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to create source reader for: ", p_file);
        return false;
    }
    
    // Set up video stream
    if (!setup_video_stream()) {
        UtilityFunctions::printerr("Failed to set up video stream for: ", p_file);
        clear_wmf_objects();
        return false;
    }
    
    // Set up audio stream (optional)
    if (!setup_audio_stream()) {
        UtilityFunctions::print_verbose("No suitable audio stream found or failed to set up audio");
        // Continue anyway, audio is optional
    }
    
    // Get duration - need to use the proper WMF API for this
    // Instead of using GetPresentationDescriptor (which is not part of IMFSourceReader),
    // we need to use GetPresentationAttribute on the source reader
    
    PROPVARIANT var;
    PropVariantInit(&var);
    
    // Get the duration using the source reader's stream attribute
    hr = source_reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, 
                                                MF_PD_DURATION, 
                                                &var);
    if (SUCCEEDED(hr) && var.vt == VT_UI8) {
        // Store the duration
        duration = var.uhVal.QuadPart;
        UtilityFunctions::print_verbose("Media duration: " + String::num_real(wmf_time_to_seconds(duration)) + " seconds");
    } else {
        UtilityFunctions::printerr("Failed to get media duration");
        duration = 0;
    }
    
    PropVariantClear(&var);
    
    // Create initial texture
    ensure_frame_buffer(dimensions.frame.x, dimensions.frame.y);
    Ref<Image> img = Image::create_empty(dimensions.frame.x, dimensions.frame.y, false, Image::FORMAT_RGBA8);
    if (img.is_null()) {
        clear_wmf_objects();
        UtilityFunctions::printerr("Failed to create initial texture");
        return false;
    }
    
    texture->set_image(img);
    detect_framerate();
    
    initialization_complete = true;
    if (play_requested) {
        _play();
    }
    
    return true;
}

bool VideoStreamPlaybackWMF::setup_video_stream() {
    if (!source_reader) return false;
    
    // Select the first video stream
    HRESULT hr = source_reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to deselect all streams");
        return false;
    }
    
    hr = source_reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to select video stream");
        return false;
    }
    
    // Get native media type
    ComPtr<IMFMediaType> native_type;
    hr = source_reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &native_type);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to get native video media type");
        return false;
    }
    
    // Get video dimensions
    UINT32 width, height;
    hr = MFGetAttributeSize(native_type.Get(), MF_MT_FRAME_SIZE, &width, &height);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to get video dimensions");
        return false;
    }
    
    // Store dimensions
    dimensions.frame.x = width;
    dimensions.frame.y = height;
    dimensions.aligned_width = align_dimension(width);
    dimensions.aligned_height = align_dimension(height);
    
    UtilityFunctions::print_verbose("Video dimensions: " + itos(width) + "x" + itos(height));
    
    // Create a media type for RGBA output
    hr = MFCreateMediaType(&video_media_type);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to create video media type");
        return false;
    }
    
    hr = native_type->CopyAllItems(video_media_type.Get());
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to copy media type attributes");
        return false;
    }
    
    // Set up RGB32 format for easy conversion to Godot's RGBA8
    hr = video_media_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set video format to RGB32");
        return false;
    }
    
    // Set the media type on the source reader
    hr = source_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, video_media_type.Get());
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set video media type on reader");
        return false;
    }
    
    return true;
}

bool VideoStreamPlaybackWMF::setup_audio_stream() {
    if (!source_reader) return false;
    
    // Select the first audio stream
    HRESULT hr = source_reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    if (FAILED(hr)) {
        // No audio stream available
        return false;
    }
    
    // Get native media type
    ComPtr<IMFMediaType> native_type;
    hr = source_reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &native_type);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to get native audio media type");
        return false;
    }
    
    // Create a media type for PCM audio output
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
    
    // Set the media type on the source reader
    hr = source_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, audio_media_type.Get());
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set audio media type on reader");
        return false;
    }
    
    return true;
}

void VideoStreamPlaybackWMF::_seek(double p_time) {
    if (!source_reader) return;
    
    LONGLONG seek_time = time_to_wmf_time(p_time);
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = seek_time;
    
    // Seek to the specified position
    HRESULT hr = source_reader->SetCurrentPosition(GUID_NULL, var);
    PropVariantClear(&var);
    
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to seek to position: ", String::num_real(p_time));
        return;
    }
    
    // Reset frame queue and engine time
    frame_queue.clear();
    state.engine_time = p_time;
    last_frame_time = p_time;
    audio_frame_time = p_time;
}

void VideoStreamPlaybackWMF::_play() {
    UtilityFunctions::print_verbose("VideoStreamPlaybackWMF::_play() invoked.");
    
    if (!initialization_complete) {
        UtilityFunctions::print_verbose("VideoStreamPlaybackWMF::_play() initialization not complete, deferring play.");
        play_requested = true;
        return;
    }
    
    if (!state.playing) {
        // Reset to beginning
        _seek(0.0);
        frame_queue.clear();
        state.engine_time = 0.0;
        state.playing = true;
        state.paused = false;
    } else if (state.paused) {
        state.paused = false;
    }
}

// -----------------------------------------------------------------------------
// Video Processing
// -----------------------------------------------------------------------------
void VideoStreamPlaybackWMF::process_frame_queue() {
    if (!source_reader) return;
    
    // Only read more frames if our queue isn't full
    while (frame_queue.size() < FrameQueue::MAX_SIZE && state.playing && !state.paused) {
        // Read the next video sample
        DWORD stream_flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        
        HRESULT hr = source_reader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,                     // Flags
            nullptr,               // Actual stream index
            &stream_flags,         // Stream flags
            &timestamp,            // Timestamp
            &sample                // Sample
        );
        
        if (FAILED(hr)) {
            UtilityFunctions::printerr("Failed to read video sample");
            break;
        }
        
        // Check for end of stream or gaps in the stream
        if (stream_flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            UtilityFunctions::print_verbose("End of stream reached");
            break;
        }
        
        if (stream_flags & MF_SOURCE_READERF_STREAMTICK) {
            UtilityFunctions::print_verbose("Stream tick detected, possible gap in the data");
            continue;
        }
        
        if (!sample) {
            UtilityFunctions::print_verbose("Null sample received");
            continue;
        }
        
        // Get the sample timestamp for accurate sync (in case it differs from the one returned by ReadSample)
        LONGLONG sample_time = 0;
        hr = sample->GetSampleTime(&sample_time);
        if (SUCCEEDED(hr)) {
            timestamp = sample_time; // Use the sample's own timestamp for better accuracy
        }
        
        // Process the sample
        ComPtr<IMFMediaBuffer> buffer;
        hr = sample->ConvertToContiguousBuffer(&buffer);
        if (FAILED(hr)) {
            UtilityFunctions::printerr("Failed to get media buffer from sample");
            continue;
        }
        
        // Lock the buffer
        BYTE* data = nullptr;
        DWORD data_length = 0;
        hr = buffer->Lock(&data, nullptr, &data_length);
        if (FAILED(hr)) {
            UtilityFunctions::printerr("Failed to lock media buffer");
            continue;
        }
        
        // Create a video frame
        VideoFrame frame;
        frame.size = dimensions.frame;
        frame.presentation_time = wmf_time_to_seconds(timestamp);
        
        // Convert BGRA to RGBA
        frame.data.resize(dimensions.frame.x * dimensions.frame.y * 4);
        uint8_t* src = data;
        uint8_t* dst = frame.data.data();
        
        for (int y = 0; y < dimensions.frame.y; y++) {
            for (int x = 0; x < dimensions.frame.x; x++) {
                dst[0] = src[2]; // R
                dst[1] = src[1]; // G
                dst[2] = src[0]; // B
                dst[3] = src[3]; // A
                
                src += 4;
                dst += 4;
            }
        }
        
        // Unlock the buffer
        buffer->Unlock();
        
        // Store the frame
        frame_queue.push(std::move(frame));
        last_frame_time = frame.presentation_time;
    }
    
    // Also process audio samples
    process_audio_samples_for_sync();
}

void VideoStreamPlaybackWMF::process_audio_samples_for_sync() {
    if (!audio_media_type || !source_reader) return;
    
    // Determine the current video time we're targeting for audio
    double target_time = state.engine_time;
    double audio_window_start = target_time - 0.05; // 50ms before
    double audio_window_end = target_time + 0.5;    // 500ms ahead
    
    // Process audio samples until we've buffered enough ahead
    while (state.playing && !state.paused) {
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
            UtilityFunctions::printerr("Failed to read audio sample: ", hr);
            break;
        }
        
        if (stream_flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            UtilityFunctions::print_verbose("End of audio stream reached");
            break;
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
        
        // Only process audio that's in our sync window
        if (audio_time >= audio_window_start && audio_time <= audio_window_end) {
            // Process this audio sample
            process_audio_sample(sample.Get(), audio_time);
            
            // Update our last processed audio time
            audio_frame_time = audio_time;
        } else if (audio_time > audio_window_end) {
            // This audio is too far ahead, save it for later
            break;
        } else {
            // This audio is too old, skip it
            continue;
        }
    }
}

void VideoStreamPlaybackWMF::process_audio_sample(IMFSample* audio_sample, double sample_time) {
    if (!audio_sample) return;
    
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = audio_sample->ConvertToContiguousBuffer(&buffer);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to get audio buffer");
        return;
    }
    
    BYTE* data = nullptr;
    DWORD data_length = 0;
    hr = buffer->Lock(&data, nullptr, &data_length);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to lock audio buffer");
        return;
    }
    
    // Resize mix buffer if needed
    size_t num_samples = data_length / sizeof(float);
    if (num_samples == 0) {
        buffer->Unlock();
        return;
    }
    
    mix_buffer.resize(num_samples);
    
    // Copy audio data
    float* src = reinterpret_cast<float*>(data);
    for (size_t i = 0; i < num_samples; i++) {
        mix_buffer.set(i, src[i]);
    }
    
    // Debug output
    UtilityFunctions::print_verbose("Processing audio: time=" + String::num_real(sample_time) +
                                   ", samples=" + itos(num_samples) +
                                   ", engine_time=" + String::num_real(state.engine_time));
    
    // Mix the audio using the base class method (from FrameQueueVideoStream)
    mix_audio(num_samples / channels, mix_buffer);
    
    // Unlock the buffer
    buffer->Unlock();
}

// Replace the original update_frame_queue with this improved version
void VideoStreamPlaybackWMF::update_frame_queue(double p_delta) {
    if (!source_reader) return;
    
    // Process frames based on buffering needs
    if (frame_queue.should_decode(state.engine_time, state.fps)) {
        process_frame_queue();
        
        // Process audio here instead of in _mix_audio
        if (audio_media_type) {
            process_audio_samples_for_sync();
        }
    }
    
    // Update our internal playback state using the actual media time
    // This helps keep audio and video in sync
    double media_time = get_media_time();
    if (std::abs(media_time - state.engine_time) > 0.1) {
        // If our internal time has drifted too far, correct it
        UtilityFunctions::print_verbose("Correcting time drift: Engine=" + 
                                       String::num_real(state.engine_time) + 
                                       ", Media=" + String::num_real(media_time));
        state.engine_time = media_time;
    }
}

// -----------------------------------------------------------------------------
// Public Interface
// -----------------------------------------------------------------------------
void VideoStreamPlaybackWMF::set_file(const String &p_file) {
    file_name = p_file;
    
    Ref<FileAccess> file = FileAccess::open(p_file, FileAccess::READ);
    ERR_FAIL_COND_MSG(file.is_null(), "Cannot open file '" + p_file + "'.");
    
    clear_wmf_objects();
    
    if (!setup_wmf_pipeline(file->get_path_absolute())) {
        clear_wmf_objects();
        ERR_FAIL_MSG("Failed to setup WMF pipeline for '" + p_file + "'.");
    }
}

// -----------------------------------------------------------------------------
// Getters
// -----------------------------------------------------------------------------
bool VideoStreamPlaybackWMF::_is_playing() const { 
    return state.playing; 
}

bool VideoStreamPlaybackWMF::_is_paused() const { 
    return state.paused; 
}

double VideoStreamPlaybackWMF::_get_length() const {
    return wmf_time_to_seconds(duration);
}

double VideoStreamPlaybackWMF::get_media_time() const {
    if (!source_reader) return 0.0;
    
    // If we're actively playing, use the engine time for a smoother experience
    if (state.playing && !state.paused) {
        return state.engine_time;
    }
    
    // For paused or stopped states, try to get the precise position
    // Note: There's no direct way to get the position from source reader
    // MF_PD_POSITION is not a valid attribute for source reader
    // Instead, we'll rely on our tracked time
    
    return last_frame_time;
}

double VideoStreamPlaybackWMF::_get_playback_position() const {
    return get_media_time();
}

Ref<Texture2D> VideoStreamPlaybackWMF::_get_texture() const { 
    return texture; 
}

// -----------------------------------------------------------------------------
// Audio Handling
// -----------------------------------------------------------------------------
void VideoStreamPlaybackWMF::_set_audio_track(int p_idx) {
    // Set the audio track index but do not apply it dynamically
    audio_track = p_idx;
}

int VideoStreamPlaybackWMF::_get_channels() const {
    return channels;
}

int VideoStreamPlaybackWMF::_get_mix_rate() const {
    return mix_rate;
}

void VideoStreamPlaybackWMF::_stop() {
    if (!source_reader) return;
    
    // Reset to beginning
    _seek(0.0);
    frame_queue.clear();
    state.playing = false;
    state.paused = false;
    state.engine_time = 0.0;
}

void VideoStreamPlaybackWMF::_set_paused(bool p_paused) {
    if (state.paused == p_paused) return;
    
    state.paused = p_paused;
    // No need to do anything with WMF, we control playback through our read loop
}

// -----------------------------------------------------------------------------
// Helper Functions
// -----------------------------------------------------------------------------
void VideoStreamPlaybackWMF::ensure_frame_buffer(size_t width, size_t height) {
    size_t required_size = width * height * 4;
    if (frame_buffer.size() != required_size) {
        frame_buffer.resize(required_size);
    }
}

void VideoStreamPlaybackWMF::detect_framerate() {
    if (!video_media_type) {
        state.fps = 30.0f; // Default
        return;
    }
    
    // Try to get frame rate from media type
    UINT32 numerator, denominator;
    HRESULT hr = MFGetAttributeRatio(video_media_type.Get(), MF_MT_FRAME_RATE, &numerator, &denominator);
    if (SUCCEEDED(hr) && denominator != 0) {
        state.fps = static_cast<float>(numerator) / static_cast<float>(denominator);
        UtilityFunctions::print_verbose("Detected framerate: " + String::num(state.fps));
    } else {
        state.fps = 30.0f; // Default
        UtilityFunctions::print_verbose("Using default framerate: 30.0");
    }
}

// -----------------------------------------------------------------------------
// ResourceFormatLoaderWMF Implementation
// -----------------------------------------------------------------------------
Variant ResourceFormatLoaderWMF::_load(const String &p_path,
                                       const String &p_original_path,
                                       bool p_use_sub_threads,
                                       int32_t p_cache_mode) const {
    VideoStreamWMF *stream = memnew(VideoStreamWMF);
    stream->set_file(p_path);
    
    Ref<VideoStreamWMF> wmf_stream = Ref<VideoStreamWMF>(stream);
    
    return {wmf_stream};
}

PackedStringArray ResourceFormatLoaderWMF::_get_recognized_extensions() const {
    PackedStringArray arr;
    
    arr.push_back("mp4");
    arr.push_back("wmv");
    arr.push_back("avi");
    arr.push_back("mov");
    arr.push_back("mkv");
    
    return arr;
}

bool ResourceFormatLoaderWMF::_handles_type(const StringName &p_type) const {
    return ClassDB::is_parent_class(p_type, "VideoStream");
}

String ResourceFormatLoaderWMF::_get_resource_type(const String &p_path) const {
    String ext = p_path.get_extension().to_lower();
    if (ext == "mp4" || ext == "wmv" || ext == "avi" || ext == "mov" || ext == "mkv") {
        return "VideoStreamWMF";
    }
    
    return "";
}

bool VideoStreamPlaybackWMF::check_end_of_stream() {
    if (!source_reader) return true;
    
    // Check if we've reached the end of the stream by comparing 
    // the current time with the duration
    double media_time = get_media_time();
    double duration_sec = wmf_time_to_seconds(duration);
    
    // Consider the stream ended if:
    // 1. We've reached the end of the stream duration
    // 2. Or we're past the last frame and the queue is empty (for cases where duration isn't accurate)
    return (media_time >= duration_sec && duration_sec > 0.0) || 
           (frame_queue.empty() && media_time > last_frame_time + 0.5);
}
