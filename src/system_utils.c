#include "system_utils.h"
#include <windows.h>
#include <stdio.h>

// Hide console window for GUI applications
void system_hide_console(void) {
    FreeConsole();
    freopen_s((FILE**)stdout, "NUL", "w", stdout);
    freopen_s((FILE**)stderr, "NUL", "w", stderr);
    freopen_s((FILE**)stdin, "NUL", "r", stdin);
}
