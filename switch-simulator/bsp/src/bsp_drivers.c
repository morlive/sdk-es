/**
 * @file bsp_drivers.c
 * @brief Implementation of BSP driver management and hardware resource handling
 */

#include "../include/bsp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// Resource type definitions
#define RESOURCE_TYPE_MEMORY   1
#define RESOURCE_TYPE_PORT     2
#define RESOURCE_TYPE_TIMER    3
#define RESOURCE_TYPE_QUEUE    4

// Structure for tracking allocated resources
typedef struct resource_entry {
    bsp_resource_handle_t handle;
    uint32_t resource_type;
    uint32_t size;
    void* data;
    struct resource_entry* next;
} resource_entry_t;

// List head for resource tracking
static resource_entry_t* resource_list = NULL;

// Port callback structure
typedef struct port_callback {
    uint32_t port_id;
    void (*callback)(uint32_t port_id, bsp_port_status_t status, void* user_data);
    void* user_data;
    struct port_callback* next;
} port_callback_t;

// List head for port callbacks
static port_callback_t* port_callback_list = NULL;

// Port status tracking
static bsp_port_status_t* port_statuses = NULL;
static uint32_t num_ports = 0;

/**
 * @brief Register a driver with the BSP
 */
bsp_error_t bsp_register_driver(const char* driver_name, void* driver_init_func) {
    if (driver_name == NULL || driver_init_func == NULL) {
        return BSP_ERROR_INVALID_PARAM;
    }
    
    printf("BSP: Registered driver '%s'\n", driver_name);
    
    // In a real implementation, this would store the driver in a registry
    // For simulation, we'll just acknowledge the registration
    
    return BSP_SUCCESS;
}

/**
 * @brief Allocate a hardware resource
 */
bsp_error_t bsp_allocate_resource(uint32_t resource_type, uint32_t size, bsp_resource_handle_t* handle) {
    if (handle == NULL) {
        return BSP_ERROR_INVALID_PARAM;
    }
    
    if (!bsp_is_initialized()) {
        return BSP_ERROR_NOT_INITIALIZED;
    }
    
    // Validate resource type
    if (resource_type != RESOURCE_TYPE_MEMORY && 
        resource_type != RESOURCE_TYPE_PORT &&
        resource_type != RESOURCE_TYPE_TIMER &&
        resource_type != RESOURCE_TYPE_QUEUE) {
        return BSP_ERROR_INVALID_PARAM;
    }
    
    // Allocate the resource entry
    resource_entry_t* entry = (resource_entry_t*)malloc(sizeof(resource_entry_t));
    if (entry == NULL) {
        return BSP_ERROR_RESOURCE_UNAVAILABLE;
    }
    
    // Allocate the resource data
    entry->data = malloc(size);
    if (entry->data == NULL) {
        free(entry);
        return BSP_ERROR_RESOURCE_UNAVAILABLE;
    }
    
    // Initialize the resource
    memset(entry->data, 0, size);
    entry->handle = entry->data; // Use the data pointer as the handle
    entry->resource_type = resource_type;
    entry->size = size;
    
    // Add to the resource list
    entry->next = resource_list;
    resource_list = entry;
    
    *handle = entry->handle;
    
    printf("BSP: Allocated resource type %u, size %u\n", resource_type, size);
    
    return BSP_SUCCESS;
}

/**
 * @brief Free a previously allocated hardware resource
 */
bsp_error_t bsp_free_resource(bsp_resource_handle_t handle) {
    if (handle == NULL) {
        return BSP_ERROR_INVALID_PARAM;
    }
    
    if (!bsp_is_initialized()) {
        return BSP_ERROR_NOT_INITIALIZED;
    }
    
    // Find the resource in the list
    resource_entry_t* prev = NULL;
    resource_entry_t* current = resource_list;
    
    while (current != NULL) {
        if (current->handle == handle) {
            // Remove from the list
            if (prev == NULL) {
                resource_list = current->next;
            } else {
                prev->next = current->next;
            }
            
            // Free the resource data
            free(current->data);
            
            // Free the entry
            free(current);
            
            printf("BSP: Freed resource\n");
            
            return BSP_SUCCESS;
        }
        
        prev = current;
        current = current->next;
    }
    
    return BSP_ERROR_INVALID_PARAM; // Handle not found
}

/**
 * @brief Initialize port statuses
 */
bsp_error_t bsp_init_port_statuses(uint32_t port_count) {
    if (port_statuses != NULL) {
        // Already initialized, free the old statuses
        free(port_statuses);
    }
    
    port_statuses = (bsp_port_status_t*)calloc(port_count, sizeof(bsp_port_status_t));
    if (port_statuses == NULL) {
        return BSP_ERROR_RESOURCE_UNAVAILABLE;
    }
    
    num_ports = port_count;
    
    // Initialize default port statuses
    for (uint32_t i = 0; i < num_ports; i++) {
        port_statuses[i].link_up = false;
        port_statuses[i].speed = BSP_PORT_SPEED_1G;
        port_statuses[i].duplex = BSP_PORT_DUPLEX_FULL;
        port_statuses[i].flow_control_enabled = true;
        port_statuses[i].rx_bytes = 0;
        port_statuses[i].tx_bytes = 0;
        port_statuses[i].rx_packets = 0;
        port_statuses[i].tx_packets = 0;
        port_statuses[i].rx_errors = 0;
        port_statuses[i].tx_errors = 0;
    }
    
    return BSP_SUCCESS;
}

/**
 * @brief Initialize a specific port
 */
