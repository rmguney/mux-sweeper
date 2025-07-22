#ifndef CALLBACKS_H
#define CALLBACKS_H

#include <windows.h>

// Console callback functions (for CLI)
void console_status_callback(const char* message);
void console_progress_callback(int frame_count, DWORD elapsed_ms);

#endif // CALLBACKS_H
