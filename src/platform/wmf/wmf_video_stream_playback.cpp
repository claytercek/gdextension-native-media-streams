#include "wmf_video_stream_playback.hpp"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/file_access.hpp>

namespace godot {

void VideoStreamPlaybackWMF::_bind_methods() {
    // No additional bindings needed 
}

VideoStreamPlaybackWMF::VideoStreamPlaybackWMF() {
    // Initialize texture
    texture.instantiate();
}

VideoStreamPlaybackWMF::~VideoStreamPlaybackWMF() {
    // Base class destructor handles player cleanup
}

void VideoStreamPlaybackWMF::initialize(const String& p_file) {
    // Store file path
    file_path = p_file;
    
    // Get absolute path
    Ref<FileAccess> file = FileAccess::open(p_file, FileAccess::READ);
    if (file.is_null()) {
        UtilityFunctions::printerr("Cannot open file '" + p_file + "'.");
        return;
    }
    
    String absolute_path = file->get_path_absolute();
    
    // Create WMF player
    auto wmf_player = std::make_unique<WMFPlayer>();
    if (!wmf_player->open(absolute_path.utf8().get_data())) {
        UtilityFunctions::printerr("Failed to open media file: " + p_file);
        return;
    }
    
    // Store the player in the base class
    media_player = std::move(wmf_player);
    
    // Get media information
    IMediaPlayer::MediaInfo info = media_player->get_media_info();
    
    // Log media information
    UtilityFunctions::print("Loaded media: " + file_path);
    UtilityFunctions::print("  Duration: " + String::num_real(info.duration) + " seconds");
    UtilityFunctions::print("  Resolution: " + itos(info.width) + "x" + itos(info.height));
    UtilityFunctions::print("  Framerate: " + String::num_real(info.framerate) + " fps");
    if (info.audio_channels > 0) {
        UtilityFunctions::print("  Audio: " + itos(info.audio_channels) + " channels @ " + 
                                itos(info.audio_sample_rate) + " Hz");
    } else {
        UtilityFunctions::print("  Audio: None");
    }
}

void VideoStreamWMF::_bind_methods() {
    // No additional bindings needed
}

Ref<VideoStreamPlaybackBase> VideoStreamWMF::_create_playback_instance() {
    Ref<VideoStreamPlaybackWMF> playback;
    playback.instantiate();
    playback->initialize(get_file());
    return playback;
}

} // namespace godot