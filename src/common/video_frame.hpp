#pragma once
#include <godot_cpp/variant/vector2i.hpp>
#include <vector>

namespace godot {

/**
 * Platform-agnostic representation of a video frame
 */
struct VideoFrame {
    std::vector<uint8_t> data;
    double presentation_time{0.0};
    Vector2i size;
};

} // namespace godot
