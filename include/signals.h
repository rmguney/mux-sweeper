#ifndef SIGNALS_H
#define SIGNALS_H

#include <windows.h>
#include "engine.h"

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P) (P)
#endif

// Signal handling functions
void signals_init(capture_engine_t* engine);
BOOL signals_shutdown_requested(void);
void signals_cleanup(void);

// Emergency timeout thread
DWORD WINAPI emergency_timeout_thread(LPVOID lpParam);

#endif // SIGNALS_H
