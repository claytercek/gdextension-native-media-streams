#pragma once
#include "video_frame.hpp"
#include "audio_frame.hpp"
#include <deque>
#include <mutex>
#include <optional>

namespace godot {

/**
 * Thread-safe generic queue for media frames.
 * Can be specialized for video or audio frames.
 */
template<typename FrameType>
class MediaFrameQueue {
public:
    static constexpr size_t DEFAULT_MAX_SIZE = 5;

private:
    std::deque<FrameType> frames;
    mutable std::mutex mutex;
    size_t max_size;
    
public:
    explicit MediaFrameQueue(size_t max_queue_size = DEFAULT_MAX_SIZE) 
        : max_size(max_queue_size) {}
    
    // Check if the queue is empty
    bool empty() const {
        const std::lock_guard<std::mutex> lock(mutex);
        return frames.empty();
    }
    
    // Get the current size of the queue
    size_t size() const {
        const std::lock_guard<std::mutex> lock(mutex);
        return frames.size();
    }
    
    // Push a new frame onto the queue (move semantics)
    void push(FrameType&& frame) {
        const std::lock_guard<std::mutex> lock(mutex);
        frames.push_back(std::move(frame));
        
        // Maintain max size by discarding oldest frames when full
        while (frames.size() > max_size) {
            frames.pop_front();
        }
    }
    
    // Try to get a frame at or before the specified time
    std::optional<FrameType> try_pop_frame_at_time(double current_time) {
        const std::lock_guard<std::mutex> lock(mutex);
        
        if (frames.empty()) return std::nullopt;
        
        // Get the front frame
        const FrameType& frame = frames.front();
        
        // If the frame should be presented now or earlier, return it
        if (frame.presentation_time <= current_time) {
            FrameType result = std::move(frame);
            frames.pop_front();
            return result;
        }
        
        return std::nullopt;
    }
    
    // Peek at the next frame without removing it
    std::optional<FrameType> peek_next_frame() const {
        const std::lock_guard<std::mutex> lock(mutex);
        
        if (frames.empty()) return std::nullopt;
        
        return frames.front();
    }
    
    // Get the timestamp of the latest frame in the queue
    std::optional<double> get_latest_timestamp() const {
        const std::lock_guard<std::mutex> lock(mutex);
        
        if (frames.empty()) return std::nullopt;
        
        return frames.back().presentation_time;
    }
    
    // Clear all frames from the queue
    void clear() {
        const std::lock_guard<std::mutex> lock(mutex);
        frames.clear();
    }
    
    // Check if we should decode more frames based on current time and playback rate
    bool should_buffer_more_frames(double current_time, float playback_rate = 1.0f) const {
        const std::lock_guard<std::mutex> lock(mutex);
        
        // If empty, we definitely need more frames
        if (frames.empty()) return true;
        
        // Get the timestamp of the last frame in the queue
        double latest_time = frames.back().presentation_time;
        
        // Calculate buffer ahead time based on playback rate
        // (higher playback rates need more buffering)
        double buffer_time = 0.5 * playback_rate;
        
        // Buffer more if we don't have enough ahead
        return (latest_time - current_time) < buffer_time;
    }
};

// Type aliases for common frame queue types
using VideoFrameQueue = MediaFrameQueue<VideoFrame>;
using AudioFrameQueue = MediaFrameQueue<AudioFrame>;

} // namespace godot