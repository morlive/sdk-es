/**
 * @file packet.c
 * @brief Packet processing implementation for switch simulator
 */

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "../../include/hal/packet.h"
#include "../../include/common/logging.h"

/* Forward declarations for hardware simulation functions */
extern status_t hw_sim_receive_packet(port_id_t port_id, packet_buffer_t *packet);
extern status_t hw_sim_transmit_packet(packet_buffer_t *packet, port_id_t port_id);

/* Private structures */

/**
 * @brief Packet processor registration record
 */
typedef struct processor_node {
    packet_process_cb_t callback;
    uint32_t priority;
    void *user_data;
    uint32_t handle;
    struct processor_node *next;
} processor_node_t;

/* Private variables */
static bool g_packet_initialized = false;
static processor_node_t *g_processor_list = NULL;
static uint32_t g_next_handle = 1;
static pthread_mutex_t g_packet_mutex;

/* Private functions */
static void sort_processor_list(void);
static status_t execute_processor_chain(packet_buffer_t *packet);

/**
 * @brief Initialize packet processing subsystem
 * 
 * @return status_t STATUS_SUCCESS if successful
 */
status_t packet_init(void)
{
    LOG_INFO(LOG_CATEGORY_HAL, "Initializing packet processing subsystem");
    
    if (g_packet_initialized) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Packet processing subsystem already initialized");
        return STATUS_SUCCESS;
    }
    
    /* Initialize mutex */
    if (pthread_mutex_init(&g_packet_mutex, NULL) != 0) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to initialize packet processing mutex");
        return STATUS_FAILURE;
    }
    
    g_processor_list = NULL;
    g_next_handle = 1;
    
    g_packet_initialized = true;
    LOG_INFO(LOG_CATEGORY_HAL, "Packet processing subsystem initialized successfully");
    
    return STATUS_SUCCESS;
}

/**
 * @brief Shutdown packet processing subsystem
 * 
 * @return status_t STATUS_SUCCESS if successful
 */
status_t packet_shutdown(void)
{
    LOG_INFO(LOG_CATEGORY_HAL, "Shutting down packet processing subsystem");
    
    if (!g_packet_initialized) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return STATUS_SUCCESS;
    }
    
    pthread_mutex_lock(&g_packet_mutex);
    
    /* Free all processor nodes */
    processor_node_t *node = g_processor_list;
    while (node) {
        processor_node_t *next = node->next;
        free(node);
        node = next;
    }
    
    g_processor_list = NULL;
    g_packet_initialized = false;
    
    pthread_mutex_unlock(&g_packet_mutex);
    pthread_mutex_destroy(&g_packet_mutex);
    
    LOG_INFO(LOG_CATEGORY_HAL, "Packet processing subsystem shutdown successfully");
    
    return STATUS_SUCCESS;
}

/**
 * @brief Allocate a new packet buffer
 * 
 * @param size Initial size of packet data
 * @return packet_buffer_t* Newly allocated packet buffer or NULL if failed
 */
packet_buffer_t* packet_buffer_alloc(uint32_t size)
{
    if (!g_packet_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return NULL;
    }
    
    if (size > MAX_PACKET_SIZE) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Requested packet size %u exceeds maximum (%u)", 
                 size, MAX_PACKET_SIZE);
        return NULL;
    }
    
    /* Allocate packet buffer structure */
    packet_buffer_t *packet = (packet_buffer_t *)malloc(sizeof(packet_buffer_t));
    if (!packet) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to allocate packet buffer structure");
        return NULL;
    }
    
    /* Allocate data buffer */
    packet->data = (uint8_t *)malloc(size);
    if (!packet->data) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to allocate packet data buffer of size %u", size);
        free(packet);
        return NULL;
    }
    
    /* Initialize packet buffer */
    packet->size = size;
    packet->capacity = size;
    packet->user_data = NULL;
    memset(&packet->metadata, 0, sizeof(packet_metadata_t));
    memset(packet->data, 0, size);
    
    LOG_DEBUG(LOG_CATEGORY_HAL, "Allocated packet buffer of size %u", size);
    
    return packet;
}

/**
 * @brief Free a previously allocated packet buffer
 * 
 * @param packet Packet buffer to free
 */
void packet_buffer_free(packet_buffer_t *packet)
{
    if (!packet) {
        return;
    }
    
    if (packet->data) {
        free(packet->data);
    }
    
    free(packet);
    
    LOG_DEBUG(LOG_CATEGORY_HAL, "Freed packet buffer");
}

/**
 * @brief Clone a packet buffer
 * 
 * @param packet Source packet buffer
 * @return packet_buffer_t* New copy of packet buffer or NULL if failed
 */
packet_buffer_t* packet_buffer_clone(const packet_buffer_t *packet)
{
    if (!g_packet_initialized) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Packet processing subsystem not initialized");
        return NULL;
    }
    
    if (!packet) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Cannot clone NULL packet");
        return NULL;
    }
    
    /* Allocate new packet buffer */
    packet_buffer_t *clone = packet_buffer_alloc(packet->capacity);
    if (!clone) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to allocate memory for packet clone");
        return NULL;
    }
    
    /* Copy data and metadata */
    memcpy(clone->data, packet->data, packet->size);
    clone->size = packet->size;
    clone->metadata = packet->metadata;
    
    LOG_DEBUG(LOG_CATEGORY_HA
