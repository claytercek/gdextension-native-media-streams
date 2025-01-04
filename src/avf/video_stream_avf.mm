#include "video_stream_avf.hpp"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>

using namespace godot;

// -----------------------------------------------------------------------------
// VideoStreamAVF Implementation
// -----------------------------------------------------------------------------
void VideoStreamAVF::_bind_methods() {
  // No additional bindings needed for the stream class
}

// -----------------------------------------------------------------------------
// VideoStreamPlaybackAVF Implementation
// -----------------------------------------------------------------------------
void VideoStreamPlaybackAVF::_bind_methods() {
  // No additional bindings needed as we're implementing VideoStreamPlayback
  // interface
}

VideoStreamPlaybackAVF::VideoStreamPlaybackAVF() { texture.instantiate(); }

VideoStreamPlaybackAVF::~VideoStreamPlaybackAVF() { clear_avf_objects(); }

// -----------------------------------------------------------------------------
// Resource Management
// -----------------------------------------------------------------------------
void VideoStreamPlaybackAVF::clear_avf_objects() {
  if (player) {
    [(AVPlayer *)player pause];
    [(AVPlayer *)player release];
    player = nullptr;
  }

  if (video_output) {
    [(AVPlayerItemVideoOutput *)video_output release];
    video_output = nullptr;
  }

  player_item = nullptr; // owned by player
  frames_pending = 0;
  playing = false;
}

