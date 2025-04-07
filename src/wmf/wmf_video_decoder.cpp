#include "wmf_video_decoder.hpp"

namespace godot {

WMFVideoDecoder::WMFVideoDecoder() {
    // Constructor
}

WMFVideoDecoder::~WMFVideoDecoder() {
    clear();
}

void WMFVideoDecoder::clear() {
    video_media_type = nullptr;
    d3d_device = nullptr;
    d3d_context = nullptr;
    frame_buffer.clear();
}

LONGLONG WMFVideoDecoder::time_to_wmf_time(double time) {
    // Convert seconds to 100-nanosecond units
    return static_cast<LONGLONG>(time * 10000000);
}

double WMFVideoDecoder::wmf_time_to_seconds(LONGLONG wmf_time) {
    // Convert 100-nanosecond units to seconds
    return static_cast<double>(wmf_time) / 10000000.0;
}

size_t WMFVideoDecoder::align_dimension(size_t dim, size_t alignment) const {
    return (dim + alignment - 1) & ~(alignment - 1);
}

bool WMFVideoDecoder::setup_video_stream(ComPtr<IMFSourceReader>& source_reader) {
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
    dimensions.x = width;
    dimensions.y = height;
    aligned_width = align_dimension(width);
    aligned_height = align_dimension(height);
    
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
    
    // Initialize frame buffer
    ensure_frame_buffer(dimensions.x, dimensions.y);
    
    // Detect framerate
    detect_framerate();
    
    return true;
}

void WMFVideoDecoder::detect_framerate() {
    if (!video_media_type) {
        framerate = 30.0f; // Default
        return;
    }
    
    // Try to get frame rate from media type
    UINT32 numerator, denominator;
    HRESULT hr = MFGetAttributeRatio(video_media_type.Get(), MF_MT_FRAME_RATE, &numerator, &denominator);
    if (SUCCEEDED(hr) && denominator != 0) {
        framerate = static_cast<float>(numerator) / static_cast<float>(denominator);
        UtilityFunctions::print_verbose("Detected framerate: " + String::num(framerate));
    } else {
        framerate = 30.0f; // Default
        UtilityFunctions::print_verbose("Using default framerate: 30.0");
    }
}

void WMFVideoDecoder::ensure_frame_buffer(size_t width, size_t height) {
    size_t required_size = width * height * 4;
    if (frame_buffer.size() < required_size) {
        frame_buffer.resize(required_size);
    }
}

void WMFVideoDecoder::process_frames(ComPtr<IMFSourceReader>& source_reader, FrameQueue& frame_queue) {
    if (!source_reader || !video_media_type) return;
    
    // Only read more frames if our queue isn't full
    while (frame_queue.size() < FrameQueue::MAX_SIZE) {
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
        frame.size = dimensions;
        frame.presentation_time = wmf_time_to_seconds(timestamp);
        
        // Convert BGRA to RGBA
        frame.data.resize(dimensions.x * dimensions.y * 4);
        uint8_t* src = data;
        uint8_t* dst = frame.data.data();
        
        for (int y = 0; y < dimensions.y; y++) {
            for (int x = 0; x < dimensions.x; x++) {
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
    }
}

} // namespace godot