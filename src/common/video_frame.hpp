#pragma once
#include <godot_cpp/variant/vector2i.hpp>
#include <vector>
#include <memory_resource>

namespace godot {

/**
 * Platform-agnostic representation of a video frame
 */
struct VideoFrame {
    std::vector<uint8_t, std::pmr::polymorphic_allocator<uint8_t>> data;
    double presentation_time{0.0};
    Vector2i size;
    
    VideoFrame(std::pmr::polymorphic_allocator<uint8_t>& alloc) 
        : data(alloc) {}
    VideoFrame() : data(std::pmr::polymorphic_allocator<uint8_t>{}) {}
};

/**
 * Memory management for video frames using a monotonic buffer
 */
class VideoFramePool {
    static constexpr size_t BLOCK_SIZE = 1024 * 1024;
    struct PoolState {
        std::unique_ptr<std::pmr::monotonic_buffer_resource> resource;
        std::pmr::polymorphic_allocator<uint8_t> allocator;
        
        PoolState() : 
            resource(std::make_unique<std::pmr::monotonic_buffer_resource>(BLOCK_SIZE)),
            allocator(resource.get()) {}
    };
    
    std::unique_ptr<PoolState> state;
    
public:
    VideoFramePool() : state(std::make_unique<PoolState>()) {}
    
    auto get_allocator() { return state->allocator; }
    void reset() {
        state = std::make_unique<PoolState>();
    }
};

} // namespace godot
