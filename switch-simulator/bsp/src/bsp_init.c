/**
 * @file bsp_init.c
 * @brief Implementation of BSP initialization and management functions
 */
#include "../include/bsp.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// External declarations for functions defined in other BSP files
extern bsp_error_t bsp_set_config(const bsp_config_t* config);
extern bsp_error_t bsp_init_port_statuses(uint32_t port_count);
extern void bsp_cleanup_resources(void);
extern bool bsp_is_config_initialized(void);

// BSP initialization status
static bool bsp_initialized = false;

/**
 * @brief Initialize the Board Support Package
 */
bsp_error_t bsp_init(const bsp_config_t* config) {
    if (bsp_initialized) {
        printf("BSP: Already initialized, deinitializing first\n");
        bsp_deinit();
    }
    
    if (config == NULL) {
        return BSP_ERROR_INVALID_PARAM;
    }
    
    // Set the configuration
    bsp_error_t result = bsp_set_config(config);
    if (result != BSP_SUCCESS) {
        return result;
    }
    
    // Initialize port statuses
    result = bsp_init_port_statuses(config->num_ports);
    if (result != BSP_SUCCESS) {
        return result;
    }
    
    // Initialize ports
    for (uint32_t i = 0; i < config->num_ports; i++) {
        // Default to 1G full-duplex
        result = bsp_port_init(i, BSP_PORT_SPEED_1G, BSP_PORT_DUPLEX_FULL);
        if (result != BSP_SUCCESS) {
            bsp_deinit();
            return result;
        }
    }
    
    printf("BSP: Initialized %s with %u ports\n", 
           config->board_name, config->num_ports);
    
    bsp_initialized = true;
    
    return BSP_SUCCESS;
}

/**
 * @brief Deinitialize the Board Support Package and release resources
 */
bsp_error_t bsp_deinit(void) {
    if (!bsp_initialized) {
        return BSP_ERROR_NOT_INITIALIZED;
    }
    
    // Clean up all resources
    bsp_cleanup_resources();
    
    // Reset initialization flag
    bsp_initialized = false;
    
    printf("BSP: Deinitialized successfully\n");
    
    return BSP_SUCCESS;
}

/**
 * @brief Check if the BSP is currently initialized
 * 
 * @return true if BSP is initialized, false otherwise
 */
bool bsp_is_initialized(void) {
    return bsp_initialized;
}

/**
 * @brief Get the current BSP configuration
 * 
 * @param[out] config Pointer to store the configuration
 * @return bsp_error_t BSP_SUCCESS if successful, error code otherwise
 */
bsp_error_t bsp_get_config(bsp_config_t* config) {
    if (!bsp_initialized) {
        return BSP_ERROR_NOT_INITIALIZED;
    }
    
    if (config == NULL) {
        return BSP_ERROR_INVALID_PARAM;
    }
    
    if (!bsp_is_config_initialized()) {
        return BSP_ERROR_INTERNAL;
    }
    
    // Copy the configuration (implementation in bsp_config.c)
    extern bsp_error_t bsp_copy_config(bsp_config_t* dest);
    return bsp_copy_config(config);
}

/**
 * @brief Get the version of the BSP
 * 
 * @return const char* String containing the BSP version
 */
const char* bsp_get_version(void) {
    return BSP_VERSION_STRING;
}

/**
 * @brief Get the current status of the BSP
 * 
 * @param[out] status Pointer to store the status information
 * @return bsp_error_t BSP_SUCCESS if successful, error code otherwise
 */
bsp_error_t bsp_get_status(bsp_status_t* status) {
    if (status == NULL) {
        return BSP_ERROR_INVALID_PARAM;
    }
    
    status->initialized = bsp_initialized;
    
    if (!bsp_initialized) {
        status->port_count = 0;
        status->active_ports = 0;
        return BSP_SUCCESS;
    }
    
    // Get port count from configuration
    extern uint32_t bsp_get_port_count(void);
    status->port_count = bsp_get_port_count();
    
    // Count active ports
    status->active_ports = 0;
    for (uint32_t i = 0; i < status->port_count; i++) {
        bsp_port_status_t port_status;
        if (bsp_port_get_status(i, &port_status) == BSP_SUCCESS) {
            if (port_status.state == BSP_PORT_STATE_UP) {
                status->active_ports++;
            }
        }
    }
    
    return BSP_SUCCESS;
}

/**
 * @brief Reset the BSP to its default state
 * 
 * @return bsp_error_t BSP_SUCCESS if successful, error code otherwise
 */
bsp_error_t bsp_reset(void) {
    if (!bsp_initialized) {
        return BSP_ERROR_NOT_INITIALIZED;
    }
    
    printf("BSP: Performing reset...\n");
    
    // Get current configuration before deinitializing
    bsp_config_t current_config;
    bsp_error_t result = bsp_get_config(&current_config);
    if (result != BSP_SUCCESS) {
        return result;
    }
    
    // Deinitialize first
    result = bsp_deinit();
    if (result != BSP_SUCCESS) {
        return result;
    }
    
    // Reinitialize with the same configuration
    return bsp_init(&current_config);
}
