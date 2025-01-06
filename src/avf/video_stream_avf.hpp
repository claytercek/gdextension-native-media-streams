#pragma once
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/video_stream.hpp>
#include <godot_cpp/classes/video_stream_playback.hpp>
#include <deque>
#include <simd/simd.h>
#include <memory_resource>
#include <mutex>
#include <optional>

// Forward declare Objective-C classes to avoid including headers in hpp
#ifdef __OBJC__
@class AVPlayer;
@class AVPlayerItem;
@class AVPlayerItemVideoOutput;
#else
typedef struct objc_object AVPlayer;
typedef struct objc_object AVPlayerItem;
typedef struct objc_object AVPlayerItemVideoOutput;
#endif

namespace godot {

/**
 * RAII wrapper for AVFoundation resources that manages the lifecycle
 * of AVPlayer, AVPlayerItem, and AVPlayerItemVideoOutput objects.
 * Ensures proper cleanup of Objective-C objects.
 */
class AVFResources {
private:
    AVPlayer* player_{nullptr};
    AVPlayerItemVideoOutput* output_{nullptr};
    AVPlayerItem* item_{nullptr}; // owned by player

public:
    AVFResources() = default;
    ~AVFResources();
    
    bool initialize(const String& path);
    void clear();
    
    AVPlayer* player() const { return player_; }
    AVPlayerItem* item() const { return item_; }
    AVPlayerItemVideoOutput* output() const { return output_; }
};

/**
 * Memory pool for video frames that uses a monotonic buffer resource
 * to minimize allocations and improve performance. The pool automatically
 * manages memory and provides RAII cleanup.
 */
class VideoFramePool {
private:
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
    
    std::vector<uint8_t, std::pmr::polymorphic_allocator<uint8_t>> allocate(size_t size) {
        return std::vector<uint8_t, std::pmr::polymorphic_allocator<uint8_t>>(size, state->allocator);
    }
    
    void reset() {
        // Create entirely new state instead of trying to modify existing allocator
        state = std::make_unique<PoolState>();
    }
};

/**
 * Structure for error reporting that provides type-safe error handling
 * and descriptive error messages for video playback issues.
 */
struct VideoError {
    enum class Type {
        None,
        FileNotFound,
        InvalidFormat,
        DecoderError,
        ResourceError
    };
    
    Type type{Type::None};
    String message;
    
    static VideoError ok() { return {}; }
    static VideoError error(Type t, String msg) { return {t, std::move(msg)}; }
    bool has_error() const { return type != Type::None; }
};

/**
 * Represents a single video frame with its presentation time and dimensions.
 * Uses pooled memory allocation for better performance.
 */
struct VideoFrame {
    std::vector<uint8_t, std::pmr::polymorphic_allocator<uint8_t>> data;
    double presentation_time;
    Size2i size;
    
    VideoFrame(std::pmr::polymorphic_allocator<uint8_t>& alloc) 
        : data(alloc) {}
    VideoFrame() : data(std::pmr::polymorphic_allocator<uint8_t>{}) {} // Add default constructor
};

/**
 * Main video playback implementation that handles video decoding,
 * frame queueing, and presentation using AVFoundation.
 */
class VideoStreamPlaybackAVF : public VideoStreamPlayback {
  GDCLASS(VideoStreamPlaybackAVF, VideoStreamPlayback);

private:
  // Playback state management
  struct State {
      bool playing{false};      // Whether video is currently playing
      bool paused{false};       // Whether video is paused
      bool buffering{false};    // Whether video is buffering
      double time{0.0};         // Current playback time
      float fps{30.0f};         // Detected frame rate
      bool use_hardware_decode{true}; // Whether to use hardware decoding
      float detected_fps{30.0f};
  } state_;
  
  // Video dimensions and alignment
  struct Dimensions {
      Size2i frame;            // Original frame dimensions
      size_t aligned_width{0}; // Width aligned for SIMD operations
      size_t aligned_height{0};// Height aligned for SIMD operations
  } dimensions_;
  
  /**
   * Thread-safe frame queue that manages video frame buffering
   * and presentation timing.
   */
  struct FrameQueue {
      static const size_t MAX_SIZE = 3;
      std::deque<VideoFrame> frames;
      
      bool empty() const { return frames.empty(); }
      size_t size() const { return frames.size(); }
      void push_back(VideoFrame&& frame) { frames.push_back(std::move(frame)); }
      void pop_front() { frames.pop_front(); }
      const VideoFrame& front() const { return frames.front(); }
      VideoFrame& front() { return frames.front(); }
      
      void push(VideoFrame&& frame) {
          push_back(std::move(frame));
          while (size() > MAX_SIZE) {
              pop_front();
          }
      }
      
      std::optional<VideoFrame> pop() {
          if (frames.empty()) return std::nullopt;
          VideoFrame frame = std::move(frames.front());
          frames.pop_front();
          return frame;
      }
      
