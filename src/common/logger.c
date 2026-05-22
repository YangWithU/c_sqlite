#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static log_level_t current_level = LOG_INFO;

static const char* level_strings[] = {
    "DEBUG", "INFO", "WARN", "ERROR"
};

void logger_set_level(log_level_t level) {
    current_level = level;
}

void logger_log(log_level_t level, const char* file, int line,
                const char* fmt, ...) {
    if (level < current_level)
        return;

    const char* basename = strrchr(file, '/');
    basename = basename ? basename + 1 : file;

    fprintf(stderr, "[%s] %s:%d: ", level_strings[level], basename, line);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
}
