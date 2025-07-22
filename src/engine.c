#include "engine.h"
#include "screen.h"
#include "microphone.h"
#include "system.h"
#include "encoder.h"
#include <stdio.h>
#include <string.h>

// Internal contexts - completely isolated for modular recording
static screen_capture_t screen_ctx = {0};
static microphone_context_t microphone_ctx = {0};
static system_context_t system_ctx = {0};
static encoder_context_t encoder_ctx = {0};

// Default status callback (prints to console)
static void default_status_callback(const char* message) {
    printf("%s\n", message);
}

// Default progress callback (prints to console)
static void default_progress_callback(int frame_count, DWORD elapsed_ms) {
    if (frame_count % 30 == 0) {
        printf("Captured %d frames in %lu ms\n", frame_count, elapsed_ms);
    }
}

int engine_init(capture_engine_t* engine) {
    if (!engine) return -1;
    
    memset(engine, 0, sizeof(capture_engine_t));
    engine->status_callback = default_status_callback;
    engine->progress_callback = default_progress_callback;
    
    return 0;
}

void engine_set_status_callback(capture_engine_t* engine, capture_status_callback_t callback) {
    if (engine) {
        engine->status_callback = callback ? callback : default_status_callback;
    }
}

void engine_set_progress_callback(capture_engine_t* engine, capture_progress_callback_t callback) {
    if (engine) {
        engine->progress_callback = callback ? callback : default_progress_callback;
    }
}

BOOL engine_is_running(const capture_engine_t* engine) {
    return engine ? engine->is_running : FALSE;
}

const capture_stats_t* engine_get_stats(const capture_engine_t* engine) {
    return engine ? &engine->stats : NULL;
}

