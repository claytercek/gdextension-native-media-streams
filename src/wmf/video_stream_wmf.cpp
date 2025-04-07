#include "video_stream_wmf.hpp"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>

using namespace godot;

// -----------------------------------------------------------------------------
// VideoStreamWMF Implementation
// -----------------------------------------------------------------------------
void VideoStreamWMF::_bind_methods() {
  // No additional bindings needed for the stream class
}

Ref<VideoStreamPlayback> VideoStreamWMF::_instantiate_playback() {
    Ref<VideoStreamPlaybackWMF> playback;
    playback.instantiate();
    playback->set_file(get_file());
    return playback;
}

// -----------------------------------------------------------------------------
// VideoStreamPlaybackWMF Implementation
// -----------------------------------------------------------------------------
void VideoStreamPlaybackWMF::_bind_methods() {
  // No additional bindings needed as we're implementing VideoStreamPlayback interface
}

VideoStreamPlaybackWMF::VideoStreamPlaybackWMF() { 
    texture.instantiate();
}

VideoStreamPlaybackWMF::~VideoStreamPlaybackWMF() { 
    // Component cleanup happens in their destructors
}

// -----------------------------------------------------------------------------
// Resource Management
// -----------------------------------------------------------------------------
void VideoStreamPlaybackWMF::set_file(const String &p_file) {
    file_name = p_file;
    
    Ref<FileAccess> file = FileAccess::open(p_file, FileAccess::READ);
    ERR_FAIL_COND_MSG(file.is_null(), "Cannot open file '" + p_file + "'.");
    
    // Reset state
    initialization_complete = false;
    state.playing = false;
    state.paused = false;
    
    // Create the media source
    if (!media_source.create_source_reader(file->get_path_absolute())) {
        ERR_FAIL_MSG("Failed to create media source for '" + p_file + "'.");
    }
    
    // Set up video decoder
    if (!video_decoder.setup_video_stream(media_source.get_source_reader())) {
        ERR_FAIL_MSG("Failed to setup video stream for '" + p_file + "'.");
    }
    
    // Set up audio handler (optional, may fail without error)
    audio_handler.setup_audio_stream(media_source.get_source_reader());
    
    // Initialize frame queue
    frame_queue.clear();
    
    // Set up dimensions and frame rate
    dimensions.frame = video_decoder.get_dimensions();
    state.fps = video_decoder.get_framerate();
    
    // Create initial texture
    Ref<Image> img = Image::create_empty(dimensions.frame.x, dimensions.frame.y, false, Image::FORMAT_RGBA8);
    if (img.is_null()) {
        ERR_FAIL_MSG("Failed to create initial texture");
    }
    
    texture->set_image(img);
    
    initialization_complete = true;
    if (play_requested) {
        _play();
    }
}

void VideoStreamPlaybackWMF::_seek(double p_time) {
    // Reset state
    frame_queue.clear();
    audio_handler.clear_audio_sample_queue();
    state.engine_time = p_time;
    last_frame_time = p_time;
    
    // Perform seek
    if (media_source.seek_to_position(p_time)) {
        UtilityFunctions::print_verbose("Seek performed to: " + String::num_real(p_time));
    }
}

void VideoStreamPlaybackWMF::_play() {
    UtilityFunctions::print_verbose("VideoStreamPlaybackWMF::_play() invoked.");
    
    if (!initialization_complete) {
        UtilityFunctions::print_verbose("VideoStreamPlaybackWMF::_play() initialization not complete, deferring play.");
        play_requested = true;
        return;
    }
    
    if (!state.playing) {
        // Reset to beginning
        _seek(0.0);
        frame_queue.clear();
        state.engine_time = 0.0;
        state.playing = true;
        state.paused = false;
    } else if (state.paused) {
        state.paused = false;
    }
}

// -----------------------------------------------------------------------------
// Video Processing
// -----------------------------------------------------------------------------
void VideoStreamPlaybackWMF::process_frame_queue() {
    auto source_reader = media_source.get_source_reader();
    if (!source_reader) return;
    
    // Process video frames
    video_decoder.process_frames(source_reader, frame_queue);
    
    // Process audio samples
    if (state.playing && !state.paused) {
        audio_handler.process_audio_samples(source_reader, state.engine_time);
    }
}

// -----------------------------------------------------------------------------
// Public Interface
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Getters
// -----------------------------------------------------------------------------
bool VideoStreamPlaybackWMF::_is_playing() const { 
    return state.playing; 
}

bool VideoStreamPlaybackWMF::_is_paused() const { 
    return state.paused; 
}

double VideoStreamPlaybackWMF::_get_length() const {
    return media_source.get_duration();
}

double VideoStreamPlaybackWMF::get_media_time() const {
    // Use engine time for consistent playback timing when active
    if (state.playing && !state.paused) {
        return state.engine_time;
    }
    
    // For paused or stopped states, use last frame time for stability
    return last_frame_time;
}

double VideoStreamPlaybackWMF::_get_playback_position() const {
    return get_media_time();
}

Ref<Texture2D> VideoStreamPlaybackWMF::_get_texture() const { 
    return texture; 
}

// -----------------------------------------------------------------------------
// Audio Handling
// -----------------------------------------------------------------------------
void VideoStreamPlaybackWMF::_set_audio_track(int p_idx) {
    audio_handler.set_audio_track(p_idx);
}

int VideoStreamPlaybackWMF::_get_channels() const {
    return audio_handler.get_channels();
}

int VideoStreamPlaybackWMF::_get_mix_rate() const {
    return audio_handler.get_mix_rate();
}

void VideoStreamPlaybackWMF::_stop() {
    if (!media_source.get_source_reader()) return;
    
    // Reset to beginning
    _seek(0.0);
    frame_queue.clear();
    state.playing = false;
    state.paused = false;
    state.engine_time = 0.0;
}

void VideoStreamPlaybackWMF::_set_paused(bool p_paused) {
    if (state.paused == p_paused) return;
    
    state.paused = p_paused;
    // No need to do anything with WMF, we control playback through our read loop
}

// Improved update_frame_queue with better audio handling
void VideoStreamPlaybackWMF::update_frame_queue(double p_delta) {
    auto source_reader = media_source.get_source_reader();
    if (!source_reader) return;
    
    // Update our engine time properly first before any other operations
    if (state.playing && !state.paused) {
        state.engine_time += p_delta;
    }
    
    // Process frames based on buffering needs
    if (frame_queue.should_decode(state.engine_time, state.fps)) {
        process_frame_queue();
    }
    
    // Process audio synchronization - do this every frame for smoother audio
    if (state.playing && !state.paused) {
        // Process queued audio samples for the current time
        audio_handler.update_audio_sync(state.engine_time);
    }
    
    // Track the last frame time for end-of-stream detection
    if (!frame_queue.empty()) {
        auto next_frame = frame_queue.peek_next_frame();
        if (next_frame) {
            last_frame_time = next_frame->presentation_time;
        }
    }
}

bool VideoStreamPlaybackWMF::check_end_of_stream() {
    if (!media_source.get_source_reader()) return true;
    
    // Check if we've reached the end of the stream by comparing 
    // the current time with the duration
    double media_time = get_media_time();
    double duration_sec = media_source.get_duration();
    
    // Consider the stream ended if:
    // 1. We've reached the end of the stream duration
    // 2. Or we're past the last frame and the queue is empty (for cases where duration isn't accurate)
    // Also ensure we're not just at the beginning with some buffering delay
    return (duration_sec > 0.0 && media_time >= duration_sec - 0.1) || 
           (frame_queue.empty() && last_frame_time > 0.5 && media_time > last_frame_time + 1.0);
}