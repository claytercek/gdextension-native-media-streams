#include "video_stream_avf.hpp"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/core/class_db.hpp>

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>

using namespace godot;

void VideoStreamAVF::_bind_methods() {}

void VideoStreamPlaybackAVF::_bind_methods() {}

VideoStreamPlaybackAVF::VideoStreamPlaybackAVF()
    : format(Image::FORMAT_RGBA8), frames_pending(0), player(nullptr),
      player_item(nullptr), video_output(nullptr), playing(false),
      buffering(false), paused(false), last_update_time(0), time(0),
      delay_compensation(0) {
  texture.instantiate();
}

VideoStreamPlaybackAVF::~VideoStreamPlaybackAVF() { clear(); }

void VideoStreamPlaybackAVF::clear() {
  if (player) {
    [(AVPlayer *)player pause];
    [(AVPlayer *)player release];
    player = nullptr;
  }

  if (video_output) {
    [(AVPlayerItemVideoOutput *)video_output release];
    video_output = nullptr;
  }

  // player_item is owned by player, no need to release
  player_item = nullptr;

  frames_pending = 0;
  playing = false;
  file.unref();
}

void VideoStreamPlaybackAVF::set_file(const String& p_file) {
    // Store filename for potential resets
    file_name = p_file;

    UtilityFunctions::print("Loading video file: ", p_file);

    // Verify file exists and is readable
    Ref<FileAccess> file = FileAccess::open(p_file, FileAccess::READ);
    ERR_FAIL_COND_MSG(file.is_null(), "Cannot open file '" + p_file + "'.");

    // Clean up any existing playback resources
    clear();

    // Convert Godot String to NSString for AVFoundation
    NSString* path = [NSString stringWithUTF8String:file->get_path_absolute().utf8().get_data()];
    NSURL* url = [NSURL fileURLWithPath:path];

    // Create asset with improved loading options
    NSDictionary* options = @{
        AVURLAssetPreferPreciseDurationAndTimingKey: @YES,
        AVURLAssetPreferPreciseDurationAndTimingKey: @YES
    };
    AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:options];
    ERR_FAIL_COND_MSG(!asset, "Failed to create AVAsset for '" + p_file + "'.");

    // Use dispatch group to handle multiple async operations
    dispatch_group_t load_group = dispatch_group_create();
    __block bool video_load_success = false;
    __block bool audio_load_success = false;

    // Load video tracks asynchronously
    dispatch_group_enter(load_group);
    [asset loadTracksWithMediaType:AVMediaTypeVideo completionHandler:^(NSArray<AVAssetTrack *>* tracks, NSError* error) {
        if (error || tracks.count == 0) {
            UtilityFunctions::printerr("Failed to load video tracks: ", error ? [[error localizedDescription] UTF8String] : "No tracks found");
            dispatch_group_leave(load_group);
            return;
        }

        // Get video dimensions and prepare format conversion
        AVAssetTrack* video_track = tracks.firstObject;
        CGSize track_size = video_track.naturalSize;

        // Update size with alignment for better performance
        size.x = static_cast<int32_t>(track_size.width + 3) & ~3;  // Align to 4 pixels
        size.y = static_cast<int32_t>(track_size.height);

        // Determine best pixel format based on hardware capabilities
        NSArray* pixelFormats = @[
            @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange), // Most hardware efficient
            @(kCVPixelFormatType_32BGRA),                        // Good CPU fallback
            @(kCVPixelFormatType_420YpCbCr8Planar)              // Another alternative
        ];

        // Set up video output configuration with the correct dimensions
        NSDictionary* pixelBufferAttributes = @{
            (id)kCVPixelBufferPixelFormatTypeKey: pixelFormats,
            (id)kCVPixelBufferMetalCompatibilityKey: @YES,
            (id)kCVPixelBufferIOSurfacePropertiesKey: @{},
            (id)kCVPixelBufferWidthKey: @(size.x),
            (id)kCVPixelBufferHeightKey: @(size.y)
        };

        // Create video output with the correct dimensions
        AVPlayerItemVideoOutput* output = [[AVPlayerItemVideoOutput alloc]
            initWithPixelBufferAttributes:pixelBufferAttributes];

        if (!output) {
            UtilityFunctions::printerr("Failed to create video output");
            dispatch_group_leave(load_group);
            return;
        }

        // Store video output
        video_output = output;

        // Allocate frame buffer
        size_t buffer_size = size.x * size.y * 4;
        frame_data.resize(buffer_size);

        // Create initial texture
        Ref<Image> img = Image::create_empty(size.x, size.y, false, Image::FORMAT_RGBA8);
        ERR_FAIL_COND_MSG(img.is_null(), "Failed to create initial texture.");
        texture->set_image(img);

        video_load_success = true;
        dispatch_group_leave(load_group);
    }];

    // Load audio tracks asynchronously
    dispatch_group_enter(load_group);
    [asset loadTracksWithMediaType:AVMediaTypeAudio completionHandler:^(NSArray<AVAssetTrack *>* tracks, NSError* error) {
        if (!error && tracks.count > 0) {
            // Store audio track information
            audio_tracks.clear();

            for (AVAssetTrack* audio_track in tracks) {
                AudioTrackInfo track_info;
                track_info.channels = audio_track.formatDescriptions.firstObject ?
                    CMAudioFormatDescriptionGetStreamBasicDescription((__bridge CMAudioFormatDescriptionRef)audio_track.formatDescriptions.firstObject)->mChannelsPerFrame : 2;
                track_info.sample_rate = audio_track.naturalTimeScale;
                track_info.language = [audio_track.languageCode UTF8String];
                audio_tracks.push_back(track_info);
            }

            audio_load_success = true;
        }
        dispatch_group_leave(load_group);
    }];

    // Wait for all loading operations with timeout
    dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC);
    long result = dispatch_group_wait(load_group, timeout);
    dispatch_release(load_group);

    // Check if loading was successful
    ERR_FAIL_COND_MSG(!video_load_success, "Failed to load video tracks.");

    // Create player item with improved buffering
    player_item = [AVPlayerItem playerItemWithAsset:asset];
    ERR_FAIL_COND_MSG(!player_item, "Failed to create player item.");

    // Configure buffering behavior
    player_item.preferredForwardBufferDuration = 2.0; // Buffer 2 seconds ahead
    [player_item setPreferredMaximumResolution:CGSizeMake(size.x, size.y)];

    // Add video output to player item
    [player_item addOutput:video_output];

    // Create player with initial settings
    player = [[AVPlayer alloc] initWithPlayerItem:player_item];
    ERR_FAIL_COND_MSG(!player, "Failed to create player.");
}

