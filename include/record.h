#ifndef RECORD_H
#define RECORD_H

#include "engine.h"

// Recording result structure
typedef struct {
    int success;
    char error_message[256];
    capture_stats_t stats;
} recording_result_t;

// Recording function that both CLI and GUI can use
int record_start(capture_engine_t* engine, const capture_params_t* params, recording_result_t* result);

// Recording cleanup function
void record_cleanup(capture_engine_t* engine);

#endif // RECORD_H
