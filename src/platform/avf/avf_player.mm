#include "avf_player.hpp"
#include <godot_cpp/variant/utility_functions.hpp>

// Include AVFoundation headers
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

namespace godot {

// Forward declarations of private Objective-C implementation classes

// Wrapper for AVPlayer and AVPlayerItem
@interface AVFPlayerImpl : NSObject {
    AVPlayer* player;
    AVPlayerItem* playerItem;
    AVPlayerItemVideoOutput* videoOutput;
    
    BOOL isPlaying;
    BOOL isPaused;
    BOOL hasVideo;
    BOOL hasAudio;
    
    Float64 duration;
    NSInteger videoWidth;
    NSInteger videoHeight;
    Float64 framerate;
    
    id timeObserverToken;
}

@property (readonly) AVPlayer* player;
@property (readonly) AVPlayerItem* playerItem;
@property (readonly) AVPlayerItemVideoOutput* videoOutput;
@property (readonly) BOOL hasVideo;
@property (readonly) BOOL hasAudio;
@property (readonly) NSInteger videoWidth;
@property (readonly) NSInteger videoHeight;
@property (readonly) Float64 framerate;
@property (readonly) Float64 duration;

- (instancetype)init;
- (BOOL)openURL:(NSURL*)url;
- (void)close;
- (void)play;
- (void)pause;
- (void)stop;
- (void)seekToTime:(Float64)seconds;
- (Float64)getCurrentTime;
- (BOOL)isAtEnd;
- (CVPixelBufferRef)copyNextPixelBuffer:(Float64)atTime;

@end

// Wrapper for AVAssetReader for audio extraction
@interface AVFAudioReaderImpl : NSObject {
    AVAssetReader* assetReader;
    AVAssetReaderTrackOutput* trackOutput;
    
