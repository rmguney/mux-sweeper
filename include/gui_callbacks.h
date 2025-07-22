#ifndef GUI_CALLBACKS_H
#define GUI_CALLBACKS_H

#include <windows.h>

// GUI callback functions (for GUI interface)
void gui_status_callback(const char* message);
void gui_progress_callback(int frame_count, DWORD elapsed_ms);

#endif // GUI_CALLBACKS_H
