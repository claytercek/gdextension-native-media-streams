#ifndef VIDEO_STREAM_AVF_HPP
#define VIDEO_STREAM_AVF_HPP

#include <AVFoundation/AVFoundation.h>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/video_stream.hpp>
#include <godot_cpp/classes/video_stream_playback.hpp>

namespace godot {

class VideoStreamPlaybackAVF : public VideoStreamPlayback {
  GDCLASS(VideoStreamPlaybackAVF, VideoStreamPlayback);

  struct AudioTrackInfo {
      int channels;
      int sample_rate;
      String language;
  };

private:
  enum {
    MAX_FRAMES = 4,
  };

  Vector<AudioTrackInfo> audio_tracks;
  Image::Format format;
  Vector<uint8_t> frame_data;
  int frames_pending;
  Ref<FileAccess> file;
  String file_name;
  Point2i size;

  AVPlayer *player;
  AVPlayerItem *player_item;
  AVPlayerItemVideoOutput *video_output;

  bool playing;
  bool buffering;
  bool paused;

  double last_update_time;
  double time;
  double delay_compensation;

  Ref<ImageTexture> texture;

  void clear();
  void video_write();

protected:
  static void _bind_methods();

public:
  VideoStreamPlaybackAVF();
  ~VideoStreamPlaybackAVF();

  void set_file(const String &p_file);

   void _play() override;
   void _stop() override;
   bool _is_playing() const override;
   void _set_paused(bool p_paused) override;
   bool _is_paused() const override;
   double _get_length() const override;
   double _get_playback_position() const override;
   void _seek(double p_time) override;
   void _set_audio_track(int p_idx) override;
   Ref<Texture2D> _get_texture() const override;
   void _update(double p_delta) override;
   int _get_channels() const override;
   int _get_mix_rate() const override;
};

class VideoStreamAVF : public VideoStream {
  GDCLASS(VideoStreamAVF, VideoStream);

protected:
  static void _bind_methods();

private:
  int audio_track;

public:
   Ref<VideoStreamPlayback> _instantiate_playback() override {
    Ref<VideoStreamPlaybackAVF> pb = memnew(VideoStreamPlaybackAVF);
    pb->_set_audio_track(audio_track);
    pb->set_file(get_file());
    return pb;
  }

   void set_audio_track(int p_track) { audio_track = p_track; }

  VideoStreamAVF() { audio_track = 0; }
};

class ResourceFormatLoaderAVF : public ResourceFormatLoader {
  GDCLASS(ResourceFormatLoaderAVF, ResourceFormatLoader);

protected:
    static void _bind_methods(){};

public:
   Variant _load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int32_t p_cache_mode) const override;
   PackedStringArray _get_recognized_extensions() const override;
   bool _handles_type(const StringName &p_type) const override;
   String _get_resource_type(const String &p_path) const override;
};


} // namespace godot

#endif // VIDEO_STREAM_AVF_HPP
