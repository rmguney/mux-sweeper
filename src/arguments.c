#include "arguments.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void arguments_print_usage(const char* program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  -o, --out <file>       Output filename (default: yymmddhhmmss.mp4)\n");
    printf("  -t, --time <seconds>   Recording duration in seconds (default: unlimited)\n");
    printf("  -v, --video            Enable video capture\n");
#ifdef MUXSW_ENABLE_AUDIO
    printf("  -s, --system           Enable system audio capture\n");
    printf("  -m, --microphone       Enable microphone capture\n");
#else
    printf("  -s, --system           Enable system audio capture (Disabled - MVP)\n");
    printf("  -m, --microphone       Enable microphone capture (Disabled - MVP)\n");
#endif
    printf("  --fps <rate>           Frame rate (default: 30)\n");
    printf("  --monitor <index>      Monitor index to capture (default: 0)\n");
    printf("  --cursor [on|off]      Include cursor in capture (default: on)\n");
    printf("  --region x y w h       Capture specific region (default: full screen)\n");
    printf("  -h, --help             Show this help message\n");
    printf("Notes:\n");
#ifdef MUXSW_ENABLE_AUDIO
    printf("  - Default: Video + both audio (MP4) unlimited time and 30 FPS\n");
    printf("  - Enabling only the audio options will continue MP4 recording\n");
    printf("  - Using any combination of --video, --system, and --microphone will record with the selected sources.\n");
#else
    printf("  - MVP: Video capture only (MP4) unlimited time and 30 FPS\n");
    printf("  - Audio capture is disabled in this MVP build\n");
#endif
}

int arguments_parse(int argc, char* argv[], capture_params_t* params) {
    if (!params) return -1;
    
    // Initialize defaults
    params_init_defaults(params);
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            arguments_print_usage(argv[0]);
            return 1; // Special return code for help
        }
        else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--out") == 0) {
            if (i + 1 < argc) {
                strncpy(params->output_filename, argv[++i], sizeof(params->output_filename) - 1);
                params->output_filename[sizeof(params->output_filename) - 1] = '\0';
            } else {
                fprintf(stderr, "Error: %s requires a filename\n", argv[i]);
                return -1;
            }
        }
        else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--time") == 0) {
            if (i + 1 < argc) {
                params->duration = atoi(argv[++i]);
                if (params->duration <= 0) {
                    fprintf(stderr, "Error: Duration must be positive\n");
                    return -1;
                }
            } else {
                fprintf(stderr, "Error: %s requires a duration in seconds\n", argv[i]);
                return -1;
            }
        }
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--video") == 0) {
            params->enable_video = TRUE;
        }
        else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--system") == 0) {
#ifdef MUXSW_ENABLE_AUDIO
            params->enable_system_audio = TRUE;
#else
            fprintf(stderr, "Warning: System audio capture is disabled in MVP build\n");
#endif
        }
        else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--microphone") == 0) {
#ifdef MUXSW_ENABLE_AUDIO
            params->enable_microphone = TRUE;
#else
            fprintf(stderr, "Warning: Microphone capture is disabled in MVP build\n");
#endif
        }
        else if (strcmp(argv[i], "--fps") == 0) {
            if (i + 1 < argc) {
                params->fps = atoi(argv[++i]);
                if (params->fps <= 0 || params->fps > 120) {
                    fprintf(stderr, "Error: FPS must be between 1 and 120\n");
                    return -1;
                }
            } else {
                fprintf(stderr, "Error: --fps requires a frame rate\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--monitor") == 0) {
            if (i + 1 < argc) {
                params->monitor_index = atoi(argv[++i]);
                if (params->monitor_index < 0) {
                    fprintf(stderr, "Error: Monitor index must be >= 0\n");
                    return -1;
                }
            } else {
                fprintf(stderr, "Error: --monitor requires an index\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--cursor") == 0) {
            if (i + 1 < argc) {
                const char* cursor_opt = argv[++i];
                if (strcmp(cursor_opt, "on") == 0) {
                    params->cursor_enabled = TRUE;
                } else if (strcmp(cursor_opt, "off") == 0) {
                    params->cursor_enabled = FALSE;
                } else {
                    fprintf(stderr, "Error: --cursor must be 'on' or 'off'\n");
                    return -1;
                }
            } else {
                fprintf(stderr, "Error: --cursor requires 'on' or 'off'\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--region") == 0) {
            if (i + 4 < argc) {
                params->region_x = atoi(argv[++i]);
                params->region_y = atoi(argv[++i]);
                params->region_w = atoi(argv[++i]);
                params->region_h = atoi(argv[++i]);
                if (params->region_w <= 0 || params->region_h <= 0) {
                    fprintf(stderr, "Error: Region width and height must be positive\n");
                    return -1;
                }
                params->region_enabled = TRUE;
            } else {
                fprintf(stderr, "Error: --region requires x y width height\n");
                return -1;
            }
        }
        else {
            fprintf(stderr, "Error: Unknown argument '%s'\n", argv[i]);
            arguments_print_usage(argv[0]);
            return -1;
        }
    }
    
    // If no specific modes were enabled, use defaults (all enabled)
    if (!params->enable_video && !params->enable_system_audio && !params->enable_microphone) {
#ifdef MUXSW_ENABLE_AUDIO
        params->enable_video = TRUE;
        params->enable_system_audio = TRUE;
        params->enable_microphone = TRUE;
#else
        // MVP: Default to video only
        params->enable_video = TRUE;
        params->enable_system_audio = FALSE;
        params->enable_microphone = FALSE;
#endif
    }
    
    // Apply shared logic for final setup
    BOOL video_enabled = params->enable_video;
    BOOL system_enabled = params->enable_system_audio;
    BOOL mic_enabled = params->enable_microphone;
    
    if (params_set_recording_mode(params, video_enabled, system_enabled, mic_enabled) != 0) {
        fprintf(stderr, "Error: Invalid recording mode configuration\n");
        return -1;
    }
    
    if (params_validate_and_finalize(params) != 0) {
        fprintf(stderr, "Error: Parameter validation failed\n");
        return -1;
    }
    
    return 0;
}
