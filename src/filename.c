#include "filename.h"
#include <time.h>
#include <stdio.h>

void filename_generate_timestamp(char* buffer, size_t buffer_size) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_s(&timeinfo, &now);
    
    snprintf(buffer, buffer_size, "%02d%02d%02d%02d%02d%02d.mp4",
            timeinfo.tm_year % 100,
            timeinfo.tm_mon + 1,
            timeinfo.tm_mday,
            timeinfo.tm_hour,
            timeinfo.tm_min,
            timeinfo.tm_sec);
}
