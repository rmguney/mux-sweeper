#include "screen.h"
#include <stdio.h>
#include <string.h>

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P) (P)
#endif

int screen_init(screen_capture_t* capture) {
    if (!capture) return -1;
    
    memset(capture, 0, sizeof(screen_capture_t));
    
    HRESULT hr;
    IDXGIFactory1* factory = NULL;
    IDXGIAdapter1* adapter = NULL;
    IDXGIOutput* output = NULL;
    IDXGIOutput1* output1 = NULL;
    D3D_FEATURE_LEVEL feature_level;
    
    hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void**)&factory);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create DXGI factory: 0x%08X\n", hr);
        return -1;
    }
    
    hr = IDXGIFactory1_EnumAdapters1(factory, 0, &adapter);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to enumerate adapters: 0x%08X\n", hr);
        IDXGIFactory1_Release(factory);
        return -1;
    }
    
    hr = IDXGIAdapter1_EnumOutputs(adapter, 0, &output);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to enumerate outputs: 0x%08X\n", hr);
        IDXGIAdapter1_Release(adapter);
        IDXGIFactory1_Release(factory);
        return -1;
    }
    
    hr = IDXGIOutput_QueryInterface(output, &IID_IDXGIOutput1, (void**)&output1);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to get IDXGIOutput1: 0x%08X\n", hr);
        IDXGIOutput_Release(output);
        IDXGIAdapter1_Release(adapter);
        IDXGIFactory1_Release(factory);
        return -1;
    }
    
    hr = D3D11CreateDevice(
        (IDXGIAdapter*)adapter,
        D3D_DRIVER_TYPE_UNKNOWN,
        NULL,
        0,
        NULL,
        0,
        D3D11_SDK_VERSION,
        &capture->device,
        &feature_level,
        &capture->context
    );
    
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create D3D11 device: 0x%08X\n", hr);
        IDXGIOutput1_Release(output1);
        IDXGIOutput_Release(output);
        IDXGIAdapter1_Release(adapter);
        IDXGIFactory1_Release(factory);
        return -1;
    }
    
    hr = IDXGIOutput1_DuplicateOutput(output1, (IUnknown*)capture->device, &capture->duplication);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create desktop duplication: 0x%08X\n", hr);
        ID3D11DeviceContext_Release(capture->context);
        ID3D11Device_Release(capture->device);
        IDXGIOutput1_Release(output1);
        IDXGIOutput_Release(output);
        IDXGIAdapter1_Release(adapter);
        IDXGIFactory1_Release(factory);
        return -1;
    }
    
    IDXGIOutputDuplication_GetDesc(capture->duplication, &capture->duplication_desc);
    capture->width = capture->duplication_desc.ModeDesc.Width;
    capture->height = capture->duplication_desc.ModeDesc.Height;
    
    printf("Screen capture initialized: %dx%d\n", capture->width, capture->height);
    
    // Cleanup temporary objects
    IDXGIOutput1_Release(output1);
    IDXGIOutput_Release(output);
    IDXGIAdapter1_Release(adapter);
    IDXGIFactory1_Release(factory);
    
    return 0;
}

int screen_start_capture(screen_capture_t* capture) {
    if (!capture || !capture->duplication) return -1;
    
    capture->is_capturing = TRUE;
    printf("Screen capture started\n");
    return 0;
}

