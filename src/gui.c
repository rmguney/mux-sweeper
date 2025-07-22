#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <stdio.h>
#include <time.h>
#include "engine.h"
#include "record.h"
#include "params.h"
#include "filename.h"
#include "gui_callbacks.h"
#include "system_utils.h"

// Prevent console window from appearing in GUI mode (using modular system_utils)
// Note: system_hide_console() function is now in system_utils.c

// Window dimensions
#define WINDOW_WIDTH 400
#define WINDOW_HEIGHT 250

// Control IDs
#define ID_START_BUTTON         1001
#define ID_STOP_BUTTON          1002
#define ID_OUTPUT_EDIT          1003
#define ID_BROWSE_BUTTON        1004
#define ID_AUDIO_CHECKBOX       1005
#define ID_FPS_EDIT             1006
#define ID_DURATION_EDIT        1007
#define ID_STATUS_TEXT          1008
#define ID_PROGRESS_BAR         1009
#define ID_AUDIO_SYSTEM_RADIO   1010
#define ID_AUDIO_MIC_RADIO      1011
#define ID_AUDIO_BOTH_RADIO     1012
#define ID_TIMESTAMP_BUTTON     1013
#define ID_VIDEO_CHECKBOX       1014
#define ID_SYSTEM_CHECKBOX      1015
#define ID_MICROPHONE_CHECKBOX  1016

// Global variables
HWND g_hMainWindow = NULL;
HWND g_hStartButton = NULL;
HWND g_hStopButton = NULL;
HWND g_hOutputEdit = NULL;
HWND g_hBrowseButton = NULL;
HWND g_hFpsEdit = NULL;
HWND g_hDurationEdit = NULL;
HWND g_hStatusText = NULL;  // Made available to gui_callbacks.c
HWND g_hProgressBar = NULL;
HWND g_hVideoCheckbox = NULL;
HWND g_hSystemCheckbox = NULL;
HWND g_hMicrophoneCheckbox = NULL;

// Capture engine and thread
capture_engine_t g_engine = {0};
HANDLE g_recordingThread = NULL;
BOOL g_isRecording = FALSE;

// Thread parameter structure
typedef struct {
    capture_params_t params;
} thread_params_t;

// Function declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void CreateControls(HWND hwnd);
void OnStartRecording(void);
void OnStopRecording(void);
void OnBrowseOutputFile(HWND hwnd);
DWORD WINAPI RecordingThread(LPVOID lpParam);
void UpdateUI(BOOL isRecording);
void SetStatus(const char* message);

// Note: GUI callback functions are now in gui_callbacks.c module

