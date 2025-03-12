/**
 * @file logging.c
 * @brief Logging system implementation for switch simulator
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

#include "../../include/common/logging.h"

/* Private variables */
static FILE *g_log_file = NULL;
static log_level_t g_global_log_level = LOG_LEVEL_INFO;
static log_level_t g_category_levels[LOG_CATEGORY_COUNT];
static pthread_mutex_t g_log_mutex;
static bool g_initialized = false;

/* Private string constants for log levels and categories */
static const char *g_level_strings[] = {
    "FATAL",
    "ERROR",
    "WARNING",
    "INFO",
    "DEBUG",
    "TRACE"
};

static const char *g_category_strings[] = {
    "SYSTEM",
    "HAL",
    "BSP",
    "L2",
    "L3",
    "SAI",
    "CLI",
    "DRIVER",
    "TEST"
};

/**
 * @brief Initialize logging system
 * 
 * @param log_file Path to log file or NULL for console output
 * @return status_t STATUS_SUCCESS if successful
 */
status_t log_init(const char* log_file)
{
    if (g_initialized) {
        return STATUS_SUCCESS; /* Already initialized */
    }
    
    /* Initialize mutex */
    if (pthread_mutex_init(&g_log_mutex, NULL) != 0) {
        return STATUS_FAILURE;
    }
    
    /* Open log file if specified */
    if (log_file) {
        g_log_file = fopen(log_file, "a");
        if (!g_log_file) {
            pthread_mutex_destroy(&g_log_mutex);
            return STATUS_FAILURE;
        }
    }
    
    /* Set default log levels for all categories */
    for (int i = 0; i < LOG_CATEGORY_COUNT; i++) {
        g_category_levels[i] = g_global_log_level;
    }
    
    g_initialized = true;
    
    /* Log initialization message */
    LOG_INFO(LOG_CATEGORY_SYSTEM, "Logging system initialized (level: %s, output: %s)",
            log_level_to_string(g_global_log_level),
            log_file ? log_file : "console");
    
    return STATUS_SUCCESS;
}

/**
 * @brief Shut down logging system
 * 
 * @return status_t STATUS_SUCCESS if successful
 */
status_t log_shutdown(void)
{
    if (!g_initialized) {
        return STATUS_SUCCESS; /* Not initialized */
    }
    
    pthread_mutex_lock(&g_log_mutex);
    
    /* Close log file if open */
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    
    g_initialized = false;
    
    pthread_mutex_unlock(&g_log_mutex);
    pthread_mutex_destroy(&g_log_mutex);
    
    return STATUS_SUCCESS;
}

/**
 * @brief Set global log level
 * 
 * @param level New log level
 */
void log_set_level(log_level_t level)
{
    if (level > LOG_LEVEL_TRACE) {
        return;
    }
    
    pthread_mutex_lock(&g_log_mutex);
    g_global_log_level = level;
    pthread_mutex_unlock(&g_log_mutex);
    
    LOG_INFO(LOG_CATEGORY_SYSTEM, "Global log level changed to %s", 
            log_level_to_string(level));
}

/**
 * @brief Set log level for specific category
 * 
 * @param category Log category
 * @param level New log level for category
 */
void log_set_category_level(log_category_t category, log_level_t level)
{
    if (category >= LOG_CATEGORY_COUNT || level > LOG_LEVEL_TRACE) {
        return;
    }
    
    pthread_mutex_lock(&g_log_mutex);
    g_category_levels[category] = level;
    pthread_mutex_unlock(&g_log_mutex);
    
    LOG_INFO(LOG_CATEGORY_SYSTEM, "Log level for category %s changed to %s", 
            log_category_to_string(category), log_level_to_string(level));
}

/**
 * @brief Log a message
 * 
 * @param level Log level
 * @param category Log category
 * @param file Source file name
 * @param line Source line number
 * @param func Function name
 * @param format Format string
 * @param ... Format arguments
 */
void log_message(log_level_t level, log_category_t category, 
                const char* file, int line, const char* func,
                const char* format, ...)
{
    va_list args;
    va_start(args, format);
    log_message_v(level, category, file, line, func, format, args);
    va_end(args);
}

/**
 * @brief Log a message with va_list arguments
 * 
 * @param level Log level
 * @param category Log category
 * @param file Source file name
 * @param line Source line number
 * @param func Function name
 * @param format Format string
 * @param args Format arguments as va_list
 */
void log_message_v(log_level_t level, log_category_t category,
                  const char* file, int line, const char* func,
                  const char* format, va_list args)
{
    /* Check if initialized */
    if (!g_initialized) {
        /* Try to initialize with default settings */
        if (log_init(NULL) != STATUS_SUCCESS) {
            return;
        }
    }
    
    /* Check log level filtering */
    if (level > g_global_log_level && level > g_category_levels[category]) {
        return;
    }
    
    /* Get the filename without path */
    const char *filename = file;
    const char *last_slash = strrchr(file, '/');
    if (last_slash) {
        filename = last_slash + 1;
    }
    
    /* Get current timestamp */
    time_t now;
    time(&now);
    struct tm *timeinfo = localtime(&now);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);
    
    /* Prepare log message buffer */
    char message[1024];
    va_list args_copy;
    va_copy(args_copy, args);
    vsnprintf(message, sizeof(message), format, args_copy);
    va_end(args_copy);
    
    pthread_mutex_lock(&g_log_mutex);
    
    /* Format: timestamp | level | category | filename:line:function | message */
    if (g_log_file) {
        fprintf(g_log_file, "%s | %-7s | %-7s | %s:%d:%s | %s\n",
                timestamp, g_level_strings[level], g_category_strings[category],
                filename, line, func, message);
        fflush(g_log_file);
    } else {
        /* For console output, add colors based on log level */
        const char *color_code = "";
        const char *reset_code = "\033[0m";
        
        switch (level) {
            case LOG_LEVEL_FATAL:
                color_code = "\033[1;31m"; /* Bold Red */
                break;
            case LOG_LEVEL_ERROR:
                color_code = "\033[31m"; /* Red */
                break;
            case LOG_LEVEL_WARNING:
                color_code = "\033[33m"; /* Yellow */
                break;
            case LOG_LEVEL_INFO:
                color_code = "\033[32m"; /* Green */
                break;
            case LOG_LEVEL_DEBUG:
                color_code = "\033[36m"; /* Cyan */
                break;
            case LOG_LEVEL_TRACE:
                color_code = "\033[37m"; /* White */
                break;
        }
        
        fprintf(stdout, "%s%s | %-7s | %-7s | %s:%d:%s | %s%s\n",
                color_code, timestamp, g_level_strings[level], g_category_strings[category],
                filename, line, func, message, reset_code);
        fflush(stdout);
    }
    
    pthread_mutex_unlock(&g_log_mutex);
    
    /* For fatal errors, flush logs and potentially exit */
    if (level == LOG_LEVEL_FATAL) {
        log_shutdown();
        /* We could call exit(EXIT_FAILURE) here, but it's better to let the application decide */
    }
}

/**
 * @brief Get string representation of log level
 * 
 * @param level Log level
 * @return const char* String representation
 */
const char* log_level_to_string(log_level_t level)
{
    if (level <= LOG_LEVEL_TRACE) {
        return g_level_strings[level];
    }
    return "UNKNOWN";
}

/**
 * @brief Get string representation of log category
 * 
 * @param category Log category
 * @return const char* String representation
 */
const char* log_category_to_string(log_category_t category)
{
    if (category < LOG_CATEGORY_COUNT) {
        return g_category_strings[category];
    }
    return "UNKNOWN";
}
