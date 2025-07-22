#include "callbacks.h"
#include <stdio.h>

// Console status callback (for CLI)
void console_status_callback(const char* message) {
    printf("Status: %s\n", message);
}

// Console progress callback (for CLI)
void console_progress_callback(int frame_count, DWORD elapsed_ms) {
    if (frame_count > 0 && frame_count % 30 == 0) {
        float fps = frame_count * 1000.0f / elapsed_ms;
        printf("Progress: %d frames, %.1f seconds, %.1f FPS\n", 
               frame_count, elapsed_ms / 1000.0f, fps);
    }
}
