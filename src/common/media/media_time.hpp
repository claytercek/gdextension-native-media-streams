#pragma once
#include <cstdint>
#include <string>

namespace godot {

/**
 * Utilities for handling media time conversions.
 * Provides common time conversion functions used by all platform implementations.
 */
class MediaTime {
public:
    // Convert seconds to HH:MM:SS.mmm format
    static std::string format_time(double seconds) {
        int hours = static_cast<int>(seconds) / 3600;
        int minutes = (static_cast<int>(seconds) % 3600) / 60;
        int secs = static_cast<int>(seconds) % 60;
        int milliseconds = static_cast<int>((seconds - static_cast<int>(seconds)) * 1000);
        
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", 
                hours, minutes, secs, milliseconds);
        
        return std::string(buffer);
    }
    
    // Platform-specific time conversions
    
    // Convert seconds to Windows Media Foundation 100-nanosecond units
    static int64_t seconds_to_wmf_time(double seconds) {
        return static_cast<int64_t>(seconds * 10000000.0);
    }
    
    // Convert Windows Media Foundation time to seconds
    static double wmf_time_to_seconds(int64_t wmf_time) {
        return static_cast<double>(wmf_time) / 10000000.0;
    }
    
    // Convert seconds to CoreMedia time with a specific time scale
    static int64_t seconds_to_cm_time(double seconds, int32_t timescale = 600) {
        return static_cast<int64_t>(seconds * timescale);
    }
    
    // Convert CoreMedia time to seconds
    static double cm_time_to_seconds(int64_t cm_time, int32_t timescale = 600) {
        return static_cast<double>(cm_time) / timescale;
    }
    
    // Calculate next frame time based on current time and framerate
    static double predict_next_frame_time(double current_time, float fps) {
        return current_time + (1.0 / fps);
    }
};

} // namespace godot