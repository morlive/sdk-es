/**
 * @file packet.c
 * @brief Implementation of packet processing interface for switch simulator
 *
 * This file implements the packet processing subsystem for a switch simulator.
 * It provides functions for packet buffer management, packet processing,
 * and packet handling (transmission, reception, injection).
 */
#include "../include/hal/packet.h"
#include "../include/common/logging.h"
#include "../include/common/error_codes.h"
#include "../include/hal/port.h"
#include "../include/hal/hw_resources.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/**
 * @brief External interfaces for hardware simulation
 */
extern status_t hw_sim_transmit_packet(packet_buffer_t *packet, port_id_t port_id);
extern status_t hw_sim_receive_packet(packet_buffer_t *packet, port_id_t *port_id);

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

/**
 * @brief Simple lock acquisition function
 *
 * Acquires a lock to protect critical sections of code from concurrent access.
 * In a real implementation, this would use proper synchronization primitives
 */
static inline void acquire_lock(void) {
    // Simulate lock acquisition with atomic operation
    // In real implementation, this would be replaced with proper mutex/spinlock code
    while (__sync_lock_test_and_set(&g_processor_lock, 1)) {
        // Spin until acquired
    }
}

/**
 * @brief Simple lock release function
 *
 * Releases a lock previously acquired with acquire_lock().
 * In a real implementation, this would use proper synchronization primitives
 */
static inline void release_lock(void) {
    // Simulate lock release with atomic operation
    // In real implementation, this would be replaced with proper mutex/spinlock code
    __sync_lock_release(&g_processor_lock);
}

/**
 * @brief Check validity of a packet buffer
 *
 * Performs basic validation of a packet buffer structure to ensure
 * it has valid properties before operations are performed on it.
 *
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
 *
 * Used as a comparison function for qsort() to sort packet processors
 * by their priority values.
 *
 * @param a Pointer to first processor to compare
 * @param b Pointer to second processor to compare
 * @return Integer result of comparison (negative if a < b, 0 if a == b, positive if a > b)
 */
static int compare_processors(const void *a, const void *b) {
    const packet_processor_t *p1 = (const packet_processor_t *)a;
    const packet_processor_t *p2 = (const packet_processor_t *)b;
    
    // Sort by priority (lower numbers come first)
    return (int)p1->priority - (int)p2->priority;
}

/**
 * @brief Sort processors by priority
 *
 * Sorts the array of packet processors by their priority values
 * to ensure they are processed in the correct order.
 */
static void sort_processors(void) {
    qsort(g_processors, g_processor_count, sizeof(packet_processor_t), compare_processors);
}

/**
 * @brief Initialize the packet processing subsystem
 *
 * Initializes the packet processing subsystem, preparing it for use.
 * This function must be called before any other packet processing functions.
 *
 * @return STATUS_SUCCESS on success, appropriate error code otherwise
 */
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

/**
 * @brief Shut down the packet processing subsystem
 * 
 * Shuts down the packet processing subsystem, freeing all resources
 * and deregistering all packet processors.
 * 
 * @return STATUS_SUCCESS on success, appropriate error code otherwise
 */
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

/**
 * @brief Allocate a new packet buffer
 *
 * Allocates a new packet buffer with the specified capacity.
 * The buffer is initialized with default metadata values.
 *
 * @param size Capacity of the new packet buffer in bytes
 * @return Pointer to the newly allocated packet buffer, or NULL on failure
 */
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

/**
 * @brief Free a packet buffer
 * 
 * Frees a packet buffer previously allocated with packet_buffer_alloc().
 * This function securely clears the packet data before freeing the memory.
 * 
 * @param packet Pointer to the packet buffer to free
 */
void packet_buffer_free(packet_buffer_t *packet) {
    if (!packet) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Attempted to free NULL packet buffer");
        return;
    }
    
    // Free data buffer if present
    if (packet->data) {
        // Securely clear data before freeing
        memset(packet->data, 0, packet->capacity);
        free(packet->data);
        packet->data = NULL;
    }
    
    // Clear packet structure before freeing
    memset(packet, 0, sizeof(packet_buffer_t));
    
    // Free packet structure
    free(packet);
    
    LOG_DEBUG(LOG_CATEGORY_HAL, "Freed packet buffer");
}

