/**
 * @file logging.h
 * @brief Logging system for switch simulator
 */

#ifndef SWITCH_SIM_LOGGING_H
#define SWITCH_SIM_LOGGING_H

#include <stdint.h>
#include <stdarg.h>
#include "types.h"

typedef enum 
{
  LOG_LEVEL_FATAL = 0,   /**< Critical errors causing application termination */
  LOG_LEVEL_ERROR,       /**< Error events that might still allow application to continue */
  LOG_LEVEL_WARNING,     /**< Potentially harmful situations */
  LOG_LEVEL_INFO,        /**< Informational messages highlighting progress */
  LOG_LEVEL_DEBUG,       /**< Detailed debug information */
  LOG_LEVEL_TRACE        /**< Very detailed traces */
} log_level_t;

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

status_t log_init(const char* log_file);
status_t log_shutdown(void);

void log_set_level(log_level_t level);
void log_set_category_level(log_category_t category, log_level_t level);

void log_message(log_level_t level, log_category_t category, 
                 const char* file, int line, const char* func,
                 const char* format, ...);

void log_message_v(log_level_t level, log_category_t category,
                   const char* file, int line, const char* func,
                   const char* format, va_list args);


#endif /* SWITCH_SIM_LOGGING_H */
