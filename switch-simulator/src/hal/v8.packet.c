/**
 * @file packet.c
 * @brief Implementation of packet processing interface for switch simulator
 */
#include "../include/hal/packet.h"
#include "../include/hal/hw_resources.h"
#include "../include/common/logging.h"
#include "../include/common/error_codes.h"
#include "../include/hal/port.h"
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
 * @brief Lock to protect packet processor array during concurrent operations
 * In a real implementation, this would be a mutex or spinlock
 */
static volatile int g_processor_lock = 0;

// <d1i:sim ---- > need to check
/* Внешние интерфейсы для симуляции оборудования */
// extern status_t hw_sim_transmit_packet(packet_buffer_t *packet, port_id_t port_id);
// extern status_t hw_sim_receive_packet(packet_buffer_t *packet, port_id_t *port_id);
// :d1i>
/**
 * @brief Simple lock acquisition function
 * In a real implementation, this would use proper synchronization primitives
 */
static inline void acquire_lock(void) {
    // Simulate lock acquisition
    // In real implementation, this would be replaced with proper mutex/spinlock code
    g_processor_lock = 1;
}

/**
 * @brief Simple lock release function
 * In a real implementation, this would use proper synchronization primitives
 */
static inline void release_lock(void) {
    // Simulate lock release
    // In real implementation, this would be replaced with proper mutex/spinlock code
    g_processor_lock = 0;
}

/**
 * @brief Check validity of a packet buffer
 * @param packet Packet buffer to validate
 * @return true if packet is valid, false otherwise
 */
static bool packet_buffer_is_valid(const packet_buffer_t *packet) {
    // Basic validation of packet buffer
    if (!packet || !packet->data || packet->size > packet->capacity) {
        return false;
    }
    return true;
}

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
    g_processor_lock = 0;
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
    
    // Acquire lock to ensure no concurrent operation
    acquire_lock();
    
    // Clear all processor slots
    memset(g_processors, 0, sizeof(g_processors));
    g_processor_count = 0;
    g_initialized = false;
    
    // Release lock
    release_lock();
    
    LOG_INFO(LOG_CATEGORY_HAL, "Packet processing subsystem shut down successfully");
    return STATUS_SUCCESS;
}

packet_buffer_t* packet_buffer_alloc(uint32_t size) {
    if (!g_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return NULL;
    }
    
    // Validate input
    if (size == 0) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Cannot allocate packet buffer with zero size");
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
    
    // Initialize metadata with default values
    packet->metadata.port = PORT_ID_INVALID;
    packet->metadata.direction = PACKET_DIR_INVALID;
    packet->metadata.timestamp = 0; // In real implementation, set to current time
    packet->metadata.priority = 0;
    packet->metadata.vlan_id = 0;
    
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
    
    if (!packet_buffer_is_valid(packet)) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Cannot clone invalid packet buffer");
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
    
    if (!packet_buffer_is_valid(packet)) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Cannot resize invalid packet buffer");
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
    
    if (!handle_out) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Output handle pointer cannot be NULL");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Acquire lock to prevent concurrent modification
    acquire_lock();
    
    if (g_processor_count >= MAX_PACKET_PROCESSORS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Maximum number of packet processors (%d) already registered", 
                 MAX_PACKET_PROCESSORS);
        release_lock();
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
    *handle_out = slot;
    
    // Release lock
    release_lock();
    
    LOG_INFO(LOG_CATEGORY_HAL, "Registered packet processor with priority %u, handle %u", 
             priority, slot);
    return STATUS_SUCCESS;
}

