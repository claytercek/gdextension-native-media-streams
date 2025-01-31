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

Ref<VideoStreamPlayback> VideoStreamAVF::_instantiate_playback() {
    Ref<VideoStreamPlaybackAVF> playback;
    playback.instantiate();
    playback->set_file(get_file());
    return playback;
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

    if (audio_output) {
        [(AVAssetReaderTrackOutput*)audio_output release];
        audio_output = nullptr;
    }

    if (audio_reader) {
        [(AVAssetReader*)audio_reader release];
        audio_reader = nullptr;
    }

    player_item = nullptr;
    state.playing = false;
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

            audio_success = true;
        }
        dispatch_group_leave(group);
    }];

    // Wait for both tasks to complete
    dispatch_group_notify(group, dispatch_get_main_queue(), ^{
        if (video_success && audio_success) {
            // Create player item and add outputs
            AVPlayerItem *item = [AVPlayerItem playerItemWithAsset:asset];
            if (!item) {
                clear_avf_objects();
                UtilityFunctions::printerr("Failed to create player item");
                return;
            }

            [item addOutput:(AVPlayerItemVideoOutput*)video_output];
            player_item = item;

            // Create player with audio volume set to 0 (we'll handle audio ourselves)
            AVPlayer *avf_player = [[AVPlayer alloc] initWithPlayerItem:item];
            if (!avf_player) {
                clear_avf_objects();
                UtilityFunctions::printerr("Failed to create player");
                return;
            }
            [avf_player setVolume:0.0f];  // Mute native audio output
            player = avf_player;

            // Setup audio reader
            if (!setup_audio_reader()) {
                UtilityFunctions::printerr("Warning: Failed to setup audio reader");
            }

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

bool VideoStreamPlaybackAVF::setup_audio_reader() {
    if (!player_item) return false;

    AVAsset* asset = [(AVPlayerItem*)player_item asset];
    if (!asset) return false;

    // Clean up existing reader if any
    if (audio_reader) {
        [(AVAssetReader*)audio_reader release];
        audio_reader = nullptr;
    }
    if (audio_output) {
        [(AVAssetReaderTrackOutput*)audio_output release];
        audio_output = nullptr;
    }

    NSError* error = nil;
    AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
    if (error) {
        UtilityFunctions::printerr("Failed to create audio reader: ", error.localizedDescription.UTF8String);
        return false;
    }

    // Get the first audio track using the new async API
    __block AVAssetTrack* audioTrack = nil;
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    
    [asset loadTracksWithMediaType:AVMediaTypeAudio completionHandler:^(NSArray<AVAssetTrack *> *tracks, NSError *trackError) {
        if (!trackError && tracks.count > 0) {
            audioTrack = [tracks firstObject];
        }
        dispatch_semaphore_signal(semaphore);
    }];
    
    dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 1 * NSEC_PER_SEC));
    dispatch_release(semaphore);
    
    if (!audioTrack) {
        [reader release];
        return false;
    }

    // Configure audio output settings
    NSDictionary* outputSettings = @{
        AVFormatIDKey: @(kAudioFormatLinearPCM),
        AVSampleRateKey: @(mix_rate),
        AVNumberOfChannelsKey: @(channels),
        AVLinearPCMBitDepthKey: @32,
        AVLinearPCMIsFloatKey: @YES,
        AVLinearPCMIsNonInterleaved: @NO
    };

    // Create audio output
    AVAssetReaderTrackOutput* output = [[AVAssetReaderTrackOutput alloc] 
        initWithTrack:audioTrack outputSettings:outputSettings];
    output.alwaysCopiesSampleData = NO;  // Optimize performance
    [reader addOutput:output];

    // Set initial read position if needed
    if (audio_read_position > 0.0) {
        CMTime startTime = CMTimeMakeWithSeconds(audio_read_position, NSEC_PER_SEC);
        reader.timeRange = CMTimeRangeMake(startTime, kCMTimePositiveInfinity);
    }

    // Start reading
    if (![reader startReading]) {
        UtilityFunctions::printerr("Failed to start audio reader");
        [output release];
        [reader release];
        return false;
    }

    audio_reader = reader;
    audio_output = output;
    return true;
}

void VideoStreamPlaybackAVF::process_audio_queue() {
    if (!audio_output || !audio_reader) return;
    
    // Check if we need to restart the audio reader (after seek or loop)
    if (audio_needs_restart) {
        if (!setup_audio_reader()) {
            UtilityFunctions::printerr("Failed to restart audio reader");
            return;
        }
        audio_needs_restart = false;
        
        // Clear existing audio frames
        for (CMSampleBufferRef buffer : available_audio_frames) {
            if (buffer) {
                CFRelease(buffer);
            }
        }
        available_audio_frames.clear();
    }
    
    AVAssetReaderTrackOutput* output = (AVAssetReaderTrackOutput*)audio_output;
    AVAssetReader* reader = (AVAssetReader*)audio_reader;
    
    // Check reader status
    if (reader.status == AVAssetReaderStatusFailed) {
        UtilityFunctions::printerr("Audio reader failed: ", reader.error.localizedDescription.UTF8String);
        audio_needs_restart = true;
        return;
    }
    
    // Get current video time for sync
    CMTime current_video_time = [(AVPlayer*)player currentTime];
    double video_time = CMTimeGetSeconds(current_video_time);
    
    // Fill the audio frame queue
    while (available_audio_frames.size() < 10) { // Keep a buffer of 10 frames
        CMSampleBufferRef sample_buffer = [output copyNextSampleBuffer];
        if (!sample_buffer) break;
        
        available_audio_frames.push_back(sample_buffer);
    }
    
    // Process available audio frames
    while (available_audio_frames.front()) {
        CMSampleBufferRef sample_buffer = available_audio_frames.front()->get();
        CMTime presentation_time = CMSampleBufferGetPresentationTimeStamp(sample_buffer);
        double frame_time = CMTimeGetSeconds(presentation_time);
        
        // Check if this frame is too far ahead of video
        if (frame_time > video_time + 0.1) { // Reduced tolerance to 100ms
            break; // Future frame, keep it for later
        }
        
        // Only mix frames that are within a tight window of the video time
        if (frame_time <= video_time + 0.1 && frame_time >= video_time - 0.1) {
            @try {
                // Get audio buffer list
                CMBlockBufferRef block_buffer;
                AudioBufferList audio_buffer_list;
                
                OSStatus status = CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
                    sample_buffer,
                    NULL,
                    &audio_buffer_list,
                    sizeof(audio_buffer_list),
                    NULL,
                    NULL,
                    0,
                    &block_buffer
                );
                
                if (status == noErr) {
                    // Copy audio data to our mix buffer
                    float* src = (float*)audio_buffer_list.mBuffers[0].mData;
                    int buffer_size = audio_buffer_list.mBuffers[0].mDataByteSize / sizeof(float);
                    
                    // Resize mix buffer if needed
                    mix_buffer.resize(buffer_size);
                    
                    // Copy samples
                    for (int i = 0; i < buffer_size; i++) {
                        mix_buffer.set(i, src[i]);
                    }
                    
                    // Mix the audio
                    mix_audio(buffer_size / channels, mix_buffer);
                    
                    // Update our tracking time
                    audio_frame_time = frame_time;
                }
                
                // Clean up
                CFRelease(block_buffer);
            } @finally {
                CFRelease(sample_buffer);
            }
        } else if (frame_time < video_time - 0.1) {
            // Frame is too old, discard it
            CFRelease(sample_buffer);
        }
        
        available_audio_frames.pop_front();
    }
    
    // Check if we need more frames
    if (!available_audio_frames.front() && reader.status == AVAssetReaderStatusCompleted) {
        if (state.playing) {
            audio_needs_restart = true;
            audio_read_position = 0.0;
            audio_frame_time = 0.0;
        }
    }
}

