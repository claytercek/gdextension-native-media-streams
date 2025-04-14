#include "wmf_hardware_helper.hpp"
#include <mferror.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace godot {

WMFHardwareHelper::WMFHardwareHelper() {
    // Try to initialize D3D11 for hardware acceleration
    hardware_available = initialize_d3d11();
    if (hardware_available) {
        UtilityFunctions::print("D3D11 hardware acceleration initialized successfully");
    } else {
        UtilityFunctions::print("Hardware acceleration unavailable, falling back to software decoding");
    }
}

WMFHardwareHelper::~WMFHardwareHelper() {
    cleanup_d3d11();
}

bool WMFHardwareHelper::initialize() {
    return hardware_available;
}

bool WMFHardwareHelper::initialize_d3d11() {
    // Create DXGI Factory
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&dxgi_factory);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to create DXGI Factory: " + hr_to_string(hr));
        return false;
    }

    // First, try to get hardware accelerated video decoding device
    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1
    };
    
    UINT creation_flags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    
    // Try with hardware acceleration first
    D3D_FEATURE_LEVEL actual_feature_level;
    hr = D3D11CreateDevice(
        nullptr,                          // Use default adapter
        D3D_DRIVER_TYPE_HARDWARE,         // Use hardware driver
        nullptr,                          // No software driver
        creation_flags,                   // Flags 
        feature_levels,                   // Feature levels to try
        ARRAYSIZE(feature_levels),        // Number of feature levels
        D3D11_SDK_VERSION,                // SDK version
        &d3d_device,                      // Output device
        &actual_feature_level,            // Output feature level
        &d3d_context                      // Output device context
    );

    // If hardware failed, try to fall back to software (WARP) renderer
    if (FAILED(hr)) {
        UtilityFunctions::print("Failed to create hardware D3D11 device, falling back to WARP: " + hr_to_string(hr));
        
        hr = D3D11CreateDevice(
            nullptr,                     // Use default adapter
            D3D_DRIVER_TYPE_WARP,        // Use WARP driver
            nullptr,                     // No software driver
            creation_flags,              // Flags 
            feature_levels,              // Feature levels to try
            ARRAYSIZE(feature_levels),   // Number of feature levels
            D3D11_SDK_VERSION,           // SDK version
            &d3d_device,                 // Output device
            &actual_feature_level,       // Output feature level
            &d3d_context                 // Output device context
        );
        
        if (FAILED(hr)) {
            UtilityFunctions::printerr("Failed to create WARP D3D11 device: " + hr_to_string(hr));
            return false;
        }
    }

    // Multithread protection - ensure the device is properly configured for multithreaded access
    ComPtr<ID3D10Multithread> multithread;
    hr = d3d_context.As(&multithread);
    if (SUCCEEDED(hr) && multithread) {
        multithread->SetMultithreadProtected(TRUE);
    }

    // Create DXGI Device Manager
    hr = MFCreateDXGIDeviceManager(&device_reset_token, &dxgi_device_manager);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to create DXGI device manager: " + hr_to_string(hr));
        return false;
    }

    // Associate our device with the manager
    hr = dxgi_device_manager->ResetDevice(d3d_device.Get(), device_reset_token);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to reset DXGI device: " + hr_to_string(hr));
        return false;
    }

    return true;
}

void WMFHardwareHelper::cleanup_d3d11() {
    // Release D3D11 and DXGI resources
    dxgi_device_manager.Reset();
    d3d_context.Reset();
    d3d_device.Reset();
    dxgi_factory.Reset();
    
    hardware_available = false;
    hardware_active = false;
}

bool WMFHardwareHelper::configure_reader(IMFAttributes* attributes) {
    if (!attributes || !hardware_available) {
        return false;
    }

    HRESULT hr = S_OK;

    // Enable hardware acceleration
    hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to enable advanced video processing: " + hr_to_string(hr));
        return false;
    }

    // Set D3D manager for hardware acceleration
    hr = attributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, dxgi_device_manager.Get());
    if (FAILED(hr)) {
        UtilityFunctions::printerr("Failed to set D3D manager: " + hr_to_string(hr));
        return false;
    }

    // Enable low latency
    hr = attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
    if (FAILED(hr)) {
        UtilityFunctions::print("Warning: Failed to set low latency hint");
        // Not critical, continue
    }

    return true;
}

GUID WMFHardwareHelper::get_recommended_output_format() const {
    // For hardware acceleration, NV12 is the best choice
    // as it's directly compatible with most hardware decoders
    if (hardware_available) {
        return MFVideoFormat_NV12;
    }
    
    // For software decoding, RGB32 is more directly usable
    return MFVideoFormat_RGB32;
}

