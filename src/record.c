#include "record.h"
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>

int record_start(capture_engine_t* engine, const capture_params_t* params, recording_result_t* result) {
    if (!engine || !params || !result) {
        if (result) {
            result->success = 0;
            strcpy(result->error_message, "Invalid parameters");
        }
        return -1;
    }
    
    // Initialize result structure
    memset(result, 0, sizeof(recording_result_t));
    result->success = 0;
    
    // Initialize COM for this thread (required for Media Foundation)
    // Note: For GUI usage, COM should already be initialized in the recording thread
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    BOOL com_initialized_here = SUCCEEDED(hr);
    
    // If apartment threaded failed, try multithreaded mode (CLI scenario)
    if (!com_initialized_here) {
        hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        com_initialized_here = SUCCEEDED(hr);
    }
    
    // If COM is already initialized in compatible mode, that's fine
    if (!com_initialized_here && hr != RPC_E_CHANGED_MODE) {
        strcpy(result->error_message, "Failed to initialize COM");
        return -1;
    }
    
    // Only mark for cleanup if we actually initialized COM here
    BOOL should_cleanup_com = com_initialized_here;
    
    // Start capture (engine should already be initialized with callbacks)
    int capture_result = engine_start(engine, params);
    
    // Get final statistics
    const capture_stats_t* stats = engine_get_stats(engine);
    if (stats) {
        result->stats = *stats;
    }
    
    if (capture_result == 0) {
        result->success = 1;
        snprintf(result->error_message, sizeof(result->error_message), 
                "Recording completed: %d frames in %.2f seconds", 
                result->stats.total_frames,
                result->stats.recording_duration_ms / 1000.0f);
    } else {
        strcpy(result->error_message, "Recording failed during capture");
    }
    
    // DO NOT cleanup engine here - let the caller handle it to avoid double cleanup
    // The engine_start function already handles internal resource cleanup
    
    // Only uninitialize COM if we initialized it here
    if (should_cleanup_com) {
        CoUninitialize();
    }
    
    return capture_result;
}

void record_cleanup(capture_engine_t* engine) {
    if (engine) {
        engine_cleanup(engine);
    }
}
