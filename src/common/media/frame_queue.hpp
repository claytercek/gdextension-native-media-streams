#pragma once
#include "video_frame.hpp"
#include "audio_frame.hpp"
#include <deque>
#include <mutex>
#include <optional>
#include <condition_variable>

namespace godot {

/**
 * Thread-safe generic queue for media frames.
 * Can be specialized for video or audio frames.
 * Supports blocking operations for producer-consumer pattern.
 */
template<typename FrameType>
class MediaFrameQueue {
public:
    // Default max frames to buffer
    static constexpr size_t DEFAULT_MAX_SIZE = 10;

private:
    std::deque<FrameType> frames;
    mutable std::mutex mutex;
    std::condition_variable cv_not_full;
    std::condition_variable cv_not_empty;
    size_t max_size;
    bool abort_flag = false;
    
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
    
    size_t max_queue_size() const {
        return max_size;
    }
    
    // Push a new frame onto the queue (move semantics) - non-blocking, drops oldest when full
    void push(FrameType&& frame) {
        const std::lock_guard<std::mutex> lock(mutex);
        frames.push_back(std::move(frame));
        
        // Maintain max size by discarding oldest frames when full
        while (frames.size() > max_size) {
            frames.pop_front();
        }
        
        // Notify consumers that new data is available
        cv_not_empty.notify_one();
    }
    
    // Push a new frame and block if queue is full (for producer threads)
    bool push_blocking(FrameType&& frame, std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        std::unique_lock<std::mutex> lock(mutex);
        
        // Wait until the queue has space or timeout
        bool not_full = cv_not_full.wait_for(lock, timeout, [this] { 
            return frames.size() < max_size || abort_flag; 
        });
        
        if (abort_flag) return false;
        if (!not_full) return false; // Timed out
        
        frames.push_back(std::move(frame));
        cv_not_empty.notify_one();
        return true;
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
            cv_not_full.notify_one();
            return result;
        }
        
        return std::nullopt;
    }
    
    // Pop a frame (blocking) - used by decoder threads
    std::optional<FrameType> pop_blocking(std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        std::unique_lock<std::mutex> lock(mutex);
        
        // Wait until the queue has data or timeout
        bool not_empty = cv_not_empty.wait_for(lock, timeout, [this] { 
            return !frames.empty() || abort_flag; 
        });
        
        if (abort_flag) return std::nullopt;
        if (frames.empty()) return std::nullopt; // Either timeout or spurious wakeup
        
        FrameType result = std::move(frames.front());
        frames.pop_front();
        cv_not_full.notify_one();
        return result;
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
    
    // Signal all waiting threads to abort
    void abort() {
        const std::lock_guard<std::mutex> lock(mutex);
        abort_flag = true;
        cv_not_empty.notify_all();
        cv_not_full.notify_all();
    }
    
    // Reset abort flag
    void reset() {
        const std::lock_guard<std::mutex> lock(mutex);
        abort_flag = false;
    }
    
    // Clear all frames from the queue
    void clear() {
        const std::lock_guard<std::mutex> lock(mutex);
        frames.clear();
        // cv_not_full.notify_all();
    }
};

// Type aliases for common frame queue types
using VideoFrameQueue = MediaFrameQueue<VideoFrame>;
using AudioFrameQueue = MediaFrameQueue<AudioFrame>;

} // namespace godot