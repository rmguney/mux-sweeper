#ifdef MUXSW_ENABLE_AUDIO

#include "system.h"
#include "guids.h"
#include <stdio.h>
#include <string.h>

int system_init(system_context_t* ctx) {
    if (!ctx) return -1;
    
    memset(ctx, 0, sizeof(system_context_t));
    ctx->using_silent_buffer = FALSE;
    
    HRESULT hr;
    
    // Initialize COM if not already done - use same fallback pattern as record.c
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    BOOL com_initialized_here = SUCCEEDED(hr);
    
    // If apartment threaded failed, try multithreaded mode (CLI scenario)
    if (!com_initialized_here) {
        hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        com_initialized_here = SUCCEEDED(hr);
    }
    
    // If COM is already initialized in compatible mode, that's fine
    if (!com_initialized_here && hr != RPC_E_CHANGED_MODE) {
        fprintf(stderr, "System: Failed to initialize COM: 0x%08X\n", hr);
        return -1;
    }
    
    // Create device enumerator
    hr = CoCreateInstance(
        &CLSID_MMDeviceEnumerator,
        NULL,
        CLSCTX_ALL,
        &IID_IMMDeviceEnumerator,
        (void**)&ctx->enumerator
    );
    
    if (FAILED(hr)) {
        fprintf(stderr, "System: Failed to create device enumerator: 0x%08X\n", hr);
        return -1;
    }
    
    // Get default system audio endpoint (render device for loopback)
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(
        ctx->enumerator,
        eRender,   // System audio requires render device for loopback
        eConsole,
        &ctx->device
    );
    
    if (FAILED(hr)) {
        fprintf(stderr, "System: Failed to get default render endpoint: 0x%08X\n", hr);
        IMMDeviceEnumerator_Release(ctx->enumerator);
        return -1;
    }
    
    // Activate audio client
    hr = IMMDevice_Activate(
        ctx->device,
        &IID_IAudioClient,
        CLSCTX_ALL,
        NULL,
        (void**)&ctx->audio_client
    );
    
    if (FAILED(hr)) {
        fprintf(stderr, "System: Failed to activate audio client: 0x%08X\n", hr);
        IMMDevice_Release(ctx->device);
        IMMDeviceEnumerator_Release(ctx->enumerator);
        return -1;
    }
    
    // Get mix format
    hr = IAudioClient_GetMixFormat(ctx->audio_client, &ctx->wave_format);
    if (FAILED(hr)) {
        fprintf(stderr, "System: Failed to get mix format: 0x%08X\n", hr);
        IAudioClient_Release(ctx->audio_client);
        IMMDevice_Release(ctx->device);
        IMMDeviceEnumerator_Release(ctx->enumerator);
        return -1;
    }
    
    // Initialize audio client for system audio loopback capture
    AUDCLNT_SHAREMODE share_mode = AUDCLNT_SHAREMODE_SHARED;
    DWORD stream_flags = AUDCLNT_STREAMFLAGS_LOOPBACK;  // Critical for system audio
    
    // Use smaller buffer (50ms) to reduce latency
    REFERENCE_TIME buffer_duration = 500000; // 50ms in 100-nanosecond units
    
    hr = IAudioClient_Initialize(
        ctx->audio_client,
        share_mode,
        stream_flags,
        buffer_duration,
        0,
        ctx->wave_format,
        NULL
    );
    
    if (FAILED(hr)) {
        fprintf(stderr, "System: Failed to initialize audio client: 0x%08X\n", hr);
        CoTaskMemFree(ctx->wave_format);
        IAudioClient_Release(ctx->audio_client);
        IMMDevice_Release(ctx->device);
        IMMDeviceEnumerator_Release(ctx->enumerator);
        return -1;
    }
    
    // Get buffer frame count
    hr = IAudioClient_GetBufferSize(ctx->audio_client, &ctx->buffer_frame_count);
    if (FAILED(hr)) {
        fprintf(stderr, "System: Failed to get buffer size: 0x%08X\n", hr);
        CoTaskMemFree(ctx->wave_format);
        IAudioClient_Release(ctx->audio_client);
        IMMDevice_Release(ctx->device);
        IMMDeviceEnumerator_Release(ctx->enumerator);
        return -1;
    }
    
    // Get capture client
    hr = IAudioClient_GetService(
        ctx->audio_client,
        &IID_IAudioCaptureClient,
        (void**)&ctx->capture_client
    );
    
    if (FAILED(hr)) {
        fprintf(stderr, "System: Failed to get capture client: 0x%08X\n", hr);
        CoTaskMemFree(ctx->wave_format);
        IAudioClient_Release(ctx->audio_client);
        IMMDevice_Release(ctx->device);
        IMMDeviceEnumerator_Release(ctx->enumerator);
        return -1;
    }
    
    printf("System audio initialized: %d Hz, %d channels, %d bits\n",
           ctx->wave_format->nSamplesPerSec,
           ctx->wave_format->nChannels,
           ctx->wave_format->wBitsPerSample);
    
    return 0;
}

int system_start_capture(system_context_t* ctx) {
    if (!ctx || !ctx->audio_client) {
        printf("System: Invalid context for start\n");
        return -1;
    }
    
    printf("System: Starting capture...\n");
    HRESULT hr = IAudioClient_Start(ctx->audio_client);
    if (FAILED(hr)) {
        fprintf(stderr, "System: Failed to start capture: 0x%08X\n", hr);
        return -1;
    }
    
    ctx->is_capturing = TRUE;
    printf("System audio capture started successfully\n");
    return 0;
}