void VideoStreamPlaybackAVF::_seek(double p_time) {
    if (!player) return;

    audio_needs_restart = true;
    audio_read_position = p_time;
    audio_frame_time = p_time;

    CMTime seek_time = CMTimeMakeWithSeconds(p_time, NSEC_PER_SEC);
    [(AVPlayer*)player seekToTime:seek_time toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
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
        audio_frame_time = 0.0;
        audio_read_position = 0.0;
        audio_needs_restart = true;

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

// -----------------------------------------------------------------------------
// Video Processing
// -----------------------------------------------------------------------------
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

void VideoStreamPlaybackAVF::process_frame_queue() {
    if (!video_output || !player_item) return;
    
    AVPlayerItemVideoOutput* output = (AVPlayerItemVideoOutput*)video_output;
    double current_time = state.engine_time;
    
    while (frame_queue.size() < FrameQueue::MAX_SIZE) {
        double next_time = predict_next_frame_time(current_time, state.fps);
        CMTime next_frame_time = CMTimeMakeWithSeconds(next_time, NSEC_PER_SEC);
        
        // Check if we have a new frame available
        if (![output hasNewPixelBufferForItemTime:next_frame_time]) {
            break;
        }
        
        CVPixelBufferRef pixel_buffer = [output copyPixelBufferForItemTime:next_frame_time itemTimeForDisplay:nil];
        if (!pixel_buffer) break;
        
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

bool VideoStreamPlaybackAVF::check_end_of_stream() {
    double media_time = get_media_time();
    double duration = _get_length();
    return media_time >= duration;
}

void VideoStreamPlaybackAVF::update_frame_queue(double p_delta) {
    if (frame_queue.should_decode(state.engine_time, state.fps)) {
        process_frame_queue();
        process_audio_queue();
    }
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
    return channels;
}

int VideoStreamPlaybackAVF::_get_mix_rate() const {
    return mix_rate;
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

void VideoStreamPlaybackAVF::_stop() {
    if (!player) return;

    [(AVPlayer*)player pause];
    
    // Clear frame queue and reset state
    frame_queue.clear();
    
    // Clean up audio frames
    for (CMSampleBufferRef buffer : available_audio_frames) {
        if (buffer) {
            CFRelease(buffer);
        }
    }
    available_audio_frames.clear();
    
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

// -----------------------------------------------------------------------------
// Helper Functions
// -----------------------------------------------------------------------------
void VideoStreamPlaybackAVF::ensure_frame_buffer(size_t width, size_t height) {
  size_t required_size = width * height * 4;
  if (frame_buffer.size() != required_size) {
    frame_buffer.resize(required_size);
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
