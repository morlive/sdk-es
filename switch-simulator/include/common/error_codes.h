/**
 * @file error_codes.h
 * @brief Error code definitions for switch simulator
 */

#ifndef SWITCH_SIM_ERROR_CODES_H
#define SWITCH_SIM_ERROR_CODES_H

#include "types.h"

typedef enum 
{
    COMPONENT_GENERAL = 0,     /**< General/system level */
    COMPONENT_HAL,             /**< Hardware Abstraction Layer */
    COMPONENT_BSP,             /**< Board Support Package */
    COMPONENT_L2,              /**< L2 functionality */
    COMPONENT_L3,              /**< L3 functionality */
    COMPONENT_SAI,             /**< Switch Abstraction Interface */
    COMPONENT_CLI,             /**< Command Line Interface */
    COMPONENT_DRIVER,          /**< Hardware drivers */
    COMPONENT_MAX              /**< Maximum component identifier */
} component_id_t;

enum 
{
    ERROR_NONE = 0,                /**< No error */
    ERROR_INVALID_PARAMETER,       /**< Invalid parameter */
    ERROR_RESOURCE_UNAVAILABLE,    /**< Resource not available */
    ERROR_TIMEOUT,                 /**< Operation timed out */
    ERROR_NOT_INITIALIZED,         /**< Module not initialized */
    ERROR_INSUFFICIENT_MEMORY,     /**< Not enough memory */
    ERROR_INTERNAL,                /**< Internal error */
    ERROR_NOT_SUPPORTED,           /**< Operation not supported */
    ERROR_INVALID_STATE,           /**< Invalid state for operation */
    ERROR_IO,                      /**< I/O error */
    ERROR_BUSY,                    /**< Resource busy */
    ERROR_OVERFLOW,                /**< Overflow condition */
    ERROR_UNDERFLOW               /**< Underflow condition */
};

#endif /* SWITCH_SIM_ERROR_CODES_H */

