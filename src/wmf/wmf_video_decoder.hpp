#pragma once
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/classes/video_stream_playback.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <vector>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <d3d11.h>

#include "../common/video_frame.hpp"
#include "../common/frame_queue.hpp"

using Microsoft::WRL::ComPtr;

namespace godot {

class WMFVideoDecoder {
public:
    WMFVideoDecoder();
    ~WMFVideoDecoder();

    // Setup methods
    bool setup_video_stream(ComPtr<IMFSourceReader>& source_reader);
    void clear();
    
    // Video frame processing
    void process_frames(ComPtr<IMFSourceReader>& source_reader, FrameQueue& frame_queue);
    void detect_framerate();
    
    // Getters
    Vector2i get_dimensions() const { return dimensions; }
    float get_framerate() const { return framerate; }
    
    // Time conversion utilities
    static LONGLONG time_to_wmf_time(double time);
    static double wmf_time_to_seconds(LONGLONG wmf_time);

private:
    // Media types
    ComPtr<IMFMediaType> video_media_type;
    
    // DirectX resources for hardware acceleration (future use)
    ComPtr<ID3D11Device> d3d_device;
    ComPtr<ID3D11DeviceContext> d3d_context;
    
    // Video properties
    Vector2i dimensions;
    size_t aligned_width{0};
    size_t aligned_height{0};
    float framerate{30.0f};
    
    // Buffer management
    std::vector<uint8_t> frame_buffer;
    
    // Helper methods
    void ensure_frame_buffer(size_t width, size_t height);
    size_t align_dimension(size_t dim, size_t alignment = 16) const;
};

} // namespace godot