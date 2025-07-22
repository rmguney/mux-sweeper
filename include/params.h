#ifndef PARAMS_H
#define PARAMS_H

#include "record.h"

// Function to set up default parameters
void params_init_defaults(capture_params_t* params);

// Function to validate and finalize parameters (common logic for CLI and GUI)
int params_validate_and_finalize(capture_params_t* params);

// Function to set recording mode based on individual flags
int params_set_recording_mode(capture_params_t* params, BOOL enable_video, BOOL enable_system, BOOL enable_mic);

// Function to auto-adjust filename extension based on mode
void params_adjust_filename_extension(capture_params_t* params);

#endif // PARAMS_H
