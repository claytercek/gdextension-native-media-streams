#pragma once
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/video_stream.hpp>
#include <godot_cpp/classes/video_stream_playback.hpp>

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

class VideoStreamPlaybackAVF : public VideoStreamPlayback {
  GDCLASS(VideoStreamPlaybackAVF, VideoStreamPlayback);

private:
  // Video state
  String file_name;
  bool playing{false};
  bool paused{false};
  bool buffering{false};
  double time{0.0};
  int frames_pending{0};

  // Texture handling
  Ref<ImageTexture> texture;
  Vector<uint8_t> frame_data;
  Size2i frame_size;

  // AVFoundation objects
  AVPlayer *player{nullptr};
  AVPlayerItem *player_item{nullptr};
  AVPlayerItemVideoOutput *video_output{nullptr};

  // Private helper functions
  void clear_avf_objects();
  bool setup_video_pipeline(const String &p_file);
  void ensure_frame_buffer(size_t width, size_t height);
  void convert_bgra_to_rgba(const uint8_t *src, uint8_t *dst,
                            size_t pixel_count);
  void update_texture(size_t width, size_t height);

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
