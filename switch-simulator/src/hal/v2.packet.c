/**
 * @file packet.c
 * @brief Packet processing implementation for switch simulator
 */
#include <stdlib.h>
#include <string.h>
#include "../include/hal/packet.h"
#include "../include/common/logging.h"

// Structure to hold registered packet processors
typedef struct packet_processor {
    packet_process_cb_t callback;
    uint32_t priority;
    void *user_data;
    uint32_t handle;
    struct packet_processor *next;
} packet_processor_t;

// Global packet processing state
static packet_processor_t *processors = NULL;
static uint32_t next_handle = 1;
static bool packet_initialized = false;

status_t packet_init(void) {
    if (packet_initialized) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Packet processing subsystem already initialized");
        return STATUS_SUCCESS;
    }

    LOG_INFO(LOG_CATEGORY_HAL, "Initializing packet processing subsystem");
    
    // No additional initialization needed at this point
    packet_initialized = true;
    
    LOG_INFO(LOG_CATEGORY_HAL, "Packet processing subsystem initialized successfully");
    return STATUS_SUCCESS;
}

status_t packet_shutdown(void) {
    if (!packet_initialized) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    LOG_INFO(LOG_CATEGORY_HAL, "Shutting down packet processing subsystem");
    
    // Free all registered processors
    packet_processor_t *current = processors;
    while (current != NULL) {
        packet_processor_t *next = current->next;
        free(current);
        current = next;
    }
    
    processors = NULL;
    next_handle = 1;
    packet_initialized = false;
    
    LOG_INFO(LOG_CATEGORY_HAL, "Packet processing subsystem shut down successfully");
    return STATUS_SUCCESS;
}

packet_buffer_t* packet_buffer_alloc(uint32_t size) {
    if (!packet_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return NULL;
    }
    
    if (size > MAX_PACKET_SIZE) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Requested packet size %u exceeds maximum %u",
                 size, MAX_PACKET_SIZE);
        return NULL;
    }
    
    // Allocate packet buffer structure
    packet_buffer_t *packet = (packet_buffer_t *)calloc(1, sizeof(packet_buffer_t));
    if (packet == NULL) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to allocate packet buffer structure");
        return NULL;
    }
    
    // Allocate packet data
    packet->data = (uint8_t *)malloc(size);
    if (packet->data == NULL) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to allocate packet data buffer");
        free(packet);
        return NULL;
    }
    
    packet->size = size;
    packet->capacity = size;
    
    LOG_DEBUG(LOG_CATEGORY_HAL, "Allocated packet buffer of size %u", size);
    return packet;
}

void packet_buffer_free(packet_buffer_t *packet) {
    if (packet == NULL) {
        return;
    }
    
    // Free packet data
    if (packet->data != NULL) {
        free(packet->data);
    }
    
    // Free packet structure
    free(packet);
    
    LOG_DEBUG(LOG_CATEGORY_HAL, "Freed packet buffer");
}

packet_buffer_t* packet_buffer_clone(const packet_buffer_t *packet) {
    if (!packet_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return NULL;
    }
    
    if (packet == NULL) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid parameter: packet is NULL");
        return NULL;
    }
    
    // Allocate new packet buffer
    packet_buffer_t *clone = packet_buffer_alloc(packet->capacity);
    if (clone == NULL) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to allocate memory for packet clone");
        return NULL;
    }
    
    // Copy data
    memcpy(clone->data, packet->data, packet->size);
    clone->size = packet->size;
    
    // Copy metadata
    clone->metadata = packet->metadata;
    
    // User data is not copied
    clone->user_data = NULL;
    
    LOG_DEBUG(LOG_CATEGORY_HAL, "Cloned packet buffer of size %u", packet->size);
    return clone;
}