bool VideoStreamPlaybackAVF::setup_video_pipeline(const String &p_file) {
  NSString *path = [NSString stringWithUTF8String:p_file.utf8().get_data()];
  NSURL *url = [NSURL fileURLWithPath:path];

  // Create asset
  AVURLAsset *asset = [AVURLAsset URLAssetWithURL:url options:nil];
  if (!asset) {
    UtilityFunctions::printerr("Failed to create asset for: ", p_file);
    return false;
  }

  // Load video track
  dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
  __block bool success = false;
  __block CGSize track_size = CGSizeZero;

  [asset loadTracksWithMediaType:AVMediaTypeVideo
               completionHandler:^(NSArray<AVAssetTrack *> *tracks,
                                   NSError *error) {
                 if (error || tracks.count == 0) {
                 UtilityFunctions::printerr(
                       "Error loading video tracks for: ", p_file);
                   if (error) {
                     UtilityFunctions::printerr(
                         "Error: ", error.localizedDescription.UTF8String);
                   } else {
                     UtilityFunctions::printerr("No video tracks found");
                   }

                   dispatch_semaphore_signal(semaphore);
                   return;
                 }

                 AVAssetTrack *video_track = tracks.firstObject;
                 track_size = video_track.naturalSize;
                 success = true;
                 dispatch_semaphore_signal(semaphore);
               }];

  // Wait for track loading
  dispatch_time_t timeout =
      dispatch_time(DISPATCH_TIME_NOW, (int64_t)(5 * NSEC_PER_SEC));
  if (dispatch_semaphore_wait(semaphore, timeout) != 0) {
    dispatch_release(semaphore);
    UtilityFunctions::printerr("Timeout while loading video tracks");
    return false;
  }
  dispatch_release(semaphore);

  if (!success) {
    UtilityFunctions::printerr("Failed to load video tracks for file: ", p_file);
    return false;
  }

  // Update size with alignment
  frame_size.x = static_cast<int32_t>(track_size.width + 3) & ~3;
  frame_size.y = static_cast<int32_t>(track_size.height);

  // Configure video output
  NSDictionary *attributes = @{
    (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
    (id)kCVPixelBufferMetalCompatibilityKey : @YES,
    (id)kCVPixelBufferWidthKey : @(frame_size.x),
    (id)kCVPixelBufferHeightKey : @(frame_size.y),
    (id)kCVPixelBufferCGBitmapContextCompatibilityKey : @YES
  };

  AVPlayerItemVideoOutput *output = [[AVPlayerItemVideoOutput alloc]
      initWithPixelBufferAttributes:attributes];

  if (!output) {
    UtilityFunctions::printerr("Failed to create video output");
    return false;
  }

  // Create player item and player
  AVPlayerItem *item = [AVPlayerItem playerItemWithAsset:asset];
  if (!item) {
    [output release];
    UtilityFunctions::printerr("Failed to create player item");
    return false;
  }

  [item addOutput:output];

  AVPlayer *avf_player = [[AVPlayer alloc] initWithPlayerItem:item];
  if (!avf_player) {
    [output release];
    UtilityFunctions::printerr("Failed to create player");
    return false;
  }

  // Store objects
  video_output = output;
  player_item = item;
  player = avf_player;

  // Create initial texture
  ensure_frame_buffer(frame_size.x, frame_size.y);
  Ref<Image> img = Image::create_empty(frame_size.x, frame_size.y, false,
                                       Image::FORMAT_RGBA8);
  if (img.is_null()) {
    clear_avf_objects();
    UtilityFunctions::printerr("Failed to create initial texture");
    return false;
  }

  texture->set_image(img);
  return true;
}

// -----------------------------------------------------------------------------
// Video Processing
// -----------------------------------------------------------------------------
void VideoStreamPlaybackAVF::video_write() {
  if (!video_output || !player_item || !texture.is_valid()) {
    return;
  }

  AVPlayerItemVideoOutput *output = (AVPlayerItemVideoOutput *)video_output;
  CMTime player_time = [(AVPlayer *)player currentTime];

  // Early exit if no new frame is available
  if (![output hasNewPixelBufferForItemTime:player_time]) {
    return;
  }

  // Get the pixel buffer
  CVPixelBufferRef pixel_buffer = [output copyPixelBufferForItemTime:player_time
                                                  itemTimeForDisplay:nil];
  if (!pixel_buffer) {
    return;
  }

  // RAII-style buffer management
  struct ScopedPixelBuffer {
    CVPixelBufferRef buffer;
    ScopedPixelBuffer(CVPixelBufferRef b) : buffer(b) {
      CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    }
    ~ScopedPixelBuffer() {
      CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
      CVBufferRelease(buffer);
    }
  } scoped_buffer(pixel_buffer);

  // Get buffer info
  const uint8_t *src_data =
      (const uint8_t *)CVPixelBufferGetBaseAddress(pixel_buffer);
  size_t src_stride = CVPixelBufferGetBytesPerRow(pixel_buffer);
  size_t width = CVPixelBufferGetWidth(pixel_buffer);
  size_t height = CVPixelBufferGetHeight(pixel_buffer);

  // Update frame data if needed
  ensure_frame_buffer(width, height);

  // Fast path: if strides match, we can do a single conversion
  uint8_t *dst_data = frame_data.ptrw();
  size_t dst_stride = width * 4;

  if (src_stride == dst_stride) {
    convert_bgra_to_rgba(src_data, dst_data, width * height);
  } else {
    for (size_t y = 0; y < height; y++) {
      convert_bgra_to_rgba(src_data + y * src_stride, dst_data + y * dst_stride,
                           width);
    }
  }

  update_texture(width, height);
}

// -----------------------------------------------------------------------------
// Public Interface
// -----------------------------------------------------------------------------
void VideoStreamPlaybackAVF::set_file(const String &p_file) {
  file_name = p_file;

  Ref<FileAccess> file = FileAccess::open(p_file, FileAccess::READ);
  ERR_FAIL_COND_MSG(file.is_null(), "Cannot open file '" + p_file + "'.");

  clear_avf_objects();

  if (!setup_video_pipeline(file->get_path_absolute())) {
    clear_avf_objects();
    ERR_FAIL_MSG("Failed to setup video pipeline for '" + p_file + "'.");
  }
}

void VideoStreamPlaybackAVF::_play() {
  if (!player) {
    return;
  }

  if (!playing) {
    time = 0;
    [(AVPlayer *)player seekToTime:kCMTimeZero];
  }

  [(AVPlayer *)player play];
  playing = true;
  paused = false;
}

void VideoStreamPlaybackAVF::_stop() {
  if (!player) {
    return;
  }

  [(AVPlayer *)player pause];
  [(AVPlayer *)player seekToTime:kCMTimeZero];
  playing = false;
  paused = false;
  time = 0;
}

void VideoStreamPlaybackAVF::_set_paused(bool p_paused) {
  if (!player || paused == p_paused) {
    return;
  }

  if (p_paused) {
    [(AVPlayer *)player pause];
  } else {
    [(AVPlayer *)player play];
  }
  paused = p_paused;
}

void VideoStreamPlaybackAVF::_seek(double p_time) {
  if (!player) {
    return;
  }

  double length = _get_length();
  double seek_time = CLAMP(p_time, 0.0, length);

  CMTime time_value = CMTimeMakeWithSeconds(seek_time, NSEC_PER_SEC);
  [(AVPlayer *)player seekToTime:time_value
                 toleranceBefore:kCMTimeZero
                  toleranceAfter:kCMTimeZero];
}

void VideoStreamPlaybackAVF::_update(double p_delta) {
  if (!playing || paused || !player) {
    return;
  }

  time += p_delta;

  CMTime current_time = [(AVPlayer *)player currentTime];
  if (CMTIME_IS_INVALID(current_time)) {
    return;
  }

  double current_seconds = CMTimeGetSeconds(current_time);
  if (current_seconds <= time) {
    video_write();
  }
}

// -----------------------------------------------------------------------------
// Helper Functions
// -----------------------------------------------------------------------------
void VideoStreamPlaybackAVF::ensure_frame_buffer(size_t width, size_t height) {
  size_t required_size = width * height * 4;
  if (frame_data.size() != required_size) {
    frame_data.resize(required_size);
  }
}

void VideoStreamPlaybackAVF::convert_bgra_to_rgba(const uint8_t *src,
                                                  uint8_t *dst,
                                                  size_t pixel_count) {
  for (size_t i = 0; i < pixel_count; i++) {
    dst[0] = src[2]; // R
    dst[1] = src[1]; // G
    dst[2] = src[0]; // B
    dst[3] = src[3]; // A
    src += 4;
    dst += 4;
  }
}

void VideoStreamPlaybackAVF::update_texture(size_t width, size_t height) {
  PackedByteArray pba;
  pba.resize(frame_data.size());
  memcpy(pba.ptrw(), frame_data.ptr(), frame_data.size());

  Ref<Image> img =
      Image::create_from_data(width, height, false, Image::FORMAT_RGBA8, pba);

  if (img.is_valid()) {
    texture->update(img);
    frames_pending = 1;
  }
}

// -----------------------------------------------------------------------------
// Getters
// -----------------------------------------------------------------------------
bool VideoStreamPlaybackAVF::_is_playing() const { return playing; }

bool VideoStreamPlaybackAVF::_is_paused() const { return paused; }

double VideoStreamPlaybackAVF::_get_length() const {
  if (!player_item) {
    return 0.0;
  }

  CMTime duration = [(AVPlayerItem *)player_item duration];
  if (CMTIME_IS_INVALID(duration)) {
    return 0.0;
  }
  return CMTimeGetSeconds(duration);
}

double VideoStreamPlaybackAVF::_get_playback_position() const {
  if (!player) {
    return 0.0;
  }

  CMTime current = [(AVPlayer *)player currentTime];
  if (CMTIME_IS_INVALID(current)) {
    return 0.0;
  }
  return CMTimeGetSeconds(current);
}

Ref<Texture2D> VideoStreamPlaybackAVF::_get_texture() const { return texture; }

// -----------------------------------------------------------------------------
// Audio Handling
// -----------------------------------------------------------------------------
void VideoStreamPlaybackAVF::_set_audio_track(int p_idx) {
  // TODO: Implement if needed
}

int VideoStreamPlaybackAVF::_get_channels() const {
  if (!player_item) {
    return 2;
  }

  __block int channels = 2;
  dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

  AVAsset *asset = player_item.asset;
  [asset
      loadTracksWithMediaType:AVMediaTypeAudio
            completionHandler:^(NSArray<AVAssetTrack *> *tracks,
                                NSError *error) {
              if (!error && tracks.count > 0) {
                AVAssetTrack *audio_track = tracks.firstObject;
                CMFormatDescriptionRef format =
                    (__bridge CMFormatDescriptionRef)
                        audio_track.formatDescriptions.firstObject;

                if (format) {
                  const AudioStreamBasicDescription *asbd =
                      CMAudioFormatDescriptionGetStreamBasicDescription(format);
                  if (asbd) {
                    channels = asbd->mChannelsPerFrame;
                  }
                }
              }
              dispatch_semaphore_signal(semaphore);
            }];

  // Wait with timeout
  dispatch_time_t timeout =
      dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.1 * NSEC_PER_SEC));
  if (dispatch_semaphore_wait(semaphore, timeout) != 0) {
    UtilityFunctions::print_verbose(
        "Timeout while getting audio channel count");
  }
  dispatch_release(semaphore);

  return channels;
}

