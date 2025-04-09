#include "wmf_player.hpp"
#include <godot_cpp/variant/utility_functions.hpp>
#include <mferror.h>
#include <shlwapi.h>
#include <algorithm>

// Link necessary libraries
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "shlwapi.lib")

namespace godot {

// Helper function to convert HRESULT to readable string for debugging
static String hr_to_string(HRESULT hr) {
    return "0x" + String::num_int64(hr, 16);
}

WMFPlayer::WMFPlayer() {
    // Initialize WMF
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to initialize Windows Media Foundation: " + hr_to_string(hr));
    }
}

WMFPlayer::~WMFPlayer() {
    // Close any open media
    close();
    
    // Shutdown WMF
    MFShutdown();
}

bool WMFPlayer::open(const std::string& file_path) {
    close(); // Close any previous media
    
    if (file_path.empty()) {
        UtilityFunctions::printerr("Empty file path provided to WMFPlayer");
        return false;
    }
    
    UtilityFunctions::print("Loading media: " + String(file_path.c_str()));
    
    // Configure the source reader with the provided path - it should already be an absolute path
    // from VideoStreamPlaybackWMF::initialize()
    if (!configure_source_reader(file_path)) {
        UtilityFunctions::printerr("Failed to configure source reader for: " + String(file_path.c_str()));
        return false;
    }
    
    // Configure video and audio streams
    bool has_video = configure_video_stream();
    if (!has_video) {
        UtilityFunctions::print("No video stream found or failed to configure video");
        // Not fatal, some media might be audio-only
    }
    
    bool has_audio = configure_audio_stream();
    if (!has_audio) {
        UtilityFunctions::print("No audio stream found or failed to configure audio");
        // Not fatal, some media might be video-only
    }
    
    if (!has_video && !has_audio) {
        UtilityFunctions::printerr("Failed to configure any streams. Media file may be corrupted or unsupported.");
        return false;
    }
    
    current_state = State::STOPPED;
    return true;
}

void WMFPlayer::close() {
    // Release the source reader
    source_reader.Reset();
    
    // Reset media info
    media_info = MediaInfo();
    current_state = State::STOPPED;
    current_audio_track = 0;
    
    // Reset position tracking
    last_video_position = 0.0;
    last_audio_position = 0.0;
}

bool WMFPlayer::is_open() const {
    return source_reader != nullptr;
}

void WMFPlayer::play() {
    if (!is_open()) return;
    
    current_state = State::PLAYING;
}

void WMFPlayer::pause() {
    if (!is_open()) return;
    
    if (current_state == State::PLAYING) {
        current_state = State::PAUSED;
    }
}

void WMFPlayer::stop() {
    if (!is_open()) return;
    
    current_state = State::STOPPED;
    
    // Seek to beginning
    seek(0.0);
}

void WMFPlayer::seek(double time_sec) {
    if (!is_open()) return;
    
    // Create PROPVARIANT with 100ns precision timestamp for WMF
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = MediaTime::seconds_to_wmf_time(time_sec);
    
    // Seek the source reader
    HRESULT hr = source_reader->SetCurrentPosition(GUID_NULL, var);
    PropVariantClear(&var);
    
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to seek to position: " + String::num_real(time_sec) + 
                                  " seconds. Error: " + hr_to_string(hr));
        return;
    }
    
    // Update last positions
    last_video_position = time_sec;
    last_audio_position = time_sec;
}

WMFPlayer::State WMFPlayer::get_state() const {
    return current_state;
}

bool WMFPlayer::is_playing() const {
    return current_state == State::PLAYING;
}

bool WMFPlayer::is_paused() const {
    return current_state == State::PAUSED;
}

bool WMFPlayer::has_ended() const {
    // Check if we've reached the end of the media
    if (!is_open() || current_state == State::STOPPED) {
        return false;
    }
    
    // If we have a duration, check if we're near the end
    if (media_info.duration > 0.0) {
        // Consider ended if we're within 0.1 seconds of the end
        return last_video_position >= (media_info.duration - 0.1);
    }
    
    return false;
}

IMediaPlayer::MediaInfo WMFPlayer::get_media_info() const {
    return media_info;
}

double WMFPlayer::get_position() const {
    return last_video_position;
}

