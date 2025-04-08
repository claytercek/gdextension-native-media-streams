#include "avf_video_stream_playback.hpp"
#include "avf_player.hpp"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <memory> // For std::unique_ptr, std::make_unique

namespace godot {

void VideoStreamPlaybackAVF::_bind_methods() {
    // No additional bindings needed 
}

VideoStreamPlaybackAVF::VideoStreamPlaybackAVF() {
    // Initialize texture
    texture.instantiate();
}

VideoStreamPlaybackAVF::~VideoStreamPlaybackAVF() {
    // Base class destructor handles player cleanup
}

void VideoStreamPlaybackAVF::initialize(const String& p_file) {
    // Store file path
    file_path = p_file;
    
    // Get absolute path
    Ref<FileAccess> file = FileAccess::open(p_file, FileAccess::READ);
    if (file.is_null()) {
        UtilityFunctions::printerr("Cannot open file '" + p_file + "'.");
        return;
    }
    
    String absolute_path = file->get_path_absolute();
    
    // Create AVF player
    auto avf_player = std::make_unique<AVFPlayer>();
    if (!avf_player->open(absolute_path.utf8().get_data())) {
        UtilityFunctions::printerr("Failed to open media file: " + p_file);
        return;
    }
    
    // Store the player in the base class
    media_player = std::move(avf_player);
    
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

void VideoStreamAVF::_bind_methods() {
    // No additional bindings needed
}

Ref<VideoStreamPlayback> VideoStreamAVF::_instantiate_playback() {
    Ref<VideoStreamPlaybackAVF> playback;
    playback.instantiate();
    playback->initialize(file_path);
    return playback;
}

} // namespace godot