/**
 * @file logging.h
 * @brief Logging system for switch simulator
 */

#ifndef SWITCH_SIM_LOGGING_H
#define SWITCH_SIM_LOGGING_H

#include <stdint.h>
#include <stdarg.h>
#include "types.h"

/**
 * @brief Log levels
 */
typedef enum 
{
    LOG_LEVEL_FATAL = 0,   /**< Critical errors causing application termination */
    LOG_LEVEL_ERROR,       /**< Error events that might still allow application to continue */
    LOG_LEVEL_WARNING,     /**< Potentially harmful situations */
    LOG_LEVEL_INFO,        /**< Informational messages highlighting progress */
    LOG_LEVEL_DEBUG,       /**< Detailed debug information */
    LOG_LEVEL_TRACE        /**< Very detailed traces */
} log_level_t;

/**
 * @brief Log category identifiers
 */
typedef enum 
{
    LOG_CATEGORY_SYSTEM = 0,   /**< System-level messages */
    LOG_CATEGORY_HAL,          /**< Hardware Abstraction Layer */
    LOG_CATEGORY_BSP,          /**< Board Support Package */
    LOG_CATEGORY_L2,           /**< L2 functionality */
    LOG_CATEGORY_L3,           /**< L3 functionality */
    LOG_CATEGORY_SAI,          /**< Switch Abstraction Interface */
    LOG_CATEGORY_CLI,          /**< Command Line Interface */
    LOG_CATEGORY_DRIVER,       /**< Hardware drivers */
    LOG_CATEGORY_TEST,         /**< Testing infrastructure */
    LOG_CATEGORY_COUNT         /**< Number of log categories */
} log_category_t;

/**
 * @brief Initialize logging system
 * 
 * @param log_file Path to log file or NULL for console output
 * @return status_t STATUS_SUCCESS if successful
 */
status_t log_init(const char* log_file);

/**
 * @brief Shut down logging system
 * 
 * @return status_t STATUS_SUCCESS if successful
 */
status_t log_shutdown(void);

/**
 * @brief Set global log level
 * 
 * @param level New log level
 */
void log_set_level(log_level_t level);

/**
 * @brief Set log level for specific category
 * 
 * @param category Log category
 * @param level New log level for category
 */
void log_set_category_level(log_category_t category, log_level_t level);

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
                 const char* format, ...);

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
                  const char* format, va_list args);

/**
 * @brief Helper macros for logging
 */
#define LOG_FATAL(category, ...) \
    log_message(LOG_LEVEL_FATAL, category, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define LOG_ERROR(category, ...) \
    log_message(LOG_LEVEL_ERROR, category, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define LOG_WARNING(category, ...) \
    log_message(LOG_LEVEL_WARNING, category, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define LOG_INFO(category, ...) \
    log_message(LOG_LEVEL_INFO, category, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define LOG_DEBUG(category, ...) \
    log_message(LOG_LEVEL_DEBUG, category, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define LOG_TRACE(category, ...) \
    log_message(LOG_LEVEL_TRACE, category, __FILE__, __LINE__, __func__, __VA_ARGS__)

/**
 * @brief Get string representation of log level
 * 
 * @param level Log level
 * @return const char* String representation
 */
const char* log_level_to_string(log_level_t level);

/**
 * @brief Get string representation of log category
 * 
 * @param category Log category
 * @return const char* String representation
 */
const char* log_category_to_string(log_category_t category);

#endif /* SWITCH_SIM_LOGGING_H */
