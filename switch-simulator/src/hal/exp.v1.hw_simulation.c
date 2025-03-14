/**
 * @file hw_simulation.c
 * @brief Hardware simulation implementation for switch simulator
 */
#include <stdlib.h>
#include <string.h>
#include "../include/hal/hw_resources.h"
#include "../include/common/logging.h"

// Simulated hardware resources
typedef struct {
    hw_capabilities_t capabilities;
    hw_resource_usage_t resources[8]; // Использую размер на основе enum hw_resource_type_t
    bool initialized;
} hw_simulation_t;

// Global simulation state
static hw_simulation_t hw_sim = {0};

/**
 * @brief Initialize hardware simulation with default values
 */
static void hw_simulation_set_defaults(void) {
    // Set default capabilities
    hw_sim.capabilities.l2_switching = true;
    hw_sim.capabilities.l3_routing = true;
    hw_sim.capabilities.vlan_filtering = true;
    hw_sim.capabilities.qos = true;
    hw_sim.capabilities.acl = true;
    hw_sim.capabilities.link_aggregation = true;
    hw_sim.capabilities.jumbo_frames = true;
    hw_sim.capabilities.ipv6 = true;
    hw_sim.capabilities.multicast = true;
    hw_sim.capabilities.mirroring = true;
    hw_sim.capabilities.max_ports = MAX_PORTS;
    hw_sim.capabilities.max_vlans = MAX_VLANS;
    hw_sim.capabilities.max_mac_entries = MAX_MAC_TABLE_ENTRIES;
    hw_sim.capabilities.max_routes = 16384; // Example value

    // Set default resource usage
    for (int i = 0; i < HW_RESOURCE_QUEUE + 1; i++) {
        switch (i) {
            case HW_RESOURCE_PORT:
                hw_sim.resources[i].total = MAX_PORTS;
                hw_sim.resources[i].reserved = 2; // CPU port and management port
                break;
            case HW_RESOURCE_BUFFER:
                hw_sim.resources[i].total = 32 * 1024 * 1024; // 32 MB buffer
                hw_sim.resources[i].reserved = 1 * 1024 * 1024; // 1 MB reserved
                break;
            case HW_RESOURCE_MAC_TABLE:
                hw_sim.resources[i].total = MAX_MAC_TABLE_ENTRIES;
                hw_sim.resources[i].reserved = 100; // Reserved entries
                break;
            case HW_RESOURCE_VLAN_TABLE:
                hw_sim.resources[i].total = MAX_VLANS;
                hw_sim.resources[i].reserved = 1; // Default VLAN
                break;
            case HW_RESOURCE_ROUTE_TABLE:
                hw_sim.resources[i].total = 16384;
                hw_sim.resources[i].reserved = 10; // Default routes
                break;
            case HW_RESOURCE_ACL:
                hw_sim.resources[i].total = 2048;
                hw_sim.resources[i].reserved = 20; // System ACLs
                break;
            case HW_RESOURCE_COUNTER:
                hw_sim.resources[i].total = 8192;
                hw_sim.resources[i].reserved = 64; // System counters
                break;
            case HW_RESOURCE_QUEUE:
                hw_sim.resources[i].total = 8 * MAX_PORTS; // 8 queues per port
                hw_sim.resources[i].reserved = 8; // System queues
                break;
        }
        
        // Calculate available resources
        hw_sim.resources[i].used = 0;
        hw_sim.resources[i].available = hw_sim.resources[i].total - 
                                        hw_sim.resources[i].reserved;
    }
}

status_t hw_resources_init(void) {
    if (hw_sim.initialized) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Hardware resources already initialized");
        return STATUS_SUCCESS;
    }

    LOG_INFO(LOG_CATEGORY_HAL, "Initializing hardware resources");
    
    // Set default values
    hw_simulation_set_defaults();
    
    hw_sim.initialized = true;
    
    LOG_INFO(LOG_CATEGORY_HAL, "Hardware resources initialized successfully");
    return STATUS_SUCCESS;
}

status_t hw_resources_shutdown(void) {
    if (!hw_sim.initialized) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Hardware resources not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    LOG_INFO(LOG_CATEGORY_HAL, "Shutting down hardware resources");
    
    // Reset simulation state
    memset(&hw_sim, 0, sizeof(hw_sim));
    
    LOG_INFO(LOG_CATEGORY_HAL, "Hardware resources shut down successfully");
    return STATUS_SUCCESS;
}

status_t hw_resources_get_usage(hw_resource_type_t resource, hw_resource_usage_t *usage) {
    if (!hw_sim.initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Hardware resources not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (resource > HW_RESOURCE_QUEUE || usage == NULL) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid parameters for hw_resources_get_usage");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Copy resource usage information
    *usage = hw_sim.resources[resource];
    return STATUS_SUCCESS;
}

status_t hw_resources_get_capabilities(hw_capabilities_t *capabilities) {
    if (!hw_sim.initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Hardware resources not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (capabilities == NULL) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid parameter for hw_resources_get_capabilities");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Copy capabilities information
    *capabilities = hw_sim.capabilities;
    return STATUS_SUCCESS;
}

status_t hw_resources_reserve(hw_resource_type_t resource, uint32_t amount) {
    if (!hw_sim.initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Hardware resources not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (resource > HW_RESOURCE_QUEUE) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid resource type");
        return STATUS_INVALID_PARAMETER;
    }
    
    hw_resource_usage_t *res = &hw_sim.resources[resource];
    
    // Check if enough resources are available
    if (amount > res->available) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Not enough resources available (requested: %u, available: %u)",
                 amount, res->available);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Reserve resources
    res->used += amount;
    res->available -= amount;
    
    LOG_DEBUG(LOG_CATEGORY_HAL, "Reserved %u resources of type %d (remaining: %u)",
             amount, resource, res->available);
    return STATUS_SUCCESS;
}

status_t hw_resources_release(hw_resource_type_t resource, uint32_t amount) {
    if (!hw_sim.initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Hardware resources not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (resource > HW_RESOURCE_QUEUE) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid resource type");
        return STATUS_INVALID_PARAMETER;
    }
    
    hw_resource_usage_t *res = &hw_sim.resources[resource];
    
    // Check if trying to release more than used
    if (amount > res->used) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Trying to release more resources than used (release: %u, used: %u)",
                 amount, res->used);
        return STATUS_INVALID_PARAMETER;
    }
    
    // Release resources
    res->used -= amount;
    res->available += amount;
    
    LOG_DEBUG(LOG_CATEGORY_HAL, "Released %u resources of type %d (available now: %u)",
             amount, resource, res->available);
    return STATUS_SUCCESS;
}

status_t hw_resources_check_available(hw_resource_type_t resource, uint32_t amount, bool *available) {
    if (!hw_sim.initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Hardware resources not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (resource > HW_RESOURCE_QUEUE || available == NULL) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid parameters for hw_resources_check_available");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Check if resources are available
    *available = (hw_sim.resources[resource].available >= amount);
    return STATUS_SUCCESS;
}