/**
 * @brief Clone a packet buffer
 * 
 * Creates a new packet buffer that is a copy of the original.
 * The new buffer has the same size, capacity, and content as the original.
 * Metadata is also copied, but user data is not.
 * 
 * @param packet Pointer to the packet buffer to clone
 * @return Pointer to the newly allocated clone, or NULL on failure
 */
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

/**
 * @brief Resize a packet buffer
 * 
 * Resizes a packet buffer to the specified size. If the new size fits within
 * the current capacity, no memory reallocation is performed. Otherwise,
 * the buffer is reallocated to the new size.
 * 
 * @param packet Pointer to the packet buffer to resize
 * @param new_size New size for the packet buffer in bytes
 * @return STATUS_SUCCESS on success, appropriate error code otherwise
 */
status_t packet_buffer_resize(packet_buffer_t *packet, uint32_t new_size) {
    if (!g_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (!packet_buffer_is_valid(packet)) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Cannot resize invalid packet buffer");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Check for zero size
    if (new_size == 0) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Cannot resize packet to zero size");
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

/**
 * @brief Register a packet processor
 * 
 * Registers a callback function to process packets. The callback will be
 * called when packets are processed, in order of priority (lower priority
 * values are processed first).
 * 
 * @param callback Function to call for packet processing
 * @param priority Priority of the processor (lower values are processed first)
 * @param user_data User data to pass to the callback
 * @param handle_out Pointer to store the handle for the registered processor
 * @return STATUS_SUCCESS on success, appropriate error code otherwise
 */
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

/**
 * @brief Unregister a packet processor
 * 
 * Unregisters a packet processor previously registered with packet_register_processor().
 * After this function returns successfully, the processor will no longer be called
 * during packet processing.
 * 
 * @param handle Handle of the processor to unregister
 * @return STATUS_SUCCESS on success, appropriate error code otherwise
 */
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

/**
 * @brief Process a packet through registered processors
 * 
 * Processes a packet through all registered packet processors in priority order.
 * Processing continues until one of the processors drops or consumes the packet,
 * or all processors have been called.
 * 
 * @param packet Pointer to the packet buffer to process
 * @return Result of packet processing (FORWARD, DROP, CONSUME, RECIRCULATE)
 */
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
    #ifdef __STDC_NO_THREADS__
    // Fallback for compilers without thread_local support
    static int recursion_depth = 0;
    #else
    static thread_local int recursion_depth = 0;
    #endif
    
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

/**
 * @brief Inject a packet into the processing pipeline
 *
 * Injects a packet into the processing pipeline as if it were generated
 * internally by the switch. The packet is processed by all registered
 * packet processors in priority order.
 *
 * @param packet Pointer to the packet buffer to inject
 * @return STATUS_SUCCESS on success, appropriate error code otherwise
 */
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

/**
 * @brief Transmit a packet on a specific port
 *
 * Transmits a packet on the specified port. This function checks that the
 * port is valid and operational before attempting transmission.
 *
 * @param packet Pointer to the packet buffer to transmit
 * @param port_id ID of the port to transmit on
 * @return STATUS_SUCCESS on success, appropriate error code otherwise
 */
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

    // Check if port is operational
    port_state_t port_state;
    status_t status = port_get_state(port_id, &port_state);
    if (status != STATUS_SUCCESS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to get state for port %u: %s",
                 port_id, error_to_string(status));
        return status;
    }

    if (port_state != PORT_STATE_UP) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Cannot transmit packet on port %u: port not up (state: %d)",
                   port_id, port_state);
        return STATUS_RESOURCE_UNAVAILABLE;
    }

    // Update packet metadata
    packet->metadata.port = port_id;
    packet->metadata.direction = PACKET_DIR_TX;
    packet->metadata.timestamp = 0; // In real implementation, set to current time

    // Integrate with hardware simulation
    status = hw_sim_transmit_packet(packet, port_id);
    if (status != STATUS_SUCCESS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to transmit packet on port %u via hardware simulation: %s",
                 port_id, error_to_string(status));
        return status;
    }

    LOG_INFO(LOG_CATEGORY_HAL, "Transmitted packet of size %u bytes on port %u",
             packet->size, port_id);

    return STATUS_SUCCESS;
}

