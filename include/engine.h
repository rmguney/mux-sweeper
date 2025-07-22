#ifndef ENGINE_H
#define ENGINE_H

#include <windows.h>

// Audio source type enumeration
typedef enum {
    AUDIO_SOURCE_NONE = 0,
    AUDIO_SOURCE_SYSTEM = 1,
    AUDIO_SOURCE_MICROPHONE = 2,
    AUDIO_SOURCE_BOTH = 3
} audio_source_type_t;

// Capture parameters structure
typedef struct {
    char output_filename[MAX_PATH];
    int fps;
    int duration;
    audio_source_type_t audio_sources;
    BOOL force_stop; // For external stop requests
    BOOL enable_video; // Enable video recording
    BOOL enable_system_audio; // Enable system audio recording
    BOOL enable_microphone; // Enable microphone recording
    BOOL audio_only_mode; // True if recording audio only (MP3 output)
    // MVP: Video capture parameters
    int monitor_index; // Monitor to capture (default: 0)
    BOOL cursor_enabled; // Include cursor in capture (default: TRUE)
    BOOL region_enabled; // Use specific region instead of full screen
    int region_x, region_y, region_w, region_h; // Region coordinates
} capture_params_t;

// Capture statistics
typedef struct {
    int total_frames;
    int failed_frames;
    DWORD recording_duration_ms;
    BOOL audio_enabled;
    int audio_sample_rate;
    int audio_channels;
    int audio_bits_per_sample;
} capture_stats_t;

// Callback function types for status updates
typedef void (*capture_status_callback_t)(const char* message);
typedef void (*capture_progress_callback_t)(int frame_count, DWORD elapsed_ms);

// Capture engine context
typedef struct {
    capture_params_t params;
    capture_stats_t stats;
    capture_status_callback_t status_callback;
    capture_progress_callback_t progress_callback;
    BOOL is_running;
    BOOL force_stop;
} capture_engine_t;

// Function declarations
int engine_init(capture_engine_t* engine);
int engine_start(capture_engine_t* engine, const capture_params_t* params);
int engine_stop(capture_engine_t* engine);
void engine_cleanup(capture_engine_t* engine);
void engine_set_status_callback(capture_engine_t* engine, capture_status_callback_t callback);
void engine_set_progress_callback(capture_engine_t* engine, capture_progress_callback_t callback);
BOOL engine_is_running(const capture_engine_t* engine);
const capture_stats_t* engine_get_stats(const capture_engine_t* engine);

#endif // ENGINE_H
