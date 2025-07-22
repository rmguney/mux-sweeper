#include "encoder.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wmcodecdsp.h>

// Conditional debug output - only in debug builds
#ifdef DEBUG
    #define DEBUG_PRINT(fmt, ...) printf(fmt, __VA_ARGS__)
#else
    #define DEBUG_PRINT(fmt, ...) do { (void)(fmt); } while(0)
#endif

// Additional GUIDs for performance optimization and container type
DEFINE_GUID(MF_LOW_LATENCY, 0x9c27891a, 0xed7a, 0x40e1, 0x88, 0xe8, 0xb2, 0x27, 0x27, 0xa0, 0x24, 0xee);
DEFINE_GUID(MF_SINK_WRITER_DISABLE_THROTTLING, 0x08b845d8, 0x2b74, 0x4afe, 0x9d, 0x53, 0xbe, 0x16, 0xd2, 0xd5, 0xae, 0x4f);
DEFINE_GUID(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, 0xa634a91c, 0x822b, 0x41b9, 0xa4, 0x94, 0x4d, 0xe4, 0x64, 0x36, 0x12, 0xb0);
DEFINE_GUID(MF_TRANSCODE_CONTAINERTYPE, 0x150ff23f, 0x4abc, 0x478b, 0xac, 0x4f, 0xe1, 0x91, 0x6f, 0xba, 0x1c, 0xca);
DEFINE_GUID(MFTranscodeContainerType_MPEG4, 0xdc6cd05d, 0xb9d0, 0x40ef, 0xbd, 0x35, 0xfa, 0x62, 0x2a, 0x1a, 0xb0, 0x26);

// Windows Media Foundation encoding implementation
static IMFSinkWriter* g_sink_writer = NULL;
static DWORD g_video_stream_index = 0;
static DWORD g_audio_stream_index = 0;
static DWORD g_system_audio_stream_index = 0; // For dual-track mode
static DWORD g_mic_audio_stream_index = 0;    // For dual-track mode
static BOOL g_dual_track_mode = FALSE;
static UINT64 g_video_frame_count = 0;
static UINT64 g_audio_sample_count = 0;
static UINT64 g_system_audio_sample_count = 0; // For dual-track mode
static UINT64 g_mic_audio_sample_count = 0;    // For dual-track mode
static UINT32 g_video_width = 0;
static UINT32 g_video_height = 0;
static UINT32 g_video_fps = 30;
static UINT32 g_audio_sample_rate = 44100; // Store actual sample rate for accurate timing
static DWORD g_recording_start_time = 0; // For real-time timestamps
static LONGLONG g_last_video_timestamp = 0; // Track last video timestamp for duration calculation

// Define standard container timescale for proper MP4 timing
#define STANDARD_CONTAINER_TIMESCALE 30000  // Use 30000 (30 FPS * 1000) for consistent timing

// Global timescale override for audio-only mode
static UINT32 g_container_timescale = 30000;