void VideoStreamPlaybackAVF::video_write() {
    if (!video_output || !player_item) {
        return;
    }

    // Get current player time and check if output is ready
    AVPlayerItemVideoOutput* output = (AVPlayerItemVideoOutput*)video_output;
    CMTime player_time = [(AVPlayer*)player currentTime];

    // Early exit if no new frame is available
    if (![output hasNewPixelBufferForItemTime:player_time]) {
        return;
    }

    // Get the pixel buffer for the current time
    CVPixelBufferRef pixel_buffer = [output copyPixelBufferForItemTime:player_time
                                                   itemTimeForDisplay:nil];
    if (!pixel_buffer) {
        return;
    }


    size_t buffer_width = CVPixelBufferGetWidth(pixel_buffer);
    size_t buffer_height = CVPixelBufferGetHeight(pixel_buffer);

    // Use RAII for pixel buffer locking
    struct PixelBufferLock {
        CVPixelBufferRef buffer;
        PixelBufferLock(CVPixelBufferRef b) : buffer(b) {
            CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
        }
        ~PixelBufferLock() {
            CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
            CVBufferRelease(buffer);
        }
    } buffer_lock(pixel_buffer);

    // Get buffer properties
    void* base_address = CVPixelBufferGetBaseAddress(pixel_buffer);
    size_t bytes_per_row = CVPixelBufferGetBytesPerRow(pixel_buffer);

    // Ensure our frame data buffer is the correct size
    size_t required_size = buffer_width * buffer_height * 4;
    if (frame_data.size() != required_size) {
        frame_data.resize(required_size);
    }

    // Get write access to our frame data
    uint8_t* dst = frame_data.ptrw();
    const uint8_t* src = (const uint8_t*)base_address;

    // Optimized copy with BGRA to RGBA conversion
    // Using row-by-row copy to handle potential padding in source buffer
    for (size_t y = 0; y < buffer_height; y++) {
        const uint8_t* row_src = src + (y * bytes_per_row);
        uint8_t* row_dst = dst + (y * buffer_width * 4);

        for (size_t x = 0; x < buffer_width; x++) {
            size_t src_idx = x * 4;
            size_t dst_idx = x * 4;

            // BGRA to RGBA conversion
            row_dst[dst_idx + 0] = row_src[src_idx + 2]; // R
            row_dst[dst_idx + 1] = row_src[src_idx + 1]; // G
            row_dst[dst_idx + 2] = row_src[src_idx + 0]; // B
            row_dst[dst_idx + 3] = row_src[src_idx + 3]; // A
        }
    }

    // Create PackedByteArray for the Image
    PackedByteArray pba;
    pba.resize(frame_data.size());
    memcpy(pba.ptrw(), frame_data.ptr(), frame_data.size());

    // Update texture with new frame
    Ref<Image> img = Image::create_from_data(
        buffer_width,
        buffer_height,
        false, // no mipmaps
        Image::FORMAT_RGBA8,
        pba
    );

    if (img.is_valid()) {
        texture->update(img);
        frames_pending = 1;
    }
}