int VideoStreamPlaybackAVF::_get_mix_rate() const {
  if (!player_item) {
    return 44100;
  }

  __block int sample_rate = 44100;
  dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

  AVAsset *asset = player_item.asset;
  [asset
      loadTracksWithMediaType:AVMediaTypeAudio
            completionHandler:^(NSArray<AVAssetTrack *> *tracks,
                                NSError *error) {
              if (!error && tracks.count > 0) {
                AVAssetTrack *audio_track = tracks.firstObject;
                CMFormatDescriptionRef format =
                    (__bridge CMFormatDescriptionRef)
                        audio_track.formatDescriptions.firstObject;

                if (format) {
                  const AudioStreamBasicDescription *asbd =
                      CMAudioFormatDescriptionGetStreamBasicDescription(format);
                  if (asbd) {
                    sample_rate = asbd->mSampleRate;
                  }
                }
              }
              dispatch_semaphore_signal(semaphore);
            }];

  // Wait with timeout
  dispatch_time_t timeout =
      dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.1 * NSEC_PER_SEC));
  if (dispatch_semaphore_wait(semaphore, timeout) != 0) {
    UtilityFunctions::print_verbose("Timeout while getting audio sample rate");
  }
  dispatch_release(semaphore);

  return sample_rate;
}

// -----------------------------------------------------------------------------
// ResourceFormatLoaderAVF Implementation
// -----------------------------------------------------------------------------
Variant ResourceFormatLoaderAVF::_load(const String &p_path,
                                       const String &p_original_path,
                                       bool p_use_sub_threads,
                                       int32_t p_cache_mode) const {
  VideoStreamAVF *stream = memnew(VideoStreamAVF);
  stream->set_file(p_path);

  Ref<VideoStreamAVF> avf_stream = Ref<VideoStreamAVF>(stream);

  return {avf_stream};
}

PackedStringArray ResourceFormatLoaderAVF::_get_recognized_extensions() const {
  PackedStringArray arr;

  arr.push_back("mp4");
  arr.push_back("mov");
  arr.push_back("m4v");

  return arr;
}

bool ResourceFormatLoaderAVF::_handles_type(const StringName &p_type) const {
  return ClassDB::is_parent_class(p_type, "VideoStream");
}

String ResourceFormatLoaderAVF::_get_resource_type(const String &p_path) const {
  String ext = p_path.get_extension().to_lower();
  if (ext == "mp4" || ext == "mov" || ext == "m4v") {
    return "VideoStreamAVF";
  }

  return "";
}
