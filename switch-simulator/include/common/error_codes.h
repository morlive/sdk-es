/**
 * @file error_codes.h
 * @brief Error code definitions for switch simulator
 */

#ifndef SWITCH_SIM_ERROR_CODES_H
#define SWITCH_SIM_ERROR_CODES_H

#include "types.h"

/**
 * @brief Component identifiers for error code generation
 */
typedef enum {
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

/**
 * @brief Generate error code based on component and specific error
 * 
 * Format: [31:24] - Reserved
 *         [23:16] - Component ID
 *         [15:0]  - Error code
 *
 * @param component Component identifier
 * @param error Specific error code
 * @return uint32_t Combined error code
 */
#define MAKE_ERROR_CODE(component, error) \
    ((uint32_t)(((component) & 0xFF) << 16) | ((error) & 0xFFFF))

/**
 * @brief Extract component ID from error code
 * 
 * @param error_code Combined error code
 * @return component_id_t Component identifier
 */
#define GET_ERROR_COMPONENT(error_code) \
    ((component_id_t)(((error_code) >> 16) & 0xFF))

/**
 * @brief Extract specific error from error code
 * 
 * @param error_code Combined error code
 * @return uint16_t Specific error code
 */
#define GET_ERROR_CODE(error_code) \
    ((uint16_t)((error_code) & 0xFFFF))

/**
 * @brief Convert error code to human-readable string
 * 
 * @param error_code Error code to convert
 * @return const char* Human readable string
 */
const char* error_to_string(uint32_t error_code);

/**
 * @brief Common error codes for all components
 */
enum {
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
