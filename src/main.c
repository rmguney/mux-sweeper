#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "engine.h"
#include "record.h"
#include "params.h"
#include "arguments.h"
#include "signals.h"
#include "callbacks.h"

// Global capture engine
static capture_engine_t g_engine = {0};

// Note: Signal handling is now in signals.c module

void print_usage(const char* program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  -o, --out <file>       Output filename (default: yymmddhhmmss.mp4)\n");
    printf("  -t, --time <seconds>   Recording duration in seconds (default: unlimited)\n");
    printf("  -v, --video            Enable video capture\n");
    printf("  -s, --system           Enable system audio capture\n");
    printf("  -m, --microphone       Enable microphone capture\n");
    printf("  --fps <rate>           Frame rate (default: 30)\n");
    printf("  -h, --help             Show this help message\n");
    printf("\nNotes:\n");
    printf("  - Default: Video + both audio (MP4) unlimited time and 30 FPS\n");
    printf("  - Enabling only the audio options will continue MP4 recording\n");
    printf("  - Using any combination of --video, --system, and --microphone will record with the selected sources.\n");
}

// Note: Console callback functions are now in callbacks.c module

int main(int argc, char* argv[]) {
    // Initialize default parameters and parse arguments using modular components
    capture_params_t params;
    
    // Parse command line arguments using modular parser
    int parse_result = arguments_parse(argc, argv, &params);
    if (parse_result != 0) {
        return (parse_result == 1) ? 0 : 1; // 1 means help was shown (success)
    }
    
    // Initialize signal handling with emergency timeout
    signals_init(&g_engine);

    // Print startup information
    printf("Mux Sweeper (muxsw) - Starting capture...\n");
#ifdef MUXSW_ENABLE_AUDIO
    printf("Mode: %s\n", params.audio_only_mode ? "Audio-only (MP4)" : "Video + Audio (MP4)");
#else
    printf("Mode: Video-only (MP4) - MVP Build\n");
#endif
    printf("Output file: %s\n", params.output_filename);
    if (!params.audio_only_mode) {
        printf("FPS: %d\n", params.fps);
        printf("Monitor: %d\n", params.monitor_index);
        printf("Cursor: %s\n", params.cursor_enabled ? "Enabled" : "Disabled");
        if (params.region_enabled) {
            printf("Region: %d,%d %dx%d\n", params.region_x, params.region_y, params.region_w, params.region_h);
        } else {
            printf("Region: Full screen\n");
        }
    }
    
#ifdef MUXSW_ENABLE_AUDIO
    const char* audio_desc = "Disabled";
    if (params.audio_sources == AUDIO_SOURCE_SYSTEM) audio_desc = "System audio";
    else if (params.audio_sources == AUDIO_SOURCE_MICROPHONE) audio_desc = "Microphone";
    else if (params.audio_sources == AUDIO_SOURCE_BOTH) audio_desc = "System + Microphone";
    printf("Audio: %s\n", audio_desc);
#else
    printf("Audio: Disabled (MVP)\n");
#endif
    
    if (params.duration > 0) {
        printf("Duration: %d seconds\n", params.duration);
    } else {
        printf("Duration: Unlimited (press Ctrl+C to stop)\n");
    }
    printf("Press Ctrl+C to stop recording.\n\n");

    // Initialize capture engine and set modular callbacks
    if (engine_init(&g_engine) != 0) {
        fprintf(stderr, "Failed to initialize capture engine\n");
        signals_cleanup();
        return 1;
    }

    engine_set_status_callback(&g_engine, console_status_callback);
    engine_set_progress_callback(&g_engine, console_progress_callback);

    // Use modular recording function
    recording_result_t result;
    int recording_success = record_start(&g_engine, &params, &result);
    
    // Display results
    if (recording_success == 0 && result.success) {
        printf("\n=== Recording Summary ===\n");
        if (!params.audio_only_mode) {
            printf("Total frames: %d\n", result.stats.total_frames);
            printf("Failed frames: %d\n", result.stats.failed_frames);
        }
        printf("Duration: %.2f seconds\n", result.stats.recording_duration_ms / 1000.0f);
        if (result.stats.audio_enabled) {
            printf("Audio: %d Hz, %d channels, %d bits\n", 
                   result.stats.audio_sample_rate, result.stats.audio_channels, result.stats.audio_bits_per_sample);
        } else {
            printf("Audio: Not captured\n");
        }
        
        if (!params.audio_only_mode && result.stats.total_frames > 0) {
            float actual_fps = result.stats.total_frames * 1000.0f / result.stats.recording_duration_ms;
            printf("Average FPS: %.2f\n", actual_fps);
        }
        
        printf("Recording saved to: %s\n", params.output_filename);
        
        // Cleanup using modular components
        engine_cleanup(&g_engine);
        signals_cleanup();
        Sleep(50); // Allow cleanup to complete
        
        return 0;
    } else {
        fprintf(stderr, "Recording failed: %s\n", result.error_message);
        
        // Cleanup using modular components
        engine_cleanup(&g_engine);
        signals_cleanup();
        Sleep(50); // Allow cleanup to complete
        
        return 1;
    }
}
