/**
 * @file port.c
 * @brief Port management implementation for switch simulator
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../include/hal/port.h"
#include "../include/hal/hw_resources.h"
#include "../include/common/logging.h"

// Port information array
static port_info_t *ports = NULL;
static uint32_t port_count = 0;
static bool ports_initialized = false;

status_t port_init(void) {
    if (ports_initialized) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Port subsystem already initialized");
        return STATUS_SUCCESS;
    }

    LOG_INFO(LOG_CATEGORY_HAL, "Initializing port subsystem");
    
    // Get hardware capabilities to determine max ports
    hw_capabilities_t capabilities;
    status_t status = hw_resources_get_capabilities(&capabilities);
    if (status != STATUS_SUCCESS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to get hardware capabilities");
        return status;
    }
    
    port_count = capabilities.max_ports;
    
    // Allocate port information array
    ports = (port_info_t *)calloc(port_count, sizeof(port_info_t));
    if (ports == NULL) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to allocate memory for port information");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Initialize default port configuration
    for (uint32_t i = 0; i < port_count; i++) {
        port_info_t *port = &ports[i];
        
        port->id = i + 1;  // Port IDs typically start from 1
        port->type = PORT_TYPE_PHYSICAL;
        
        // Generate port name
        snprintf(port->name, sizeof(port->name), "Port%u", port->id);
        
        // Default configuration
        port->config.admin_state = false;  // Disabled by default
        port->config.speed = PORT_SPEED_1G;
        port->config.duplex = PORT_DUPLEX_FULL;
        port->config.auto_neg = true;
        port->config.flow_control = false;
        port->config.mtu = 1500;
        port->config.pvid = 1;  // Default VLAN
        
        // Operational state
        port->state = PORT_STATE_DOWN;
        
        // Generate a unique MAC address for each port
        // Use a simple scheme: 00:00:00:00:00:xx where xx is the port ID
        memset(port->mac_addr.addr, 0, sizeof(port->mac_addr.addr));
        port->mac_addr.addr[5] = port->id & 0xFF;
        
        // Initialize statistics to zero (already done by calloc)
    }
    
    // Reserve ports in hardware resources
    status = hw_resources_reserve(HW_RESOURCE_PORT, port_count);
    if (status != STATUS_SUCCESS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to reserve port resources");
        free(ports);
        ports = NULL;
        return status;
    }
    
    ports_initialized = true;
    LOG_INFO(LOG_CATEGORY_HAL, "Port subsystem initialized with %u ports", port_count);
    return STATUS_SUCCESS;
}

status_t port_shutdown(void) {
    if (!ports_initialized) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Port subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    LOG_INFO(LOG_CATEGORY_HAL, "Shutting down port subsystem");
    
    // Release hardware resources
    hw_resources_release(HW_RESOURCE_PORT, port_count);
    
    // Free allocated memory
    free(ports);
    ports = NULL;
    port_count = 0;
    ports_initialized = false;
    
    LOG_INFO(LOG_CATEGORY_HAL, "Port subsystem shut down successfully");
    return STATUS_SUCCESS;
}

status_t port_get_info(port_id_t port_id, port_info_t *info) {
    if (!ports_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Port subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (info == NULL) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid parameter: info is NULL");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Check port ID validity
    if (port_id == 0 || port_id > port_count) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid port ID: %u", port_id);
        return STATUS_INVALID_PARAMETER;
    }
    
    // Port IDs start from 1, but array indices from 0
    *info = ports[port_id - 1];
    return STATUS_SUCCESS;
}

status_t port_set_config(port_id_t port_id, const port_config_t *config) {
    if (!ports_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Port subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (config == NULL) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid parameter: config is NULL");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Check port ID validity
    if (port_id == 0 || port_id > port_count) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid port ID: %u", port_id);
        return STATUS_INVALID_PARAMETER;
    }
    
    // Port IDs start from 1, but array indices from 0
    port_info_t *port = &ports[port_id - 1];
    
    // Update configuration
    port->config = *config;
    
    // Update port state based on admin state
    if (port->config.admin_state) {
        port->state = PORT_STATE_UP;
    } else {
        port->state = PORT_STATE_DOWN;
    }
    
    LOG_INFO(LOG_CATEGORY_HAL, "Updated configuration for port %u (%s)", 
            port_id, port->name);
    return STATUS_SUCCESS;
}

status_t port_set_admin_state(port_id_t port_id, bool admin_up) {
    if (!ports_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Port subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    // Check port ID validity
    if (port_id == 0 || port_id > port_count) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid port ID: %u", port_id);
        return STATUS_INVALID_PARAMETER;
    }
    
    // Port IDs start from 1, but array indices from 0
    port_info_t *port = &ports[port_id - 1];
    
    // Update admin state
    port->config.admin_state = admin_up;
    
    // Update port state based on admin state
    if (admin_up) {
        port->state = PORT_STATE_UP;
        LOG_INFO(LOG_CATEGORY_HAL, "Port %u (%s) set to administratively UP", 
                port_id, port->name);
    } else {
        port->state = PORT_STATE_DOWN;
        LOG_INFO(LOG_CATEGORY_HAL, "Port %u (%s) set to administratively DOWN", 
                port_id, port->name);
    }
    
    return STATUS_SUCCESS;
}

status_t port_get_stats(port_id_t port_id, port_stats_t *stats) {
    if (!ports_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Port subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (stats == NULL) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid parameter: stats is NULL");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Check port ID validity
    if (port_id == 0 || port_id > port_count) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid port ID: %u", port_id);
        return STATUS_INVALID_PARAMETER;
    }
    
    // Port IDs start from 1, but array indices from 0
    *stats = ports[port_id - 1].stats;
    return STATUS_SUCCESS;
}

status_t port_clear_stats(port_id_t port_id) {
    if (!ports_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Port subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    // Check port ID validity
    if (port_id == 0 || port_id > port_count) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid port ID: %u", port_id);
        return STATUS_INVALID_PARAMETER;
    }
    
    // Port IDs start from 1, but array indices from 0
    memset(&ports[port_id - 1].stats, 0, sizeof(port_stats_t));
    
    LOG_INFO(LOG_CATEGORY_HAL, "Cleared statistics for port %u", port_id);
    return STATUS_SUCCESS;
}

status_t port_get_count(uint32_t *count) {
    if (!ports_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Port subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (count == NULL) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid parameter: count is NULL");
        return STATUS_INVALID_PARAMETER;
    }
    
    *count = port_count;
    return STATUS_SUCCESS;
}

status_t port_get_list(port_id_t *port_ids, uint32_t *count) {
    if (!ports_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Port subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (port_ids == NULL || count == NULL) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid parameters");
        return STATUS_INVALID_PARAMETER;
    }
    
    uint32_t max_count = *count;
    *count = 0;
    
    for (uint32_t i = 0; i < port_count && *count < max_count; i++) {
        port_ids[(*count)++] = ports[i].id;
    }
    
    return STATUS_SUCCESS;
}
