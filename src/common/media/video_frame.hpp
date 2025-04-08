#pragma once
#include <vector>
#include <godot_cpp/variant/vector2i.hpp>

namespace godot {

/**
 * Platform-agnostic representation of a video frame.
 * Provides a common structure for passing video frames between components.
 */
struct VideoFrame {
    // Frame pixel data (RGBA8 format)
    std::vector<uint8_t> data;
    
    // Presentation timestamp in seconds
    double presentation_time{0.0};
    
    // Frame dimensions (width, height)
    Vector2i size;
    
    // Default constructor
    VideoFrame() = default;
    
    // Copy constructor
    VideoFrame(const VideoFrame& other)
        : data(other.data)
        , presentation_time(other.presentation_time)
        , size(other.size) {}
    
    // Copy assignment operator 
    VideoFrame& operator=(const VideoFrame& other) {
        if (this != &other) {
            data = other.data;
            presentation_time = other.presentation_time;
            size = other.size;
        }
        return *this;
    }
    
    // Move constructor for efficient handling
    VideoFrame(VideoFrame&& other) noexcept
        : data(std::move(other.data))
        , presentation_time(other.presentation_time)
        , size(other.size) {}
    
    // Move assignment operator
    VideoFrame& operator=(VideoFrame&& other) noexcept {
        if (this != &other) {
            data = std::move(other.data);
            presentation_time = other.presentation_time;
            size = other.size;
        }
        return *this;
    }
};

} // namespace godot