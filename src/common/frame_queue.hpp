#pragma once
#include "video_frame.hpp"
#include <deque>
#include <mutex>
#include <optional>

namespace godot {

/**
 * Thread-safe frame queue for managing video frame buffering
 * and presentation timing across all platforms
 */
class FrameQueue {
public:
    static const size_t MAX_SIZE = 3;

private:
    std::deque<VideoFrame> frames;
    mutable std::mutex mutex;

public:
    bool empty() const {
        const std::lock_guard<std::mutex> lock(mutex);
        return frames.empty();
    }
    
    size_t size() const {
        const std::lock_guard<std::mutex> lock(mutex);
        return frames.size();
    }
    
    void push(VideoFrame&& frame) {
        const std::lock_guard<std::mutex> lock(mutex);
        frames.push_back(std::move(frame));
        while (frames.size() > MAX_SIZE) {
            frames.pop_front();
        }
    }
    
    /**
     * Pop the next frame from the queue that should be presented.
     * Returns nullptr if no frame is ready for presentation.
     */
    const std::optional<VideoFrame> try_pop_next_frame(double current_time) {
        const std::lock_guard<std::mutex> lock(mutex);

        if (frames.empty()) return std::nullopt;

        const VideoFrame& frame = frames.front();
        if (frame.presentation_time <= current_time) {
            VideoFrame next_frame = std::move(frame);
            frames.pop_front();
            return next_frame;
        }

        return std::nullopt;
    }
    
    void clear() {
        const std::lock_guard<std::mutex> lock(mutex);
        frames.clear();
    }
    
    bool should_decode(double current_time, float fps) const {
        const std::lock_guard<std::mutex> lock(mutex);
        if (frames.empty()) return true;
        double next_frame_time = frames.back().presentation_time + (1.0 / fps);
        return (next_frame_time - current_time) < 0.5; // TODO: make this 0.5 sec buffer configurable
    }
};

} // namespace godot