    CMTimeRange timeRange;
    Float64 sampleRate;
    NSInteger channelCount;
    BOOL isSetup;
}

@property (readonly) Float64 sampleRate;
@property (readonly) NSInteger channelCount;

- (instancetype)init;
- (BOOL)setupWithAsset:(AVAsset*)asset timeRange:(CMTimeRange)range;
- (void)close;
- (CMSampleBufferRef)copyNextSampleBuffer;
- (BOOL)isAtEnd;

@end

// Implementation of AVFPlayerImpl
@implementation AVFPlayerImpl

@synthesize player, playerItem, videoOutput, hasVideo, hasAudio, videoWidth, videoHeight, framerate, duration;

- (instancetype)init {
    self = [super init];
    if (self) {
        player = nil;
        playerItem = nil;
        videoOutput = nil;
        isPlaying = NO;
        isPaused = NO;
        hasVideo = NO;
        hasAudio = NO;
        duration = 0.0;
        videoWidth = 0;
        videoHeight = 0;
        framerate = 0.0;
        timeObserverToken = nil;
    }
    return self;
}

- (void)dealloc {
    [self close];
    [super dealloc];
}

- (BOOL)openURL:(NSURL*)url {
    // Clean up any existing resources
    [self close];
    
    // Create an AVAsset
    AVAsset* asset = [AVAsset assetWithURL:url];
    if (!asset) {
        return NO;
    }
    
    // Create player item
    playerItem = [[AVPlayerItem alloc] initWithAsset:asset];
    if (!playerItem) {
        return NO;
    }
    
    // Create player
    player = [[AVPlayer alloc] initWithPlayerItem:playerItem];
    if (!player) {
        [playerItem release];
        playerItem = nil;
        return NO;
    }
    
    // Pause immediately (we'll handle playback manually)
    [player pause];
    
    // Check for video and audio tracks
    NSArray* videoTracks = [asset tracksWithMediaType:AVMediaTypeVideo];
    NSArray* audioTracks = [asset tracksWithMediaType:AVMediaTypeAudio];
    
    hasVideo = [videoTracks count] > 0;
    hasAudio = [audioTracks count] > 0;
    
    // Set up video output if we have video
    if (hasVideo) {
        // Get track dimensions
        AVAssetTrack* videoTrack = [videoTracks objectAtIndex:0];
        CGSize trackDimensions = [videoTrack naturalSize];
        videoWidth = trackDimensions.width;
        videoHeight = trackDimensions.height;
        
        // Get framerate
        float nominalFrameRate = [videoTrack nominalFrameRate];
        framerate = (nominalFrameRate > 0) ? nominalFrameRate : 30.0;
        
        // Create video output with BGRA pixel format (which we'll convert to RGBA later)
        NSDictionary* pixelBufferAttributes = @{
            (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
            (id)kCVPixelBufferOpenGLCompatibilityKey: @YES,
            (id)kCVPixelBufferIOSurfacePropertiesKey: @{}
        };
        
        videoOutput = [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:pixelBufferAttributes];
        [playerItem addOutput:videoOutput];
    }
    
    // Get duration
    if (CMTIME_IS_NUMERIC([asset duration])) {
        duration = CMTimeGetSeconds([asset duration]);
    }
    
    // Add time observer for tracking playback
    __block AVFPlayerImpl* blockSelf = self;
    timeObserverToken = [player addPeriodicTimeObserverForInterval:CMTimeMake(1, 30)
                                                            queue:dispatch_get_main_queue()
                                                       usingBlock:^(CMTime time) {
        // This block will be called periodically during playback
        if (CMTIME_IS_VALID(time) && blockSelf->isPlaying && !blockSelf->isPaused) {
            // Check for end of playback
            if (CMTimeGetSeconds(time) >= blockSelf->duration - 0.1) {
                [blockSelf->player pause];
                [blockSelf->player seekToTime:kCMTimeZero toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
                blockSelf->isPlaying = NO;
            }
        }
    }];
    
    return YES;
}

- (void)close {
    // Clean up time observer
    if (timeObserverToken) {
        [player removeTimeObserver:timeObserverToken];
        timeObserverToken = nil;
    }
    
    // Clean up video output
    if (videoOutput) {
        [playerItem removeOutput:videoOutput];
        [videoOutput release];
        videoOutput = nil;
    }
    
    // Clean up player and player item
    if (player) {
        [player pause];
        [player release];
        player = nil;
    }
    
    if (playerItem) {
        [playerItem release];
        playerItem = nil;
    }
    
    // Reset state
    isPlaying = NO;
    isPaused = NO;
    hasVideo = NO;
    hasAudio = NO;
    duration = 0.0;
    videoWidth = 0;
    videoHeight = 0;
    framerate = 0.0;
}

- (void)play {
    if (player) {
        [player play];
        isPlaying = YES;
        isPaused = NO;
    }
}

- (void)pause {
    if (player) {
        [player pause];
        isPaused = YES;
    }
}

- (void)stop {
    if (player) {
        [player pause];
        [player seekToTime:kCMTimeZero toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
        isPlaying = NO;
        isPaused = NO;
    }
}

- (void)seekToTime:(Float64)seconds {
    if (player) {
        CMTime seekTime = CMTimeMakeWithSeconds(seconds, NSEC_PER_SEC);
        [player seekToTime:seekTime toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
    }
}

- (Float64)getCurrentTime {
    if (player) {
        CMTime currentTime = [player currentTime];
        return CMTIME_IS_VALID(currentTime) ? CMTimeGetSeconds(currentTime) : 0.0;
    }
    return 0.0;
}

- (BOOL)isAtEnd {
    if (player && duration > 0.0) {
        return [self getCurrentTime] >= duration - 0.1;
    }
    return NO;
}

- (CVPixelBufferRef)copyNextPixelBuffer:(Float64)atTime {
    if (!videoOutput) return NULL;
    
    CMTime outputItemTime = CMTimeMakeWithSeconds(atTime, NSEC_PER_SEC);
    
    if ([videoOutput hasNewPixelBufferForItemTime:outputItemTime]) {
        return [videoOutput copyPixelBufferForItemTime:outputItemTime itemTimeForDisplay:nil];
    }
    
    return NULL;
}

@end

// Implementation of AVFAudioReaderImpl
@implementation AVFAudioReaderImpl

@synthesize sampleRate, channelCount;

- (instancetype)init {
    self = [super init];
    if (self) {
        assetReader = nil;
        trackOutput = nil;
        sampleRate = 0.0;
        channelCount = 0;
        isSetup = NO;
    }
    return self;
}

- (void)dealloc {
    [self close];
    [super dealloc];
}

- (BOOL)setupWithAsset:(AVAsset*)asset timeRange:(CMTimeRange)range {
    // Clean up any existing reader
    [self close];
    
    // Check for audio tracks
    NSArray* audioTracks = [asset tracksWithMediaType:AVMediaTypeAudio];
    if ([audioTracks count] == 0) {
        return NO;
    }
    
    // Get the first audio track
    AVAssetTrack* audioTrack = [audioTracks objectAtIndex:0];
    
    // Create asset reader
    NSError* error = nil;
    assetReader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
    if (error || !assetReader) {
        NSLog(@"Failed to create asset reader: %@", error);
        return NO;
    }
    
    // Set time range if valid
    if (CMTIMERANGE_IS_VALID(range)) {
        timeRange = range;
        assetReader.timeRange = range;
    }
    
    // Create output settings for linear PCM
    NSDictionary* outputSettings = @{
        AVFormatIDKey: @(kAudioFormatLinearPCM),
        AVSampleRateKey: @(44100),
        AVNumberOfChannelsKey: @(2),
        AVLinearPCMBitDepthKey: @(32),
        AVLinearPCMIsFloatKey: @YES,
        AVLinearPCMIsNonInterleaved: @NO
    };
    
    // Create track output
    trackOutput = [[AVAssetReaderTrackOutput alloc] initWithTrack:audioTrack outputSettings:outputSettings];
    if (!trackOutput) {
        [assetReader release];
        assetReader = nil;
        return NO;
    }
    
    [assetReader addOutput:trackOutput];
    
    // Get audio format information
    CMAudioFormatDescriptionRef formatDesc = (__bridge CMAudioFormatDescriptionRef)[audioTrack.formatDescriptions firstObject];
    if (formatDesc) {
        const AudioStreamBasicDescription *basicDesc = CMAudioFormatDescriptionGetStreamBasicDescription(formatDesc);
        if (basicDesc) {
            sampleRate = basicDesc->mSampleRate;
            channelCount = basicDesc->mChannelsPerFrame;
        }
    }
    
    // Use 44.1kHz stereo as fallback
    if (sampleRate <= 0) sampleRate = 44100.0;
    if (channelCount <= 0) channelCount = 2;
    
    // Start reading
    if (![assetReader startReading]) {
        NSLog(@"Failed to start reading audio: %@", assetReader.error);
        [trackOutput release];
        trackOutput = nil;
        [assetReader release];
        assetReader = nil;
        return NO;
    }
    
    isSetup = YES;
    return YES;
}

- (void)close {
    if (trackOutput) {
        [trackOutput release];
        trackOutput = nil;
    }
    
    if (assetReader) {
        [assetReader cancelReading];
        [assetReader release];
        assetReader = nil;
    }
    
    isSetup = NO;
}

- (CMSampleBufferRef)copyNextSampleBuffer {
    if (!isSetup || !assetReader || !trackOutput) {
        return NULL;
    }
    
    if (assetReader.status != AVAssetReaderStatusReading) {
        return NULL;
    }
    
    CMSampleBufferRef sampleBuffer = [trackOutput copyNextSampleBuffer];
    return sampleBuffer;
}

- (BOOL)isAtEnd {
    return assetReader.status == AVAssetReaderStatusCompleted;
}

@end

// C++ implementation of AVFPlayer

AVFPlayer::AVFPlayer() 
    : player(nullptr), audio_reader(nullptr) {
    // Create AVF player implementation
    player = new AVFPlayerImpl();
    audio_reader = new AVFAudioReaderImpl();
}

AVFPlayer::~AVFPlayer() {
    // Close media and release resources
    close();
    
    // Delete implementations
    if (player) {
        delete player;
        player = nullptr;
    }
    
    if (audio_reader) {
        delete audio_reader;
        audio_reader = nullptr;
    }
}

bool AVFPlayer::open(const std::string& file_path) {
    close(); // Close any open media
    
    if (file_path.empty()) {
        UtilityFunctions::printerr("Empty file path provided to AVFPlayer");
        return false;
    }
    
    // Convert path to NSURL
    NSString* path = [NSString stringWithUTF8String:file_path.c_str()];
    NSURL* url = [NSURL fileURLWithPath:path];
    if (!url) {
        UtilityFunctions::printerr("Failed to create URL from path: " + String(file_path.c_str()));
        return false;
    }
    
    // Open media using AVFPlayerImpl
    if (![(AVFPlayerImpl*)player openURL:url]) {
        UtilityFunctions::printerr("Failed to open media file: " + String(file_path.c_str()));
        return false;
    }
    
    // Update media info
    update_media_info();
    
    // Setup audio reader
    setup_audio_reader(url);
    
    current_state = State::STOPPED;
    return true;
}

void AVFPlayer::close() {
    // Close player implementation
    if (player) {
        [(AVFPlayerImpl*)player close];
    }
    
    // Close audio reader
    if (audio_reader) {
        [(AVFAudioReaderImpl*)audio_reader close];
    }
    
    // Reset media info
    media_info = MediaInfo();
    current_state = State::STOPPED;
    current_audio_track = 0;
    
    // Reset positions
    last_video_position = 0.0;
    last_audio_position = 0.0;
}

bool AVFPlayer::is_open() const {
    return player && [(AVFPlayerImpl*)player player] != nil;
}

void AVFPlayer::play() {
    if (!is_open()) return;
    
    [(AVFPlayerImpl*)player play];
    current_state = State::PLAYING;
}

void AVFPlayer::pause() {
    if (!is_open()) return;
    
    if (current_state == State::PLAYING) {
        [(AVFPlayerImpl*)player pause];
        current_state = State::PAUSED;
    }
}

void AVFPlayer::stop() {
    if (!is_open()) return;
    
    [(AVFPlayerImpl*)player stop];
    current_state = State::STOPPED;
    
    // Reset positions
    last_video_position = 0.0;
    last_audio_position = 0.0;
}

void AVFPlayer::seek(double time_sec) {
    if (!is_open()) return;
    
    // Seek with AVFPlayerImpl
    [(AVFPlayerImpl*)player seekToTime:time_sec];
    
    // Setup a new audio reader at this position
    // Need to recreate the reader since AVAssetReader can't seek
    if ([(AVFPlayerImpl*)player hasAudio]) {
        NSString* path = [[(AVFPlayerImpl*)player playerItem].asset.URL path];
        NSURL* url = [NSURL fileURLWithPath:path];
        setup_audio_reader(url, time_sec);
    }
    
    // Update positions
    last_video_position = time_sec;
    last_audio_position = time_sec;
}

IMediaPlayer::State AVFPlayer::get_state() const {
    return current_state;
}

bool AVFPlayer::is_playing() const {
    return current_state == State::PLAYING;
}

bool AVFPlayer::is_paused() const {
    return current_state == State::PAUSED;
}

bool AVFPlayer::has_ended() const {
    if (!is_open() || current_state == State::STOPPED) {
        return false;
    }
    
    return [(AVFPlayerImpl*)player isAtEnd];
}

IMediaPlayer::MediaInfo AVFPlayer::get_media_info() const {
    return media_info;
}

double AVFPlayer::get_position() const {
    if (!is_open()) return 0.0;
    
    return [(AVFPlayerImpl*)player getCurrentTime];
}

bool AVFPlayer::read_video_frame(VideoFrame& frame) {
    if (!is_open() || current_state == State::STOPPED || ![(AVFPlayerImpl*)player hasVideo]) {
        return false;
    }
    
    // Calculate precise frame time based on playback position and framerate
    double position = get_position();
    
    // Get pixel buffer at this time from AVPlayerItemVideoOutput
    CVPixelBufferRef pixelBuffer = [(AVFPlayerImpl*)player copyNextPixelBuffer:position];
    if (!pixelBuffer) {
        return false;
    }
    
    // RAII for pixel buffer
    struct PixelBufferReleaser {
        CVPixelBufferRef buffer;
        PixelBufferReleaser(CVPixelBufferRef b) : buffer(b) {}
        ~PixelBufferReleaser() { if (buffer) CVBufferRelease(buffer); }
    } releaser(pixelBuffer);
    
    // Lock the pixel buffer base address
    CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    
    // RAII for unlocking
    struct PixelBufferUnlocker {
        CVPixelBufferRef buffer;
        PixelBufferUnlocker(CVPixelBufferRef b) : buffer(b) {}
        ~PixelBufferUnlocker() { CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly); }
    } unlocker(pixelBuffer);
    
    // Get buffer dimensions and data
    size_t width = CVPixelBufferGetWidth(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);
    size_t stride = CVPixelBufferGetBytesPerRow(pixelBuffer);
    void* baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);
    
    if (!baseAddress) {
        return false;
    }
    
    // Set frame dimensions and timestamp
    frame.size.x = width;
    frame.size.y = height;
    frame.presentation_time = position;
    last_video_position = position;
    
    // Allocate frame data
    frame.data.resize(width * height * 4); // RGBA format (4 bytes per pixel)
    
    // Convert BGRA to RGBA (AVFoundation gives us BGRA, Godot expects RGBA)
    const uint8_t* src = static_cast<const uint8_t*>(baseAddress);
    
    if (width * 4 == stride) {
        // No padding, can convert in one loop
        for (size_t i = 0; i < width * height; i++) {
            size_t src_offset = i * 4;
            size_t dst_offset = i * 4;
            
            // Copy pixel data, swapping R and B channels
            frame.data[dst_offset + 0] = src[src_offset + 2]; // R <- B
            frame.data[dst_offset + 1] = src[src_offset + 1]; // G <- G
            frame.data[dst_offset + 2] = src[src_offset + 0]; // B <- R
            frame.data[dst_offset + 3] = src[src_offset + 3]; // A <- A
        }
    } else {
        // Handle padding - convert row by row
        for (size_t y = 0; y < height; y++) {
            const uint8_t* src_row = src + y * stride;
            for (size_t x = 0; x < width; x++) {
                size_t src_offset = x * 4;
                size_t dst_offset = (y * width + x) * 4;
                
                // Copy pixel data, swapping R and B channels
                frame.data[dst_offset + 0] = src_row[src_offset + 2]; // R <- B
                frame.data[dst_offset + 1] = src_row[src_offset + 1]; // G <- G
                frame.data[dst_offset + 2] = src_row[src_offset + 0]; // B <- R
                frame.data[dst_offset + 3] = src_row[src_offset + 3]; // A <- A
            }
        }
    }
    
    return true;
}

bool AVFPlayer::read_audio_frame(AudioFrame& frame) {
    if (!is_open() || current_state == State::STOPPED || ![(AVFPlayerImpl*)player hasAudio]) {
        return false;
    }
    
    // If the audio reader is not setup, try to setup it again
    if (![(AVFAudioReaderImpl*)audio_reader sampleRate]) {
        NSString* path = [[(AVFPlayerImpl*)player playerItem].asset.URL path];
        NSURL* url = [NSURL fileURLWithPath:path];
        if (!setup_audio_reader(url)) {
            return false;
        }
    }
    
    // Check if we've reached the end of the audio stream
    if ([(AVFAudioReaderImpl*)audio_reader isAtEnd]) {
        return false;
    }
    
    // Get the next audio sample buffer
    CMSampleBufferRef sampleBuffer = [(AVFAudioReaderImpl*)audio_reader copyNextSampleBuffer];
    if (!sampleBuffer) {
        return false;
    }
    
    // RAII for sample buffer
    struct SampleBufferReleaser {
        CMSampleBufferRef buffer;
        SampleBufferReleaser(CMSampleBufferRef b) : buffer(b) {}
        ~SampleBufferReleaser() { if (buffer) CFRelease(buffer); }
    } releaser(sampleBuffer);
    
    // Get presentation timestamp
    CMTime presentationTime = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
    frame.presentation_time = CMTimeGetSeconds(presentationTime);
    last_audio_position = frame.presentation_time;
    
    // Get audio channel and sample rate info
    frame.channels = [(AVFAudioReaderImpl*)audio_reader channelCount];
    frame.sample_rate = [(AVFAudioReaderImpl*)audio_reader sampleRate];
    
    // Get audio data
    AudioBufferList audioBufferList;
    CMBlockBufferRef blockBuffer;
    
    OSStatus status = CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
        sampleBuffer,
        nullptr,
        &audioBufferList,
        sizeof(audioBufferList),
        nullptr,
        nullptr,
        kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment,
        &blockBuffer
    );
    
    if (status != noErr) {
        return false;
    }
    
    // RAII for block buffer
    struct BlockBufferReleaser {
        CMBlockBufferRef buffer;
        BlockBufferReleaser(CMBlockBufferRef b) : buffer(b) {}
        ~BlockBufferReleaser() { if (buffer) CFRelease(buffer); }
    } blockReleaser(blockBuffer);
    
    // Copy audio data
    size_t totalBytes = 0;
    for (size_t i = 0; i < audioBufferList.mNumberBuffers; i++) {
        totalBytes += audioBufferList.mBuffers[i].mDataByteSize;
    }
    
    // Calculate total number of float samples
    size_t numSamples = totalBytes / sizeof(float);
    
    // Resize the audio frame data
    frame.data.resize(numSamples);
    
    // Copy audio data (assuming interleaved PCM float format)
    if (audioBufferList.mNumberBuffers == 1) {
        // Interleaved data - copy as-is
        const float* src = static_cast<const float*>(audioBufferList.mBuffers[0].mData);
        for (size_t i = 0; i < numSamples; i++) {
            frame.data.set(i, src[i]);
        }
    } else {
        // Non-interleaved data - we need to interleave it
        size_t sampleCount = numSamples / frame.channels;
        for (size_t i = 0; i < sampleCount; i++) {
            for (size_t ch = 0; ch < frame.channels; ch++) {
                const float* src = static_cast<const float*>(audioBufferList.mBuffers[ch].mData);
                frame.data.set(i * frame.channels + ch, src[i]);
            }
        }
    }
    
    return true;
}

int AVFPlayer::get_audio_track_count() const {
    // For now, return 1 if we have audio, 0 otherwise
    if (is_open() && [(AVFPlayerImpl*)player hasAudio]) {
        return 1;
    }
    return 0;
}

IMediaPlayer::TrackInfo AVFPlayer::get_audio_track_info(int track_index) const {
    TrackInfo info;
    info.index = track_index;
    info.language = ""; // AVFoundation doesn't easily expose language info through our API
    info.name = "Audio Track " + std::to_string(track_index + 1);
    return info;
}

void AVFPlayer::set_audio_track(int track_index) {
    // Only one track supported for now
    current_audio_track = track_index;
}

int AVFPlayer::get_current_audio_track() const {
    return current_audio_track;
}

void AVFPlayer::update_media_info() {
    if (!player) return;
    
    AVFPlayerImpl* avfPlayer = (AVFPlayerImpl*)player;
    
    // Set video properties
    media_info.width = avfPlayer.videoWidth;
    media_info.height = avfPlayer.videoHeight;
    media_info.framerate = avfPlayer.framerate;
    media_info.duration = avfPlayer.duration;
    
    // Set audio properties
    if (audio_reader) {
        AVFAudioReaderImpl* avfAudioReader = (AVFAudioReaderImpl*)audio_reader;
        media_info.audio_channels = avfAudioReader.channelCount;
        media_info.audio_sample_rate = avfAudioReader.sampleRate;
    }
}

bool AVFPlayer::setup_audio_reader(NSURL* url, double start_time = 0.0) {
    if (!audio_reader || !player || ![(AVFPlayerImpl*)player hasAudio]) {
        return false;
    }
    
    // Clean up existing reader
    [(AVFAudioReaderImpl*)audio_reader close];
    
    // Create AVAsset from URL
    AVAsset* asset = [AVAsset assetWithURL:url];
    if (!asset) {
        return false;
    }
    
    // Create time range starting from current position
    CMTimeRange timeRange = kCMTimeRangeInvalid;
    if (start_time > 0.0) {
        CMTime startTime = CMTimeMakeWithSeconds(start_time, NSEC_PER_SEC);
        CMTime duration = CMTimeMakeWithSeconds([(AVFPlayerImpl*)player duration] - start_time, NSEC_PER_SEC);
        timeRange = CMTimeRangeMake(startTime, duration);
    }
    
    // Setup the audio reader
    return [(AVFAudioReaderImpl*)audio_reader setupWithAsset:asset timeRange:timeRange];
}

} // namespace godot