int engine_start(capture_engine_t* engine, const capture_params_t* params) {
    if (!engine || !params || engine->is_running) return -1;
    
    // Copy parameters
    engine->params = *params;
    engine->force_stop = FALSE;
    memset(&engine->stats, 0, sizeof(capture_stats_t));
    
    engine->status_callback("Initializing capture...");
    
    // Initialize screen capture (skip for audio-only mode)
    if (!params->audio_only_mode) {
        if (screen_init(&screen_ctx) != 0) {
            engine->status_callback("Error: Failed to initialize screen capture");
            return -1;
        }
    }
    
    // Initialize audio capture if enabled - use modular approach
    BOOL use_microphone = (params->audio_sources == AUDIO_SOURCE_MICROPHONE || params->audio_sources == AUDIO_SOURCE_BOTH);
    BOOL use_system = (params->audio_sources == AUDIO_SOURCE_SYSTEM || params->audio_sources == AUDIO_SOURCE_BOTH);
    // Disable dual-track mode for audio-only recordings to fix container timescale issues
    BOOL use_dual_track = (params->audio_sources == AUDIO_SOURCE_BOTH) && !params->audio_only_mode;
    
#ifndef MUXSW_ENABLE_AUDIO
    // MVP: Force disable all audio functionality
    use_microphone = FALSE;
    use_system = FALSE;
    use_dual_track = FALSE;
    if (params->audio_only_mode) {
        engine->status_callback("Error: Audio-only mode not supported in MVP build");
        return -1;
    }
#endif
    
    int microphone_result = -1;
    int system_result = -1;
    
    // Initialize microphone if needed
    if (use_microphone) {
#ifdef MUXSW_ENABLE_AUDIO
        microphone_result = microphone_init(&microphone_ctx);
        if (microphone_result == 0) {
            engine->stats.audio_sample_rate = microphone_ctx.wave_format->nSamplesPerSec;
            engine->stats.audio_channels = microphone_ctx.wave_format->nChannels;
            engine->stats.audio_bits_per_sample = microphone_ctx.wave_format->wBitsPerSample;
            engine->status_callback("Microphone initialized successfully");
        } else {
            engine->status_callback("Warning: Failed to initialize microphone");
        }
#else
        engine->status_callback("Warning: Microphone disabled in MVP build");
        microphone_result = -1;
#endif
    }
    
    // Initialize system audio if needed  
    if (use_system) {
#ifdef MUXSW_ENABLE_AUDIO
        system_result = system_init(&system_ctx);
        if (system_result == 0) {
            // Use system audio format if microphone wasn't initialized
            if (!use_microphone || microphone_result != 0) {
                engine->stats.audio_sample_rate = system_ctx.wave_format->nSamplesPerSec;
                engine->stats.audio_channels = system_ctx.wave_format->nChannels;
                engine->stats.audio_bits_per_sample = system_ctx.wave_format->wBitsPerSample;
            }
            engine->status_callback("System audio initialized successfully");
        } else {
            engine->status_callback("Warning: Failed to initialize system audio");
        }
#else
        engine->status_callback("Warning: System audio disabled in MVP build");
        system_result = -1;
#endif
    }
    
    // Check if any audio source is available
    BOOL audio_available = (use_microphone && microphone_result == 0) || (use_system && system_result == 0);
    
    if ((use_microphone || use_system) && !audio_available) {
        if (params->audio_only_mode) {
            engine->status_callback("Error: Audio-only mode requires working audio capture");
            goto cleanup;
        } else {
            engine->status_callback("Warning: No audio sources available, continuing with video-only");
        }
    }
    
    engine->stats.audio_enabled = audio_available;
    
    // Test audio availability if enabled
    if (audio_available && !params->audio_only_mode) {
        engine->status_callback("Testing audio capture availability...");
        
        // Start audio capture for testing
        BOOL test_success = FALSE;
        
        if (use_microphone && microphone_result == 0) {
            if (microphone_start_capture(&microphone_ctx) == 0) {
                // Test microphone data
                BYTE* test_data = NULL;
                UINT32 test_frames = 0;
                for (int attempt = 0; attempt < 5 && !test_success; attempt++) {
                    Sleep(100);
                    if (microphone_get_buffer(&microphone_ctx, &test_data, &test_frames) == 0 && test_frames > 0) {
                        microphone_release_buffer(&microphone_ctx, test_frames);
                        test_success = TRUE;
                        engine->status_callback("Microphone test successful");
                    }
                }
                microphone_stop_capture(&microphone_ctx);
            }
        }
        
        if (use_system && system_result == 0) {
            if (system_start_capture(&system_ctx) == 0) {
                // Test system audio data (more lenient since audio might not be playing)
                BYTE* test_data = NULL;
                UINT32 test_frames = 0;
                for (int attempt = 0; attempt < 3 && !test_success; attempt++) {
                    Sleep(100);
                    if (system_get_buffer(&system_ctx, &test_data, &test_frames) == 0 && test_frames > 0) {
                        system_release_buffer(&system_ctx, test_frames);
                        test_success = TRUE;
                        engine->status_callback("System audio test successful");
                    }
                }
                // For system audio, if no audio is detected but capture works, assume it's okay
                if (!test_success) {
                    engine->status_callback("System audio capture ready (no audio currently playing)");
                    test_success = TRUE; // Allow system audio even if silent
                }
                system_stop_capture(&system_ctx);
            }
        }
        
        if (!test_success) {
            engine->status_callback("Warning: No audio data detected, continuing with video-only");
            audio_available = FALSE;
            engine->stats.audio_enabled = FALSE;
        }
    } else if (audio_available && params->audio_only_mode) {
        // For audio-only mode, skip test phase and start capture directly
        engine->status_callback("Audio-only mode: starting audio capture directly");
        
        if (use_microphone && microphone_result == 0) {
            if (microphone_start_capture(&microphone_ctx) != 0) {
                engine->status_callback("Error: Failed to start microphone capture for audio-only mode");
                audio_available = FALSE;
            }
        }
        
        if (use_system && system_result == 0) {
            if (system_start_capture(&system_ctx) != 0) {
                engine->status_callback("Error: Failed to start system audio capture for audio-only mode");
                audio_available = FALSE;
            }
        }
        
        if (!audio_available) {
            engine->status_callback("Error: Audio-only mode requires working audio capture");
            goto cleanup;
        }
    }
    
    // Initialize encoder
    // CRITICAL FIX: For video-only mode, pass 0 audio parameters to prevent audio stream creation
    int sample_rate = engine->stats.audio_enabled ? engine->stats.audio_sample_rate : 0;
    int channels = engine->stats.audio_enabled ? engine->stats.audio_channels : 0;
    int bits_per_sample = engine->stats.audio_enabled ? engine->stats.audio_bits_per_sample : 0;
    
    int encoder_result = -1;
    if (params->audio_only_mode) {
        if (use_dual_track && audio_available) {
            // Dual-track audio mode for audio-only recording
            encoder_result = encoder_init_audio_only_dual_track(&encoder_ctx, params->output_filename, sample_rate, channels, bits_per_sample);
            engine->status_callback("Initialized audio-only dual-track encoder (system + mic as separate tracks)");
        } else {
            // Single-track audio-only recording
            encoder_result = encoder_init_audio_only(&encoder_ctx, params->output_filename, sample_rate, channels, bits_per_sample);
            engine->status_callback("Initialized audio-only encoder (MP4 output)");
        }
    } else {
        if (use_dual_track && audio_available) {
            // Dual-track mode for video + audio recording
            encoder_result = encoder_init_dual_track(&encoder_ctx, params->output_filename, screen_ctx.width, screen_ctx.height, 
                                 params->fps, sample_rate, channels, bits_per_sample);
            engine->status_callback("Initialized dual-track encoder (video + system audio + microphone)");
        } else {
            // Single-track or no audio recording
            encoder_result = encoder_init(&encoder_ctx, params->output_filename, screen_ctx.width, screen_ctx.height, 
                                 params->fps, sample_rate, channels, bits_per_sample);
        }
    }
    
    if (encoder_result != 0) {
        engine->status_callback("Error: Failed to initialize encoder");
        goto cleanup;
    }
    
    // Start screen capture (skip for audio-only mode)
    if (!params->audio_only_mode) {
        if (screen_start_capture(&screen_ctx) != 0) {
            engine->status_callback("Error: Failed to start screen capture");
            goto cleanup;
        }
    }
    
    // Start/restart audio capture to sync with encoder timestamps
    // This ensures consistent audio handling for both audio-only and video+audio modes
    if (audio_available) {
        // For audio-only mode, audio is already started; for video+audio, restart after test
        if (!params->audio_only_mode) {
            // Restart audio capture fresh for recording
            if (use_microphone && microphone_result == 0) {
                if (microphone_start_capture(&microphone_ctx) != 0) {
                    engine->status_callback("Warning: Failed to restart microphone capture");
                    use_microphone = FALSE;
                }
            }
            
            if (use_system && system_result == 0) {
                if (system_start_capture(&system_ctx) != 0) {
                    engine->status_callback("Warning: Failed to restart system audio capture");
                    use_system = FALSE;
                }
            }
            
            // Update audio availability
            audio_available = (use_microphone && microphone_result == 0) || (use_system && system_result == 0);
            engine->stats.audio_enabled = audio_available;
        }
    }
    
    // Synchronize recording start time
    DWORD start_time = GetTickCount();
    encoder_set_recording_start_time(start_time);
    
    engine->is_running = TRUE;
    char status_msg[256];
    sprintf(status_msg, "Recording started: %s (%s)", params->output_filename, 
            audio_available ? "with audio" : "video only");
    engine->status_callback(status_msg);
    
    // Main capture loop
    DWORD frame_interval = 1000 / params->fps;
    DWORD next_frame_time = start_time;
    int frame_count = 0;
    int failed_frame_attempts = 0;
    int consecutive_audio_failures = 0; // Track audio failures
    
    // CRITICAL MEMORY LEAK PROTECTION: Emergency termination counters
    DWORD emergency_check_interval = 1000; // Check every 1 second
    DWORD next_emergency_check = start_time + emergency_check_interval;
    int loop_iterations = 0;
    const int MAX_LOOP_ITERATIONS_PER_SECOND = 2000; // Safety limit
    
    while (engine->is_running && !engine->force_stop && !params->force_stop) {
        DWORD current_time = GetTickCount();
        loop_iterations++;
        
        // EMERGENCY TERMINATION: Prevent runaway processes
        if (current_time >= next_emergency_check) {
            if (loop_iterations > MAX_LOOP_ITERATIONS_PER_SECOND) {
                engine->status_callback("EMERGENCY: Loop frequency too high, terminating to prevent memory leak");
                break;
            }
            
            // Reset counters for next interval
            loop_iterations = 0;
            next_emergency_check = current_time + emergency_check_interval;
            
            // Additional safety: terminate if running too long without duration limit
            if (params->duration == 0 && (current_time - start_time) > (60 * 1000)) {
                engine->status_callback("EMERGENCY: Unlimited recording running over 60 seconds, auto-terminating");
                break;
            }
        }
        
        // Check duration limit
        if (params->duration > 0 && (current_time - start_time) >= (DWORD)(params->duration * 1000)) {
            break;
        }
        
        // Capture frame at specified FPS (skip in audio-only mode)
        if (!params->audio_only_mode && current_time >= next_frame_time) {
            void* frame_data = NULL;
            size_t frame_size = 0;
            
            // Use dual-track aware frame capture to fix video flipping issue
            int frame_result = screen_get_frame_dual_track(&screen_ctx, &frame_data, &frame_size, encoder_ctx.dual_track_mode);
            if (frame_result == 0 && frame_data) {
                encoder_add_video_frame(&encoder_ctx, frame_data, frame_size, current_time - start_time);
                free(frame_data);
                frame_count++;
                
                // Update progress
                engine->progress_callback(frame_count, current_time - start_time);
            } else {
                failed_frame_attempts++;
            }
            
            next_frame_time += frame_interval;
        }
        
        // Capture audio if enabled (run continuously, not tied to video FPS)
        if (audio_available) {
            BOOL audio_success = FALSE;
            
            if (use_dual_track && use_microphone && use_system) {
                // Dual-track mode: capture system and microphone separately
                BYTE* system_data = NULL;
                BYTE* mic_data = NULL;
                UINT32 system_frames = 0;
                UINT32 mic_frames = 0;
                
                // Get system audio buffer
                if (system_result == 0) {
                    int sys_result = system_get_buffer(&system_ctx, &system_data, &system_frames);
                    if (sys_result == 0 && system_frames > 0) {
                        encoder_add_system_audio_frame(&encoder_ctx, system_data, system_frames, current_time - start_time);
                        system_release_buffer(&system_ctx, system_frames);
                        audio_success = TRUE;
                    }
                }
                
                // Get microphone buffer  
                if (microphone_result == 0) {
                    int mic_result = microphone_get_buffer(&microphone_ctx, &mic_data, &mic_frames);
                    if (mic_result == 0 && mic_frames > 0) {
                        encoder_add_mic_audio_frame(&encoder_ctx, mic_data, mic_frames, current_time - start_time);
                        microphone_release_buffer(&microphone_ctx, mic_frames);
                        audio_success = TRUE;
                    }
                }
            } else {
                // Single audio source mode
                if (use_microphone && microphone_result == 0) {
                    BYTE* audio_data = NULL;
                    UINT32 num_frames = 0;
                    int mic_result = microphone_get_buffer(&microphone_ctx, &audio_data, &num_frames);
                    if (mic_result == 0 && num_frames > 0) {
                        encoder_add_audio_frame(&encoder_ctx, audio_data, num_frames, current_time - start_time);
                        microphone_release_buffer(&microphone_ctx, num_frames);
                        audio_success = TRUE;
                    }
                }
                
                if (use_system && system_result == 0) {
                    BYTE* audio_data = NULL;
                    UINT32 num_frames = 0;
                    int sys_result = system_get_buffer(&system_ctx, &audio_data, &num_frames);
                    if (sys_result == 0 && num_frames > 0) {
                        encoder_add_audio_frame(&encoder_ctx, audio_data, num_frames, current_time - start_time);
                        system_release_buffer(&system_ctx, num_frames);
                        audio_success = TRUE;
                    }
                }
            }
            
            // Track audio failures for audio-only mode
            if (audio_success) {
                consecutive_audio_failures = 0;
            } else {
                consecutive_audio_failures++;
                
                // For audio-only mode, if we have too many consecutive failures, abort
                if (params->audio_only_mode && consecutive_audio_failures > 1000) {
                    engine->status_callback("Error: Too many audio capture failures in audio-only mode, stopping recording");
                    break;
                }
            }
        }
        
        // Sleep management: prevent memory leak by controlling loop frequency
        DWORD time_until_next_frame = (next_frame_time > current_time) ? (next_frame_time - current_time) : 0;
        if (audio_available) {
            // CRITICAL FIX: Balanced sleep for audio capture with time-based silent generation
            // Audio modules now handle timing internally, so we can use moderate polling frequency
            Sleep(5);  // Balanced sleep for audio capture - works with time-based silent generation
        } else {
            // When no audio, use normal sleep to reduce CPU usage
            if (time_until_next_frame > 5) {
                Sleep(5);  // Standard sleep when no audio
            } else if (time_until_next_frame > 1) {
                Sleep(time_until_next_frame - 1);
            } else {
                Sleep(3);  // Increased minimum sleep to 3ms
            }
        }
    }
    
    // Update final statistics
    engine->stats.total_frames = frame_count;
    engine->stats.failed_frames = failed_frame_attempts;
    engine->stats.recording_duration_ms = GetTickCount() - start_time;
    
    engine->status_callback("Stopping capture...");
    
    // Stop captures
    if (!params->audio_only_mode) {
        screen_stop_capture(&screen_ctx);
    }
    if (audio_available) {
        if (use_microphone && microphone_result == 0) {
            microphone_stop_capture(&microphone_ctx);
        }
        if (use_system && system_result == 0) {
            system_stop_capture(&system_ctx);
        }
    }
    
    engine->status_callback("Finalizing recording...");
    encoder_finalize(&encoder_ctx);
    
    if (params->audio_only_mode) {
        sprintf(status_msg, "Audio recording completed: %lu ms", 
                GetTickCount() - start_time);
    } else {
        sprintf(status_msg, "Recording completed: %d frames, %lu ms", 
                frame_count, GetTickCount() - start_time);
    }
    engine->status_callback(status_msg);
    
    engine->is_running = FALSE;
    return 0;
    
cleanup:
    engine->is_running = FALSE;
    
    // CRITICAL MEMORY LEAK FIX: Ensure all resources are properly cleaned up
    if (!params->audio_only_mode) {
        screen_stop_capture(&screen_ctx);
        screen_cleanup(&screen_ctx);
    }
    
    if (use_microphone && microphone_result == 0) {
        microphone_stop_capture(&microphone_ctx);
        microphone_cleanup(&microphone_ctx);
    }
    
    if (use_system && system_result == 0) {
        system_stop_capture(&system_ctx);
        system_cleanup(&system_ctx);
    }
    
    encoder_cleanup(&encoder_ctx);
    
    // Force garbage collection
    Sleep(100);
    
    return -1;
}