bool WMFPlayer::configure_source_reader(const std::string& file_path) {
    if (file_path.empty()) return false;

    
    wchar_t wide_path[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, file_path.c_str(), -1, wide_path, MAX_PATH);;
    
    // Create source reader from URL
    ComPtr<IMFSourceReader> reader;
    
    // Setup source reader configuration attributes
    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 4);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to create source reader attributes: " + hr_to_string(hr));
        return false;
    }
    
    // Enable hardware decoding (important for better performance and compatibility)
    hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
    if (FAILED(hr)) {
        UtilityFunctions::print("Warning: Failed to enable advanced video processing");
        // Continue anyway, hardware acceleration is optional
    }
    
    // Set low-latency hint
    hr = attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
    if (FAILED(hr)) {
        UtilityFunctions::print("Warning: Failed to set low latency hint");
        // Continue anyway
    }
    
    // Create source reader
    hr = MFCreateSourceReaderFromURL(wide_path, attributes.Get(), &reader);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to create source reader from URL: " + hr_to_string(hr) + 
                                  " - File may not exist or be accessible, or format may be unsupported");
        return false;
    }
    
    // Store the source reader
    source_reader = reader;
    
    // Get duration from the media source
    // SourceReader doesn't have GetPresentationDescriptor directly
    // We need to get the source from the reader and then get the descriptor
    ComPtr<IMFMediaSource> media_source;
    hr = reader->GetServiceForStream(MF_SOURCE_READER_MEDIASOURCE, GUID_NULL, IID_PPV_ARGS(&media_source));
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to get media source: " + hr_to_string(hr));
        return false;
    }
    
    ComPtr<IMFPresentationDescriptor> pd;
    hr = media_source->CreatePresentationDescriptor(&pd);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to create presentation descriptor: " + hr_to_string(hr));
        return false;
    }
    
    UINT64 duration_100ns;
    hr = pd->GetUINT64(MF_PD_DURATION, &duration_100ns);
    if (SUCCEEDED(hr)) {
        media_info.duration = MediaTime::wmf_time_to_seconds(duration_100ns);
    } else {
        UtilityFunctions::print("Warning: Failed to get media duration");
        media_info.duration = 0.0;
    }
    
    // Get stream count to verify we have streams to process
    DWORD stream_count = 0;
    hr = pd->GetStreamDescriptorCount(&stream_count);
    if (SUCCEEDED(hr)) {
        UtilityFunctions::print_verbose("Media file has " + String::num_int64(stream_count) + " streams");
    }
    
    // Deselect all streams by default
    hr = source_reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to deselect all streams: " + hr_to_string(hr));
        // Continue anyway
    }
    
    return true;
}

