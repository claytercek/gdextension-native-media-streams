#pragma once
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/string.hpp>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace godot {

class WMFMediaSource {
public:
    WMFMediaSource();
    ~WMFMediaSource();

    // Setup methods
    bool create_source_reader(const String& file_path);
    void clear();
    
    // Media control
    bool seek_to_position(double time_sec);
    
    // Getters
    ComPtr<IMFSourceReader> get_source_reader() { return source_reader; }
    double get_duration() const { return wmf_time_to_seconds(duration); }
    
    // Time conversion utilities
    static LONGLONG time_to_wmf_time(double time);
    static double wmf_time_to_seconds(LONGLONG wmf_time);

private:
    // Media Foundation resources
    ComPtr<IMFSourceReader> source_reader;
    LONGLONG duration{0};
};

} // namespace godot