int engine_stop(capture_engine_t* engine) {
    if (!engine || !engine->is_running) return -1;
    
    engine->force_stop = TRUE;
    engine->status_callback("Stopping and encoding, please wait...");
    
    // CRITICAL: More aggressive termination - shorter timeout
    for (int i = 0; i < 20 && engine->is_running; i++) {
        Sleep(50);  // Reduced from 10ms to 50ms, fewer iterations
    }
    
    // EMERGENCY: Force stop if still running
    if (engine->is_running) {
        engine->status_callback("EMERGENCY: Force stopping unresponsive engine");
        engine->is_running = FALSE;
    }
    
    return 0;
}

void engine_cleanup(capture_engine_t* engine) {
    if (!engine) return;
    
    if (engine->is_running) {
        engine_stop(engine);
    }
    
    // Cleanup contexts (these functions already check for NULL/invalid contexts)
    // The individual cleanup functions are designed to be idempotent
    screen_cleanup(&screen_ctx);
    microphone_cleanup(&microphone_ctx);
    system_cleanup(&system_ctx);
    encoder_cleanup(&encoder_ctx);
    
    // CRITICAL: Reset static contexts to prevent any carryover state
    memset(&screen_ctx, 0, sizeof(screen_ctx));
    memset(&microphone_ctx, 0, sizeof(microphone_ctx));
    memset(&system_ctx, 0, sizeof(system_ctx));
    memset(&encoder_ctx, 0, sizeof(encoder_ctx));
    
    memset(engine, 0, sizeof(capture_engine_t));
}