void VideoStreamPlaybackAVF::_update(double p_delta) {
  if (!playing || paused) {
    return;
  }

  time += p_delta;

  if (player) {
    CMTime current_time = [(AVPlayer *)player currentTime];
    double current_seconds = CMTimeGetSeconds(current_time);

    // Only write video if we're not too far ahead
    if (current_seconds <= time) {
      video_write();
    }
  }
}

void VideoStreamPlaybackAVF::_play() {
  if (!playing) {
    time = 0;
  } else {
    _stop();
  }

  playing = true;

  if (player) {
    [(AVPlayer *)player play];
  }
}

void VideoStreamPlaybackAVF::_stop() {
  if (playing) {
    if (player) {
      [(AVPlayer *)player pause];
      [(AVPlayer *)player seekToTime:kCMTimeZero];
    }
    clear();
    set_file(file_name); // reset
  }
  playing = false;
  time = 0;
}

bool VideoStreamPlaybackAVF::_is_playing() const { return playing; }

void VideoStreamPlaybackAVF::_set_paused(bool p_paused) {
  if (playing && player) {
    if (p_paused) {
      [(AVPlayer *)player pause];
    } else {
      [(AVPlayer *)player play];
    }
  }
  paused = p_paused;
}

bool VideoStreamPlaybackAVF::_is_paused() const { return paused; }

double VideoStreamPlaybackAVF::_get_length() const {
  if (player_item) {
    CMTime duration = [(AVPlayerItem *)player_item duration];
    return CMTimeGetSeconds(duration);
  }
  return 0.0;
}

double VideoStreamPlaybackAVF::_get_playback_position() const {
  if (player) {
    CMTime current_time = [(AVPlayer *)player currentTime];
    return CMTimeGetSeconds(current_time);
  }
  return 0.0;
}

void VideoStreamPlaybackAVF::_seek(double p_time) {
  if (player) {
    CMTime seek_time = CMTimeMakeWithSeconds(p_time, NSEC_PER_SEC);
    [(AVPlayer *)player seekToTime:seek_time];
  }
}

Ref<Texture2D> VideoStreamPlaybackAVF::_get_texture() const { return texture; }

void VideoStreamPlaybackAVF::_set_audio_track(int p_idx) {
  // TODO: Implement audio track selection
}

int VideoStreamPlaybackAVF::_get_channels() const {
  // TODO: Get actual audio channel count
  return 2;
}

int VideoStreamPlaybackAVF::_get_mix_rate() const {
  // TODO: Get actual audio sample rate
  return 44100;
}

// ResourceFormatLoaderAVF Implementation
Variant ResourceFormatLoaderAVF::_load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int32_t p_cache_mode) const {
  VideoStreamAVF *stream = memnew(VideoStreamAVF);
  stream->set_file(p_path);

  Ref<VideoStreamAVF> avf_stream = Ref<VideoStreamAVF>(stream);

  return { avf_stream };
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
