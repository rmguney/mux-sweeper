#ifndef ENCODER_H
#define ENCODER_H

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

// Encoder context for muxing video and audio streams
typedef struct {
    const char* output_filename;
    BOOL dual_track_mode;
    BOOL audio_only_mode;
    BOOL is_recording;
    
    // Input format specifications
    int input_sample_rate;
    int input_channels;
    int input_bits_per_sample;
    
    // Video specifications (for video+audio mode)
    int video_width;
    int video_height;
    int video_fps;
} encoder_context_t;

// Core encoding functions
int encoder_init(encoder_context_t* context, const char* filename, 
                int width, int height, int fps,
                int sample_rate, int channels, int bits_per_sample);

int encoder_init_audio_only(encoder_context_t* context, const char* filename,
                           int sample_rate, int channels, int bits_per_sample);

int encoder_init_dual_track(encoder_context_t* context, const char* filename,
                           int width, int height, int fps,
                           int sample_rate, int channels, int bits_per_sample);

int encoder_init_audio_only_dual_track(encoder_context_t* context, const char* filename,
                                      int sample_rate, int channels, int bits_per_sample);

// Stream management
void encoder_set_recording_start_time(DWORD start_time);

// Data input functions
int encoder_add_video_frame(encoder_context_t* context, void* frame_data, size_t frame_size, DWORD elapsed_ms);
int encoder_add_audio_frame(encoder_context_t* context, BYTE* audio_data, UINT32 num_frames, DWORD elapsed_ms);
int encoder_add_system_audio_frame(encoder_context_t* context, BYTE* audio_data, UINT32 num_frames, DWORD elapsed_ms);
int encoder_add_mic_audio_frame(encoder_context_t* context, BYTE* audio_data, UINT32 num_frames, DWORD elapsed_ms);

// Finalization
int encoder_finalize(encoder_context_t* context);
void encoder_cleanup(encoder_context_t* context);

#endif // ENCODER_H
