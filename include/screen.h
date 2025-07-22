#ifndef SCREEN_H
#define SCREEN_H

#include <windows.h>

// Suppress Windows SDK warnings for non-standard extensions (unnamed struct/union)
#pragma warning(push)
#pragma warning(disable: 4201)
#include <dxgi1_2.h>
#include <d3d11.h>
#pragma warning(pop)

typedef struct {
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    IDXGIOutputDuplication* duplication;
    DXGI_OUTDUPL_DESC duplication_desc;
    int width;
    int height;
    BOOL is_capturing;
    // Frame caching for consistent FPS
    void* cached_frame;
    size_t cached_frame_size;
    BOOL has_cached_frame;
} screen_capture_t;

// Function declarations
int screen_init(screen_capture_t* capture);
int screen_start_capture(screen_capture_t* capture);
int screen_get_frame(screen_capture_t* capture, void** frame_data, size_t* frame_size);
int screen_get_frame_dual_track(screen_capture_t* capture, void** frame_data, size_t* frame_size, BOOL dual_track_mode);
void screen_stop_capture(screen_capture_t* capture);
void screen_cleanup(screen_capture_t* capture);

#endif // SCREEN_H
