/**
 * @file logging.c
 * @brief Implementation of the logging system
 */

#include "common/logging.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

/* Current log level */
static log_level_t current_log_level = LOG_LEVEL_INFO;

/* Log file pointer */
static FILE *log_file = NULL;

/* Log colors for terminal output */
static const char *log_colors[] = {
    [LOG_LEVEL_FATAL] = "\033[1;31m", /* Bright Red */
    [LOG_LEVEL_ERROR] = "\033[31m",   /* Red */
    [LOG_LEVEL_WARN]  = "\033[33m",   /* Yellow */
    [LOG_LEVEL_INFO]  = "\033[32m",   /* Green */
    [LOG_LEVEL_DEBUG] = "\033[36m",   /* Cyan */
    [LOG_LEVEL_TRACE] = "\033[35m"    /* Magenta */
};

static const char *log_level_names[] = {
    [LOG_LEVEL_FATAL] = "FATAL",
    [LOG_LEVEL_ERROR] = "ERROR",
    [LOG_LEVEL_WARN]  = "WARN ",
    [LOG_LEVEL_INFO]  = "INFO ",
    [LOG_LEVEL_DEBUG] = "DEBUG",
    [LOG_LEVEL_TRACE] = "TRACE"
};

error_code_t log_init(const char *log_file_path) {
    if (log_file != NULL) {
        fclose(log_file);
    }
    
    if (log_file_path != NULL) {
        log_file = fopen(log_file_path, "a");
        if (log_file == NULL) {
            fprintf(stderr, "Failed to open log file: %s\n", log_file_path);
            return ERROR_FILE_OPEN_FAILED;
        }
    }
    
    return ERROR_NONE;
}

void log_set_level(log_level_t level) {
    if (level >= LOG_LEVEL_FATAL && level <= LOG_LEVEL_TRACE) {
        current_log_level = level;
    }
}

log_level_t log_get_level(void) {
    return current_log_level;
}

void log_close(void) {
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
}

void log_message(log_level_t level, const char *module, const char *file, int line, const char *fmt, ...) {
    if (level > current_log_level) {
        return;
    }

    char timestamp[32];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    va_list args;
    va_start(args, fmt);
    
    /* Prepare log message buffer */
    char message[1024];
    vsnprintf(message, sizeof(message), fmt, args);
    
    /* Print to console with color */
    printf("%s[%s][%s][%s:%d] %s\033[0m\n", 
           log_colors[level], timestamp, log_level_names[level], 
           module, line, message);
    
    /* Write to log file if opened */
    if (log_file != NULL) {
        fprintf(log_file, "[%s][%s][%s:%d] %s\n", 
                timestamp, log_level_names[level], module, line, message);
        fflush(log_file);
    }
    
    va_end(args);
}