status_t packet_unregister_processor(uint32_t handle) {
    if (!g_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    // Acquire lock to prevent concurrent modification
    acquire_lock();
    
    if (handle >= MAX_PACKET_PROCESSORS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid processor handle: %u", handle);
        release_lock();
        return STATUS_INVALID_PARAMETER;
    }
    
    if (!g_processors[handle].active) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Processor handle %u is not active", handle);
        release_lock();
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
    
    // Release lock
    release_lock();
    
    LOG_INFO(LOG_CATEGORY_HAL, "Unregistered packet processor with handle %u", handle);
    return STATUS_SUCCESS;
}

packet_result_t packet_process(packet_buffer_t *packet) {
    if (!g_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return PACKET_RESULT_DROP;
    }
    
    if (!packet_buffer_is_valid(packet)) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Cannot process invalid packet");
        return PACKET_RESULT_DROP;
    }
    
    // Initialize result to forward (default action)
    packet_result_t result = PACKET_RESULT_FORWARD;
    
    // Track recursion depth to prevent infinite recirculation
    static thread_local int recursion_depth = 0;
    recursion_depth++;
    
    // Prevent deep recursion which could lead to stack overflow
    if (recursion_depth > 16) { // Arbitrary limit to prevent stack overflow
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet recirculation depth exceeded limit (16), dropping packet");
        recursion_depth--;
        return PACKET_RESULT_DROP;
    }
    
    // Acquire lock for reading (could use reader lock in real implementation)
    acquire_lock();
    
    // Make a local copy of active processors for consistent execution
    // This allows us to release the lock sooner
    uint32_t processor_count = g_processor_count;
    packet_processor_t processors[MAX_PACKET_PROCESSORS];
    memcpy(processors, g_processors, processor_count * sizeof(packet_processor_t));
    
    // Release lock as we don't need to keep it during processing
    release_lock();
    
    // Process packet through all active processors in priority order
    for (uint32_t i = 0; i < processor_count; i++) {
        if (processors[i].active && processors[i].callback) {
            // Call the processor
            result = processors[i].callback(packet, processors[i].user_data);
            
            // If processor consumed or dropped the packet, stop processing
            if (result == PACKET_RESULT_CONSUME || result == PACKET_RESULT_DROP) {
                LOG_DEBUG(LOG_CATEGORY_HAL, "Packet processing stopped with result %d by processor %u", 
                          result, i);
                break;
            }
            
            // If processor requested recirculation, start over with processing
            if (result == PACKET_RESULT_RECIRCULATE) {
                LOG_DEBUG(LOG_CATEGORY_HAL, "Packet recirculation requested by processor %u", i);
                result = packet_process(packet);
                recursion_depth--;
                return result;
            }
        }
    }

    recursion_depth--;
    LOG_DEBUG(LOG_CATEGORY_HAL, "Packet processing completed with result %d", result);
    return result;
}

status_t packet_inject(packet_buffer_t *packet) {
    if (!g_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (!packet_buffer_is_valid(packet)) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Cannot inject invalid packet");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Set direction to internal
    packet->metadata.direction = PACKET_DIR_INTERNAL;
    packet->metadata.timestamp = 0; // In real implementation, set to current time
    
    // Process the packet
    packet_result_t result = packet_process(packet);
    
    // Handle result
    switch (result) {
        case PACKET_RESULT_FORWARD:
            LOG_DEBUG(LOG_CATEGORY_HAL, "Injected packet forwarded to switching/routing engine");
            // In a real implementation, this would hand off the packet to the forwarding engine
            // Call to hw_simulation forwarding function would go here
            break;
            
        case PACKET_RESULT_DROP:
            LOG_DEBUG(LOG_CATEGORY_HAL, "Injected packet dropped during processing");
            break;
            
        case PACKET_RESULT_CONSUME:
            LOG_DEBUG(LOG_CATEGORY_HAL, "Injected packet consumed by a processor");
            break;
            
        case PACKET_RESULT_RECIRCULATE:
            LOG_WARNING(LOG_CATEGORY_HAL, "Packet recirculation should have been handled by packet_process");
            break;
            
        default:
            LOG_ERROR(LOG_CATEGORY_HAL, "Unknown packet result value: %d", result);
            return STATUS_UNKNOWN_ERROR;
    }
    
    return STATUS_SUCCESS;
}

status_t packet_transmit(packet_buffer_t *packet, port_id_t port_id) {
    if (!g_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (!packet_buffer_is_valid(packet)) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Cannot transmit invalid packet");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Validate port ID
    if (!port_is_valid(port_id)) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid port ID %u for packet transmission", port_id);
        return STATUS_INVALID_PARAMETER;
    }
    
    // Update packet metadata
    packet->metadata.port = port_id;
    packet->metadata.direction = PACKET_DIR_TX;
    packet->metadata.timestamp = 0; // In real implementation, set to current time
    
    // In a real implementation, this would integrate with the HAL port transmission functionality
    status_t status = port_transmit_packet(port_id, packet);
    if (status != STATUS_SUCCESS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to transmit packet on port %u: %s", 
                 port_id, error_to_string(status));
        return status;
    }
    
    LOG_INFO(LOG_CATEGORY_HAL, "Transmitted packet of size %u bytes on port %u", 
             packet->size, port_id);
    
    return STATUS_SUCCESS;
}