bool WMFPlayer::configure_video_stream() {
    if (!source_reader) return false;
    
    try {
        // Select the first video stream
        HRESULT hr = source_reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
        if (FAILED(hr)) {
            UtilityFunctions::print("No video stream available");
            return false;
        }
        
        // Get native media type for video
        ComPtr<IMFMediaType> native_type;
        hr = source_reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &native_type);
        if (FAILED(hr)) {
            UtilityFunctions::printerr("Failed to get native video media type: " + hr_to_string(hr));
            source_reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, FALSE);
            return false;
        }
        
        // Get and display the native subtype (format)
        GUID native_subtype;
        hr = native_type->GetGUID(MF_MT_SUBTYPE, &native_subtype);
        if (SUCCEEDED(hr)) {
            // Check for common video formats
            if (native_subtype == MFVideoFormat_H264) {
                UtilityFunctions::print_verbose("Native video format: H264");
            } else if (native_subtype == MFVideoFormat_HEVC) {
                UtilityFunctions::print_verbose("Native video format: HEVC/H265");
            } else if (native_subtype == MFVideoFormat_MJPG) {
                UtilityFunctions::print_verbose("Native video format: MJPEG");
            } else if (native_subtype == MFVideoFormat_MP4V) {
                UtilityFunctions::print_verbose("Native video format: MPEG-4 Video");
            } else {
                UtilityFunctions::print_verbose("Native video format: Other/Unknown");
            }
        }
        
        // Get video dimensions
        UINT32 width = 0, height = 0;
        hr = MFGetAttributeSize(native_type.Get(), MF_MT_FRAME_SIZE, &width, &height);
        if (FAILED(hr) || width == 0 || height == 0) {
            UtilityFunctions::printerr("Failed to get valid video dimensions: " + hr_to_string(hr));
            source_reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, FALSE);
            return false;
        }
        
        // Store dimensions
        media_info.width = width;
        media_info.height = height;
        
        UtilityFunctions::print_verbose("Native video dimensions: " + 
            String::num_int64(media_info.width) + "x" + String::num_int64(media_info.height));
        
        // Get frame rate
        UINT32 numerator = 0, denominator = 1;
        hr = MFGetAttributeRatio(native_type.Get(), MF_MT_FRAME_RATE, &numerator, &denominator);
        if (SUCCEEDED(hr) && denominator > 0) {
            media_info.framerate = static_cast<float>(numerator) / static_cast<float>(denominator);
        } else {
            media_info.framerate = 30.0f; // Default
        }

        UtilityFunctions::print_verbose("Native video framerate: " + 
            String::num_real(media_info.framerate) + " FPS");
        
        // Create output media type
        ComPtr<IMFMediaType> output_type;
        hr = MFCreateMediaType(&output_type);
        if (FAILED(hr)) {
            UtilityFunctions::printerr("Failed to create video media type: " + hr_to_string(hr));
            source_reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, FALSE);
            return false;
        }
        
        // copy all attributes from native type
        hr = native_type->CopyAllItems(output_type.Get());
        if (FAILED(hr)) {
            UtilityFunctions::printerr("Failed to copy media type attributes: " + hr_to_string(hr));
            // Continue anyway, set essential attributes manually
        }
        
        // Set major type
        output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        
        // Set frame size
        hr = MFSetAttributeSize(output_type.Get(), MF_MT_FRAME_SIZE, media_info.width, media_info.height);
        if (FAILED(hr)) {
            UtilityFunctions::printerr("Failed to set frame size: " + hr_to_string(hr));
            // Continue anyway
        }
        
        // Set frame rate if available
        if (numerator > 0 && denominator > 0) {
            hr = MFSetAttributeRatio(output_type.Get(), MF_MT_FRAME_RATE, numerator, denominator);
            if (FAILED(hr)) {
                UtilityFunctions::printerr("Failed to set frame rate: " + hr_to_string(hr));
                // Not critical
            }
        }
        
        // Set pixel aspect ratio (typically 1:1)
        hr = MFSetAttributeRatio(output_type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (FAILED(hr)) {
            UtilityFunctions::printerr("Failed to set pixel aspect ratio: " + hr_to_string(hr));
            // Not critical
        }
        
        // Ensure all samples are independent (important for easier decoding)
        hr = output_type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        if (FAILED(hr)) {
            UtilityFunctions::printerr("Failed to set independent samples flag: " + hr_to_string(hr));
            // Not critical
        }
        
        // Try different formats in order of preference:
        // 1. RGB32 (most compatible with Godot's RGBA8)
        bool format_set = false;
        
        // Try RGB32 first
        hr = output_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(hr)) {
            hr = source_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, output_type.Get());
            if (SUCCEEDED(hr)) {
                UtilityFunctions::print("Using RGB32 format for video");
                format_set = true;
            } else {
                UtilityFunctions::printerr("Failed to set RGB32 media type: " + hr_to_string(hr));
            }
        }
        
        // Try RGB24 if RGB32 failed
        if (!format_set) {
            hr = output_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB24);
            if (SUCCEEDED(hr)) {
                hr = source_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, output_type.Get());
                if (SUCCEEDED(hr)) {
                    UtilityFunctions::print("Using RGB24 format for video");
                    format_set = true;
                } else {
                    UtilityFunctions::printerr("Failed to set RGB24 media type: " + hr_to_string(hr));
                }
            }
        }
        
        // Try YUY2 if previous formats failed
        if (!format_set) {
            hr = output_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_YUY2);
            if (SUCCEEDED(hr)) {
                hr = source_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, output_type.Get());
                if (SUCCEEDED(hr)) {
                    UtilityFunctions::print("Using YUY2 format for video (will convert to RGB)");
                    format_set = true;
                } else {
                    UtilityFunctions::printerr("Failed to set YUY2 media type: " + hr_to_string(hr));
                }
            }
        }
        
        // Try NV12 as last resort (common intermediate format)
        if (!format_set) {
            hr = output_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
            if (SUCCEEDED(hr)) {
                hr = source_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, output_type.Get());
                if (SUCCEEDED(hr)) {
                    UtilityFunctions::print("Using NV12 format for video (will convert to RGB)");
                    format_set = true;
                } else {
                    UtilityFunctions::printerr("Failed to set NV12 media type: " + hr_to_string(hr));
                }
            }
        }
        
        if (!format_set) {
            UtilityFunctions::printerr("Could not set any supported video format. Video will be disabled.");
            source_reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, FALSE);
            return false;
        }
        
        // Get the actual media type that was set (might be different from what we requested)
        ComPtr<IMFMediaType> actual_type;
        hr = source_reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actual_type);
        if (SUCCEEDED(hr)) {
            // Get actual dimensions and update our info if needed
            UINT32 actual_width = 0, actual_height = 0;
            if (SUCCEEDED(MFGetAttributeSize(actual_type.Get(), MF_MT_FRAME_SIZE, &actual_width, &actual_height))) {
                if (actual_width != media_info.width || actual_height != media_info.height) {
                    UtilityFunctions::print("Actual video dimensions adjusted to: " + 
                        String::num_int64(actual_width) + "x" + String::num_int64(actual_height));
                    media_info.width = actual_width;
                    media_info.height = actual_height;
                }
            }
        }
        
        return true;
    } catch (...) {
        UtilityFunctions::printerr("Unexpected exception in configure_video_stream");
        source_reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, FALSE);
        return false;
    }
}

