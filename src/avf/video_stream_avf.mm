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
        [(AVPlayer*)player pause];
        [(AVPlayer*)player release];
        player = nullptr;
    }

    if (video_output) {
        [(AVPlayerItemVideoOutput*)video_output release];
        video_output = nullptr;
    }

    player_item = nullptr;
    state.playing = false;
    frame_pool.reset();
}

bool VideoStreamPlaybackAVF::setup_video_pipeline(const String &p_file) {
    UtilityFunctions::print_verbose("VideoStreamPlaybackAVF::setup_video_pipeline() called for file: " + p_file);
    NSString *path = [NSString stringWithUTF8String:p_file.utf8().get_data()];
    NSURL *url = [NSURL fileURLWithPath:path];

    // Create asset
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:url options:nil];
    if (!asset) {
        UtilityFunctions::printerr("Failed to create asset for: ", p_file);
        return false;
    }

    // Create a dispatch group to synchronize audio and video track loading
    dispatch_group_t group = dispatch_group_create();

    __block bool video_success = false;
    __block bool audio_success = false;

    // Load video track asynchronously
    dispatch_group_enter(group);
    [asset loadTracksWithMediaType:AVMediaTypeVideo completionHandler:^(NSArray<AVAssetTrack *> *tracks, NSError *error) {
        UtilityFunctions::print_verbose("VideoStreamPlaybackAVF::setup_video_pipeline() Tracks loaded with count: " + itos(tracks.count));
        if (error || tracks.count == 0) {
            UtilityFunctions::printerr("Error loading video tracks for: ", p_file);
            if (error) {
                UtilityFunctions::printerr(error.localizedDescription.UTF8String);
                UtilityFunctions::printerr(error.localizedFailureReason.UTF8String);
            } else {
                UtilityFunctions::printerr("No video tracks found");
            }
        } else {
            AVAssetTrack *video_track = tracks.firstObject;
            CGSize track_size = video_track.naturalSize;

            // Update size with alignment
            dimensions.frame.x = static_cast<int32_t>(track_size.width + 3) & ~3;
            dimensions.frame.y = static_cast<int32_t>(track_size.height);

            // Configure video output
            NSDictionary *attributes = @{
                (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
                (id)kCVPixelBufferMetalCompatibilityKey : @YES,
                (id)kCVPixelBufferWidthKey : @(dimensions.frame.x),
                (id)kCVPixelBufferHeightKey : @(dimensions.frame.y),
                (id)kCVPixelBufferCGBitmapContextCompatibilityKey : @YES
            };

            AVPlayerItemVideoOutput *output = [[AVPlayerItemVideoOutput alloc]
                initWithPixelBufferAttributes:attributes];

            if (!output) {
                UtilityFunctions::printerr("Failed to create video output");
            } else {
                // Create player item and player
                AVPlayerItem *item = [AVPlayerItem playerItemWithAsset:asset];
                if (!item) {
                    [output release];
                    UtilityFunctions::printerr("Failed to create player item");
                } else {
                    [item addOutput:output];

                    AVPlayer *avf_player = [[AVPlayer alloc] initWithPlayerItem:item];
                    if (!avf_player) {
                        [output release];
                        UtilityFunctions::printerr("Failed to create player");
                    } else {
                        // Store objects
                        video_output = output;
                        player_item = item;
                        player = avf_player;

                        // Create initial texture
                        ensure_frame_buffer(dimensions.frame.x, dimensions.frame.y);
                        Ref<Image> img = Image::create_empty(dimensions.frame.x, dimensions.frame.y, false,
                                                             Image::FORMAT_RGBA8);
                        if (img.is_null()) {
                            clear_avf_objects();
                            UtilityFunctions::printerr("Failed to create initial texture");
                        } else {
                            texture->set_image(img);
                            detect_framerate();
                            setup_aligned_dimensions();

                            // Try enabling hardware acceleration
                            if (@available(macOS 10.13, *)) {
                                [(AVPlayer*)player setAutomaticallyWaitsToMinimizeStalling:NO];
                            }

                            video_success = true;
                        }
                    }
                }
            }
        }
        dispatch_group_leave(group);
    }];

    // Load audio tracks asynchronously
    dispatch_group_enter(group);
    [asset loadTracksWithMediaType:AVMediaTypeAudio completionHandler:^(NSArray<AVAssetTrack *> *loaded_tracks, NSError *error) {
        UtilityFunctions::print_verbose("Loaded audio tracks count: " + itos(loaded_tracks.count));
        if (error) {
            UtilityFunctions::printerr("Error loading audio tracks: ", error.localizedDescription.UTF8String);
        } else {
            audio_tracks.clear();
            for (NSUInteger i = 0; i < loaded_tracks.count; i++) {
                AVAssetTrack* track = loaded_tracks[i];
                
                AudioTrack info;
                info.index = i;
                
                // Get language
                NSString* language = track.languageCode;
                info.language = language ? String(language.UTF8String) : "unknown";
                
                // Get name/title if available
                NSString* name = nil;
                for (AVMetadataItem* item in track.metadata) {
                    if ([item.commonKey isEqualToString:AVMetadataCommonKeyTitle]) {
                        name = (NSString*)item.value;
                        break;
                    }
                }
                info.name = name ? String(name.UTF8String) : String("Track ") + String::num_int64(i + 1);
                
                audio_tracks.push_back(info);
            }

            // Select the initial audio track
            if (audio_track < 0 || audio_track >= (int)audio_tracks.size()) {
                audio_track = 0; // Default to the first track if the selected track is invalid
            }
            if (!audio_tracks.empty()) {
                [asset loadMediaSelectionGroupForMediaCharacteristic:AVMediaCharacteristicAudible completionHandler:^(AVMediaSelectionGroup *audioGroup, NSError *error) {
                    if (audioGroup) {
                        NSArray<AVMediaSelectionOption *> *options = audioGroup.options;
                        if (audio_track < options.count) {
                            AVMediaSelectionOption *selectedOption = options[audio_track];
                            [player_item selectMediaOption:selectedOption inMediaSelectionGroup:audioGroup];
                        }
                    }
                }];
            }
            audio_success = true;
        }
        dispatch_group_leave(group);
    }];

    // Wait for both tasks to complete
    dispatch_group_notify(group, dispatch_get_main_queue(), ^{
        if (video_success && audio_success) {
            UtilityFunctions::print_verbose("VideoStreamPlaybackAVF::setup_video_pipeline() completed successfully.");
            initialization_complete = true;
            if (play_requested) {
                _play();
            }
        } else {
            clear_avf_objects();
            UtilityFunctions::printerr("Failed to setup video pipeline for '" + p_file + "'.");
        }
    });

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
    convert_bgra_to_rgba_simd(src_data, dst_data, width * height);
  } else {
    for (size_t y = 0; y < height; y++) {
      convert_bgra_to_rgba_simd(src_data + y * src_stride, dst_data + y * dst_stride,
                           width);
    }
  }

  update_texture(width, height);
}