status_t packet_receive(packet_buffer_t *packet, port_id_t port_id) {
    if (!g_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (!packet_buffer_is_valid(packet)) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Cannot process invalid received packet");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Validate port ID
    if (!port_is_valid(port_id)) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid port ID %u for packet reception", port_id);
        return STATUS_INVALID_PARAMETER;
    }
    
    // Update packet metadata
    packet->metadata.port = port_id;
    packet->metadata.direction = PACKET_DIR_RX;
    packet->metadata.timestamp = 0; // In real implementation, set to current time
    
    // Process the packet
    packet_result_t result = packet_process(packet);
    
    // Handle result
    switch (result) {
        case PACKET_RESULT_FORWARD:
            LOG_DEBUG(LOG_CATEGORY_HAL, "Received packet forwarded to switching/routing engine");
            // In a real implementation, this would hand off the packet to the forwarding engine
            // Call to hw_simulation forwarding function would go here
            break;
            
        case PACKET_RESULT_DROP:
            LOG_DEBUG(LOG_CATEGORY_HAL, "Received packet dropped during processing");
            break;
            
        case PACKET_RESULT_CONSUME:
            LOG_DEBUG(LOG_CATEGORY_HAL, "Received packet consumed by a processor");
            break;
            
        case PACKET_RESULT_RECIRCULATE:
            LOG_WARNING(LOG_CATEGORY_HAL, "Packet recirculation should have been handled by packet_process");
            break;
            
        default:
            LOG_ERROR(LOG_CATEGORY_HAL, "Unknown packet result value: %d", result);
            return STATUS_UNKNOWN_ERROR;
    }
    
    return STATUS_SUCCESS;
}

// <d1r:sim ======================================================================
// status_t packet_transmit(packet_buffer_t *packet, port_id_t port_id) {
//     if (!g_initialized) {
//         LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
//         return STATUS_NOT_INITIALIZED;
//     }
//     
//     if (!packet_buffer_is_valid(packet)) {
//         LOG_ERROR(LOG_CATEGORY_HAL, "Cannot transmit invalid packet");
//         return STATUS_INVALID_PARAMETER;
//     }
//     
//     // Validate port ID
//     if (!port_is_valid(port_id)) {
//         LOG_ERROR(LOG_CATEGORY_HAL, "Invalid port ID %u for packet transmission", port_id);
//         return STATUS_INVALID_PARAMETER;
//     }
//     
//     // Update packet metadata
//     packet->metadata.port = port_id;
//     packet->metadata.direction = PACKET_DIR_TX;
//     packet->metadata.timestamp = 0; // In real implementation, set to current time
//     
//     // Call the hardware simulation function for packet transmission
//     status_t status = hw_sim_transmit_packet(packet, port_id);
//     if (status != STATUS_SUCCESS) {
//         LOG_ERROR(LOG_CATEGORY_HAL, "Failed to transmit packet on port %u: %s", 
//                  port_id, error_to_string(status));
//         return status;
//     }
//     
//     LOG_INFO(LOG_CATEGORY_HAL, "Transmitted packet of size %u bytes on port %u", 
//              packet->size, port_id);
//     
//     return STATUS_SUCCESS;
// }
// 
// status_t packet_transmit(packet_buffer_t *packet, port_id_t port_id) {
//     if (!g_initialized) {
//         LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
//         return STATUS_NOT_INITIALIZED;
//     }
//     
//     if (!packet_buffer_is_valid(packet)) {
//         LOG_ERROR(LOG_CATEGORY_HAL, "Cannot transmit invalid packet");
//         return STATUS_INVALID_PARAMETER;
//     }
//     
//     // Validate port ID
//     if (!port_is_valid(port_id)) {
//         LOG_ERROR(LOG_CATEGORY_HAL, "Invalid port ID %u for packet transmission", port_id);
//         return STATUS_INVALID_PARAMETER;
//     }
//     
//     // Update packet metadata
//     packet->metadata.port = port_id;
//     packet->metadata.direction = PACKET_DIR_TX;
//     packet->metadata.timestamp = 0; // In real implementation, set to current time
//     
//     // Call the hardware simulation function for packet transmission
//     status_t status = hw_sim_transmit_packet(packet, port_id);
//     if (status != STATUS_SUCCESS) {
//         LOG_ERROR(LOG_CATEGORY_HAL, "Failed to transmit packet on port %u: %s", 
//                  port_id, error_to_string(status));
//         return status;
//     }
//     
//     LOG_INFO(LOG_CATEGORY_HAL, "Transmitted packet of size %u bytes on port %u", 
//              packet->size, port_id);
//     
//     return STATUS_SUCCESS;
// }
// :d1r>