bool WMFPlayer::configure_audio_stream() {
    if (!source_reader) return false;
    
    // Select the first audio stream
    HRESULT hr = source_reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    if (FAILED(hr)) {
        UtilityFunctions::print("No audio stream available");
        return false;
    }
    
    // Get native media type for audio
    ComPtr<IMFMediaType> native_type;
    hr = source_reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &native_type);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to get native audio media type: " + hr_to_string(hr));
        return false;
    }
    
    // Get audio properties
    UINT32 channels = 0;
    UINT32 sample_rate = 0;
    
    hr = native_type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
    if (SUCCEEDED(hr) && channels > 0) {
        media_info.audio_channels = channels;
    } else {
        media_info.audio_channels = 2; // Default to stereo
    }
    
    hr = native_type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sample_rate);
    if (SUCCEEDED(hr) && sample_rate > 0) {
        media_info.audio_sample_rate = sample_rate;
    } else {
        media_info.audio_sample_rate = 44100; // Default to 44.1kHz
    }
    
    // Create a media type for PCM float audio output
    ComPtr<IMFMediaType> audio_type;
    hr = MFCreateMediaType(&audio_type);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to create audio media type: " + hr_to_string(hr));
        return false;
    }
    
    hr = audio_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set audio major type: " + hr_to_string(hr));
        return false;
    }
    
    // Setting up float PCM format
    hr = audio_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set audio subtype to float: " + hr_to_string(hr));
        return false;
    }
    
    hr = audio_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set audio bits per sample: " + hr_to_string(hr));
        return false;
    }
    
    hr = audio_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, media_info.audio_sample_rate);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set audio sample rate: " + hr_to_string(hr));
        return false;
    }
    
    hr = audio_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, media_info.audio_channels);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set audio channels: " + hr_to_string(hr));
        return false;
    }
    
    // Important PCM format details
    hr = audio_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, media_info.audio_channels * 4); // 4 bytes per float
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set audio block alignment: " + hr_to_string(hr));
        // Not critical
    }
    
    hr = audio_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 
                              media_info.audio_channels * 4 * media_info.audio_sample_rate);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set audio bytes per second: " + hr_to_string(hr));
        // Not critical
    }
    
    hr = audio_type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set independent samples flag: " + hr_to_string(hr));
        // Not critical
    }
    
    // Set the media type on the source reader
    hr = source_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, audio_type.Get());
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set audio media type: " + hr_to_string(hr));
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
            UtilityFunctions::print("Audio format configured as float PCM: " + String(is_float ? "YES" : "NO"));
        }
        
        UINT32 actual_channels;
        hr = actual_type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &actual_channels);
        if (SUCCEEDED(hr)) {
            UtilityFunctions::print("Audio channels: " + String::num_int64(actual_channels));
        }
        
        UINT32 actual_rate;
        hr = actual_type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &actual_rate);
        if (SUCCEEDED(hr)) {
            UtilityFunctions::print("Audio sample rate: " + String::num_int64(actual_rate) + " Hz");
        }
    }
    
    return true;
}

