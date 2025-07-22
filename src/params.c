#include "params.h"
#include <string.h>
#include <stdio.h>

void params_init_defaults(capture_params_t* params) {
    if (!params) return;
    
    memset(params, 0, sizeof(capture_params_t));
    strcpy(params->output_filename, "capture.mp4");
    params->fps = 30;
    params->duration = 0; // unlimited by default
    params->force_stop = FALSE;
    params->enable_video = FALSE;
    params->enable_system_audio = FALSE;
    params->enable_microphone = FALSE;
    params->audio_only_mode = FALSE;
    params->audio_sources = AUDIO_SOURCE_NONE;
    // MVP: Video capture defaults
    params->monitor_index = 0;
    params->cursor_enabled = TRUE;
    params->region_enabled = FALSE;
    params->region_x = 0;
    params->region_y = 0;
    params->region_w = 0;
    params->region_h = 0;
}

int params_validate_and_finalize(capture_params_t* params) {
    if (!params) return -1;
    
    // Validate FPS
    if (params->fps <= 0 || params->fps > 120) {
        params->fps = 30; // Default to 30 FPS
    }
    
    // Ensure at least one recording mode is enabled
    if (!params->enable_video && !params->enable_system_audio && !params->enable_microphone) {
#ifdef MUXSW_ENABLE_AUDIO
        // Default to video + both audio if nothing specified
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
    
#ifndef MUXSW_ENABLE_AUDIO
    // MVP: Force disable audio in MVP builds
    params->enable_system_audio = FALSE;
    params->enable_microphone = FALSE;
#endif
    
    // Set up audio sources based on enabled options
    params->audio_sources = AUDIO_SOURCE_NONE;
    if (params->enable_system_audio && params->enable_microphone) {
        params->audio_sources = AUDIO_SOURCE_BOTH;
    } else if (params->enable_system_audio) {
        params->audio_sources = AUDIO_SOURCE_SYSTEM;
    } else if (params->enable_microphone) {
        params->audio_sources = AUDIO_SOURCE_MICROPHONE;
    }
    
    // Determine if this is audio-only mode
    params->audio_only_mode = !params->enable_video && (params->enable_system_audio || params->enable_microphone);
    
    // Auto-adjust filename extension
    params_adjust_filename_extension(params);
    
    return 0;
}

int params_set_recording_mode(capture_params_t* params, BOOL enable_video, BOOL enable_system, BOOL enable_mic) {
    if (!params) return -1;
    
    // Validate: at least one option must be selected
    if (!enable_video && !enable_system && !enable_mic) {
        return -1; // Invalid: no recording mode selected
    }
    
    params->enable_video = enable_video;
    params->enable_system_audio = enable_system;
    params->enable_microphone = enable_mic;
    
    // Update dependent fields
    return params_validate_and_finalize(params);
}

void params_adjust_filename_extension(capture_params_t* params) {
    if (!params || !params->output_filename) return;
    
    char* ext = strrchr(params->output_filename, '.');
    const char* target_ext = ".mp4";
    
    // Check if extension needs to be changed
    if (!ext || _stricmp(ext, target_ext) != 0) {
        // Remove existing extension if present
        if (ext) *ext = '\0';
        
        // Add correct extension
        strcat(params->output_filename, target_ext);
    }
}