status_t packet_buffer_resize(packet_buffer_t *packet, uint32_t new_size) {
    if (!packet_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (packet == NULL) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid parameter: packet is NULL");
        return STATUS_INVALID_PARAMETER;
    }
    
    if (new_size > MAX_PACKET_SIZE) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Requested size %u exceeds maximum %u",
                 new_size, MAX_PACKET_SIZE);
        return STATUS_INVALID_PARAMETER;
    }
    
    // If new size is within current capacity, just update size
    if (new_size <= packet->capacity) {
        packet->size = new_size;
        return STATUS_SUCCESS;
    }
    
    // Resize data buffer
    uint8_t *new_data = (uint8_t *)realloc(packet->data, new_size);
    if (new_data == NULL) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to resize packet buffer");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    packet->data = new_data;
    packet->size = new_size;
    packet->capacity = new_size;
    
    LOG_DEBUG(LOG_CATEGORY_HAL, "Resized packet buffer to %u bytes", new_size);
    return STATUS_SUCCESS;
}

status_t packet_register_processor(packet_process_cb_t callback, 
                                  uint32_t priority, 
                                  void *user_data, 
                                  uint32_t *handle_out) {
    if (!packet_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (callback == NULL || handle_out == NULL) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid parameters");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Allocate new processor
    packet_processor_t *processor = (packet_processor_t *)malloc(sizeof(packet_processor_t));
    if (processor == NULL) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to allocate memory for packet processor");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Initialize processor
    processor->callback = callback;
    processor->priority = priority;
    processor->user_data = user_data;
    processor->handle = next_handle++;
    processor->next = NULL;
    
    // Insert in priority order (lower numbers = higher priority)
    if (processors == NULL || processors->priority > priority) {
        // Insert at head
        processor->next = processors;
        processors = processor;
    } else {
        // Find insertion point
        packet_processor_t *current = processors;
        while (current->next != NULL && current->next->priority <= priority) {
            current = current->next;
        }
        
        // Insert after current
        processor->next = current->next;
        current->next = processor;
    }
    
    *handle_out = processor->handle;
    
    LOG_INFO(LOG_CATEGORY_HAL, "Registered packet processor with handle %u and priority %u",
            processor->handle, priority);
    return STATUS_SUCCESS;
}

status_t packet_unregister_processor(uint32_t handle) {
    if (!packet_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (processors == NULL) {
        LOG_ERROR(LOG_CATEGORY_HAL, "No processors registered");
        return STATUS_NOT_FOUND;
    }
    
    // Check if first processor matches
    if (processors->handle == handle) {
        packet_processor_t *to_remove = processors;
        processors = processors->next;
        free(to_remove);
        
        LOG_INFO(LOG_CATEGORY_HAL, "Unregistered packet processor with handle %u", handle);
        return STATUS_SUCCESS;
    }
    
    // Find processor in list
    packet_processor_t *current = processors;
    while (current->next != NULL) {
        if (current->next->handle == handle) {
            packet_processor_t *to_remove = current->next;
            current->next = to_remove->next;
            free(to_remove);
            
            LOG_INFO(LOG_CATEGORY_HAL, "Unregistered packet processor with handle %u", handle);
            return STATUS_SUCCESS;
        }
        current = current->next;
    }
    
    LOG_ERROR(LOG_CATEGORY_HAL, "Processor with handle %u not found", handle);
    return STATUS_NOT_FOUND;
}

packet_result_t packet_process(packet_buffer_t *packet) {
    if (!packet_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return PACKET_RESULT_DROP;
    }
    
    if (packet == NULL) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid parameter: packet is NULL");
        return PACKET_RESULT_DROP;
    }
    
    // Default result if no processors are registered
    packet_result_t result = PACKET_RESULT_FORWARD;
    
    // Process packet through all registered processors
    packet_processor_t *current = processors;
    while (current != NULL) {
        result = current->callback(packet, current->user_data);
        
        // If processor consumes or drops the packet, stop processing
        if (result == PACKET_RESULT_CONSUME || result == PACKET_RESULT_DROP) {
            break;
        }
        
        current = current->next;
    }
    
    return result;
}

status_t packet_inject(packet_buffer_t *packet) {
    if (!packet_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (packet == NULL) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Invalid parameter: packet is NULL");
        return STATUS_INVALID_PARAMETER;