      bool should_decode(double current_time) const {
          if (frames.empty()) return true;
          return (frames.back().presentation_time - current_time) < 0.5;
      }
  } frame_queue_;
  
  /**
   * Helper methods for texture management and updates
   */
  struct TextureManagement {
      static void update_from_frame(const VideoFrame& frame, Ref<ImageTexture>& texture);
      static void ensure_buffer(Vector<uint8_t>& buffer, size_t width, size_t height);
      static Ref<Image> create_image(const uint8_t* data, size_t width, size_t height);
  };
  
  /**
   * Frame processing utilities including color conversion
   * and timing prediction
   */
  struct FrameProcessing {
      static void convert_frame(const uint8_t* src, uint8_t* dst, 
                              size_t width, size_t height,
                              size_t src_stride, size_t dst_stride);
      static double predict_presentation_time(double current_time, float fps);
  };

  // Core resources
  AVFResources avf_;              // RAII-managed AV resources
  VideoFramePool frame_pool_;     // Memory pool for frames
  std::mutex mutex_;              // Thread synchronization
  VideoError last_error_;         // Last error state
  
  // Basic state
  String file_name;               // Current video file path
  Ref<ImageTexture> texture;      // Output texture
  Vector<uint8_t> frame_data;     // Current frame buffer
  int frames_pending{0};          // Number of frames waiting

  // Temporary video objects (to be replaced by AVFResources)
  AVPlayer* player{nullptr};
  AVPlayerItem* player_item{nullptr};
  AVPlayerItemVideoOutput* video_output{nullptr};

  // Private helper functions
  void set_error(VideoError::Type type, const String& message);
  void clear_avf_objects();
  bool setup_video_pipeline(const String &p_file);
  void ensure_frame_buffer(size_t width, size_t height);
  void update_texture(size_t width, size_t height);
  void process_pending_frames();
  bool should_decode_next_frame() const;
  void detect_framerate();
  void setup_aligned_dimensions();
  bool try_hardware_decode() const;
  void update_texture_from_frame(const VideoFrame& frame);
  static size_t align_dimension(size_t dim, size_t alignment = 16) {
      return (dim + alignment - 1) & ~(alignment - 1);
  }

  // Thread-safe frame queue operations
  void push_frame(VideoFrame&& frame) {
      std::lock_guard<std::mutex> lock(mutex_);
      frame_queue_.push_back(std::move(frame));
      while (frame_queue_.size() > FrameQueue::MAX_SIZE) {
          frame_queue_.pop_front();
      }
  }

  bool pop_frame(VideoFrame& frame) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (frame_queue_.empty()) return false;
      frame = std::move(frame_queue_.front());
      frame_queue_.pop_front();
      return true;
  }

  // Static helpers
  static void convert_bgra_to_rgba_simd(const uint8_t* src, uint8_t* dst, size_t pixel_count);
  static double predict_next_frame_time(const AVPlayer* player, const std::deque<VideoFrame>& queue);

protected:
  static void _bind_methods();

public:
  VideoStreamPlaybackAVF();
  ~VideoStreamPlaybackAVF();

  // Core video functionality
  void set_file(const String &p_file);
  void video_write();

  // VideoStreamPlayback interface
  virtual void _play() override;
  virtual void _stop() override;
  virtual bool _is_playing() const override;
  virtual void _set_paused(bool p_paused) override;
  virtual bool _is_paused() const override;
  virtual double _get_length() const override;
  virtual double _get_playback_position() const override;
  virtual void _seek(double p_time) override;
  virtual void _set_audio_track(int p_idx) override;
  virtual Ref<Texture2D> _get_texture() const override;
  virtual void _update(double p_delta) override;
  virtual int _get_channels() const override;
  virtual int _get_mix_rate() const override;

  // Add error reporting
  bool has_error() const { return last_error_.has_error(); }
  const VideoError& get_last_error() const { return last_error_; }
};

class VideoStreamAVF : public VideoStream {
  GDCLASS(VideoStreamAVF, VideoStream);

protected:
  static void _bind_methods();

public:
  virtual Ref<VideoStreamPlayback> _instantiate_playback() override {
    Ref<VideoStreamPlaybackAVF> playback;
    playback.instantiate();
    playback->set_file(get_file());
    return playback;
  }
};

class ResourceFormatLoaderAVF : public ResourceFormatLoader {
  GDCLASS(ResourceFormatLoaderAVF, ResourceFormatLoader);

protected:
  static void _bind_methods(){};

public:
  Variant _load(const String &p_path, const String &p_original_path,
                bool p_use_sub_threads, int32_t p_cache_mode) const override;
  PackedStringArray _get_recognized_extensions() const override;
  bool _handles_type(const StringName &p_type) const override;
  String _get_resource_type(const String &p_path) const override;
};

} // namespace godot
