#pragma once
#include "../common/video_stream_native.hpp"
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
 * Main video playback implementation that handles video decoding,
 * frame queueing, and presentation using AVFoundation.
 */
class VideoStreamPlaybackAVF : public VideoStreamPlaybackNative {
  GDCLASS(VideoStreamPlaybackAVF, VideoStreamPlayback);

private:
  
  // Core resources
  std::mutex mutex_;              // Thread synchronization
  
  // Basic state
  Vector<uint8_t> frame_data;     // Current frame buffer

  AVPlayer* player{nullptr};
  AVPlayerItem* player_item{nullptr};
  AVPlayerItemVideoOutput* video_output{nullptr};

  // Audio track info
  struct AudioTrack {
      int index;
      String language;
      String name;
  };
  std::vector<AudioTrack> audio_tracks;

  // Private helper functions
  void clear_avf_objects();
  bool setup_video_pipeline(const String &p_file);
  void ensure_frame_buffer(size_t width, size_t height);
  void update_texture(size_t width, size_t height);
  void process_pending_frames();
  bool should_decode_next_frame() const;
  void detect_framerate();
  void setup_aligned_dimensions();

  // Static helpers
  static void convert_bgra_to_rgba_simd(const uint8_t* src, uint8_t* dst, size_t pixel_count);

  // Helper to get media time from player
  double get_media_time() const;

  bool initialization_complete{false}; // Flag to indicate initialization completion
  bool play_requested{false}; // Flag to indicate if play was requested before initialization
  int audio_track = 0; // Store the selected audio track index

protected:
  static void _bind_methods();

public:
  VideoStreamPlaybackAVF();
  ~VideoStreamPlaybackAVF();

  // Core video functionality
  void set_file(const String &p_file);

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