/**
 * Converts BGRA pixel data to RGBA using SIMD operations when available.
 * Falls back to scalar operations when SIMD is not supported.
 * 
 * @param src Source BGRA pixel data
 * @param dst Destination RGBA pixel data
 * @param pixel_count Number of pixels to convert
 */
void VideoStreamPlaybackAVF::convert_bgra_to_rgba_simd(const uint8_t* src, uint8_t* dst, size_t pixel_count) {
    // Process 4 pixels (16 bytes) at a time
    const size_t vectors = pixel_count / 4;
    const size_t remaining = pixel_count % 4;
    
    #if defined(__x86_64__)
    // x86_64 implementation using SSE
    for (size_t i = 0; i < vectors; i++) {
        __m128i pixel = _mm_loadu_si128((__m128i*)src);
        // Shuffle BGRA to RGBA: indices 2,1,0,3, 6,5,4,7, 10,9,8,11, 14,13,12,15
        __m128i shuffled = _mm_shuffle_epi8(pixel, _mm_setr_epi8(2,1,0,3, 6,5,4,7, 10,9,8,11, 14,13,12,15));
        _mm_storeu_si128((__m128i*)dst, shuffled);
        
        src += 16;
        dst += 16;
    }
    #else
    // Fallback implementation for other architectures
    for (size_t i = 0; i < vectors; i++) {
        // Process 4 pixels at a time without SIMD
        for (size_t j = 0; j < 4; j++) {
            dst[0] = src[2];  // R
            dst[1] = src[1];  // G
            dst[2] = src[0];  // B
            dst[3] = src[3];  // A
            src += 4;
            dst += 4;
        }
    }
    #endif
    
    // Handle remaining pixels
    for (size_t i = 0; i < remaining; i++) {
        dst[0] = src[2];
        dst[1] = src[1];
        dst[2] = src[0];
        dst[3] = src[3];
        src += 4;
        dst += 4;
    }
}

