#include "wmf_media_source.hpp"

namespace godot {

WMFMediaSource::WMFMediaSource() {
    // Constructor
}

WMFMediaSource::~WMFMediaSource() {
    clear();
}

void WMFMediaSource::clear() {
    source_reader = nullptr;
    duration = 0;
}

LONGLONG WMFMediaSource::time_to_wmf_time(double time) {
    // Convert seconds to 100-nanosecond units
    return static_cast<LONGLONG>(time * 10000000);
}

double WMFMediaSource::wmf_time_to_seconds(LONGLONG wmf_time) {
    // Convert 100-nanosecond units to seconds
    return static_cast<double>(wmf_time) / 10000000.0;
}

bool WMFMediaSource::create_source_reader(const String& p_file) {
    UtilityFunctions::print_verbose("WMFMediaSource::create_source_reader() called for file: " + p_file);
    
    // Convert file path to Windows format
    wchar_t filepath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, p_file.utf8().get_data(), -1, filepath, MAX_PATH);
    
    // Create source attributes
    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to create source reader attributes");
        return false;
    }
    
    // Enable hardware decoding
    hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to enable hardware video processing");
        // Continue anyway, hardware acceleration is optional
    }
    
    // Create the source reader
    hr = MFCreateSourceReaderFromURL(filepath, attributes.Get(), &source_reader);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to create source reader for: ", p_file);
        return false;
    }
    
    // Get duration using the source reader's stream attribute
    PROPVARIANT var;
    PropVariantInit(&var);
    
    hr = source_reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, 
                                              MF_PD_DURATION, 
                                              &var);
    if (SUCCEEDED(hr) && var.vt == VT_UI8) {
        // Store the duration
        duration = var.uhVal.QuadPart;
        UtilityFunctions::print_verbose("Media duration: " + String::num_real(wmf_time_to_seconds(duration)) + " seconds");
    } else {
        UtilityFunctions::printerr("Failed to get media duration");
        duration = 0;
    }
    
    PropVariantClear(&var);
    
    return true;
}

bool WMFMediaSource::seek_to_position(double p_time) {
    if (!source_reader) return false;
    
    LONGLONG seek_time = time_to_wmf_time(p_time);
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = seek_time;
    
    // Seek to the specified position
    HRESULT hr = source_reader->SetCurrentPosition(GUID_NULL, var);
    PropVariantClear(&var);
    
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to seek to position: ", String::num_real(p_time));
        return false;
    }
    
    UtilityFunctions::print_verbose("Seek performed to: " + String::num_real(p_time));
    return true;
}

} // namespace godot