int system_get_buffer(system_context_t* ctx, BYTE** data, UINT32* num_frames) {
    if (!ctx || !ctx->capture_client || !ctx->is_capturing) {
        if (data) *data = NULL;
        if (num_frames) *num_frames = 0;
        return -1;
    }

    HRESULT hr;
    DWORD flags;

    hr = IAudioCaptureClient_GetNextPacketSize(ctx->capture_client, num_frames);
    if (FAILED(hr)) {
        fprintf(stderr, "System: Failed to get next packet size: 0x%08X\n", hr);
        return -1;
    }

    if (*num_frames == 0) {
        // For system audio capture, generate silent frames when no audio is playing
        // This ensures continuous audio stream for proper MP4 encoding and matching video duration
        static BYTE* silent_buffer = NULL;
        static UINT32 silent_buffer_size = 0;
        static UINT32 silent_call_count = 0;
        static DWORD recording_start_time = 0;
        static UINT64 total_generated_samples = 0;
        static DWORD last_silent_generation = 0;
        
        silent_call_count++;
        DWORD current_time = GetTickCount();
        
        // Initialize timing on first call
        if (recording_start_time == 0) {
            recording_start_time = current_time;
            last_silent_generation = current_time;
            total_generated_samples = 0;
        }
        
        // CRITICAL TIMING FIX: Generate audio based on recording elapsed time
        // Calculate how many total samples should exist based on recording duration
        DWORD recording_elapsed_ms = current_time - recording_start_time;
        UINT64 expected_total_samples = ((UINT64)ctx->wave_format->nSamplesPerSec * recording_elapsed_ms) / 1000;
        
        // Only generate more samples if we're behind the expected total
        if (total_generated_samples >= expected_total_samples) {
            *data = NULL;
            *num_frames = 0;
            return 0; // We're already caught up
        }
        
        // Calculate how many samples we need to generate to catch up
        UINT64 samples_needed = expected_total_samples - total_generated_samples;
        
        // Limit to reasonable chunk size (50ms worth max)
        UINT32 max_chunk_samples = (ctx->wave_format->nSamplesPerSec * 50) / 1000;
        if (samples_needed > max_chunk_samples) {
            samples_needed = max_chunk_samples;
        }
        
        if (silent_call_count % 100 == 0) {
            printf("System audio: Recording %ums, expected %llu samples, generated %llu, need %llu\n", 
                   recording_elapsed_ms, expected_total_samples, total_generated_samples, samples_needed);
        }
        
        UINT32 frames_to_generate = (UINT32)samples_needed;
        UINT32 bytes_needed = frames_to_generate * ctx->wave_format->nBlockAlign;
        
        // Expand buffer if needed
        if (silent_buffer_size < bytes_needed) {
            if (silent_buffer) {
                free(silent_buffer);
            }
            silent_buffer = (BYTE*)malloc(bytes_needed);
            if (!silent_buffer) {
                *data = NULL;
                *num_frames = 0;
                return -1;
            }
            silent_buffer_size = bytes_needed;
        }
        
        // Fill with silent audio
        memset(silent_buffer, 0, bytes_needed);
        
        *data = silent_buffer;
        *num_frames = frames_to_generate;
        ctx->using_silent_buffer = TRUE;
        
        // Update our total generated samples count
        total_generated_samples += frames_to_generate;
        
        return 0; // Return success with silent frames
    }

    ctx->using_silent_buffer = FALSE;

    hr = IAudioCaptureClient_GetBuffer(
        ctx->capture_client,
        data,
        num_frames,
        &flags,
        NULL,
        NULL
    );

    if (FAILED(hr)) {
        fprintf(stderr, "System: Failed to get buffer: 0x%08X\n", hr);
        return -1;
    }

    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
        memset(*data, 0, *num_frames * ctx->wave_format->nBlockAlign);
    }

    return 0;
}

void system_release_buffer(system_context_t* ctx, UINT32 num_frames) {
    if (!ctx || !ctx->capture_client) return;
    
    // Only release real audio buffers, not our static silent buffers
    if (!ctx->using_silent_buffer) {
        HRESULT hr = IAudioCaptureClient_ReleaseBuffer(ctx->capture_client, num_frames);
        if (FAILED(hr)) {
            fprintf(stderr, "System: Failed to release buffer: 0x%08X\n", hr);
        }
    }
    // For silent buffers, no release is needed since they're static
}

void system_stop_capture(system_context_t* ctx) {
    if (!ctx || !ctx->audio_client) return;
    
    HRESULT hr = IAudioClient_Stop(ctx->audio_client);
    if (FAILED(hr)) {
        fprintf(stderr, "System: Failed to stop capture: 0x%08X\n", hr);
    }
    
    ctx->is_capturing = FALSE;
    printf("System audio capture stopped\n");
}

void system_cleanup(system_context_t* ctx) {
    if (!ctx) return;
    
    if (ctx->capture_client) {
        IAudioCaptureClient_Release(ctx->capture_client);
        ctx->capture_client = NULL;
    }
    
    if (ctx->audio_client) {
        IAudioClient_Release(ctx->audio_client);
        ctx->audio_client = NULL;
    }
    
    if (ctx->wave_format) {
        CoTaskMemFree(ctx->wave_format);
        ctx->wave_format = NULL;
    }
    
    if (ctx->device) {
        IMMDevice_Release(ctx->device);
        ctx->device = NULL;
    }
    
    if (ctx->enumerator) {
        IMMDeviceEnumerator_Release(ctx->enumerator);
        ctx->enumerator = NULL;
    }
    
    memset(ctx, 0, sizeof(system_context_t));
    printf("System audio capture cleaned up\n");
}

#endif // MUXSW_ENABLE_AUDIO