double VideoStreamPlaybackAVF::get_media_time() const {
    if (!player) return 0.0;
    CMTime current = [(AVPlayer*)player currentTime];
    return CMTIME_IS_INVALID(current) ? 0.0 : CMTimeGetSeconds(current);
}

/**
 * Processes pending video frames, decoding them from the video buffer
 * and queuing them for presentation. Handles frame timing and synchronization.
 */
void VideoStreamPlaybackAVF::process_pending_frames() {
    if (!video_output || !player_item) return;
    
    AVPlayerItemVideoOutput* output = (AVPlayerItemVideoOutput*)video_output;
    CMTime player_time = [(AVPlayer*)player currentTime];
    double current_time = CMTimeGetSeconds(player_time);
    
    while (frame_queue.size() < FrameQueue::MAX_SIZE) {
        double next_time = predict_next_frame_time(current_time, state.fps);
        CMTime next_frame_time = CMTimeMakeWithSeconds(next_time, NSEC_PER_SEC);
        
        CVPixelBufferRef pixel_buffer = [output copyPixelBufferForItemTime:next_frame_time itemTimeForDisplay:nil];
        if (!pixel_buffer) break;
        
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
        
        size_t width = CVPixelBufferGetWidth(pixel_buffer);
        size_t height = CVPixelBufferGetHeight(pixel_buffer);
        
        VideoFrame frame;
        frame.size = Size2i(width, height);
        frame.data.resize(width * height * 4);
        frame.presentation_time = next_time;
        
        const uint8_t* src_data = (const uint8_t*)CVPixelBufferGetBaseAddress(pixel_buffer);
        size_t src_stride = CVPixelBufferGetBytesPerRow(pixel_buffer);
        uint8_t* dst_data = frame.data.data();
        size_t dst_stride = width * 4;
        
        if (src_stride == dst_stride) {
            convert_bgra_to_rgba_simd(src_data, dst_data, width * height);
        } else {
            for (size_t y = 0; y < height; y++) {
                convert_bgra_to_rgba_simd(
                    src_data + y * src_stride,
                    dst_data + y * dst_stride,
                    width
                );
            }
        }
        
        frame_queue.push(std::move(frame));
        
        current_time = next_time;
    }
}

bool VideoStreamPlaybackAVF::should_decode_next_frame() const {
    if (frame_queue.empty()) return true;
    
    double current_time = CMTimeGetSeconds([(AVPlayer*)player currentTime]);
    double next_frame_time = predict_next_frame_time(current_time, state.fps);
    
    return (next_frame_time - current_time) < 0.5;
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
    UtilityFunctions::print_verbose("VideoStreamPlaybackAVF::_play() invoked.");

    if (!initialization_complete) {
        UtilityFunctions::print_verbose("VideoStreamPlaybackAVF::_play() initialization not complete, deferring play.");
        play_requested = true;
        return;
    }

    if (!state.playing) {
        frame_queue.clear();
        state.engine_time = 0.0;  // Reset engine time

        CMTime zero_time = kCMTimeZero;
        [(AVPlayer*)player seekToTime:zero_time toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero 
            completionHandler:^(BOOL finished) {
                if (finished) {
                    [(AVPlayer*)player play];
                    state.playing = true;
                    state.paused = false;
                }
            }
        ];
    } else {
        [(AVPlayer*)player play];
        state.playing = true;
        state.paused = false;
    }
}

void VideoStreamPlaybackAVF::_stop() {
    if (!player) return;

    [(AVPlayer*)player pause];
    
    // Clear frame queue and reset state
    frame_queue.clear();
    state.engine_time = 0;
    state.playing = false;
    state.paused = false;

    // Use completion handler to ensure seek completes
    [(AVPlayer*)player seekToTime:kCMTimeZero 
        completionHandler:^(BOOL finished) {
            // Nothing additional needed here
        }
    ];
}

