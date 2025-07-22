#ifndef SYSTEM_H
#define SYSTEM_H

#include <windows.h>

#ifdef MUXSW_ENABLE_AUDIO
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>

// System audio capture context
typedef struct {
    IMMDeviceEnumerator* enumerator;
    IMMDevice* device;
    IAudioClient* audio_client;
    IAudioCaptureClient* capture_client;
    WAVEFORMATEX* wave_format;
    UINT32 buffer_frame_count;
    BOOL is_capturing;
    BOOL using_silent_buffer;  // Track if we're using silent buffer
} system_context_t;

// System audio capture functions
int system_init(system_context_t* ctx);
int system_start_capture(system_context_t* ctx);
int system_get_buffer(system_context_t* ctx, BYTE** data, UINT32* num_frames);
void system_release_buffer(system_context_t* ctx, UINT32 num_frames);
void system_stop_capture(system_context_t* ctx);
void system_cleanup(system_context_t* ctx);

#else
// MVP: Audio disabled - provide stub types and no-op functions

typedef struct {
    int dummy; // Empty struct not allowed in C
} system_context_t;

// No-op stub functions for MVP
static inline int system_init(system_context_t* ctx) { (void)ctx; return 0; }
static inline int system_start_capture(system_context_t* ctx) { (void)ctx; return 0; }
static inline int system_get_buffer(system_context_t* ctx, BYTE** data, UINT32* num_frames) { 
    (void)ctx; (void)data; (void)num_frames; return -1; 
}
static inline void system_release_buffer(system_context_t* ctx, UINT32 num_frames) { 
    (void)ctx; (void)num_frames; 
}
static inline void system_stop_capture(system_context_t* ctx) { (void)ctx; }
static inline void system_cleanup(system_context_t* ctx) { (void)ctx; }

#endif // MUXSW_ENABLE_AUDIO

#endif // SYSTEM_H