/**
 * @brief Process a received packet
 * 
 * Processes a packet that has been received on the specified port.
 * The packet is processed by all registered packet processors in priority order.
 * 
 * @param packet Pointer to the packet buffer that was received
 * @param port_id ID of the port the packet was received on
 * @return STATUS_SUCCESS on success, appropriate error code otherwise
 */
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

    // Check if port is operational
    port_state_t port_state;
    status_t status = port_get_state(port_id, &port_state);
    if (status != STATUS_SUCCESS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to get state for port %u: %s",
                 port_id, error_to_string(status));
        return status;
    }

    if (port_state != PORT_STATE_UP) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Cannot receive packet on port %u: port not up (state: %d)",
                   port_id, port_state);
        return STATUS_RESOURCE_UNAVAILABLE;
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

/**
 * @brief Function to handle incoming packets from hardware simulation
 *
 * Handles a packet that has been received from the hardware simulation.
 * This function determines the source port and processes the packet.
 *
 * @param packet Incoming packet buffer
 * @return Status code
 */
status_t packet_handle_incoming(packet_buffer_t *packet) {
    if (!g_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    if (!packet_buffer_is_valid(packet)) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Cannot handle invalid incoming packet");
        return STATUS_INVALID_PARAMETER;
    }

    // Determine source port ID
    port_id_t port_id;
    status_t status = hw_sim_receive_packet(packet, &port_id);
    if (status != STATUS_SUCCESS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to determine source port for incoming packet: %s",
                 error_to_string(status));
        return status;
    }

    // Process packet as a received packet
    return packet_receive(packet, port_id);
}

/**
 * @brief Extract a header from a packet
 * 
 * Extracts a block of data from a packet at the specified offset.
 * The data is copied to the provided header buffer.
 * 
 * @param packet Pointer to the packet buffer to extract from
 * @param offset Offset in bytes from the start of the packet
 * @param header Pointer to buffer to store the extracted header
 * @param size Size of the header to extract in bytes
 * @return STATUS_SUCCESS on success, appropriate error code otherwise
 */
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

/**
 * @brief Write a header to a packet
 *
 * Writes a block of data to a packet at the specified offset.
 * The data is copied from the provided header buffer.
 *
 * @param packet Pointer to the packet buffer to write to
 * @param offset Offset in bytes from the start of the packet
 * @param header Pointer to buffer containing the header data
 * @param size Size of the header to write in bytes
 * @return STATUS_SUCCESS on success, appropriate error code otherwise
 */
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

/**
 * @brief Insert data into a packet
 * 
 * Inserts a block of data into a packet at the specified offset.
 * This function will resize the packet if necessary to accommodate the new data.
 * Existing data at and after the offset is moved to make room for the new data.
 * 
 * @param packet Pointer to the packet buffer to insert into
 * @param offset Offset in bytes from the start of the packet
 * @param data Pointer to buffer containing the data to insert
 * @param size Size of the data to insert in bytes
 * @return STATUS_SUCCESS on success, appropriate error code otherwise
 */
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

/**
 * @brief Remove data from a packet
 *
 * Removes a block of data from a packet at the specified offset.
 * Data after the removed block is moved up to fill the gap.
 *
 * @param packet Pointer to the packet buffer to remove data from
 * @param offset Offset in bytes from the start of the packet
 * @param size Size of the data to remove in bytes
 * @return STATUS_SUCCESS on success, appropriate error code otherwise
 */
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