bool WMFPlayer::read_video_frame(VideoFrame& frame) {
    // Check if we have a valid source reader and if video is enabled
    if (!source_reader || current_state == State::STOPPED) return false;
    
    // Check if video is enabled by checking dimensions
    if (media_info.width <= 0 || media_info.height <= 0) {
        // Video is disabled, so return false to skip video processing
        return false;
    }
    
    try {
        DWORD stream_flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        
        // Read the next video sample
        HRESULT hr = source_reader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,                     // Flags
            nullptr,               // Actual stream index
            &stream_flags,         // Stream flags
            &timestamp,            // Timestamp
            &sample                // Sample
        );
        
        if (FAILED(hr)) {
            UtilityFunctions::printerr("Failed to read video sample: " + hr_to_string(hr));
            return false;
        }
        
        // Check for end of stream or format changes
        if (stream_flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            UtilityFunctions::print_verbose("End of video stream reached");
            return false;
        }
        
        if (stream_flags & MF_SOURCE_READERF_STREAMTICK) {
            UtilityFunctions::print_verbose("Stream tick detected, possible gap in the data");
            return false;
        }
        
        if (stream_flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            UtilityFunctions::print("Video format changed, reconfiguring...");
            if (!configure_video_stream()) {
                return false;
            }
        }
        
        // Check if we got a sample
        if (!sample) {
            return false;
        }
        
        // Extract the video data
        return extract_video_data(sample.Get(), frame);
    }
    catch (...) {
        UtilityFunctions::printerr("Exception in read_video_frame");
        return false;
    }
}

bool WMFPlayer::read_audio_frame(AudioFrame& frame) {
    if (!source_reader || media_info.audio_channels == 0 || current_state == State::STOPPED) {
        return false;
    }
    
    // Update frame audio format information
    frame.channels = media_info.audio_channels;
    frame.sample_rate = media_info.audio_sample_rate;
    
    DWORD stream_flags = 0;
    LONGLONG timestamp = 0;
    ComPtr<IMFSample> sample;
    
    // Read the next audio sample
    HRESULT hr = source_reader->ReadSample(
        MF_SOURCE_READER_FIRST_AUDIO_STREAM,
        0,                     // Flags
        nullptr,               // Actual stream index
        &stream_flags,         // Stream flags
        &timestamp,            // Timestamp
        &sample                // Sample
    );
    
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to read audio sample: " + hr_to_string(hr));
        return false;
    }
    
    // Check for end of stream or format changes
    if (stream_flags & MF_SOURCE_READERF_ENDOFSTREAM) {
        UtilityFunctions::print_verbose("End of audio stream reached");
        return false;
    }
    
    if (stream_flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
        UtilityFunctions::print("Audio format changed, reconfiguring...");
        if (!configure_audio_stream()) {
            return false;
        }
    }
    
    // Check if we got a sample
    if (!sample) {
        return false;
    }
    
    // Get the sample timestamp
    LONGLONG sample_time = 0;
    hr = sample->GetSampleTime(&sample_time);
    if (SUCCEEDED(hr)) {
        timestamp = sample_time;
    }
    
    // Convert to seconds
    frame.presentation_time = MediaTime::wmf_time_to_seconds(timestamp);
    last_audio_position = frame.presentation_time;
    
    // Extract the audio data
    if (!extract_audio_data(sample.Get(), frame)) {
        return false;
    }
    
    return true;
}