status_t packet_get_header(packet_buffer_t *packet, uint32_t offset, void *header, uint32_t size) {
    if (!g_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (!packet_buffer_is_valid(packet) || !header) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid parameters for packet_get_header");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Check if requested data is within packet boundaries
    if (offset + size > packet->size) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Header extraction range [%u-%u] exceeds packet size %u", 
                 offset, offset + size - 1, packet->size);
        return STATUS_OUT_OF_BOUNDS;
    }
    
    // Copy header data
    memcpy(header, packet->data + offset, size);
    
    return STATUS_SUCCESS;
}

status_t packet_set_header(packet_buffer_t *packet, uint32_t offset, const void *header, uint32_t size) {
    if (!g_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (!packet_buffer_is_valid(packet) || !header) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid parameters for packet_set_header");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Check if requested operation is within packet boundaries
    if (offset + size > packet->size) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Header insertion range [%u-%u] exceeds packet size %u", 
                 offset, offset + size - 1, packet->size);
        return STATUS_OUT_OF_BOUNDS;
    }
    
    // Copy header data
    memcpy(packet->data + offset, header, size);
    
    return STATUS_SUCCESS;
}

status_t packet_insert(packet_buffer_t *packet, uint32_t offset, const void *data, uint32_t size) {
    if (!g_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (!packet_buffer_is_valid(packet) || !data || size == 0) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid parameters for packet_insert");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Check if offset is valid
    if (offset > packet->size) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Insert offset %u exceeds packet size %u", offset, packet->size);
        return STATUS_OUT_OF_BOUNDS;
    }
    
    // Calculate new size and check capacity
    uint32_t new_size = packet->size + size;
    if (new_size > packet->capacity) {
        // Need to resize the packet
        status_t status = packet_buffer_resize(packet, new_size);
        if (status != STATUS_SUCCESS) {
            LOG_ERROR(LOG_CATEGORY_HAL, "Failed to resize packet for insertion: %s", 
                     error_to_string(status));
            return status;
        }
    }
    
    // Shift existing data to make room
    if (offset < packet->size) {
        memmove(packet->data + offset + size, packet->data + offset, packet->size - offset);
    }
    
    // Insert new data
    memcpy(packet->data + offset, data, size);
    packet->size = new_size;
    
    LOG_DEBUG(LOG_CATEGORY_HAL, "Inserted %u bytes at offset %u in packet", size, offset);
    return STATUS_SUCCESS;
}

status_t packet_remove(packet_buffer_t *packet, uint32_t offset, uint32_t size) {
    if (!g_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (!packet_buffer_is_valid(packet) || size == 0) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid parameters for packet_remove");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Check if requested removal is within packet boundaries
    if (offset + size > packet->size) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Removal range [%u-%u] exceeds packet size %u", 
                 offset, offset + size - 1, packet->size);
        return STATUS_OUT_OF_BOUNDS;
    }
    
    // Shift data to close the gap
    if (offset + size < packet->size) {
        memmove(packet->data + offset, packet->data + offset + size, packet->size - (offset + size));
    }
    
    // Update size
    packet->size -= size;
    
    LOG_DEBUG(LOG_CATEGORY_HAL, "Removed %u bytes from offset %u in packet", size, offset);
    return STATUS_SUCCESS;
}





















