void VideoStreamPlaybackAVF::_set_paused(bool p_paused) {
    if (!player || state.paused == p_paused) return;

    if (p_paused) {
        [(AVPlayer*)player pause];
    } else {
        [(AVPlayer*)player play];
    }
    state.paused = p_paused;
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
    if (!state.playing || state.paused || !player) {
        return;
    }

    state.engine_time += p_delta;
    process_pending_frames();
    
    double current_time = CMTimeGetSeconds([(AVPlayer*)player currentTime]);
    const std::optional<VideoFrame> frame = frame_queue.try_pop_next_frame(current_time);

    if (frame) {
        update_texture_from_frame(*frame);
    }

    // Use media time for playback position checking
    double media_time = get_media_time();
    double duration = _get_length();
    
    if (media_time >= duration) {
        state.playing = false;
        state.engine_time = 0.0;
        return;
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

void VideoStreamPlaybackAVF::update_texture(size_t width, size_t height) {
  PackedByteArray pba;
  pba.resize(frame_data.size());
  memcpy(pba.ptrw(), frame_data.ptr(), frame_data.size());

  Ref<Image> img =
      Image::create_from_data(width, height, false, Image::FORMAT_RGBA8, pba);

  if (img.is_valid()) {
    texture->update(img);
  }
}

void VideoStreamPlaybackAVF::detect_framerate() {
    UtilityFunctions::print_verbose("VideoStreamPlaybackAVF::detect_framerate() invoked.");
    if (!player_item) return;
    
    [[player_item asset] loadTracksWithMediaType:AVMediaTypeVideo completionHandler:^(NSArray<AVAssetTrack *> *tracks, NSError *error) {
        UtilityFunctions::print_verbose("VideoStreamPlaybackAVF::detect_framerate() loaded tracks: " + itos(tracks.count));
        if (!error && tracks.count > 0) {
            AVAssetTrack *videoTrack = tracks.firstObject;
            state.fps = videoTrack.nominalFrameRate;
        } else {
            state.fps = 30.0f;
            UtilityFunctions::print_verbose("VideoStreamPlaybackAVF::detect_framerate() Timeout or error while getting frame rate");
        }
    }];
}

void VideoStreamPlaybackAVF::setup_aligned_dimensions() {
    dimensions.aligned_width = align_dimension(dimensions.frame.x);
    dimensions.aligned_height = align_dimension(dimensions.frame.y);
    
    // Update frame buffer for aligned dimensions
    ensure_frame_buffer(dimensions.aligned_width, dimensions.aligned_height);
}

// -----------------------------------------------------------------------------
// Getters
// -----------------------------------------------------------------------------
bool VideoStreamPlaybackAVF::_is_playing() const { return state.playing; }

bool VideoStreamPlaybackAVF::_is_paused() const { return state.paused; }

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
    return get_media_time();  // Always use actual media time for position
}

Ref<Texture2D> VideoStreamPlaybackAVF::_get_texture() const { return texture; }

// -----------------------------------------------------------------------------
// Audio Handling
// -----------------------------------------------------------------------------
void VideoStreamPlaybackAVF::_set_audio_track(int p_idx) {
    // Set the audio track index but do not apply it dynamically
    audio_track = p_idx;
}

int VideoStreamPlaybackAVF::_get_channels() const {
    if (!player_item) {
        return 2;
    }

    __block int channels = 2;

    [[player_item asset] loadTracksWithMediaType:AVMediaTypeAudio completionHandler:^(NSArray<AVAssetTrack *> *tracks, NSError *error) {
        if (!error && tracks.count > 0) {
            AVAssetTrack *audio_track = tracks.firstObject;
            CMFormatDescriptionRef format = (__bridge CMFormatDescriptionRef)audio_track.formatDescriptions.firstObject;

            if (format) {
                const AudioStreamBasicDescription *asbd = CMAudioFormatDescriptionGetStreamBasicDescription(format);
                if (asbd) {
                    channels = asbd->mChannelsPerFrame;
                }
            }
        } else {
            UtilityFunctions::print_verbose("VideoStreamPlaybackAVF::_get_channels() Timeout or error while getting audio channel count");
        }
    }];

    return channels;
}

int VideoStreamPlaybackAVF::_get_mix_rate() const {
    if (!player_item) {
        return 44100;
    }

    __block int sample_rate = 44100;

    [[player_item asset] loadTracksWithMediaType:AVMediaTypeAudio completionHandler:^(NSArray<AVAssetTrack *> *tracks, NSError *error) {
        if (!error && tracks.count > 0) {
            AVAssetTrack *audio_track = tracks.firstObject;
            CMFormatDescriptionRef format = (__bridge CMFormatDescriptionRef)audio_track.formatDescriptions.firstObject;

            if (format) {
                const AudioStreamBasicDescription *asbd = CMAudioFormatDescriptionGetStreamBasicDescription(format);
                if (asbd) {
                    sample_rate = asbd->mSampleRate;
                }
            }
        } else {
            UtilityFunctions::print_verbose("VideoStreamPlaybackAVF::_get_mix_rate() Timeout or error while getting audio sample rate");
        }
    }];

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
