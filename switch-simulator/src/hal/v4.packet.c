/**
 * @file packet.c
 * @brief Implementation of packet processing interface for switch simulator
 */
#include "../include/hal/packet.h"
#include "../include/common/logging.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/**
 * @brief Maximum number of packet processors that can be registered
 */
#define MAX_PACKET_PROCESSORS 64

/**
 * @brief Structure for registered packet processor
 */
typedef struct {
    packet_process_cb_t callback;   /**< Processor callback function */
    uint32_t priority;              /**< Processing priority */
    void *user_data;                /**< User data for callback */
    bool active;                    /**< Whether this entry is active */
} packet_processor_t;

/**
 * @brief Array of registered packet processors
 */
static packet_processor_t g_processors[MAX_PACKET_PROCESSORS];

/**
 * @brief Number of registered processors
 */
static uint32_t g_processor_count = 0;

/**
 * @brief Flag indicating if packet subsystem is initialized
 */
static bool g_initialized = false;

/**
 * @brief Compare function for sorting processors by priority
 */
static int compare_processors(const void *a, const void *b) {
    const packet_processor_t *p1 = (const packet_processor_t *)a;
    const packet_processor_t *p2 = (const packet_processor_t *)b;
    
    // Sort by priority (lower numbers come first)
    return (int)p1->priority - (int)p2->priority;
}

/**
 * @brief Sort processors by priority
 */
static void sort_processors(void) {
    qsort(g_processors, g_processor_count, sizeof(packet_processor_t), compare_processors);
}

status_t packet_init(void) {
    LOG_INFO(LOG_CATEGORY_HAL, "Initializing packet processing subsystem");
    
    if (g_initialized) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Packet processing subsystem already initialized");
        return STATUS_ALREADY_INITIALIZED;
    }
    
    // Clear all processor slots
    memset(g_processors, 0, sizeof(g_processors));
    g_processor_count = 0;
    g_initialized = true;
    
    LOG_INFO(LOG_CATEGORY_HAL, "Packet processing subsystem initialized successfully");
    return STATUS_SUCCESS;
}

status_t packet_shutdown(void) {
    LOG_INFO(LOG_CATEGORY_HAL, "Shutting down packet processing subsystem");
    
    if (!g_initialized) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    // Clear all processor slots
    memset(g_processors, 0, sizeof(g_processors));
    g_processor_count = 0;
    g_initialized = false;
    
    LOG_INFO(LOG_CATEGORY_HAL, "Packet processing subsystem shut down successfully");
    return STATUS_SUCCESS;
}

packet_buffer_t* packet_buffer_alloc(uint32_t size) {
    if (!g_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return NULL;
    }
    
    // Allocate packet buffer structure
    packet_buffer_t *packet = (packet_buffer_t *)malloc(sizeof(packet_buffer_t));
    if (!packet) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to allocate packet buffer structure");
        return NULL;
    }
    
    // Initialize structure
    memset(packet, 0, sizeof(packet_buffer_t));
    
    // Allocate data buffer
    packet->data = (uint8_t *)malloc(size);
    if (!packet->data) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to allocate packet data buffer of size %u", size);
        free(packet);
        return NULL;
    }
    
    packet->capacity = size;
    packet->size = 0;
    
    LOG_DEBUG(LOG_CATEGORY_HAL, "Allocated packet buffer of size %u", size);
    return packet;
}

void packet_buffer_free(packet_buffer_t *packet) {
    if (!packet) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Attempted to free NULL packet buffer");
        return;
    }
    
    // Free data buffer if present
    if (packet->data) {
        free(packet->data);
        packet->data = NULL;
    }
    
    // Free packet structure
    free(packet);
    
    LOG_DEBUG(LOG_CATEGORY_HAL, "Freed packet buffer");
}

packet_buffer_t* packet_buffer_clone(const packet_buffer_t *packet) {
    if (!g_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return NULL;
    }
    
    if (!packet) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Cannot clone NULL packet buffer");
        return NULL;
    }
    
    // Allocate new packet with same capacity
    packet_buffer_t *clone = packet_buffer_alloc(packet->capacity);
    if (!clone) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to allocate memory for packet clone");
        return NULL;
    }
    
    // Copy data
    memcpy(clone->data, packet->data, packet->size);
    clone->size = packet->size;
    
    // Copy metadata
    clone->metadata = packet->metadata;
    
    // User data is not copied - it's application-specific
    clone->user_data = NULL;
    
    LOG_DEBUG(LOG_CATEGORY_HAL, "Cloned packet buffer of size %u", packet->size);
    return clone;
}

status_t packet_buffer_resize(packet_buffer_t *packet, uint32_t new_size) {
    if (!g_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (!packet) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Cannot resize NULL packet buffer");
        return STATUS_INVALID_PARAMETER;
    }
    
    // If new size fits in current capacity, just update size
    if (new_size <= packet->capacity) {
        packet->size = new_size;
        LOG_DEBUG(LOG_CATEGORY_HAL, "Resized packet buffer to %u bytes (within capacity)", new_size);
        return STATUS_SUCCESS;
    }
    
    // Otherwise, need to reallocate
    uint8_t *new_data = (uint8_t *)realloc(packet->data, new_size);
    if (!new_data) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to resize packet buffer to %u bytes", new_size);
        return STATUS_NO_MEMORY;
    }
    
    packet->data = new_data;
    packet->capacity = new_size;
    packet->size = new_size;
    
    LOG_DEBUG(LOG_CATEGORY_HAL, "Resized packet buffer to %u bytes (reallocation)", new_size);
    return STATUS_SUCCESS;
}

