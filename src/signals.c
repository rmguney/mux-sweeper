#include "signals.h"
#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

// Global state for signal handling
static capture_engine_t* g_signal_engine = NULL;
static volatile BOOL g_shutdown_requested = FALSE;

// Signal handler for graceful shutdown
void signal_handler(int sig) {
    printf("Received signal %d, stopping capture...\n", sig);
    g_shutdown_requested = TRUE;
    if (g_signal_engine) {
        engine_stop(g_signal_engine);
    }
    
    // Emergency termination after 5 seconds
    Sleep(5000);
    if (g_signal_engine && engine_is_running(g_signal_engine)) {
        printf("EMERGENCY: Force terminating due to timeout\n");
        exit(1);
    }
}

// Windows console control handler for Ctrl+C
BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    switch (ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            printf("Console control event %lu, stopping capture...\n", ctrl_type);
            g_shutdown_requested = TRUE;
            if (g_signal_engine) {
                engine_stop(g_signal_engine);
            }
            return TRUE;
        default:
            return FALSE;
    }
}

// Emergency timeout thread function
DWORD WINAPI emergency_timeout_thread(LPVOID lpParam) {
    UNREFERENCED_PARAMETER(lpParam);
    
    Sleep(5 * 60 * 1000);
    
    if (!g_shutdown_requested && g_signal_engine && engine_is_running(g_signal_engine)) {
        printf("EMERGENCY TIMEOUT: Force terminating after 5 minutes\n");
        if (g_signal_engine) {
            engine_stop(g_signal_engine);
        }
        Sleep(2000);
        if (g_signal_engine && engine_is_running(g_signal_engine)) {
            printf("CRITICAL: Emergency exit due to unresponsive engine\n");
            exit(2);
        }
    }
    
    return 0;
}

void signals_init(capture_engine_t* engine) {
    g_signal_engine = engine;
    g_shutdown_requested = FALSE;
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
    
    // Start emergency timeout thread
    HANDLE timer_thread = CreateThread(NULL, 0, emergency_timeout_thread, NULL, 0, NULL);
    if (timer_thread) {
        CloseHandle(timer_thread);
    }
}

BOOL signals_shutdown_requested(void) {
    return g_shutdown_requested;
}

void signals_cleanup(void) {
    g_signal_engine = NULL;
    g_shutdown_requested = FALSE;
}