int encoder_init(encoder_context_t* context, const char* filename, int width, int height, int fps,
             int sample_rate, int channels, int bits_per_sample) {
    if (!context || !filename) return -1;
    
    memset(context, 0, sizeof(encoder_context_t));
    context->output_filename = filename;
    context->input_sample_rate = sample_rate;
    context->input_channels = channels;
    context->input_bits_per_sample = bits_per_sample;
    
    // Determine if we should include audio based on valid parameters
    BOOL include_audio = (sample_rate > 0 && channels > 0 && bits_per_sample > 0);
    if (!include_audio) {
        DEBUG_PRINT("Mux: Initializing video-only (no audio parameters)\n");
    }

    HRESULT hr;
    IMFMediaType* video_type_out = NULL;
    IMFMediaType* video_type_in = NULL;
    IMFMediaType* audio_type_out = NULL;
    IMFMediaType* audio_type_in = NULL;
    IMFAttributes* attributes = NULL;
    
    hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to initialize Media Foundation: 0x%08X\n", hr);
        return -1;
    }
    
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, filename, -1, NULL, 0);
    wchar_t* wide_filename = malloc(wide_len * sizeof(wchar_t));
    if (!wide_filename) {
        fprintf(stderr, "Failed to allocate memory for filename\n");
        MFShutdown();
        return -1;
    }
    MultiByteToWideChar(CP_UTF8, 0, filename, -1, wide_filename, wide_len);
    
    hr = MFCreateAttributes(&attributes, 4);  // Increased to 4 for container type
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create attributes: 0x%08X\n", hr);
        free(wide_filename);
        MFShutdown();
        return -1;
    }
    
    // CRITICAL FIX: Set MP4 container type for proper moov atom generation
    hr = IMFAttributes_SetGUID(attributes, &MF_TRANSCODE_CONTAINERTYPE, &MFTranscodeContainerType_MPEG4);
    if (FAILED(hr)) {
        fprintf(stderr, "Warning: Failed to set MP4 container type: 0x%08X\n", hr);
    }
    
    hr = IMFAttributes_SetUINT32(attributes, &MF_LOW_LATENCY, TRUE);
    if (FAILED(hr)) {
        fprintf(stderr, "Warning: Failed to set low latency mode: 0x%08X\n", hr);
    }
    
    hr = IMFAttributes_SetUINT32(attributes, &MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    if (FAILED(hr)) {
        fprintf(stderr, "Warning: Failed to disable throttling: 0x%08X\n", hr);
    }
    
    hr = IMFAttributes_SetUINT32(attributes, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    if (FAILED(hr)) {
        fprintf(stderr, "Warning: Failed to enable hardware transforms: 0x%08X\n", hr);
    }
    
    hr = MFCreateSinkWriterFromURL(wide_filename, NULL, attributes, &g_sink_writer);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create sink writer: 0x%08X\n", hr);
        IMFAttributes_Release(attributes);
        free(wide_filename);
        MFShutdown();
        return -1;
    }
    
    hr = MFCreateMediaType(&video_type_out);
    if (FAILED(hr)) goto cleanup;
    
    hr = IMFMediaType_SetGUID(video_type_out, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    if (FAILED(hr)) goto cleanup;
    
    hr = IMFMediaType_SetGUID(video_type_out, &MF_MT_SUBTYPE, &MFVideoFormat_H264);
    if (FAILED(hr)) goto cleanup;
    
    // Optimized bitrate - adaptive based on resolution and fps
    UINT32 optimized_bitrate;
    if (width >= 1920) {
        optimized_bitrate = 1200000; // 1.2 Mbps for 1080p+
    } else if (width >= 1280) {
        optimized_bitrate = 800000;  // 800 Kbps for 720p
    } else {
        optimized_bitrate = 500000;  // 500 Kbps for lower res
    }
    
    hr = IMFMediaType_SetUINT32(video_type_out, &MF_MT_AVG_BITRATE, optimized_bitrate);
    if (FAILED(hr)) goto cleanup;
    
    hr = IMFMediaType_SetUINT64(video_type_out, &MF_MT_FRAME_SIZE, ((UINT64)width << 32) | height);
    if (FAILED(hr)) goto cleanup;
    
    hr = IMFMediaType_SetUINT64(video_type_out, &MF_MT_FRAME_RATE, ((UINT64)fps << 32) | 1);
    if (FAILED(hr)) goto cleanup;
    
    hr = IMFMediaType_SetUINT32(video_type_out, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (FAILED(hr)) goto cleanup;
    
    hr = IMFMediaType_SetUINT32(video_type_out, &MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_0_255);
    if (FAILED(hr)) {
        DEBUG_PRINT("Warning: Failed to set nominal range: 0x%08X\n", hr);
    }
    
    hr = IMFSinkWriter_AddStream(g_sink_writer, video_type_out, &g_video_stream_index);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to add video stream: 0x%08X\n", hr);
        goto cleanup;
    }
    
    hr = MFCreateMediaType(&video_type_in);
    if (FAILED(hr)) goto cleanup;
    
    hr = IMFMediaType_SetGUID(video_type_in, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    if (FAILED(hr)) goto cleanup;
    
    hr = IMFMediaType_SetGUID(video_type_in, &MF_MT_SUBTYPE, &MFVideoFormat_ARGB32);
    if (FAILED(hr)) goto cleanup;
    
    hr = IMFMediaType_SetUINT64(video_type_in, &MF_MT_FRAME_SIZE, ((UINT64)width << 32) | height);
    if (FAILED(hr)) goto cleanup;
    
    hr = IMFMediaType_SetUINT64(video_type_in, &MF_MT_FRAME_RATE, ((UINT64)fps << 32) | 1);
    if (FAILED(hr)) goto cleanup;
    
    hr = IMFMediaType_SetUINT32(video_type_in, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (FAILED(hr)) goto cleanup;
    
    hr = IMFSinkWriter_SetInputMediaType(g_sink_writer, g_video_stream_index, video_type_in, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to set video input type: 0x%08X\n", hr);
        goto cleanup;
    }
    
    if (include_audio) {
        hr = MFCreateMediaType(&audio_type_out);
        if (FAILED(hr)) goto cleanup;
        
        hr = IMFMediaType_SetGUID(audio_type_out, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
        if (FAILED(hr)) goto cleanup;
        
        hr = IMFMediaType_SetGUID(audio_type_out, &MF_MT_SUBTYPE, &MFAudioFormat_AAC);
        if (FAILED(hr)) goto cleanup;
        
        hr = IMFMediaType_SetUINT32(audio_type_out, &MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100); // Force standard audio sample rate
        if (FAILED(hr)) goto cleanup;
        
        hr = IMFMediaType_SetUINT32(audio_type_out, &MF_MT_AUDIO_NUM_CHANNELS, channels);
        if (FAILED(hr)) goto cleanup;
        
        hr = IMFMediaType_SetUINT32(audio_type_out, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16); // AAC standard
        if (FAILED(hr)) goto cleanup;
        
        hr = IMFMediaType_SetUINT32(audio_type_out, &MF_MT_AVG_BITRATE, 96000);
        if (FAILED(hr)) goto cleanup;
        
        DEBUG_PRINT("Audio output: Using AAC compression\n");
        
        // Add audio stream
        hr = IMFSinkWriter_AddStream(g_sink_writer, audio_type_out, &g_audio_stream_index);
        if (FAILED(hr)) {
            fprintf(stderr, "Failed to add audio stream: 0x%08X\n", hr);
            goto cleanup;
        }
        
        // Configure audio input type (match actual capture format)
        hr = MFCreateMediaType(&audio_type_in);
        if (FAILED(hr)) goto cleanup;
        
        hr = IMFMediaType_SetGUID(audio_type_in, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
        if (FAILED(hr)) goto cleanup;
        
        // Use IEEE Float for 32-bit audio to match Windows audio format
        if (bits_per_sample == 32) {
            hr = IMFMediaType_SetGUID(audio_type_in, &MF_MT_SUBTYPE, &MFAudioFormat_Float);
            DEBUG_PRINT("Audio input: Using IEEE Float format for 32-bit audio\n");
        } else {
            hr = IMFMediaType_SetGUID(audio_type_in, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
            DEBUG_PRINT("Audio input: Using PCM format for %d-bit audio\n", bits_per_sample);
        }
        if (FAILED(hr)) goto cleanup;
        
        hr = IMFMediaType_SetUINT32(audio_type_in, &MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate);
        if (FAILED(hr)) goto cleanup;
        
        hr = IMFMediaType_SetUINT32(audio_type_in, &MF_MT_AUDIO_NUM_CHANNELS, channels);
        if (FAILED(hr)) goto cleanup;
        
        hr = IMFMediaType_SetUINT32(audio_type_in, &MF_MT_AUDIO_BITS_PER_SAMPLE, bits_per_sample);
        if (FAILED(hr)) goto cleanup;
        
        hr = IMFMediaType_SetUINT32(audio_type_in, &MF_MT_AUDIO_BLOCK_ALIGNMENT, (channels * bits_per_sample) / 8);
        if (FAILED(hr)) goto cleanup;
        
        hr = IMFMediaType_SetUINT32(audio_type_in, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sample_rate * channels * bits_per_sample / 8);
        if (FAILED(hr)) goto cleanup;
        
        // Set input type for audio stream
        hr = IMFSinkWriter_SetInputMediaType(g_sink_writer, g_audio_stream_index, audio_type_in, NULL);
        if (FAILED(hr)) {
            fprintf(stderr, "Failed to set audio input type: 0x%08X\n", hr);
            goto cleanup;
        }
        
        printf("Audio stream configured: %d Hz, %d channels, %d bits\n", sample_rate, channels, bits_per_sample);
    } else {
        printf("Skipping audio stream configuration (video-only)\n");
        g_audio_stream_index = (DWORD)-1; // Mark as invalid
    }
    
    // Begin writing
    hr = IMFSinkWriter_BeginWriting(g_sink_writer);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to begin writing: 0x%08X\n", hr);
        goto cleanup;
    }
    
    // Store global parameters
    g_video_width = width;
    g_video_height = height;
    g_video_fps = fps;
    g_audio_sample_rate = sample_rate; // Store actual sample rate for timing calculations
    g_video_frame_count = 0;
    g_audio_sample_count = 0;
    
    context->is_recording = TRUE;
    // Don't set recording start time here - it will be set when capture actually begins
    printf("Media Foundation muxer initialized: %dx%d @ %d fps, output: %s\n", width, height, fps, filename);
    
    // Cleanup
    if (video_type_out) IMFMediaType_Release(video_type_out);
    if (video_type_in) IMFMediaType_Release(video_type_in);
    if (audio_type_out) IMFMediaType_Release(audio_type_out);
    if (audio_type_in) IMFMediaType_Release(audio_type_in);
    if (attributes) IMFAttributes_Release(attributes);
    free(wide_filename);
    
    return 0;

cleanup:
    if (video_type_out) IMFMediaType_Release(video_type_out);
    if (video_type_in) IMFMediaType_Release(video_type_in);
    if (audio_type_out) IMFMediaType_Release(audio_type_out);
    if (audio_type_in) IMFMediaType_Release(audio_type_in);
    if (attributes) IMFAttributes_Release(attributes);
    if (g_sink_writer) {
        IMFSinkWriter_Release(g_sink_writer);
        g_sink_writer = NULL;
    }
    free(wide_filename);
    MFShutdown();
    return -1;
}

// Initialize muxer with dual-track audio support
int encoder_init_dual_track(encoder_context_t* context, const char* filename, int width, int height, int fps,
                        int sample_rate, int channels, int bits_per_sample) {
    if (!context || !filename) return -1;
    
    memset(context, 0, sizeof(encoder_context_t));
    context->output_filename = filename;
    context->input_sample_rate = sample_rate;
    context->input_channels = channels;
    context->input_bits_per_sample = bits_per_sample;
    context->dual_track_mode = TRUE;
    
    // Set global dual-track mode
    g_dual_track_mode = TRUE;
    
    // Determine if we should include audio based on valid parameters
    BOOL include_audio = (sample_rate > 0 && channels > 0 && bits_per_sample > 0);
    if (!include_audio) {
        DEBUG_PRINT("Mux: Initializing video-only (no audio parameters)\n");
    }
    
    HRESULT hr;
    wchar_t* wide_filename = NULL;
    IMFMediaType* video_type_out = NULL;
    IMFMediaType* video_type_in = NULL;
    IMFMediaType* system_audio_type_out = NULL;
    IMFMediaType* system_audio_type_in = NULL;
    IMFMediaType* mic_audio_type_out = NULL;
    IMFMediaType* mic_audio_type_in = NULL;
    IMFAttributes* attributes = NULL;
    
    // Initialize global state
    g_video_frame_count = 0;
    g_system_audio_sample_count = 0;
    g_mic_audio_sample_count = 0;
    g_video_width = width;
    g_video_height = height;
    g_video_fps = fps;
    
    printf("Media Foundation muxer initialized (dual-track): %dx%d @ %d fps, output: %s\n", 
           width, height, fps, filename);
    
    // Initialize Media Foundation
    hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to initialize Media Foundation: 0x%08X\n", hr);
        return -1;
    }
    
    // Convert filename to wide string
    int len = MultiByteToWideChar(CP_UTF8, 0, filename, -1, NULL, 0);
    wide_filename = malloc(len * sizeof(wchar_t));
    if (!wide_filename) {
        fprintf(stderr, "Failed to allocate wide filename\n");
        MFShutdown();
        return -1;
    }
    MultiByteToWideChar(CP_UTF8, 0, filename, -1, wide_filename, len);
    
    // Create attributes for performance optimization
    hr = MFCreateAttributes(&attributes, 11);  // Increased to 11 for container type
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create attributes: 0x%08X\n", hr);
        free(wide_filename);
        MFShutdown();
        return -1;
    }
    
    // CRITICAL FIX: Set MP4 container type for proper moov atom generation
    hr = IMFAttributes_SetGUID(attributes, &MF_TRANSCODE_CONTAINERTYPE, &MFTranscodeContainerType_MPEG4);
    if (FAILED(hr)) {
        fprintf(stderr, "Warning: Failed to set MP4 container type: 0x%08X\n", hr);
    }
    
    // Set performance attributes
    hr = IMFAttributes_SetUINT32(attributes, &MF_LOW_LATENCY, TRUE);
    if (FAILED(hr)) {
        DEBUG_PRINT("Warning: Failed to set low latency: 0x%08X\n", hr);
    }
    
    hr = IMFAttributes_SetUINT32(attributes, &MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    if (FAILED(hr)) {
        DEBUG_PRINT("Warning: Failed to disable throttling: 0x%08X\n", hr);
    }
    
    hr = IMFAttributes_SetUINT32(attributes, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    if (FAILED(hr)) {
        DEBUG_PRINT("Warning: Failed to enable hardware transforms: 0x%08X\n", hr);
    }
    
    // Create sink writer
    hr = MFCreateSinkWriterFromURL(wide_filename, NULL, attributes, &g_sink_writer);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create sink writer: 0x%08X\n", hr);
        IMFAttributes_Release(attributes);
        free(wide_filename);
        MFShutdown();
        return -1;
    }
    
    // Configure video output type (H.264) - same as single track
    hr = MFCreateMediaType(&video_type_out);
    if (FAILED(hr)) goto cleanup_dual;
    
    hr = IMFMediaType_SetGUID(video_type_out, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    if (FAILED(hr)) goto cleanup_dual;
    
    hr = IMFMediaType_SetGUID(video_type_out, &MF_MT_SUBTYPE, &MFVideoFormat_H264);
    if (FAILED(hr)) goto cleanup_dual;
    
    // Optimized bitrate - adaptive based on resolution and fps
    UINT32 optimized_bitrate;
    if (width >= 1920) {
        optimized_bitrate = 1200000; // 1.2 Mbps for 1080p+
    } else if (width >= 1280) {
        optimized_bitrate = 800000;  // 800 Kbps for 720p
    } else {
        optimized_bitrate = 500000;  // 500 Kbps for lower res
    }
    
    hr = IMFMediaType_SetUINT32(video_type_out, &MF_MT_AVG_BITRATE, optimized_bitrate);
    if (FAILED(hr)) goto cleanup_dual;
    
    // Set frame size and rate
    hr = IMFMediaType_SetUINT64(video_type_out, &MF_MT_FRAME_SIZE, ((UINT64)width << 32) | height);
    if (FAILED(hr)) goto cleanup_dual;
    
    hr = IMFMediaType_SetUINT64(video_type_out, &MF_MT_FRAME_RATE, ((UINT64)fps << 32) | 1);
    if (FAILED(hr)) goto cleanup_dual;
    
    hr = IMFMediaType_SetUINT32(video_type_out, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (FAILED(hr)) goto cleanup_dual;
    
    // Add video stream
    hr = IMFSinkWriter_AddStream(g_sink_writer, video_type_out, &g_video_stream_index);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to add video stream: 0x%08X\n", hr);
        goto cleanup_dual;
    }
    
    // Configure video input type (ARGB32)
    hr = MFCreateMediaType(&video_type_in);
    if (FAILED(hr)) goto cleanup_dual;
    
    hr = IMFMediaType_SetGUID(video_type_in, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    if (FAILED(hr)) goto cleanup_dual;
    
    hr = IMFMediaType_SetGUID(video_type_in, &MF_MT_SUBTYPE, &MFVideoFormat_ARGB32);
    if (FAILED(hr)) goto cleanup_dual;
    
    hr = IMFMediaType_SetUINT64(video_type_in, &MF_MT_FRAME_SIZE, ((UINT64)width << 32) | height);
    if (FAILED(hr)) goto cleanup_dual;
    
    hr = IMFMediaType_SetUINT64(video_type_in, &MF_MT_FRAME_RATE, ((UINT64)fps << 32) | 1);
    if (FAILED(hr)) goto cleanup_dual;
    
    hr = IMFMediaType_SetUINT32(video_type_in, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (FAILED(hr)) goto cleanup_dual;
    
    // Set input type for video stream
    hr = IMFSinkWriter_SetInputMediaType(g_sink_writer, g_video_stream_index, video_type_in, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to set video input type: 0x%08X\n", hr);
        goto cleanup_dual;
    }
    
    // Configure dual audio tracks if we have valid audio parameters
    if (include_audio) {
        // Create system audio track (track 1)
        hr = MFCreateMediaType(&system_audio_type_out);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetGUID(system_audio_type_out, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetGUID(system_audio_type_out, &MF_MT_SUBTYPE, &MFAudioFormat_AAC);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetUINT32(system_audio_type_out, &MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetUINT32(system_audio_type_out, &MF_MT_AUDIO_NUM_CHANNELS, channels);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetUINT32(system_audio_type_out, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetUINT32(system_audio_type_out, &MF_MT_AVG_BITRATE, 96000);
        if (FAILED(hr)) goto cleanup_dual;
        
        // Add system audio stream
        hr = IMFSinkWriter_AddStream(g_sink_writer, system_audio_type_out, &g_system_audio_stream_index);
        if (FAILED(hr)) {
            fprintf(stderr, "Failed to add system audio stream: 0x%08X\n", hr);
            goto cleanup_dual;
        }
        
        // Create microphone audio track (track 2) - identical format
        hr = MFCreateMediaType(&mic_audio_type_out);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetGUID(mic_audio_type_out, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetGUID(mic_audio_type_out, &MF_MT_SUBTYPE, &MFAudioFormat_AAC);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetUINT32(mic_audio_type_out, &MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetUINT32(mic_audio_type_out, &MF_MT_AUDIO_NUM_CHANNELS, channels);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetUINT32(mic_audio_type_out, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetUINT32(mic_audio_type_out, &MF_MT_AVG_BITRATE, 96000);
        if (FAILED(hr)) goto cleanup_dual;
        
        // Add microphone audio stream
        hr = IMFSinkWriter_AddStream(g_sink_writer, mic_audio_type_out, &g_mic_audio_stream_index);
        if (FAILED(hr)) {
            fprintf(stderr, "Failed to add microphone audio stream: 0x%08X\n", hr);
            goto cleanup_dual;
        }
        
        // Configure system audio input type
        hr = MFCreateMediaType(&system_audio_type_in);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetGUID(system_audio_type_in, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
        if (FAILED(hr)) goto cleanup_dual;
        
        if (bits_per_sample == 32) {
            hr = IMFMediaType_SetGUID(system_audio_type_in, &MF_MT_SUBTYPE, &MFAudioFormat_Float);
        } else {
            hr = IMFMediaType_SetGUID(system_audio_type_in, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
        }
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetUINT32(system_audio_type_in, &MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetUINT32(system_audio_type_in, &MF_MT_AUDIO_NUM_CHANNELS, channels);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetUINT32(system_audio_type_in, &MF_MT_AUDIO_BITS_PER_SAMPLE, bits_per_sample);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetUINT32(system_audio_type_in, &MF_MT_AUDIO_BLOCK_ALIGNMENT, (channels * bits_per_sample) / 8);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetUINT32(system_audio_type_in, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sample_rate * channels * bits_per_sample / 8);
        if (FAILED(hr)) goto cleanup_dual;
        
        // Set input type for system audio stream
        hr = IMFSinkWriter_SetInputMediaType(g_sink_writer, g_system_audio_stream_index, system_audio_type_in, NULL);
        if (FAILED(hr)) {
            fprintf(stderr, "Failed to set system audio input type: 0x%08X\n", hr);
            goto cleanup_dual;
        }
        
        // Configure microphone audio input type (identical to system)
        hr = MFCreateMediaType(&mic_audio_type_in);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetGUID(mic_audio_type_in, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
        if (FAILED(hr)) goto cleanup_dual;
        
        if (bits_per_sample == 32) {
            hr = IMFMediaType_SetGUID(mic_audio_type_in, &MF_MT_SUBTYPE, &MFAudioFormat_Float);
        } else {
            hr = IMFMediaType_SetGUID(mic_audio_type_in, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
        }
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetUINT32(mic_audio_type_in, &MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetUINT32(mic_audio_type_in, &MF_MT_AUDIO_NUM_CHANNELS, channels);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetUINT32(mic_audio_type_in, &MF_MT_AUDIO_BITS_PER_SAMPLE, bits_per_sample);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetUINT32(mic_audio_type_in, &MF_MT_AUDIO_BLOCK_ALIGNMENT, (channels * bits_per_sample) / 8);
        if (FAILED(hr)) goto cleanup_dual;
        
        hr = IMFMediaType_SetUINT32(mic_audio_type_in, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sample_rate * channels * bits_per_sample / 8);
        if (FAILED(hr)) goto cleanup_dual;
        
        // Set input type for microphone audio stream
        hr = IMFSinkWriter_SetInputMediaType(g_sink_writer, g_mic_audio_stream_index, mic_audio_type_in, NULL);
        if (FAILED(hr)) {
            fprintf(stderr, "Failed to set microphone audio input type: 0x%08X\n", hr);
            goto cleanup_dual;
        }
        
        printf("Dual-track audio configured: System (stream %d) + Microphone (stream %d)\n", 
               g_system_audio_stream_index, g_mic_audio_stream_index);
    }
    
    // Begin writing
    hr = IMFSinkWriter_BeginWriting(g_sink_writer);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to begin writing: 0x%08X\n", hr);
        goto cleanup_dual;
    }
    
    context->is_recording = TRUE;
    g_audio_sample_rate = sample_rate; // Store actual sample rate for timing calculations
    
    // Clean up temporary objects
    if (video_type_out) IMFMediaType_Release(video_type_out);
    if (video_type_in) IMFMediaType_Release(video_type_in);
    if (system_audio_type_out) IMFMediaType_Release(system_audio_type_out);
    if (system_audio_type_in) IMFMediaType_Release(system_audio_type_in);
    if (mic_audio_type_out) IMFMediaType_Release(mic_audio_type_out);
    if (mic_audio_type_in) IMFMediaType_Release(mic_audio_type_in);
    if (attributes) IMFAttributes_Release(attributes);
    free(wide_filename);
    
    return 0;

cleanup_dual:
    if (video_type_out) IMFMediaType_Release(video_type_out);
    if (video_type_in) IMFMediaType_Release(video_type_in);
    if (system_audio_type_out) IMFMediaType_Release(system_audio_type_out);
    if (system_audio_type_in) IMFMediaType_Release(system_audio_type_in);
    if (mic_audio_type_out) IMFMediaType_Release(mic_audio_type_out);
    if (mic_audio_type_in) IMFMediaType_Release(mic_audio_type_in);
    if (attributes) IMFAttributes_Release(attributes);
    if (g_sink_writer) {
        IMFSinkWriter_Release(g_sink_writer);
        g_sink_writer = NULL;
    }
    free(wide_filename);
    MFShutdown();
    
    return -1;
}

// Initialize muxer for audio-only recording (AAC in MP4 container)
int encoder_init_audio_only(encoder_context_t* context, const char* filename, int sample_rate, int channels, int bits_per_sample) {
    if (!context || !filename) return -1;
    
    memset(context, 0, sizeof(encoder_context_t));
    context->output_filename = filename;
    context->input_sample_rate = sample_rate;
    context->input_channels = channels;
    context->input_bits_per_sample = bits_per_sample;
    context->dual_track_mode = FALSE;
    
    // Set global state for audio-only mode
    g_dual_track_mode = FALSE;
    g_audio_sample_count = 0;
    
    HRESULT hr;
    IMFMediaType* audio_type_out = NULL;
    IMFMediaType* audio_type_in = NULL;
    IMFAttributes* attributes = NULL;
    
    // Initialize Media Foundation
    hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to initialize Media Foundation: 0x%08X\n", hr);
        return -1;
    }
    
    // Convert filename to wide string
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, filename, -1, NULL, 0);
    wchar_t* wide_filename = malloc(wide_len * sizeof(wchar_t));
    if (!wide_filename) {
        fprintf(stderr, "Failed to allocate memory for filename\n");
        MFShutdown();
        return -1;
    }
    MultiByteToWideChar(CP_UTF8, 0, filename, -1, wide_filename, wide_len);
    
    // Create sink writer attributes
    hr = MFCreateAttributes(&attributes, 5);  // Increased to 5 for container type
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create attributes: 0x%08X\n", hr);
        free(wide_filename);
        MFShutdown();
        return -1;
    }
    
    // CRITICAL FIX: Set MP4 container type for proper moov atom generation
    hr = IMFAttributes_SetGUID(attributes, &MF_TRANSCODE_CONTAINERTYPE, &MFTranscodeContainerType_MPEG4);
    if (FAILED(hr)) {
        fprintf(stderr, "Warning: Failed to set MP4 container type: 0x%08X\n", hr);
    }
    
    // Enable low latency mode
    hr = IMFAttributes_SetUINT32(attributes, &MF_LOW_LATENCY, TRUE);
    if (FAILED(hr)) {
        fprintf(stderr, "Warning: Failed to set low latency mode: 0x%08X\n", hr);
    }
    
    // Force video-compatible timescale by disabling throttling and enabling hardware transforms
    hr = IMFAttributes_SetUINT32(attributes, &MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    if (FAILED(hr)) {
        fprintf(stderr, "Warning: Failed to disable throttling: 0x%08X\n", hr);
    }
    
    hr = IMFAttributes_SetUINT32(attributes, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    if (FAILED(hr)) {
        fprintf(stderr, "Warning: Failed to enable hardware transforms: 0x%08X\n", hr);
    }
      // Create sink writer for MP4 file
    hr = MFCreateSinkWriterFromURL(wide_filename, NULL, attributes, &g_sink_writer);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create sink writer: 0x%08X\n", hr);
        goto cleanup_audio_only;
    }

    // Create AAC output audio type
    hr = MFCreateMediaType(&audio_type_out);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create audio output type: 0x%08X\n", hr);
        goto cleanup_audio_only;
    }
    
    // Configure AAC output format - use video-compatible sample rate for consistent timescale
    hr = IMFMediaType_SetGUID(audio_type_out, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetGUID(audio_type_out, &MF_MT_SUBTYPE, &MFAudioFormat_AAC);
    if (SUCCEEDED(hr))        hr = IMFMediaType_SetUINT32(audio_type_out, &MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(audio_type_out, &MF_MT_AUDIO_NUM_CHANNELS, channels);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(audio_type_out, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16); // AAC standard
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(audio_type_out, &MF_MT_AVG_BITRATE, 96000); // Same as MP4 mode
    
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to configure AAC output type: 0x%08X\n", hr);
        goto cleanup_audio_only;
    }
    
    // Add audio stream
    hr = IMFSinkWriter_AddStream(g_sink_writer, audio_type_out, &g_audio_stream_index);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to add audio stream: 0x%08X\n", hr);
        goto cleanup_audio_only;
    }
    
    // Create PCM input audio type
    hr = MFCreateMediaType(&audio_type_in);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create audio input type: 0x%08X\n", hr);
        goto cleanup_audio_only;
    }
    
    // Configure PCM input format - match MP4 mode input format (known to work)
    hr = IMFMediaType_SetGUID(audio_type_in, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    if (SUCCEEDED(hr)) {
        // Use the same input format as MP4 mode
        if (bits_per_sample == 32) {
            hr = IMFMediaType_SetGUID(audio_type_in, &MF_MT_SUBTYPE, &MFAudioFormat_Float);
        } else {
            hr = IMFMediaType_SetGUID(audio_type_in, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
        }
    }
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(audio_type_in, &MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(audio_type_in, &MF_MT_AUDIO_NUM_CHANNELS, channels);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(audio_type_in, &MF_MT_AUDIO_BITS_PER_SAMPLE, bits_per_sample);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(audio_type_in, &MF_MT_AUDIO_BLOCK_ALIGNMENT, (channels * bits_per_sample) / 8);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(audio_type_in, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sample_rate * channels * bits_per_sample / 8);
    
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to configure PCM input type: 0x%08X\n", hr);
        goto cleanup_audio_only;
    }
    
    // Set input type
    hr = IMFSinkWriter_SetInputMediaType(g_sink_writer, g_audio_stream_index, audio_type_in, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to set audio input type: 0x%08X\n", hr);
        goto cleanup_audio_only;
    }
    
    // Begin writing
    hr = IMFSinkWriter_BeginWriting(g_sink_writer);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to begin writing: 0x%08X\n", hr);
        goto cleanup_audio_only;
    }
    
    context->is_recording = TRUE;
    g_audio_sample_rate = sample_rate; // Store actual sample rate for timing calculations
    printf("Audio-only recording initialized (AAC in MP4 container): %d Hz, %d channels, %d bits\n", 
           sample_rate, channels, bits_per_sample);
    
    // Clean up temporary objects
    if (audio_type_out) IMFMediaType_Release(audio_type_out);
    if (audio_type_in) IMFMediaType_Release(audio_type_in);
    if (attributes) IMFAttributes_Release(attributes);
    free(wide_filename);
    
    return 0;

cleanup_audio_only:
    if (audio_type_out) IMFMediaType_Release(audio_type_out);
    if (audio_type_in) IMFMediaType_Release(audio_type_in);
    if (attributes) IMFAttributes_Release(attributes);
    if (g_sink_writer) {
        IMFSinkWriter_Release(g_sink_writer);
        g_sink_writer = NULL;
    }
    free(wide_filename);
    MFShutdown();
    
    return -1;
}

// Initialize muxer for audio-only dual-track recording (MP4 output with separate system/mic tracks)
int encoder_init_audio_only_dual_track(encoder_context_t* context, const char* filename, int sample_rate, int channels, int bits_per_sample) {
    if (!context || !filename) return -1;
    
    memset(context, 0, sizeof(encoder_context_t));
    context->output_filename = filename;
    context->input_sample_rate = sample_rate;
    context->input_channels = channels;
    context->input_bits_per_sample = bits_per_sample;
    context->dual_track_mode = TRUE;
    
    // Set global state for audio-only dual-track mode
    g_dual_track_mode = TRUE;
    g_system_audio_sample_count = 0;
    g_mic_audio_sample_count = 0;
    
    HRESULT hr;
    IMFMediaType* system_audio_type_out = NULL;
    IMFMediaType* system_audio_type_in = NULL;
    IMFMediaType* mic_audio_type_out = NULL;
    IMFMediaType* mic_audio_type_in = NULL;
    IMFAttributes* attributes = NULL;
    
    // Initialize Media Foundation
    hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to initialize Media Foundation: 0x%08X\n", hr);
        return -1;
    }
    
    // Convert filename to wide string
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, filename, -1, NULL, 0);
    wchar_t* wide_filename = malloc(wide_len * sizeof(wchar_t));
    if (!wide_filename) {
        fprintf(stderr, "Failed to allocate memory for filename\n");
        MFShutdown();
        return -1;
    }
    MultiByteToWideChar(CP_UTF8, 0, filename, -1, wide_filename, wide_len);
    
    // Create sink writer attributes for audio-only dual-track mode
    hr = MFCreateAttributes(&attributes, 5);  // Increased to 5 for container type
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create attributes: 0x%08X\n", hr);
        free(wide_filename);
        MFShutdown();
        return -1;
    }
    
    // CRITICAL FIX: Set MP4 container type for proper moov atom generation
    hr = IMFAttributes_SetGUID(attributes, &MF_TRANSCODE_CONTAINERTYPE, &MFTranscodeContainerType_MPEG4);
    if (FAILED(hr)) {
        fprintf(stderr, "Warning: Failed to set MP4 container type: 0x%08X\n", hr);
    }
    
    // Enable low latency mode
    hr = IMFAttributes_SetUINT32(attributes, &MF_LOW_LATENCY, TRUE);
    if (FAILED(hr)) {
        fprintf(stderr, "Warning: Failed to set low latency mode: 0x%08X\n", hr);
    }
    
    // Disable throttling
    hr = IMFAttributes_SetUINT32(attributes, &MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    if (FAILED(hr)) {
        fprintf(stderr, "Warning: Failed to disable throttling: 0x%08X\n", hr);
    }
    
    // Enable hardware transforms
    hr = IMFAttributes_SetUINT32(attributes, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    if (FAILED(hr)) {
        fprintf(stderr, "Warning: Failed to enable hardware transforms: 0x%08X\n", hr);
    }
    
    // Store original sample rate for timestamp conversion in audio-only mode
    // Temporarily disable container timescale override - needs further investigation
    
    // Create sink writer for MP4 file
    hr = MFCreateSinkWriterFromURL(wide_filename, NULL, attributes, &g_sink_writer);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create sink writer: 0x%08X\n", hr);
        goto cleanup_audio_dual;
    }

    // Create system audio AAC output type
    hr = MFCreateMediaType(&system_audio_type_out);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create system audio output type: 0x%08X\n", hr);
        goto cleanup_audio_dual;
    }
    
    // Configure system audio AAC output format (same as regular MP4 mode)
    hr = IMFMediaType_SetGUID(system_audio_type_out, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetGUID(system_audio_type_out, &MF_MT_SUBTYPE, &MFAudioFormat_AAC);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(system_audio_type_out, &MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(system_audio_type_out, &MF_MT_AUDIO_NUM_CHANNELS, channels);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(system_audio_type_out, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16); // AAC standard
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(system_audio_type_out, &MF_MT_AVG_BITRATE, 96000); // Same as MP4 mode
    
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to configure system audio AAC output type: 0x%08X\n", hr);
        goto cleanup_audio_dual;
    }
    
    // Add system audio stream
    hr = IMFSinkWriter_AddStream(g_sink_writer, system_audio_type_out, &g_system_audio_stream_index);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to add system audio stream: 0x%08X\n", hr);
        goto cleanup_audio_dual;
    }
    
    // Create microphone audio AAC output type
    hr = MFCreateMediaType(&mic_audio_type_out);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create microphone audio output type: 0x%08X\n", hr);
        goto cleanup_audio_dual;
    }
    
    // Configure microphone audio AAC output format (same as regular MP4 mode)
    hr = IMFMediaType_SetGUID(mic_audio_type_out, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetGUID(mic_audio_type_out, &MF_MT_SUBTYPE, &MFAudioFormat_AAC);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(mic_audio_type_out, &MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(mic_audio_type_out, &MF_MT_AUDIO_NUM_CHANNELS, channels);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(mic_audio_type_out, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16); // AAC standard
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(mic_audio_type_out, &MF_MT_AVG_BITRATE, 96000); // Same as MP4 mode
    
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to configure microphone audio AAC output type: 0x%08X\n", hr);
        goto cleanup_audio_dual;
    }
    
    // Add microphone audio stream
    hr = IMFSinkWriter_AddStream(g_sink_writer, mic_audio_type_out, &g_mic_audio_stream_index);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to add microphone audio stream: 0x%08X\n", hr);
        goto cleanup_audio_dual;
    }
    
    // Create system audio PCM input type
    hr = MFCreateMediaType(&system_audio_type_in);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create system audio input type: 0x%08X\n", hr);
        goto cleanup_audio_dual;
    }
    
    // Configure system audio PCM input format (match MP4 audio input logic)
    hr = IMFMediaType_SetGUID(system_audio_type_in, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    if (SUCCEEDED(hr)) {
        // Use IEEE Float for 32-bit audio to match Windows audio format, PCM for others
        if (bits_per_sample == 32) {
            hr = IMFMediaType_SetGUID(system_audio_type_in, &MF_MT_SUBTYPE, &MFAudioFormat_Float);
        } else {
            hr = IMFMediaType_SetGUID(system_audio_type_in, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
        }
    }
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(system_audio_type_in, &MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(system_audio_type_in, &MF_MT_AUDIO_NUM_CHANNELS, channels);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(system_audio_type_in, &MF_MT_AUDIO_BITS_PER_SAMPLE, bits_per_sample);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(system_audio_type_in, &MF_MT_AUDIO_BLOCK_ALIGNMENT, (channels * bits_per_sample) / 8);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(system_audio_type_in, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sample_rate * channels * bits_per_sample / 8);
    
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to configure system audio PCM input type: 0x%08X\n", hr);
        goto cleanup_audio_dual;
    }
    
    // Set system audio input type
    hr = IMFSinkWriter_SetInputMediaType(g_sink_writer, g_system_audio_stream_index, system_audio_type_in, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to set system audio input type: 0x%08X\n", hr);
        goto cleanup_audio_dual;
    }
    
    // Create microphone audio PCM input type
    hr = MFCreateMediaType(&mic_audio_type_in);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create microphone audio input type: 0x%08X\n", hr);
        goto cleanup_audio_dual;
    }
    
    // Configure microphone audio PCM input format (match MP4 audio input logic)
    hr = IMFMediaType_SetGUID(mic_audio_type_in, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    if (SUCCEEDED(hr)) {
        // Use IEEE Float for 32-bit audio to match Windows audio format, PCM for others
        if (bits_per_sample == 32) {
            hr = IMFMediaType_SetGUID(mic_audio_type_in, &MF_MT_SUBTYPE, &MFAudioFormat_Float);
        } else {
            hr = IMFMediaType_SetGUID(mic_audio_type_in, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
        }
    }
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(mic_audio_type_in, &MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(mic_audio_type_in, &MF_MT_AUDIO_NUM_CHANNELS, channels);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(mic_audio_type_in, &MF_MT_AUDIO_BITS_PER_SAMPLE, bits_per_sample);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(mic_audio_type_in, &MF_MT_AUDIO_BLOCK_ALIGNMENT, (channels * bits_per_sample) / 8);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(mic_audio_type_in, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sample_rate * channels * bits_per_sample / 8);
    
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to configure microphone audio PCM input type: 0x%08X\n", hr);
        goto cleanup_audio_dual;
    }
    
    // Set microphone audio input type
    hr = IMFSinkWriter_SetInputMediaType(g_sink_writer, g_mic_audio_stream_index, mic_audio_type_in, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to set microphone audio input type: 0x%08X\n", hr);
        goto cleanup_audio_dual;
    }
    
    // Begin writing
    hr = IMFSinkWriter_BeginWriting(g_sink_writer);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to begin writing: 0x%08X\n", hr);
        goto cleanup_audio_dual;
    }
    
    context->is_recording = TRUE;
    g_audio_sample_rate = sample_rate; // Store actual sample rate for timing calculations
    printf("Audio-only dual-track recording initialized (MP4 output): System (stream %d) + Microphone (stream %d)\n", 
           g_system_audio_stream_index, g_mic_audio_stream_index);
    
    // Clean up temporary objects
    if (system_audio_type_out) IMFMediaType_Release(system_audio_type_out);
    if (system_audio_type_in) IMFMediaType_Release(system_audio_type_in);
    if (mic_audio_type_out) IMFMediaType_Release(mic_audio_type_out);
    if (mic_audio_type_in) IMFMediaType_Release(mic_audio_type_in);
    if (attributes) IMFAttributes_Release(attributes);
    free(wide_filename);
    
    return 0;

cleanup_audio_dual:
    if (system_audio_type_out) IMFMediaType_Release(system_audio_type_out);
    if (system_audio_type_in) IMFMediaType_Release(system_audio_type_in);
    if (mic_audio_type_out) IMFMediaType_Release(mic_audio_type_out);
    if (mic_audio_type_in) IMFMediaType_Release(mic_audio_type_in);
    if (attributes) IMFAttributes_Release(attributes);
    if (g_sink_writer) {
        IMFSinkWriter_Release(g_sink_writer);
        g_sink_writer = NULL;
    }
    free(wide_filename);
    MFShutdown();

    return -1;
}

int encoder_add_video_frame(encoder_context_t* context, void* frame_data, size_t frame_size, DWORD elapsed_ms) {
    // Suppress unused parameter warning - we calculate buffer size from video dimensions for consistency
    UNREFERENCED_PARAMETER(frame_size);
    
    if (!context || !context->is_recording || !frame_data || !g_sink_writer) return -1;
    
    HRESULT hr;
    IMFSample* sample = NULL;
    IMFMediaBuffer* buffer = NULL;
    BYTE* buffer_data = NULL;
    DWORD buffer_length = g_video_width * g_video_height * 4; // BGRA = 4 bytes per pixel
    
    // Create sample
    hr = MFCreateSample(&sample);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create video sample: 0x%08X\n", hr);
        return -1;
    }
    
    // Create media buffer
    hr = MFCreateMemoryBuffer(buffer_length, &buffer);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create video buffer: 0x%08X\n", hr);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Lock buffer and copy data
    hr = IMFMediaBuffer_Lock(buffer, &buffer_data, NULL, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to lock video buffer: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Copy BGRA frame data directly (Windows Media Foundation will handle conversion)
    memcpy(buffer_data, frame_data, buffer_length);
    
    // Unlock buffer
    hr = IMFMediaBuffer_Unlock(buffer);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to unlock video buffer: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Set current length
    hr = IMFMediaBuffer_SetCurrentLength(buffer, buffer_length);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to set video buffer length: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Add buffer to sample
    hr = IMFSample_AddBuffer(sample, buffer);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to add video buffer to sample: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // CRITICAL FIX: Use frame-based timing instead of real-time for consistent playback speed
    // Calculate timestamp based on frame number and target FPS for consistent timing
    LONGLONG timestamp = (LONGLONG)(g_video_frame_count * 10000000LL / g_video_fps);
    hr = IMFSample_SetSampleTime(sample, timestamp);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to set video sample time: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Use fixed frame duration based on target FPS for consistent playback speed
    LONGLONG duration = 10000000LL / g_video_fps;
    hr = IMFSample_SetSampleDuration(sample, duration);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to set video sample duration: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    hr = IMFSinkWriter_WriteSample(g_sink_writer, g_video_stream_index, sample);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to write video sample: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    g_video_frame_count++;
    
#ifdef DEBUG
    if (g_video_frame_count % 30 == 0) {
        printf("Video: %lld frames, timestamp=%.2fs, elapsed=%lums\n", 
               g_video_frame_count, timestamp / 10000000.0, elapsed_ms);
    }
#endif
    
    IMFMediaBuffer_Release(buffer);
    IMFSample_Release(sample);
    
    return 0;
}

int encoder_add_audio_frame(encoder_context_t* context, BYTE* audio_data, UINT32 num_frames, DWORD elapsed_ms) {
    if (!context || !context->is_recording || !audio_data || !g_sink_writer) return -1;
    
    // Check if audio stream is valid (video-only mode)
    if (g_audio_stream_index == (DWORD)-1) {
        // Silently ignore audio frames when audio is disabled
        return 0;
    }
    
    HRESULT hr;
    IMFSample* sample = NULL;
    IMFMediaBuffer* buffer = NULL;
    BYTE* buffer_data = NULL;
    DWORD buffer_length = num_frames * context->input_channels * (context->input_bits_per_sample / 8);
    
    // Create sample
    hr = MFCreateSample(&sample);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create audio sample: 0x%08X\n", hr);
        return -1;
    }
    
    // Create media buffer
    hr = MFCreateMemoryBuffer(buffer_length, &buffer);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create audio buffer: 0x%08X\n", hr);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Lock buffer and copy data
    hr = IMFMediaBuffer_Lock(buffer, &buffer_data, NULL, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to lock audio buffer: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Copy audio data
    memcpy(buffer_data, audio_data, buffer_length);
    
    // Unlock buffer
    hr = IMFMediaBuffer_Unlock(buffer);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to unlock audio buffer: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Set current length
    hr = IMFMediaBuffer_SetCurrentLength(buffer, buffer_length);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to set audio buffer length: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Add buffer to sample
    hr = IMFSample_AddBuffer(sample, buffer);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to add audio buffer to sample: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // CRITICAL FIX: Use sample-based timing for audio instead of real-time for consistent sync
    // Calculate timestamp based on accumulated audio samples for accurate timing
    // CRITICAL FIX: Use OUTPUT sample rate (44100 Hz) for timing calculations, not input sample rate
    LONGLONG timestamp = (LONGLONG)(g_audio_sample_count * 10000000LL / 44100);
    hr = IMFSample_SetSampleTime(sample, timestamp);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to set audio sample time: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Update sample count before calculating duration
    g_audio_sample_count += num_frames;
    
    // DEBUG: Log sample accumulation for timing diagnosis
    static UINT64 last_log_time = 0;
    static UINT64 samples_at_last_log = 0;
    UINT64 current_sample_count = g_audio_sample_count;
    
    if (current_sample_count - samples_at_last_log >= 44100) { // Log every ~1 second of samples (44100 Hz)
        printf("Audio samples: %llu total, %llu in last batch, %.3f seconds encoded\n", 
               current_sample_count, current_sample_count - samples_at_last_log, 
               (double)current_sample_count / 44100);
        samples_at_last_log = current_sample_count;
    }
    
    // Calculate duration based on number of frames and OUTPUT sample rate
    LONGLONG duration = (LONGLONG)(num_frames * 10000000LL / 44100);
    hr = IMFSample_SetSampleDuration(sample, duration);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to set audio sample duration: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
      hr = IMFSinkWriter_WriteSample(g_sink_writer, g_audio_stream_index, sample);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to write audio sample: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    IMFMediaBuffer_Release(buffer);
    IMFSample_Release(sample);
    
    return 0;
}

// Add system audio frame (dual-track mode)
int encoder_add_system_audio_frame(encoder_context_t* context, BYTE* audio_data, UINT32 num_frames, DWORD elapsed_ms) {
    if (!context || !context->is_recording || !audio_data || !g_sink_writer || !g_dual_track_mode) return -1;
    
    HRESULT hr;
    IMFSample* sample = NULL;
    IMFMediaBuffer* buffer = NULL;
    BYTE* buffer_data = NULL;
    DWORD buffer_length = num_frames * context->input_channels * (context->input_bits_per_sample / 8);
    
    // Create sample
    hr = MFCreateSample(&sample);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create system audio sample: 0x%08X\n", hr);
        return -1;
    }
    
    // Create media buffer
    hr = MFCreateMemoryBuffer(buffer_length, &buffer);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create system audio buffer: 0x%08X\n", hr);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Lock buffer and copy data
    hr = IMFMediaBuffer_Lock(buffer, &buffer_data, NULL, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to lock system audio buffer: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Copy audio data
    memcpy(buffer_data, audio_data, buffer_length);
    
    // Unlock buffer
    hr = IMFMediaBuffer_Unlock(buffer);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to unlock system audio buffer: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Set current length
    hr = IMFMediaBuffer_SetCurrentLength(buffer, buffer_length);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to set system audio buffer length: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Add buffer to sample
    hr = IMFSample_AddBuffer(sample, buffer);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to add system audio buffer to sample: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // CRITICAL FIX: Use OUTPUT sample rate (44100 Hz) for system audio timing instead of input sample rate
    LONGLONG timestamp = (LONGLONG)(g_system_audio_sample_count * 10000000LL / 44100);
    hr = IMFSample_SetSampleTime(sample, timestamp);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to set system audio sample time: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Increment sample count before calculating duration
    g_system_audio_sample_count += num_frames;
    
    // Set sample duration based on frame count and OUTPUT sample rate
    LONGLONG duration = (LONGLONG)(num_frames * 10000000LL / 44100);
    hr = IMFSample_SetSampleDuration(sample, duration);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to set system audio sample duration: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Write sample to system audio stream
    hr = IMFSinkWriter_WriteSample(g_sink_writer, g_system_audio_stream_index, sample);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to write system audio sample: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Cleanup
    IMFMediaBuffer_Release(buffer);
    IMFSample_Release(sample);
    
    return 0;
}

// Add microphone audio frame (dual-track mode)
int encoder_add_mic_audio_frame(encoder_context_t* context, BYTE* audio_data, UINT32 num_frames, DWORD elapsed_ms) {
    if (!context || !context->is_recording || !audio_data || !g_sink_writer || !g_dual_track_mode) return -1;
    
    HRESULT hr;
    IMFSample* sample = NULL;
    IMFMediaBuffer* buffer = NULL;
    BYTE* buffer_data = NULL;
    DWORD buffer_length = num_frames * context->input_channels * (context->input_bits_per_sample / 8);
    
    // Create sample
    hr = MFCreateSample(&sample);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create microphone audio sample: 0x%08X\n", hr);
        return -1;
    }
    
    // Create media buffer
    hr = MFCreateMemoryBuffer(buffer_length, &buffer);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to create microphone audio buffer: 0x%08X\n", hr);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Lock buffer and copy data
    hr = IMFMediaBuffer_Lock(buffer, &buffer_data, NULL, NULL);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to lock microphone audio buffer: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Copy audio data
    memcpy(buffer_data, audio_data, buffer_length);
    
    // Unlock buffer
    hr = IMFMediaBuffer_Unlock(buffer);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to unlock microphone audio buffer: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Set current length
    hr = IMFMediaBuffer_SetCurrentLength(buffer, buffer_length);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to set microphone audio buffer length: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Add buffer to sample
    hr = IMFSample_AddBuffer(sample, buffer);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to add microphone audio buffer to sample: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // CRITICAL FIX: Use OUTPUT sample rate (44100 Hz) for microphone audio timing instead of input sample rate
    LONGLONG timestamp = (LONGLONG)(g_mic_audio_sample_count * 10000000LL / 44100);
    hr = IMFSample_SetSampleTime(sample, timestamp);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to set microphone audio sample time: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Increment sample count before calculating duration
    g_mic_audio_sample_count += num_frames;
    
    // Set sample duration based on frame count and OUTPUT sample rate
    LONGLONG duration = (LONGLONG)(num_frames * 10000000LL / 44100);
    hr = IMFSample_SetSampleDuration(sample, duration);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to set microphone audio sample duration: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Write sample to microphone audio stream
    hr = IMFSinkWriter_WriteSample(g_sink_writer, g_mic_audio_stream_index, sample);
    if (FAILED(hr)) {
        fprintf(stderr, "Failed to write microphone audio sample: 0x%08X\n", hr);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return -1;
    }
    
    // Cleanup
    IMFMediaBuffer_Release(buffer);
    IMFSample_Release(sample);
    
    return 0;
}

int encoder_finalize(encoder_context_t* context) {
    if (!context) return -1;
    
    if (g_sink_writer) {
        printf("Finalizing WMF sink writer with %lld frames...\n", g_video_frame_count);
        
        // CRITICAL FIX: Flush the sink writer before finalization
        printf("Flushing sink writer...\n");
        HRESULT flush_hr = IMFSinkWriter_Flush(g_sink_writer, MF_SINK_WRITER_ALL_STREAMS);
        if (FAILED(flush_hr)) {
            fprintf(stderr, "Warning: Failed to flush sink writer: 0x%08X\n", flush_hr);
        } else {
            printf("Sink writer flushed successfully\n");
        }
        
        // CRITICAL FIX: Send end-of-stream markers before finalization
        UINT64 total_audio_samples = g_audio_sample_count + g_system_audio_sample_count + g_mic_audio_sample_count;
        
        // Send end-of-stream for video if we have video
        if (g_video_frame_count > 0) {
            printf("Sending video end-of-stream...\n");
            HRESULT hr = IMFSinkWriter_SendStreamTick(g_sink_writer, g_video_stream_index, g_last_video_timestamp);
            if (FAILED(hr)) {
                fprintf(stderr, "Warning: Failed to send video end-of-stream: 0x%08X\n", hr);
            }
        }
        
        // Send end-of-stream for audio streams if we have audio
        if (total_audio_samples > 0) {
            if (g_dual_track_mode) {
                if (g_system_audio_sample_count > 0) {
                    printf("Sending system audio end-of-stream...\n");
                    LONGLONG system_timestamp = (g_system_audio_sample_count * 10000000LL) / g_audio_sample_rate;
                    HRESULT hr = IMFSinkWriter_SendStreamTick(g_sink_writer, g_system_audio_stream_index, system_timestamp);
                    if (FAILED(hr)) {
                        fprintf(stderr, "Warning: Failed to send system audio end-of-stream: 0x%08X\n", hr);
                    }
                }
                if (g_mic_audio_sample_count > 0) {
                    printf("Sending microphone audio end-of-stream...\n");
                    LONGLONG mic_timestamp = (g_mic_audio_sample_count * 10000000LL) / g_audio_sample_rate;
                    HRESULT hr = IMFSinkWriter_SendStreamTick(g_sink_writer, g_mic_audio_stream_index, mic_timestamp);
                    if (FAILED(hr)) {
                        fprintf(stderr, "Warning: Failed to send microphone audio end-of-stream: 0x%08X\n", hr);
                    }
                }
            } else {
                if (g_audio_sample_count > 0) {
                    printf("Sending audio end-of-stream...\n");
                    LONGLONG audio_timestamp = (g_audio_sample_count * 10000000LL) / g_audio_sample_rate;
                    HRESULT hr = IMFSinkWriter_SendStreamTick(g_sink_writer, g_audio_stream_index, audio_timestamp);
                    if (FAILED(hr)) {
                        fprintf(stderr, "Warning: Failed to send audio end-of-stream: 0x%08X\n", hr);
                    }
                }
            }
        }
        
        // CRITICAL FIX: Always finalize the sink writer to ensure proper MP4 structure
        // Even if no frames/samples were captured, the file needs proper moov atom
        if (g_video_frame_count == 0 && total_audio_samples == 0) {
            printf("Warning: No audio or video data captured, but finalizing anyway for proper MP4 structure\n");
        }
        
        printf("Finalizing sink writer...\n");
        HRESULT hr = IMFSinkWriter_Finalize(g_sink_writer);
        if (FAILED(hr)) {
            fprintf(stderr, "Failed to finalize sink writer: 0x%08X\n", hr);
            // For empty files, this is expected, don't treat as fatal error
            if (hr == 0xC00D4A44) {
                printf("Note: Finalization failed due to empty media file\n");
                return 0;
            }
            return -1;
        }
        
        printf("WMF finalization successful\n");
    }
    
    context->is_recording = FALSE;
    printf("Recording finalized\n");
    return 0;
}

void encoder_cleanup(encoder_context_t* context) {
    if (!context) return;
    
    // Only cleanup if we actually have resources to clean
    if (g_sink_writer) {
        IMFSinkWriter_Release(g_sink_writer);
        g_sink_writer = NULL;
        printf("Muxer cleaned up\n");
        
        // Only shutdown MF when we're actually releasing the sink writer
        MFShutdown();
    }
    
    // CRITICAL: Reset all global state variables to prevent carryover between recordings
    g_video_stream_index = 0;
    g_audio_stream_index = 0;
    g_system_audio_stream_index = 0;
    g_mic_audio_stream_index = 0;
    g_dual_track_mode = FALSE;
    g_video_frame_count = 0;
    g_audio_sample_count = 0;
    g_system_audio_sample_count = 0;
    g_mic_audio_sample_count = 0;
    g_video_width = 0;
    g_video_height = 0;
    g_video_fps = 30;
    g_recording_start_time = 0;
    g_last_video_timestamp = 0;
    
    memset(context, 0, sizeof(encoder_context_t));
}

// Set the actual recording start time when capture begins
void encoder_set_recording_start_time(DWORD start_time) {
    g_recording_start_time = start_time;
    printf("Recording start time synchronized: %lu ms\n", g_recording_start_time);
}