HRESULT WMFHardwareHelper::ensure_cpu_accessible_sample(IMFSample* sample, IMFSample** out_sample) {
    if (!sample) {
        return E_POINTER;
    }
    
    if (!hardware_active) {
        // No hardware acceleration, just add a reference and pass the sample through
        sample->AddRef();
        *out_sample = sample;
        return S_OK;
    }
    
    // Check if the sample is already accessible to the CPU
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = sample->GetBufferByIndex(0, &buffer);
    if (FAILED(hr)) {
        return hr;
    }
    
    // Try to get the buffer as a DXGI buffer
    ComPtr<IMFDXGIBuffer> dxgi_buffer;
    hr = buffer.As(&dxgi_buffer);
    
    // If it's not a DXGI buffer, it's already CPU accessible
    if (FAILED(hr)) {
        sample->AddRef();
        *out_sample = sample;
        return S_OK;
    }
    
    // If it is a DXGI buffer, we need to create a system memory copy
    // Create a new sample
    ComPtr<IMFSample> system_sample;
    hr = MFCreateSample(&system_sample);
    if (FAILED(hr)) {
        return hr;
    }
    
    // Copy sample attributes
    hr = sample->CopyAllItems(system_sample.Get());
    if (FAILED(hr)) {
        return hr;
    }
    
    // Get frame dimensions
    UINT32 width = 0, height = 0;
    hr = MFGetAttributeSize(sample, MF_MT_FRAME_SIZE, &width, &height);
    if (FAILED(hr)) {
        // Try to get dimensions from the frame if not in attributes
        ComPtr<IMFMediaType> media_type;
        hr = MFCreateMediaType(&media_type);
        if (SUCCEEDED(hr)) {
            DWORD buffer_size;
            hr = buffer->GetCurrentLength(&buffer_size);
            
            if (SUCCEEDED(hr)) {
                // Estimate for NV12 format
                width = 1920;  // Default assumption
                height = (buffer_size * 2) / (width * 3);
            }
        }
        
        if (width == 0 || height == 0) {
            return MF_E_INVALID_FORMAT;
        }
    }
    
    // Get format
    GUID subtype = MFVideoFormat_NV12;  // Default to NV12 for DXGI buffers
    
    // Create a system memory buffer of appropriate size
    ComPtr<IMFMediaBuffer> system_buffer;
    DWORD buffer_size = 0;
    
    if (subtype == MFVideoFormat_NV12) {
        // NV12 has luma and interleaved chroma planes
        buffer_size = width * height + ((width + 1) / 2) * ((height + 1) / 2) * 2;
    } else if (subtype == MFVideoFormat_RGB32) {
        buffer_size = width * height * 4;
    } else {
        // Unknown format, try to get the size from the source buffer
        hr = buffer->GetCurrentLength(&buffer_size);
        if (FAILED(hr) || buffer_size == 0) {
            return MF_E_INVALID_FORMAT;
        }
    }
    
    hr = MFCreateMemoryBuffer(buffer_size, &system_buffer);
    if (FAILED(hr)) {
        return hr;
    }
    
    // Lock the D3D11 buffer and copy to system memory
    ComPtr<ID3D11Texture2D> texture;
    UINT subresource = 0;
    
    // Get D3D11 texture from DXGI buffer
    // IMFDXGIBuffer::GetResource only takes a single parameter in some versions of the SDK
    // We'll get the resource first, then the subresource index
    ID3D11Resource* resource = nullptr;
    hr = dxgi_buffer->GetResource(__uuidof(ID3D11Resource), (void**)&resource);
    if (FAILED(hr) || !resource) {
        return hr;
    }
    
    // Get the subresource index
    hr = dxgi_buffer->GetSubresourceIndex(&subresource);
    if (FAILED(hr)) {
        resource->Release();
        return hr;
    }
    
    // Cast to texture
    ID3D11Texture2D* tex_ptr = nullptr;
    hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex_ptr);
    resource->Release(); // Release the resource as we now have the texture
    
    if (SUCCEEDED(hr) && tex_ptr) {
        texture.Attach(tex_ptr);
    } else {
        return hr;
    }
    
    // Lock the texture
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    
    // Map the texture for CPU access
    D3D11_MAPPED_SUBRESOURCE mapped_resource;
    hr = d3d_context->Map(texture.Get(), subresource, D3D11_MAP_READ, 0, &mapped_resource);
    if (FAILED(hr)) {
        return hr;
    }
    
    // Copy data from GPU to CPU
    BYTE* src = static_cast<BYTE*>(mapped_resource.pData);
    BYTE* dest = nullptr;
    DWORD max_length = 0;
    
    hr = system_buffer->Lock(&dest, &max_length, nullptr);
    if (FAILED(hr)) {
        d3d_context->Unmap(texture.Get(), subresource);
        return hr;
    }
    
    if (subtype == MFVideoFormat_NV12) {
        // Copy the NV12 data
        const UINT luma_size = width * height;
        const UINT chroma_size = ((width + 1) / 2) * ((height + 1) / 2) * 2;
        
        // Copy luma plane (Y)
        for (UINT y = 0; y < height; y++) {
            memcpy(dest + y * width, 
                  src + y * mapped_resource.RowPitch, 
                  width);
        }
        
        // Copy chroma plane (interleaved UV)
        for (UINT y = 0; y < height / 2; y++) {
            memcpy(dest + luma_size + y * width, 
                  src + mapped_resource.RowPitch * height + y * mapped_resource.RowPitch, 
                  width);
        }
    } else {
        // For other formats, just copy line by line to handle stride differences
        for (UINT y = 0; y < height; y++) {
            UINT line_size = width;
            if (subtype == MFVideoFormat_RGB32) {
                line_size *= 4;
            }
            
            memcpy(dest + y * line_size, 
                  src + y * mapped_resource.RowPitch, 
                  line_size);
        }
    }
    
    // Unlock both textures
    system_buffer->Unlock();
    d3d_context->Unmap(texture.Get(), subresource);
    
    // Add the system buffer to the new sample
    hr = system_sample->AddBuffer(system_buffer.Get());
    if (FAILED(hr)) {
        return hr;
    }
    
    // Return the new sample
    *out_sample = system_sample.Detach();
    return S_OK;
}

} // namespace godot