bool WMFPlayer::extract_video_data(IMFSample* sample, VideoFrame& frame) {
    if (!sample) return false;
    
    try {
        // Get the current video format to know how to interpret the buffer
        GUID video_format = GUID_NULL;
        ComPtr<IMFMediaType> current_type;
        HRESULT hr = source_reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &current_type);
        if (SUCCEEDED(hr)) {
            current_type->GetGUID(MF_MT_SUBTYPE, &video_format);
        }
        
        // Use ConvertToContiguousBuffer instead of GetBufferByIndex
        ComPtr<IMFMediaBuffer> buffer;
        hr = sample->ConvertToContiguousBuffer(&buffer);
        if (FAILED(hr)) {
            UtilityFunctions::printerr("Failed to convert to contiguous buffer: " + hr_to_string(hr));
            return false;
        }
        
        // Lock the buffer
        BYTE* data = nullptr;
        DWORD data_length = 0;
        hr = buffer->Lock(&data, nullptr, &data_length);
        if (FAILED(hr) || !data) {
            UtilityFunctions::printerr("Failed to lock media buffer: " + hr_to_string(hr));
            return false;
        }
        
        // Use RAII to ensure we unlock the buffer
        struct BufferUnlocker {
            IMFMediaBuffer* buffer;
            BufferUnlocker(IMFMediaBuffer* b) : buffer(b) {}
            ~BufferUnlocker() { if (buffer) buffer->Unlock(); }
        } unlocker(buffer.Get());
        
        // Get sample timestamp
        LONGLONG sample_time = 0;
        hr = sample->GetSampleTime(&sample_time);
        if (SUCCEEDED(hr)) {
            frame.presentation_time = MediaTime::wmf_time_to_seconds(sample_time);
            last_video_position = frame.presentation_time;
        }
        
        // Set frame dimensions
        frame.size.x = media_info.width;
        frame.size.y = media_info.height;
        
        if (frame.size.x <= 0 || frame.size.y <= 0) {
            UtilityFunctions::printerr("Invalid frame dimensions: " + 
                String::num_int64(frame.size.x) + "x" + String::num_int64(frame.size.y));
            return false;
        }
        
        // Create output buffer for RGBA format (4 bytes per pixel)
        frame.data.resize(frame.size.x * frame.size.y * 4);
        
        // Different conversion based on video format
        if (video_format == MFVideoFormat_RGB32) {
            uint8_t* src = data;
            uint8_t* dst = frame.data.data();

            // BGRA to RGBA conversion
            for (int y = 0; y < frame.size.y; y++) {
                for (int x = 0; x < frame.size.x; x++) {
                    dst[0] = src[2]; // R
                    dst[1] = src[1]; // G
                    dst[2] = src[0]; // B
                    dst[3] = src[3]; // A
                    
                    src += 4;
                    dst += 4;
                }
            }
        } else if (video_format == MFVideoFormat_RGB24) {
            // RGB24 (BGR) to RGBA conversion
            const int src_stride = frame.size.x * 3;
            
            for (int y = 0; y < frame.size.y; y++) {
                uint8_t* src = data + y * src_stride;
                uint8_t* dst = frame.data.data() + y * frame.size.x * 4;
                
                for (int x = 0; x < frame.size.x; x++) {
                    // BGR to RGBA conversion (safe because we know our buffer size)
                    dst[0] = src[2]; // R <- B
                    dst[1] = src[1]; // G <- G
                    dst[2] = src[0]; // B <- R
                    dst[3] = 255;    // A (opaque)
                    
                    src += 3;
                    dst += 4;
                }
            }
        } else if (video_format == MFVideoFormat_YUY2) {
            // YUY2 to RGBA conversion
            // YUY2 format packs 2 pixels into 4 bytes: [Y0, U0, Y1, V0]
            const int src_stride = (frame.size.x + 1) / 2 * 4; // YUY2 stride
            
            for (int y = 0; y < frame.size.y; y++) {
                uint8_t* src = data + y * src_stride;
                uint8_t* dst = frame.data.data() + y * frame.size.x * 4;
                
                for (int x = 0; x < frame.size.x; x += 2) {
                    // Process two pixels at once
                    int y0 = src[0]; // Y for pixel 0
                    int u  = src[1]; // U for both pixels
                    int y1 = src[2]; // Y for pixel 1
                    int v  = src[3]; // V for both pixels
                    
                    // Convert to RGB using BT.601 conversion
                    // First pixel
                    int c = y0 - 16;
                    int d = u - 128;
                    int e = v - 128;
                    
                    dst[0] = std::clamp(( 298 * c + 409 * e + 128) >> 8, 0, 255); // R
                    dst[1] = std::clamp(( 298 * c - 100 * d - 208 * e + 128) >> 8, 0, 255); // G
                    dst[2] = std::clamp(( 298 * c + 516 * d + 128) >> 8, 0, 255); // B
                    dst[3] = 255; // A
                    
                    // Second pixel (if within bounds)
                    if (x + 1 < frame.size.x) {
                    c = y1 - 16;
                    
                    dst[4] = std::clamp(( 298 * c + 409 * e + 128) >> 8, 0, 255); // R
                    dst[5] = std::clamp(( 298 * c - 100 * d - 208 * e + 128) >> 8, 0, 255); // G
                    dst[6] = std::clamp(( 298 * c + 516 * d + 128) >> 8, 0, 255); // B
                    dst[7] = 255; // A
                    }
                    
                    src += 4;     // Advance 4 bytes (2 pixels in YUY2)
                    dst += 8;     // Advance 8 bytes (2 pixels in RGBA)
                }
            }
        } else if (video_format == MFVideoFormat_NV12) {
            // NV12 to RGBA conversion
            // NV12 format has Y plane followed by interleaved U/V plane
            const int y_plane_size = frame.size.x * frame.size.y;
            const uint8_t* y_plane = data;
            const uint8_t* uv_plane = data + y_plane_size;
            const int chroma_stride = frame.size.x;
            
            for (int y = 0; y < frame.size.y; y++) {
                uint8_t* dst = frame.data.data() + y * frame.size.x * 4;
                const uint8_t* y_line = y_plane + y * frame.size.x;
                const uint8_t* uv_line = uv_plane + (y / 2) * chroma_stride;
                
                for (int x = 0; x < frame.size.x; x++) {
                    // Get Y value for this pixel
                    int y_val = y_line[x];
                    
                    // Get U/V values (shared by 2x2 pixel blocks)
                    int u_val = uv_line[(x/2) * 2];
                    int v_val = uv_line[(x/2) * 2 + 1];
                    
                    // YUV to RGB conversion
                    int c = y_val - 16;
                    int d = u_val - 128;
                    int e = v_val - 128;
                    
                    dst[0] = std::clamp(( 298 * c + 409 * e + 128) >> 8, 0, 255); // R
                    dst[1] = std::clamp(( 298 * c - 100 * d - 208 * e + 128) >> 8, 0, 255); // G
                    dst[2] = std::clamp(( 298 * c + 516 * d + 128) >> 8, 0, 255); // B
                    dst[3] = 255; // A
                    
                    dst += 4;
                }
            }
        } else {
            // Unknown format - just fill with a solid color to indicate an issue
            uint8_t* dst = frame.data.data();
            for (int y = 0; y < frame.size.y; y++) {
                for (int x = 0; x < frame.size.x; x++) {
                    *dst++ = 255; // R (red)
                    *dst++ = 0;   // G
                    *dst++ = 0;   // B
                    *dst++ = 255; // A
                }
            }
            UtilityFunctions::printerr("Unknown video format encountered in extract_video_data");
        }
        
        return true;
    } catch (...) {
        UtilityFunctions::printerr("Exception in extract_video_data");
        return false;
    }
}

