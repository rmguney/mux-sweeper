#ifndef MICROPHONE_H
#define MICROPHONE_H

#include <windows.h>

#ifdef MUXSW_ENABLE_AUDIO
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>

// Microphone capture context
typedef struct {
    IMMDeviceEnumerator* enumerator;
    IMMDevice* device;
    IAudioClient* audio_client;
    IAudioCaptureClient* capture_client;
    WAVEFORMATEX* wave_format;
    UINT32 buffer_frame_count;
    BOOL is_capturing;
    BOOL using_silent_buffer; // Track if returning static silent buffer
} microphone_context_t;

// Microphone capture functions
int microphone_init(microphone_context_t* ctx);
int microphone_start_capture(microphone_context_t* ctx);
int microphone_get_buffer(microphone_context_t* ctx, BYTE** data, UINT32* num_frames);
void microphone_release_buffer(microphone_context_t* ctx, UINT32 num_frames);
void microphone_stop_capture(microphone_context_t* ctx);
void microphone_cleanup(microphone_context_t* ctx);

#else
// MVP: Audio disabled - provide stub types and no-op functions

typedef struct {
    int dummy; // Empty struct not allowed in C
} microphone_context_t;

// No-op stub functions for MVP
static inline int microphone_init(microphone_context_t* ctx) { (void)ctx; return 0; }
static inline int microphone_start_capture(microphone_context_t* ctx) { (void)ctx; return 0; }
static inline int microphone_get_buffer(microphone_context_t* ctx, BYTE** data, UINT32* num_frames) { 
    (void)ctx; (void)data; (void)num_frames; return -1; 
}
static inline void microphone_release_buffer(microphone_context_t* ctx, UINT32 num_frames) { 
    (void)ctx; (void)num_frames; 
}
static inline void microphone_stop_capture(microphone_context_t* ctx) { (void)ctx; }
static inline void microphone_cleanup(microphone_context_t* ctx) { (void)ctx; }

#endif // MUXSW_ENABLE_AUDIO

#endif // MICROPHONE_H