status_t packet_register_processor(packet_process_cb_t callback, 
                                  uint32_t priority, 
                                  void *user_data, 
                                  uint32_t *handle_out) {
    if (!g_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (!callback) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Cannot register NULL callback");
        return STATUS_INVALID_PARAMETER;
    }
    
    if (g_processor_count >= MAX_PACKET_PROCESSORS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Maximum number of packet processors (%d) already registered", 
                 MAX_PACKET_PROCESSORS);
        return STATUS_RESOURCE_EXHAUSTED;
    }
    
    // Find an unused slot
    uint32_t slot;
    for (slot = 0; slot < MAX_PACKET_PROCESSORS; slot++) {
        if (!g_processors[slot].active) {
            break;
        }
    }
    
    // Fill the slot
    g_processors[slot].callback = callback;
    g_processors[slot].priority = priority;
    g_processors[slot].user_data = user_data;
    g_processors[slot].active = true;
    
    // Update count if needed
    if (slot >= g_processor_count) {
        g_processor_count = slot + 1;
    }
    
    // Sort processors by priority
    sort_processors();
    
    // Return handle
    if (handle_out) {
        *handle_out = slot;
    }
    
    LOG_INFO(LOG_CATEGORY_HAL, "Registered packet processor with priority %u, handle %u", 
             priority, slot);
    return STATUS_SUCCESS;
}

status_t packet_unregister_processor(uint32_t handle) {
    if (!g_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (handle >= MAX_PACKET_PROCESSORS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid processor handle: %u", handle);
        return STATUS_INVALID_PARAMETER;
    }
    
    if (!g_processors[handle].active) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Processor handle %u is not active", handle);
        return STATUS_INVALID_PARAMETER;
    }
    
    // Deactivate the processor
    g_processors[handle].active = false;
    g_processors[handle].callback = NULL;
    g_processors[handle].user_data = NULL;
    
    // Recalculate processor count if needed
    if (handle == g_processor_count - 1) {
        while (g_processor_count > 0 && !g_processors[g_processor_count - 1].active) {
            g_processor_count--;
        }
    }
    
    // Sort processors to maintain order
    sort_processors();
    
    LOG_INFO(LOG_CATEGORY_HAL, "Unregistered packet processor with handle %u", handle);
    return STATUS_SUCCESS;
}

packet_result_t packet_process(packet_buffer_t *packet) {
    if (!g_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return PACKET_RESULT_DROP;
    }
    
    if (!packet) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Cannot process NULL packet");
        return PACKET_RESULT_DROP;
    }
    
    // Initialize result to forward (default action)
    packet_result_t result = PACKET_RESULT_FORWARD;
    
    // Process packet through all active processors in priority order
    for (uint32_t i = 0; i < g_processor_count; i++) {
        if (g_processors[i].active && g_processors[i].callback) {
            // Call the processor
            result = g_processors[i].callback(packet, g_processors[i].user_data);
            
            // If processor consumed or dropped the packet, stop processing
            if (result == PACKET_RESULT_CONSUME || result == PACKET_RESULT_DROP) {
                LOG_DEBUG(LOG_CATEGORY_HAL, "Packet processing stopped with result %d by processor %u", 
                          result, i);
                break;
            }
            
            // If processor requested recirculation, start over with processing
            if (result == PACKET_RESULT_RECIRCULATE) {
                LOG_DEBUG(LOG_CATEGORY_HAL, "Packet recirculation requested by processor %u", i);
                return packet_process(packet);
            }
        }
    }
    
    LOG_DEBUG(LOG_CATEGORY_HAL, "Packet processing completed with result %d", result);
    return result;
}

status_t packet_inject(packet_buffer_t *packet) {
    if (!g_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (!packet) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Cannot inject NULL packet");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Set direction to internal
    packet->metadata.direction = PACKET_DIR_INTERNAL;
    
    // Process the packet
    packet_result_t result = packet_process(packet);
    
    // Handle result
    switch (result) {
        case PACKET_RESULT_FORWARD:
            LOG_DEBUG(LOG_CATEGORY_HAL, "Injected packet forwarded");
            // In a real implementation, this would hand off the packet to the forwarding engine
            break;
            
        case PACKET_RESULT_DROP:
            LOG_DEBUG(LOG_CATEGORY_HAL, "Injected packet dropped");
            break;
            
        case PACKET_RESULT_CONSUME:
            LOG_DEBUG(LOG_CATEGORY_HAL, "Injected packet consumed");
            break;
            
        case PACKET_RESULT_RECIRCULATE:
            LOG_WARNING(LOG_CATEGORY_HAL, "Packet recirculation should have been handled by packet_process");
            break;
    }
    
    return STATUS_SUCCESS;
}

status_t packet_transmit(packet_buffer_t *packet, port_id_t port_id) {
    if (!g_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (!packet) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Cannot transmit NULL packet");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Update packet metadata
    packet->metadata.port = port_id;
    packet->metadata.direction = PACKET_DIR_TX;
    
    // In a real implementation, this would integrate with the HAL port transmission functionality
    // For now, just log the transmission
    LOG_INFO(LOG_CATEGORY_HAL, "Transmitting packet of size %u bytes on port %u", 
             packet->size, port_id);
    
    // Simulated transmission logic would go here
    // ...
    
    return STATUS_SUCCESS;
}