bool WMFPlayer::extract_audio_data(IMFSample* sample, AudioFrame& frame) {
    if (!sample) return false;
    
    // Get the buffer
    DWORD buffer_count = 0;
    HRESULT hr = sample->GetBufferCount(&buffer_count);
    if (FAILED(hr) || buffer_count == 0) {
        UtilityFunctions::printerr("No audio buffers in sample");
        return false;
    }
    
    ComPtr<IMFMediaBuffer> buffer;
    hr = sample->GetBufferByIndex(0, &buffer);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to get audio buffer by index");
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
            UtilityFunctions::printerr("Failed to lock 2D audio buffer");
            return false;
        }
        
        // Get buffer length from regular buffer interface
        hr = buffer->GetCurrentLength(&current_length);
        if (FAILED(hr)) {
            buffer2D->Unlock2D();
            UtilityFunctions::printerr("Failed to get 2D audio buffer length");
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
        
        // Copy the audio data into the frame
        frame.data.resize(num_samples);
        memcpy(frame.data.ptrw(), data, current_length);
        
        return true;
    } else {
        // Fall back to regular buffer
        DWORD max_length = 0;
        hr = buffer->Lock(&data, &max_length, &current_length);
        if (FAILED(hr) || !data) {
            UtilityFunctions::printerr("Failed to lock audio buffer");
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
        
        // Copy the audio data into the frame
        frame.data.resize(num_samples);
        memcpy(frame.data.ptrw(), data, current_length);
        
        return true;
    }
}

int WMFPlayer::get_audio_track_count() const {
    // For now return 1 if we have audio, 0 otherwise
    return media_info.audio_channels > 0 ? 1 : 0;
}

IMediaPlayer::TrackInfo WMFPlayer::get_audio_track_info(int track_index) const {
    TrackInfo info;
    info.index = track_index;
    info.language = ""; // WMF doesn't easily expose this
    info.name = "Audio Track " + std::to_string(track_index + 1);
    return info;
}

void WMFPlayer::set_audio_track(int track_index) {
    // Only one track supported for now
    current_audio_track = track_index;
}

int WMFPlayer::get_current_audio_track() const {
    return current_audio_track;
}

} // namespace godot