#include "gui_callbacks.h"
#include <windows.h>
#include <stdio.h>

// External references to GUI controls (will be set by GUI)
extern HWND g_hStatusText;

// GUI status callback - updates status text control
void gui_status_callback(const char* message) {
    if (g_hStatusText) {
        SetWindowText(g_hStatusText, message);
    }
}

// GUI progress callback - updates status with frame count and time
void gui_progress_callback(int frame_count, DWORD elapsed_ms) {
    if (g_hStatusText) {
        char progress_text[256];
        sprintf(progress_text, "Recording: %d frames, %.1f seconds", 
                frame_count, elapsed_ms / 1000.0f);
        SetWindowText(g_hStatusText, progress_text);
    }
}
