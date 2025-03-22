/**
 * @file bsp_config.c
 * @brief Implementation of BSP configuration functions
 */

#include "../include/bsp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Default configurations for different board types
static const bsp_config_t default_configs[] = {
    // Generic board
    {
        .board_type = BSP_BOARD_TYPE_GENERIC,
        .num_ports = 8,
        .cpu_frequency_mhz = 800,
        .memory_size_mb = 512,
        .has_layer3_support = true,
        .has_qos_support = true,
        .has_acl_support = true,
        .board_name = "Generic Switch"
    },
    // Small board
    {
        .board_type = BSP_BOARD_TYPE_SMALL,
        .num_ports = 8,
        .cpu_frequency_mhz = 400,
        .memory_size_mb = 256,
        .has_layer3_support = false,
        .has_qos_support = true,
        .has_acl_support = false,
        .board_name = "Small Switch"
    },
    // Medium board
    {
        .board_type = BSP_BOARD_TYPE_MEDIUM,
        .num_ports = 24,
        .cpu_frequency_mhz = 800,
        .memory_size_mb = 512,
        .has_layer3_support = true,
        .has_qos_support = true,
        .has_acl_support = true,
        .board_name = "Medium Switch"
    },
    // Large board
    {
        .board_type = BSP_BOARD_TYPE_LARGE,
        .num_ports = 48,
        .cpu_frequency_mhz = 1200,
        .memory_size_mb = 1024,
        .has_layer3_support = true,
        .has_qos_support = true,
        .has_acl_support = true,
        .board_name = "Large Switch"
    },
    // Datacenter board
    {
        .board_type = BSP_BOARD_TYPE_DATACENTER,
        .num_ports = 64,
        .cpu_frequency_mhz = 2000,
        .memory_size_mb = 4096,
        .has_layer3_support = true,
        .has_qos_support = true,
        .has_acl_support = true,
        .board_name = "Datacenter Switch"
    }
};

// Current active configuration
static bsp_config_t active_config;
static bool is_config_initialized = false;

/**
 * @brief Get default configuration for a board type
 */
static bsp_error_t get_default_config(bsp_board_type_t board_type, bsp_config_t* config) {
    if (config == NULL) {
        return BSP_ERROR_INVALID_PARAM;
    }
    
    for (size_t i = 0; i < sizeof(default_configs) / sizeof(default_configs[0]); i++) {
        if (default_configs[i].board_type == board_type) {
            memcpy(config, &default_configs[i], sizeof(bsp_config_t));
            return BSP_SUCCESS;
        }
    }
    
    return BSP_ERROR_INVALID_PARAM;
}

/**
 * @brief Validate configuration parameters
 */
static bsp_error_t validate_config(const bsp_config_t* config) {
    if (config == NULL) {
        return BSP_ERROR_INVALID_PARAM;
    }
    
    // Check board type
    if (config->board_type < BSP_BOARD_TYPE_GENERIC || 
        config->board_type > BSP_BOARD_TYPE_DATACENTER) {
        return BSP_ERROR_INVALID_PARAM;
    }
    
    // Check port count
    if (config->num_ports == 0 || config->num_ports > 128) {
        return BSP_ERROR_INVALID_PARAM;
    }
    
    // Other validations as needed
    
    return BSP_SUCCESS;
}

/**
 * @brief Set the board configuration
 */
bsp_error_t bsp_set_config(const bsp_config_t* config) {
    bsp_error_t result = validate_config(config);
    if (result != BSP_SUCCESS) {
        return result;
    }
    
    // Apply the configuration
    memcpy(&active_config, config, sizeof(bsp_config_t));
    is_config_initialized = true;
    
    printf("BSP: Board configured as %s with %u ports\n", 
           active_config.board_name, active_config.num_ports);
    
    return BSP_SUCCESS;
}

/**
 * @brief Initialize default configuration based on board type
 */
bsp_error_t bsp_init_default_config(bsp_board_type_t board_type) {
    bsp_config_t config;
    bsp_error_t result = get_default_config(board_type, &config);
    if (result != BSP_SUCCESS) {
        return result;
    }
    
    return bsp_set_config(&config);
}

/**
 * @brief Get the current board configuration
 */
bsp_error_t bsp_get_config(bsp_config_t* config) {
    if (config == NULL) {
        return BSP_ERROR_INVALID_PARAM;
    }
    
    if (!is_config_initialized) {
        return BSP_ERROR_NOT_INITIALIZED;
    }
    
    memcpy(config, &active_config, sizeof(bsp_config_t));
    return BSP_SUCCESS;
}

/**
 * @brief Check if board configuration is initialized
 */
bool bsp_is_config_initialized(void) {
    return is_config_initialized;
}

/**
 * @brief Override specific configuration parameters
 */
bsp_error_t bsp_override_config_param(const char* param_name, const char* param_value) {
    if (!is_config_initialized) {
        return BSP_ERROR_NOT_INITIALIZED;
    }
    
    if (param_name == NULL || param_value == NULL) {
        return BSP_ERROR_INVALID_PARAM;
    }
    
    // Handle various parameter overrides
    if (strcmp(param_name, "num_ports") == 0) {
        int ports = atoi(param_value);
        if (ports <= 0 || ports > 128) {
            return BSP_ERROR_INVALID_PARAM;
        }
        active_config.num_ports = (uint32_t)ports;
    } else if (strcmp(param_name, "board_name") == 0) {
        // Allocate memory for the new name (this is simplified)
        // In a real implementation, handle memory properly
        active_config.board_name = strdup(param_value);
    } else if (strcmp(param_name, "has_layer3_support") == 0) {
        active_config.has_layer3_support = (strcmp(param_value, "true") == 0);
    } else if (strcmp(param_name, "has_qos_support") == 0) {
        active_config.has_qos_support = (strcmp(param_value, "true") == 0);
    } else if (strcmp(param_name, "has_acl_support") == 0) {
        active_config.has_acl_support = (strcmp(param_value, "true") == 0);
    } else if (strcmp(param_name, "cpu_frequency_mhz") == 0) {
        int freq = atoi(param_value);
        if (freq <= 0) {
            return BSP_ERROR_INVALID_PARAM;
        }
        active_config.cpu_frequency_mhz = (uint32_t)freq;
    } else if (strcmp(param_name, "memory_size_mb") == 0) {
        int mem = atoi(param_value);
        if (mem <= 0) {
            return BSP_ERROR_INVALID_PARAM;
        }
        active_config.memory_size_mb = (uint32_t)mem;
    } else {
        return BSP_ERROR_NOT_SUPPORTED;
    }
    
    return BSP_SUCCESS;
}
