#include "wmf_video_stream_playback.hpp"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>

namespace godot {

void VideoStreamPlaybackWMF::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_hardware_acceleration", "enabled"), &VideoStreamPlaybackWMF::set_hardware_acceleration);
    ClassDB::bind_method(D_METHOD("is_hardware_acceleration_enabled"), &VideoStreamPlaybackWMF::is_hardware_acceleration_enabled);
    ClassDB::bind_method(D_METHOD("is_hardware_acceleration_active"), &VideoStreamPlaybackWMF::is_hardware_acceleration_active);
}

VideoStreamPlaybackWMF::VideoStreamPlaybackWMF() {
    // Check project settings for default hardware acceleration setting
    if (ProjectSettings::get_singleton()->has_setting("native_media_streams/wmf/hardware_acceleration")) {
        hardware_acceleration_enabled = ProjectSettings::get_singleton()->get_setting("native_media_streams/wmf/hardware_acceleration");
    }
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
    
    // Configure hardware acceleration before opening the file
    if (wmf_player) {
        wmf_player->set_hardware_acceleration(hardware_acceleration_enabled);
    }
    
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
    
    // Log hardware acceleration status
    if (WMFPlayer* wmf = dynamic_cast<WMFPlayer*>(media_player.get())) {
        if (wmf->is_hardware_acceleration_enabled()) {
            if (wmf->is_hardware_acceleration_active()) {
                UtilityFunctions::print("  Hardware acceleration: Active");
            } else {
                UtilityFunctions::print("  Hardware acceleration: Enabled but not active (fallback to software)");
            }
        } else {
            UtilityFunctions::print("  Hardware acceleration: Disabled");
        }
    }
}

void VideoStreamPlaybackWMF::set_hardware_acceleration(bool p_enabled) {
    hardware_acceleration_enabled = p_enabled;
    
    // If we have an active player, update its settings
    if (media_player) {
        if (WMFPlayer* wmf = dynamic_cast<WMFPlayer*>(media_player.get())) {
            wmf->set_hardware_acceleration(p_enabled);
        }
    }
}

bool VideoStreamPlaybackWMF::is_hardware_acceleration_enabled() const {
    if (media_player) {
        if (WMFPlayer* wmf = dynamic_cast<WMFPlayer*>(media_player.get())) {
            return wmf->is_hardware_acceleration_enabled();
        }
    }
    return hardware_acceleration_enabled;
}

bool VideoStreamPlaybackWMF::is_hardware_acceleration_active() const {
    if (media_player) {
        if (WMFPlayer* wmf = dynamic_cast<WMFPlayer*>(media_player.get())) {
            return wmf->is_hardware_acceleration_active();
        }
    }
    return false;
}

void VideoStreamWMF::_bind_methods() {
    // Register project settings for hardware acceleration
    if (!ProjectSettings::get_singleton()->has_setting("native_media_streams/wmf/hardware_acceleration")) {
        ProjectSettings::get_singleton()->set_setting("native_media_streams/wmf/hardware_acceleration", true);
        
        // Register the property info
        PropertyInfo prop;
        prop.name = "native_media_streams/wmf/hardware_acceleration";
        prop.type = Variant::BOOL;
        prop.hint = PROPERTY_HINT_NONE;
        prop.hint_string = "Enable hardware acceleration for Windows Media Foundation video playback";
        
        ProjectSettings::get_singleton()->add_property_info(prop);
    }
}

Ref<VideoStreamPlaybackBase> VideoStreamWMF::_create_playback_instance() {
    Ref<VideoStreamPlaybackWMF> playback;
    playback.instantiate();
    playback->initialize(get_file());
    return playback;
}

} // namespace godot