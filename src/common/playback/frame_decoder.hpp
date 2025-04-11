#pragma once
#include "../interfaces/media_player.hpp"
#include "../media/frame_queue.hpp"
#include <memory>
#include <atomic>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

/**
 * Abstract FrameDecoder interface - defines common API for frame decoders
 * regardless of their implementation details (threaded or non-threaded)
 */
class FrameDecoder {
public:
    virtual ~FrameDecoder() = default;
    
    // Start the decoder
    virtual void start() = 0;
    
    // Stop the decoder
    virtual void stop() = 0;
    
    // Pause decoding (used during seeking)
    virtual void pause() = 0;
    
    // Resume decoding after pause
    virtual void resume() = 0;
    
    // Check if decoder is running
    virtual bool is_running() const = 0;
    
    // Factory method to create appropriate decoder type
    static std::unique_ptr<FrameDecoder> create(
        std::unique_ptr<IMediaPlayer>& media_player,
        VideoFrameQueue& video_queue, 
        AudioFrameQueue& audio_queue,
        bool use_threading);
};

/**
 * Simple non-threaded decoder that synchronously decodes frames
 * directly on the calling thread.
 */
class SyncFrameDecoder : public FrameDecoder {
private:
    std::unique_ptr<IMediaPlayer>& media_player;
    VideoFrameQueue& video_queue;
    AudioFrameQueue& audio_queue;
    bool running = false;
    
public:
    SyncFrameDecoder(
        std::unique_ptr<IMediaPlayer>& player, 
        VideoFrameQueue& vq, 
        AudioFrameQueue& aq)
        : media_player(player), video_queue(vq), audio_queue(aq) {}
    
    ~SyncFrameDecoder() override {
        stop();
    }
    
    void start() override {
        running = true;
    }
    
    void stop() override {
        running = false;
    }
    
    void pause() override {
        // No special handling needed for sync decoder
    }
    
    void resume() override {
        // No special handling needed for sync decoder
    }
    
    bool is_running() const override {
        return running;
    }
    
    // Fill queues with frames - called by client
    void decode_frames() {
        if (!running || !media_player) return;
        
        // Buffer video frames
        while (video_queue.size() < video_queue.max_queue_size()) {
            VideoFrame frame;
            if (media_player->read_video_frame(frame)) {
                video_queue.push(std::move(frame));
            } else {
                break;
            }
        }
        
        // Buffer audio frames
        while (audio_queue.size() < audio_queue.max_queue_size()) {
            AudioFrame frame;
            if (media_player->read_audio_frame(frame)) {
                audio_queue.push(std::move(frame));
            } else {
                break;
            }
        }
    }
};

/**
 * Threaded decoder that decodes frames in background threads
 * to keep the queues filled without blocking the main thread.
 */
class ThreadedFrameDecoder : public FrameDecoder {
private:
    std::unique_ptr<IMediaPlayer>& media_player;
    VideoFrameQueue& video_queue;
    AudioFrameQueue& audio_queue;
    
    std::thread video_thread;
    std::thread audio_thread;
    
    std::atomic<bool> running{false};
    std::atomic<bool> paused{false};
    
    // Video decoder thread function
    void video_decoder_thread() {
        while (running) {
            // Check if we're paused
            if (paused) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            // Decode and buffer video frames
            VideoFrame frame;
            if (media_player->read_video_frame(frame)) {
                video_queue.push_blocking(std::move(frame));
            } else {
                // No more frames available now, sleep a bit to avoid busy waiting
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }
    
    // Audio decoder thread function
    void audio_decoder_thread() {
        while (running) {
            // Check if we're paused
            if (paused) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            // Decode and buffer audio frames
            AudioFrame frame;
            if (media_player->read_audio_frame(frame)) {
                audio_queue.push_blocking(std::move(frame));
            } else {
                // No more frames available now, sleep a bit to avoid busy waiting
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }
    
public:
    ThreadedFrameDecoder(
        std::unique_ptr<IMediaPlayer>& player, 
        VideoFrameQueue& vq, 
        AudioFrameQueue& aq)
        : media_player(player), video_queue(vq), audio_queue(aq) {}
    
    ~ThreadedFrameDecoder() override {
        stop();
    }
    
    // Start the decoder threads
    void start() override {
        if (running) return;
        
        running = true;
        paused = false;
        
        // Clear any existing frames
        video_queue.clear();
        audio_queue.clear();
        
        // Reset abort flags
        video_queue.reset();
        audio_queue.reset();
        
        // Start decoder threads
        video_thread = std::thread(&ThreadedFrameDecoder::video_decoder_thread, this);
        audio_thread = std::thread(&ThreadedFrameDecoder::audio_decoder_thread, this);
        
        UtilityFunctions::print_verbose("Threaded frame decoder started");
    }
    
    // Stop the decoder threads
    void stop() override {
        if (!running) return;
        
        // Signal threads to stop
        running = false;
        
        // Abort any waiting operations on queues
        video_queue.abort();
        audio_queue.abort();
        
        // Wait for threads to finish
        if (video_thread.joinable()) {
            video_thread.join();
        }
        
        if (audio_thread.joinable()) {
            audio_thread.join();
        }
        
        UtilityFunctions::print_verbose("Threaded frame decoder stopped");
    }
    
    // Pause decoding temporarily (typically during seeking)
    void pause() override {
        paused = true;
    }
    
    // Resume decoding after a pause
    void resume() override {
        paused = false;
    }
    
    // Check if decoder is running
    bool is_running() const override {
        return running;
    }
};

// Factory implementation
inline std::unique_ptr<FrameDecoder> FrameDecoder::create(
    std::unique_ptr<IMediaPlayer>& media_player,
    VideoFrameQueue& video_queue, 
    AudioFrameQueue& audio_queue,
    bool use_threading) 
{
    if (use_threading) {
        return std::make_unique<ThreadedFrameDecoder>(media_player, video_queue, audio_queue);
    } else {
        return std::make_unique<SyncFrameDecoder>(media_player, video_queue, audio_queue);
    }
}

} // namespace godot