// Main entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Suppress unused parameter warnings
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    
    system_hide_console();  // Use modular system utilities
    
    // No COM initialization here - let record_start() handle it consistently
    
    // Initialize Common Controls
    INITCOMMONCONTROLSEX icex = {0};
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);
    
    // Register window class
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "muxswMainWindow";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    
    if (!RegisterClass(&wc)) {
        MessageBox(NULL, "Failed to register window class", "Error", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
    
    // Create main window
    g_hMainWindow = CreateWindow(
        "muxswMainWindow",
        "Mux Sweeper - muxsw",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        NULL, NULL, hInstance, NULL
    );
    
    if (!g_hMainWindow) {
        MessageBox(NULL, "Failed to create window", "Error", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
    
    ShowWindow(g_hMainWindow, nCmdShow);
    UpdateWindow(g_hMainWindow);
    
    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // Cleanup
    engine_cleanup(&g_engine);
    CoUninitialize();
    return 0;
}

// Window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            CreateControls(hwnd);
            SetStatus("Ready to record");
            break;
            
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_START_BUTTON:
                    OnStartRecording();
                    break;
                case ID_STOP_BUTTON:
                    OnStopRecording();
                    break;
                case ID_BROWSE_BUTTON:
                    OnBrowseOutputFile(hwnd);
                    break;
                case ID_TIMESTAMP_BUTTON:
                    {
                        char new_filename[64];
                        filename_generate_timestamp(new_filename, sizeof(new_filename));  // Use modular function
                        SetWindowText(g_hOutputEdit, new_filename);
                    }
                    break;
                case ID_VIDEO_CHECKBOX:
                case ID_SYSTEM_CHECKBOX:
                case ID_MICROPHONE_CHECKBOX:
                    // Update filename extension based on mode selection using shared logic
                    {
                        BOOL video_enabled = (SendMessage(g_hVideoCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED);
                        BOOL system_enabled = (SendMessage(g_hSystemCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED);
                        BOOL mic_enabled = (SendMessage(g_hMicrophoneCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED);
                        
                        // Validate: at least one option must be selected
                        if (!video_enabled && !system_enabled && !mic_enabled) {
                            // Re-enable the one that was just unchecked
                            SendMessage((HWND)lParam, BM_SETCHECK, BST_CHECKED, 0);
                            MessageBox(hwnd, "At least one recording mode must be selected.", 
                                      "Invalid Selection", MB_OK | MB_ICONWARNING);
                            break;
                        }
                        
                        // Use shared logic to determine filename extension
                        capture_params_t temp_params;
                        params_init_defaults(&temp_params);
                        
                        // Get current filename
                        GetWindowText(g_hOutputEdit, temp_params.output_filename, MAX_PATH);
                        
                        // Set recording mode and auto-adjust extension
                        if (params_set_recording_mode(&temp_params, video_enabled, system_enabled, mic_enabled) == 0) {
                            SetWindowText(g_hOutputEdit, temp_params.output_filename);
                        }
                    }
                    break;
            }
            break;
            
        case WM_CLOSE:
            if (g_isRecording) {
                if (MessageBox(hwnd, "Recording is in progress. Stop recording before closing?", 
                              "muxsw", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    OnStopRecording();
                    PostQuitMessage(0);
                }
            } else {
                PostQuitMessage(0);
            }
            break;
            
        case WM_USER + 1:  // Status message from recording thread
            SetStatus((const char*)lParam);
            break;
            
        case WM_USER + 2:  // Recording finished notification
            // Clean up the recording thread
            if (g_recordingThread) {
                CloseHandle(g_recordingThread);
                g_recordingThread = NULL;
            }
            
            g_isRecording = FALSE;
            UpdateUI(FALSE);
            SetStatus("Ready");
            break;
            
        case WM_DESTROY:
            // Ensure recording is stopped before closing
            if (g_isRecording) {
                engine_stop(&g_engine);
                if (g_recordingThread) {
                    WaitForSingleObject(g_recordingThread, 3000);
                    CloseHandle(g_recordingThread);
                    g_recordingThread = NULL;
                }
            }
            PostQuitMessage(0);
            break;
            
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

// Create UI controls
void CreateControls(HWND hwnd) {
    // Generate default timestamp filename using modular function
    char default_filename[64];
    filename_generate_timestamp(default_filename, sizeof(default_filename));

    // Output file section
    CreateWindow("STATIC", "Output:",
                WS_VISIBLE | WS_CHILD,
                15, 15, 50, 18,
                hwnd, NULL, GetModuleHandle(NULL), NULL);

    g_hOutputEdit = CreateWindow("EDIT", default_filename,
                                WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                                70, 13, 260, 22,
                                hwnd, (HMENU)ID_OUTPUT_EDIT, GetModuleHandle(NULL), NULL);

    g_hBrowseButton = CreateWindow("BUTTON", "...",
                                  WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                  340, 13, 25, 22,
                                  hwnd, (HMENU)ID_BROWSE_BUTTON, GetModuleHandle(NULL), NULL);

    // FPS and Duration section
    CreateWindow("STATIC", "FPS:",
                WS_VISIBLE | WS_CHILD,
                15, 45, 30, 18,
                hwnd, NULL, GetModuleHandle(NULL), NULL);

    g_hFpsEdit = CreateWindow("EDIT", "30",
                             WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER,
                             50, 43, 45, 22,
                             hwnd, (HMENU)ID_FPS_EDIT, GetModuleHandle(NULL), NULL);

    CreateWindow("STATIC", "Duration:",
                WS_VISIBLE | WS_CHILD,
                110, 45, 60, 18,
                hwnd, NULL, GetModuleHandle(NULL), NULL);

    g_hDurationEdit = CreateWindow("EDIT", "0",
                                  WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER,
                                  175, 43, 45, 22,
                                  hwnd, (HMENU)ID_DURATION_EDIT, GetModuleHandle(NULL), NULL);

    CreateWindow("STATIC", "sec (0=unlimited)",
                WS_VISIBLE | WS_CHILD,
                230, 45, 100, 18,
                hwnd, NULL, GetModuleHandle(NULL), NULL);

    // Recording modes section
    CreateWindow("STATIC", "Record:",
                WS_VISIBLE | WS_CHILD,
                15, 75, 50, 18,
                hwnd, NULL, GetModuleHandle(NULL), NULL);

    g_hVideoCheckbox = CreateWindow("BUTTON", "Video",
                                   WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                                   70, 75, 60, 22,
                                   hwnd, (HMENU)ID_VIDEO_CHECKBOX, GetModuleHandle(NULL), NULL);
    SendMessage(g_hVideoCheckbox, BM_SETCHECK, BST_CHECKED, 0); // Default checked

    g_hSystemCheckbox = CreateWindow("BUTTON", "System",
                                    WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                                    140, 75, 70, 22,
                                    hwnd, (HMENU)ID_SYSTEM_CHECKBOX, GetModuleHandle(NULL), NULL);
    SendMessage(g_hSystemCheckbox, BM_SETCHECK, BST_CHECKED, 0); // Default checked

    g_hMicrophoneCheckbox = CreateWindow("BUTTON", "Mic",
                                        WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                                        220, 75, 50, 22,
                                        hwnd, (HMENU)ID_MICROPHONE_CHECKBOX, GetModuleHandle(NULL), NULL);
    SendMessage(g_hMicrophoneCheckbox, BM_SETCHECK, BST_CHECKED, 0); // Default checked

    // Control buttons
    g_hStartButton = CreateWindow("BUTTON", "Start Recording",
                                 WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                 15, 110, 110, 35,
                                 hwnd, (HMENU)ID_START_BUTTON, GetModuleHandle(NULL), NULL);

    g_hStopButton = CreateWindow("BUTTON", "Stop Recording",
                                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                135, 110, 110, 35,
                                hwnd, (HMENU)ID_STOP_BUTTON, GetModuleHandle(NULL), NULL);
    EnableWindow(g_hStopButton, FALSE);

    // Status and progress
    CreateWindow("STATIC", "Status:",
                WS_VISIBLE | WS_CHILD,
                15, 160, 45, 18,
                hwnd, NULL, GetModuleHandle(NULL), NULL);

    g_hStatusText = CreateWindow("STATIC", "Ready to record",
                                WS_VISIBLE | WS_CHILD | SS_LEFTNOWORDWRAP,
                                65, 160, WINDOW_WIDTH - 85, 18,
                                hwnd, (HMENU)ID_STATUS_TEXT, GetModuleHandle(NULL), NULL);

    g_hProgressBar = CreateWindow(PROGRESS_CLASS, NULL,
                                 WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
                                 15, 185, WINDOW_WIDTH - 35, 18,
                                 hwnd, (HMENU)ID_PROGRESS_BAR, GetModuleHandle(NULL), NULL);
}

// Start recording
void OnStartRecording(void) {
    if (g_isRecording) return;

    thread_params_t* params = malloc(sizeof(thread_params_t));
    if (!params) {
        SetStatus("Error: Failed to allocate memory");
        return;
    }

    // Initialize parameters with defaults using shared logic
    params_init_defaults(&params->params);

    // Get parameters from UI
    GetWindowText(g_hOutputEdit, params->params.output_filename, MAX_PATH);
    
    // Auto-generate new timestamp if field is empty or looks like default timestamp
    if (strlen(params->params.output_filename) == 0 || 
        (strlen(params->params.output_filename) == 16 && // YYMMDDHHMMSS.mp4 = 16 chars
         strstr(params->params.output_filename, ".mp4") != NULL &&
         strspn(params->params.output_filename, "0123456789") == 12)) { // First 12 chars are digits
        
        filename_generate_timestamp(params->params.output_filename, MAX_PATH);  // Use modular function
        SetWindowText(g_hOutputEdit, params->params.output_filename);
    }
    
    char fpsText[16];
    GetWindowText(g_hFpsEdit, fpsText, sizeof(fpsText));
    params->params.fps = atoi(fpsText);
    if (params->params.fps <= 0) params->params.fps = 30;

    char durationText[16];
    GetWindowText(g_hDurationEdit, durationText, sizeof(durationText));
    params->params.duration = atoi(durationText);

    // Get recording mode selection from checkboxes
    BOOL video_enabled = (SendMessage(g_hVideoCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED);
    BOOL system_enabled = (SendMessage(g_hSystemCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED);
    BOOL mic_enabled = (SendMessage(g_hMicrophoneCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED);
    
    // Use shared logic to set recording mode and validate
    if (params_set_recording_mode(&params->params, video_enabled, system_enabled, mic_enabled) != 0) {
        MessageBox(g_hMainWindow, "Please select at least one recording mode (Video, System, or Microphone).", 
                  "No Recording Mode Selected", MB_OK | MB_ICONWARNING);
        free(params);
        return;
    }

    // Apply final validation using shared logic
    if (params_validate_and_finalize(&params->params) != 0) {
        MessageBox(g_hMainWindow, "Invalid parameter configuration.", 
                  "Configuration Error", MB_OK | MB_ICONERROR);
        free(params);
        return;
    }

    // Update UI with corrected filename (extension may have changed)
    SetWindowText(g_hOutputEdit, params->params.output_filename);

    params->params.force_stop = FALSE;

    // Validate output file
    if (strlen(params->params.output_filename) == 0) {
        strcpy(params->params.output_filename, "capture.mp4");
        SetWindowText(g_hOutputEdit, params->params.output_filename);
    }

    // Start recording thread
    g_recordingThread = CreateThread(NULL, 0, RecordingThread, params, 0, NULL);
    if (!g_recordingThread) {
        free(params);
        SetStatus("Error: Failed to create recording thread");
        return;
    }

    g_isRecording = TRUE;
    UpdateUI(TRUE);
}

// Stop recording
void OnStopRecording(void) {
    if (!g_isRecording) return;

    SetStatus("Stopping recording...");
    
    // Signal the engine to stop
    engine_stop(&g_engine);
    
    // Don't wait synchronously - let the recording thread finish and notify us
    // The recording thread will send WM_USER + 2 when it's done
}

// Browse for output file
void OnBrowseOutputFile(HWND hwnd) {
    OPENFILENAME ofn = {0};
    char szFile[MAX_PATH] = {0};

    GetWindowText(g_hOutputEdit, szFile, MAX_PATH);

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "MP4 Files\0*.mp4\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (GetSaveFileName(&ofn)) {
        SetWindowText(g_hOutputEdit, szFile);
    }
}

// Recording thread function
DWORD WINAPI RecordingThread(LPVOID lpParam) {
    thread_params_t* params = (thread_params_t*)lpParam;
    
    // No COM initialization here - let record_start() handle it consistently
    
    // Initialize capture engine and set callbacks
    if (engine_init(&g_engine) != 0) {
        free(params);
        PostMessage(g_hMainWindow, WM_USER + 1, 0, (LPARAM)"Failed to initialize capture engine");
        PostMessage(g_hMainWindow, WM_USER + 2, 0, 0);
        return (DWORD)-1;
    }
    
    // Set GUI callbacks
    engine_set_status_callback(&g_engine, gui_status_callback);
    engine_set_progress_callback(&g_engine, gui_progress_callback);
    
    // Use recording function
    recording_result_t result;
    int recording_success = record_start(&g_engine, &params->params, &result);
    
    // Clean up parameters
    free(params);
    
    // Update UI on completion (must be posted to main thread)
    if (recording_success == 0 && result.success) {
        char success_msg[512];
        snprintf(success_msg, sizeof(success_msg), 
                "Recording completed: %d frames in %.2f seconds", 
                result.stats.total_frames,
                result.stats.recording_duration_ms / 1000.0f);
        PostMessage(g_hMainWindow, WM_USER + 1, 0, (LPARAM)success_msg);
    } else {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Recording failed: %s", result.error_message);
        PostMessage(g_hMainWindow, WM_USER + 1, 0, (LPARAM)error_msg);
    }
    
    // No COM cleanup here - record_start() handles it consistently
    
    // Signal that recording is finished
    PostMessage(g_hMainWindow, WM_USER + 2, 0, 0);
    
    return (DWORD)recording_success;
}

// Update UI state
void UpdateUI(BOOL isRecording) {
    EnableWindow(g_hStartButton, !isRecording);
    EnableWindow(g_hStopButton, isRecording);
    EnableWindow(g_hOutputEdit, !isRecording);
    EnableWindow(g_hBrowseButton, !isRecording);
    EnableWindow(g_hFpsEdit, !isRecording);
    EnableWindow(g_hDurationEdit, !isRecording);
    EnableWindow(g_hVideoCheckbox, !isRecording);
    EnableWindow(g_hSystemCheckbox, !isRecording);
    EnableWindow(g_hMicrophoneCheckbox, !isRecording);
}

// Set status text
void SetStatus(const char* message) {
    SetWindowText(g_hStatusText, message);
}
