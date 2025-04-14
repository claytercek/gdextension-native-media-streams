#pragma once
#include <wrl/client.h>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <godot_cpp/variant/utility_functions.hpp>
#include <string>

using Microsoft::WRL::ComPtr;

namespace godot {

/**
 * Helper class for managing Windows Media Foundation hardware acceleration
 * Provides encapsulated functionality for Direct3D/DXVA initialization,
 * helps to configure hardware decoders, and handles video format conversion.
 */
class WMFHardwareHelper {
private:
    // D3D11 resources
    ComPtr<ID3D11Device> d3d_device;
    ComPtr<ID3D11DeviceContext> d3d_context;
    ComPtr<IDXGIFactory1> dxgi_factory;
    ComPtr<IMFDXGIDeviceManager> dxgi_device_manager;
    
    // Hardware acceleration state
    bool hardware_available = false;
    bool hardware_active = false;
    UINT device_reset_token = 0;
    
    // Helper method to convert HRESULT to readable string
    static String hr_to_string(HRESULT hr) {
        return "0x" + String::num_int64(hr, 16);
    }
    
    // Initialize D3D11 device and resources
    bool initialize_d3d11();
    
    // Clean up D3D resources
    void cleanup_d3d11();

public:
    WMFHardwareHelper();
    ~WMFHardwareHelper();
    
    // Main initialization method
    bool initialize();
    
    // Configure source reader for hardware acceleration
    bool configure_reader(IMFAttributes* attributes);
    
    // Status checks
    bool is_hardware_available() const { return hardware_available; }
    bool is_hardware_active() const { return hardware_active; }
    
    // Set hardware active status (called by WMFPlayer when a hw accelerated format is used)
    void set_hardware_active(bool active) { hardware_active = active; }
    
    // Get device manager for configuration
    IMFDXGIDeviceManager* get_device_manager() const { return dxgi_device_manager.Get(); }
    
    // Utility function to get recommended output format for hardware acceleration
    // Returns MFVideoFormat_NV12 for hardware rendering when available, otherwise RGB32
    GUID get_recommended_output_format() const;
    
    // Convert a hardware accelerated sample to a system memory sample if needed
    HRESULT ensure_cpu_accessible_sample(IMFSample* sample, IMFSample** out_sample);
};

} // namespace godot