// Enhanced frame capture with dual-track mode awareness to fix video flipping issue
int screen_get_frame_dual_track(screen_capture_t* capture, void** frame_data, size_t* frame_size, BOOL dual_track_mode) {
    if (!capture || !capture->duplication || !capture->is_capturing) return -1;
    
    HRESULT hr;
    IDXGIResource* desktop_resource = NULL;
    DXGI_OUTDUPL_FRAME_INFO frame_info;
    ID3D11Texture2D* desktop_texture = NULL;
    ID3D11Texture2D* staging_texture = NULL;
    D3D11_MAPPED_SUBRESOURCE mapped_resource;
    D3D11_TEXTURE2D_DESC texture_desc;
    
    // Try to acquire next frame with minimal timeout for polling
    hr = IDXGIOutputDuplication_AcquireNextFrame(
        capture->duplication,
        0, // No timeout - immediate return
        &frame_info,
        &desktop_resource
    );
    
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            // No new frame available, use cached frame if we have one
            if (capture->has_cached_frame && capture->cached_frame) {
                *frame_size = capture->cached_frame_size;
                *frame_data = malloc(*frame_size);
                if (*frame_data) {
                    memcpy(*frame_data, capture->cached_frame, *frame_size);
                    return 0; // Success with cached frame
                }
            }
            return 1; // No frame available and no cache
        }
        fprintf(stderr, "Failed to acquire frame: 0x%08X\n", hr);
        return -1;
    }
    
    // Get texture interface
    hr = IDXGIResource_QueryInterface(desktop_resource, &IID_ID3D11Texture2D, (void**)&desktop_texture);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to get texture interface: 0x%08X\n", hr);
        IDXGIResource_Release(desktop_resource);
        IDXGIOutputDuplication_ReleaseFrame(capture->duplication);
        return -1;
    }
    
    // Get texture description
    ID3D11Texture2D_GetDesc(desktop_texture, &texture_desc);
    
    // Create staging texture for CPU access
    texture_desc.Usage = D3D11_USAGE_STAGING;
    texture_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    texture_desc.BindFlags = 0;
    texture_desc.MiscFlags = 0;
    
    hr = ID3D11Device_CreateTexture2D(capture->device, &texture_desc, NULL, &staging_texture);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create staging texture: 0x%08X\n", hr);
        ID3D11Texture2D_Release(desktop_texture);
        IDXGIResource_Release(desktop_resource);
        IDXGIOutputDuplication_ReleaseFrame(capture->duplication);
        return -1;
    }
    
    // Copy desktop texture to staging texture
    ID3D11DeviceContext_CopyResource(capture->context, (ID3D11Resource*)staging_texture, (ID3D11Resource*)desktop_texture);
    
    // Map staging texture for reading
    hr = ID3D11DeviceContext_Map(capture->context, (ID3D11Resource*)staging_texture, 0, D3D11_MAP_READ, 0, &mapped_resource);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to map staging texture: 0x%08X\n", hr);
        ID3D11Texture2D_Release(staging_texture);
        ID3D11Texture2D_Release(desktop_texture);
        IDXGIResource_Release(desktop_resource);
        IDXGIOutputDuplication_ReleaseFrame(capture->duplication);
        return -1;
    }
    
    // Calculate frame size (assuming BGRA format, 4 bytes per pixel)
    *frame_size = texture_desc.Width * texture_desc.Height * 4;
    *frame_data = malloc(*frame_size);
    
    if (!*frame_data) {
        fprintf(stderr, "Failed to allocate frame buffer\n");
        ID3D11DeviceContext_Unmap(capture->context, (ID3D11Resource*)staging_texture, 0);
        ID3D11Texture2D_Release(staging_texture);
        ID3D11Texture2D_Release(desktop_texture);
        IDXGIResource_Release(desktop_resource);
        IDXGIOutputDuplication_ReleaseFrame(capture->duplication);
        return -1;
    }
    
    // Copy frame data with conditional flipping based on dual-track mode
    // DirectX screen capture needs flipping for proper video orientation
    // Single-track mode: Copy with vertical flip (bottom-up) - correct orientation
    // Dual-track mode: Copy normally (top-down) - matches dual-track encoder expectations
    BYTE* src = (BYTE*)mapped_resource.pData;
    BYTE* dst = (BYTE*)*frame_data;
    
    if (dual_track_mode) {
        // Dual-track mode: Copy normally (top to bottom) - no flip needed
        for (UINT y = 0; y < texture_desc.Height; y++) {
            memcpy(dst + y * texture_desc.Width * 4, 
                   src + y * mapped_resource.RowPitch, 
                   texture_desc.Width * 4);
        }
    } else {
        // Single-track mode: Flip vertically (copy from bottom to top) for correct orientation
        for (UINT y = 0; y < texture_desc.Height; y++) {
            UINT src_row = texture_desc.Height - 1 - y; // Read from bottom to top
            memcpy(dst + y * texture_desc.Width * 4, 
                   src + src_row * mapped_resource.RowPitch, 
                   texture_desc.Width * 4);
        }
    }
    
    // Cache this frame for future use when no new frames are available
    // MEMORY LEAK FIX: Limit cache size and prevent unbounded growth
    if (capture->cached_frame) {
        free(capture->cached_frame);
        capture->cached_frame = NULL;
    }
    
    // Only cache frames up to reasonable size limit (prevent memory leak)
    const size_t MAX_CACHE_SIZE = 32 * 1024 * 1024; // 32MB max cache
    if (*frame_size <= MAX_CACHE_SIZE) {
        capture->cached_frame = malloc(*frame_size);
        if (capture->cached_frame) {
            memcpy(capture->cached_frame, *frame_data, *frame_size);
            capture->cached_frame_size = *frame_size;
            capture->has_cached_frame = TRUE;
        }
    } else {
        capture->has_cached_frame = FALSE;
    }
    
    // Cleanup
    ID3D11DeviceContext_Unmap(capture->context, (ID3D11Resource*)staging_texture, 0);
    ID3D11Texture2D_Release(staging_texture);
    ID3D11Texture2D_Release(desktop_texture);
    IDXGIResource_Release(desktop_resource);
    IDXGIOutputDuplication_ReleaseFrame(capture->duplication);
    
    return 0;
}

// Original frame capture function (for backward compatibility)
int screen_get_frame(screen_capture_t* capture, void** frame_data, size_t* frame_size) {
    return screen_get_frame_dual_track(capture, frame_data, frame_size, FALSE);
}

void screen_stop_capture(screen_capture_t* capture) {
    if (capture) {
        capture->is_capturing = FALSE;
        printf("Screen capture stopped\n");
    }
}

void screen_cleanup(screen_capture_t* capture) {
    if (!capture) return;
    
    if (capture->duplication) {
        IDXGIOutputDuplication_Release(capture->duplication);
        capture->duplication = NULL;
    }
    
    if (capture->context) {
        ID3D11DeviceContext_Release(capture->context);
        capture->context = NULL;
    }
    
    if (capture->device) {
        ID3D11Device_Release(capture->device);
        capture->device = NULL;
    }
    
    // Free cached frame
    if (capture->cached_frame) {
        free(capture->cached_frame);
        capture->cached_frame = NULL;
    }
    
    memset(capture, 0, sizeof(screen_capture_t));
    printf("Screen capture cleaned up\n");
}