bsp_error_t bsp_port_init(uint32_t port_id, bsp_port_speed_t speed, bsp_port_duplex_t duplex) {
    if (!bsp_is_initialized()) {
        return BSP_ERROR_NOT_INITIALIZED;
    }
    
    if (port_statuses == NULL || port_id >= num_ports) {
        return BSP_ERROR_INVALID_PARAM;
    }
    
    // Set initial port status
    port_statuses[port_id].speed = speed;
    port_statuses[port_id].duplex = duplex;
    port_statuses[port_id].link_up = false; // Default to link down
    
    printf("BSP: Initialized port %u with speed %u and duplex %u\n", 
           port_id, speed, duplex);
    
    return BSP_SUCCESS;
}

/**
 * @brief Get port status
 */
bsp_error_t bsp_port_get_status(uint32_t port_id, bsp_port_status_t* status) {
    if (status == NULL) {
        return BSP_ERROR_INVALID_PARAM;
    }
    
    if (!bsp_is_initialized()) {
        return BSP_ERROR_NOT_INITIALIZED;
    }
    
    if (port_statuses == NULL || port_id >= num_ports) {
        return BSP_ERROR_INVALID_PARAM;
    }
    
    // Copy the port status
    memcpy(status, &port_statuses[port_id], sizeof(bsp_port_status_t));
    
    return BSP_SUCCESS;
}

/**
 * @brief Enable or disable a port
 */
bsp_error_t bsp_port_set_enabled(uint32_t port_id, bool enable) {
    if (!bsp_is_initialized()) {
        return BSP_ERROR_NOT_INITIALIZED;
    }
    
    if (port_statuses == NULL || port_id >= num_ports) {
        return BSP_ERROR_INVALID_PARAM;
    }
    
    // Update link status
    bool old_state = port_statuses[port_id].link_up;
    port_statuses[port_id].link_up = enable;
    
    printf("BSP: Port %u %s\n", port_id, enable ? "enabled" : "disabled");
    
    // If status changed, notify callbacks
    if (old_state != enable) {
        port_callback_t* callback = port_callback_list;
        
        while (callback != NULL) {
            if (callback->port_id == port_id && callback->callback != NULL) {
                callback->callback(port_id, port_statuses[port_id], callback->user_data);
            }
            callback = callback->next;
        }
    }
    
    return BSP_SUCCESS;
}

/**
 * @brief Register a callback for port status changes
 */
bsp_error_t bsp_port_register_callback(uint32_t port_id, 
                                      void (*callback)(uint32_t port_id, bsp_port_status_t status, void* user_data),
                                      void* user_data) {
    if (callback == NULL) {
        return BSP_ERROR_INVALID_PARAM;
    }
    
    if (!bsp_is_initialized()) {
        return BSP_ERROR_NOT_INITIALIZED;
    }
    
    if (port_id >= num_ports) {
        return BSP_ERROR_INVALID_PARAM;
    }
    
    // Create a new callback entry
    port_callback_t* entry = (port_callback_t*)malloc(sizeof(port_callback_t));
    if (entry == NULL) {
        return BSP_ERROR_RESOURCE_UNAVAILABLE;
    }
    
    entry->port_id = port_id;
    entry->callback = callback;
    entry->user_data = user_data;
    
    // Add to the callback list
    entry->next = port_callback_list;
    port_callback_list = entry;
    
    printf("BSP: Registered callback for port %u\n", port_id);
    
    return BSP_SUCCESS;
}

/**
 * @brief Update port statistics
 * This function would be called by the packet processing logic
 */
bsp_error_t bsp_port_update_stats(uint32_t port_id, uint32_t rx_bytes, uint32_t tx_bytes,
                                 uint32_t rx_packets, uint32_t tx_packets,
                                 uint32_t rx_errors, uint32_t tx_errors) {
    if (!bsp_is_initialized()) {
        return BSP_ERROR_NOT_INITIALIZED;
    }
    
    if (port_statuses == NULL || port_id >= num_ports) {
        return BSP_ERROR_INVALID_PARAM;
    }
    
    // Update statistics
    port_statuses[port_id].rx_bytes += rx_bytes;
    port_statuses[port_id].tx_bytes += tx_bytes;
    port_statuses[port_id].rx_packets += rx_packets;
    port_statuses[port_id].tx_packets += tx_packets;
    port_statuses[port_id].rx_errors += rx_errors;
    port_statuses[port_id].tx_errors += tx_errors;
    
    return BSP_SUCCESS;
}

/**
 * @brief Clean up all resources
 */
void bsp_cleanup_resources(void) {
    // Free all allocated resources
    resource_entry_t* res_current = resource_list;
    resource_entry_t* res_next;
    
    while (res_current != NULL) {
        res_next = res_current->next;
        
        if (res_current->data != NULL) {
            free(res_current->data);
        }
        
        free(res_current);
        res_current = res_next;
    }
    
    resource_list = NULL;
    
    // Free all port callbacks
    port_callback_t* cb_current = port_callback_list;
    port_callback_t* cb_next;
    
    while (cb_current != NULL) {
        cb_next = cb_current->next;
        free(cb_current);
        cb_current = cb_next;
    }
    
    port_callback_list = NULL;
    
    // Free port statuses
    if (port_statuses != NULL) {
        free(port_statuses);
        port_statuses = NULL;
    }
    
    num_ports = 0;
}

/**
 * @brief Get system timestamp in microseconds
 */
uint64_t bsp_get_timestamp_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}
