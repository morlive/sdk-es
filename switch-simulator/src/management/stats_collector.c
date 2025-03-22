/**
 * @file stats_collector.c
 * @brief Implementation of statistics collection for the switch simulator
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "../../include/management/stats.h"
#include "../../include/common/logging.h"
#include "../../include/common/error_codes.h"
#include "../../include/hal/port.h"

#define MAX_PORTS 64
#define MAX_VLANS 4096
#define MAX_QUEUES_PER_PORT 8
#define MAX_CALLBACKS 32

/**
 * @brief Threshold callback structure
 */
typedef struct {
    char stat_type[32];
    uint64_t threshold;
    void (*callback)(void *user_data);
    void *user_data;
    bool active;
} threshold_callback_t;

/**
 * @brief Private statistics context structure
 */
typedef struct {
    port_stats_t port_stats[MAX_PORTS];
    vlan_stats_t vlan_stats[MAX_VLANS];
    queue_stats_t queue_stats[MAX_PORTS][MAX_QUEUES_PER_PORT];
    routing_stats_t routing_stats;
    
    pthread_t collection_thread;
    pthread_mutex_t stats_mutex;
    bool collection_active;
    uint32_t collection_interval_ms;
    
    threshold_callback_t callbacks[MAX_CALLBACKS];
    size_t callback_count;
} stats_private_t;

/**
 * @brief Collection thread function
 */
static void* stats_collection_thread(void *arg) {
    stats_context_t *ctx = (stats_context_t *)arg;
    stats_private_t *priv = (stats_private_t *)ctx->private_data;
    
    while (priv->collection_active) {
        // Collect statistics from hardware (simulated in this case)
        // This would normally interface with hardware counters
        
        // In a simulator, we need to get this data from the simulation engine
        // For now, we'll just have placeholder code
        
        // Example: check thresholds and trigger callbacks
        pthread_mutex_lock(&priv->stats_mutex);
        
        // Check each registered callback to see if its threshold is exceeded
        for (size_t i = 0; i < priv->callback_count; i++) {
            threshold_callback_t *cb = &priv->callbacks[i];
            if (cb->active) {
                // Example: check if the callback is for a port's rx_packets
                if (strncmp(cb->stat_type, "port_rx_packets", 15) == 0) {
                    // Extract port ID from format like "port_rx_packets_1"
                    char *port_id_str = strchr(cb->stat_type, '_');
                    if (port_id_str && (port_id_str = strchr(port_id_str + 1, '_')) &&
                        (port_id_str = strchr(port_id_str + 1, '_'))) {
                        
                        port_id_t port_id = atoi(port_id_str + 1);
                        if (port_id < MAX_PORTS && 
                            priv->port_stats[port_id].rx_packets > cb->threshold) {
                            // Threshold exceeded, call the callback
                            cb->callback(cb->user_data);
                        }
                    }
                }
                // Additional cases for other stat types would go here
            }
        }
        
        pthread_mutex_unlock(&priv->stats_mutex);
        
        // Sleep for the collection interval
        struct timespec ts;
        ts.tv_sec = priv->collection_interval_ms / 1000;
        ts.tv_nsec = (priv->collection_interval_ms % 1000) * 1000000;
        nanosleep(&ts, NULL);
    }
    
    return NULL;
}

error_code_t stats_init(stats_context_t *ctx) {
    if (!ctx) {
        return ERROR_INVALID_PARAMETER;
    }
    
    // Allocate private data
    stats_private_t *priv = (stats_private_t *)calloc(1, sizeof(stats_private_t));
    if (!priv) {
        return ERROR_OUT_OF_MEMORY;
    }
    
    // Initialize mutex
    if (pthread_mutex_init(&priv->stats_mutex, NULL) != 0) {
        free(priv);
        return ERROR_INTERNAL;
    }
    
    // Set initial timestamps for all counters
    time_t current_time = time(NULL);
    for (int i = 0; i < MAX_PORTS; i++) {
        priv->port_stats[i].last_clear = current_time;
        for (int j = 0; j < MAX_QUEUES_PER_PORT; j++) {
            priv->queue_stats[i][j].last_clear = current_time;
        }
    }
    
    for (int i = 0; i < MAX_VLANS; i++) {
        priv->vlan_stats[i].last_clear = current_time;
    }
    
    priv->routing_stats.last_clear = current_time;
    
    // Initialize collection thread parameters
    priv->collection_active = false;
    priv->collection_interval_ms = 1000; // Default 1 second
    priv->callback_count = 0;
    
    // Store private data in context
    ctx->private_data = priv;
    
    LOG_INFO("Statistics module initialized");
    return ERROR_NONE;
}

error_code_t stats_get_port(stats_context_t *ctx, port_id_t port_id, port_stats_t *stats) {
    if (!ctx || !ctx->private_data || !stats || port_id >= MAX_PORTS) {
        return ERROR_INVALID_PARAMETER;
    }
    
    stats_private_t *priv = (stats_private_t *)ctx->private_data;
    
    pthread_mutex_lock(&priv->stats_mutex);
    memcpy(stats, &priv->port_stats[port_id], sizeof(port_stats_t));
    pthread_mutex_unlock(&priv->stats_mutex);
    
    return ERROR_NONE;
}

error_code_t stats_get_vlan(stats_context_t *ctx, vlan_id_t vlan_id, vlan_stats_t *stats) {
    if (!ctx || !ctx->private_data || !stats || vlan_id >= MAX_VLANS) {
        return ERROR_INVALID_PARAMETER;
    }
    
    stats_private_t *priv = (stats_private_t *)ctx->private_data;
    
    pthread_mutex_lock(&priv->stats_mutex);
    memcpy(stats, &priv->vlan_stats[vlan_id], sizeof(vlan_stats_t));
    pthread_mutex_unlock(&priv->stats_mutex);
    
    return ERROR_NONE;
}

error_code_t stats_get_queue(stats_context_t *ctx, port_id_t port_id, 
                             uint8_t queue_id, queue_stats_t *stats) {
    if (!ctx || !ctx->private_data || !stats || 
        port_id >= MAX_PORTS || queue_id >= MAX_QUEUES_PER_PORT) {
        return ERROR_INVALID_PARAMETER;
    }
    
    stats_private_t *priv = (stats_private_t *)ctx->private_data;
    
    pthread_mutex_lock(&priv->stats_mutex);
    memcpy(stats, &priv->queue_stats[port_id][queue_id], sizeof(queue_stats_t));
    pthread_mutex_unlock(&priv->stats_mutex);
    
    return ERROR_NONE;
}

error_code_t stats_get_routing(stats_context_t *ctx, routing_stats_t *stats) {
    if (!ctx || !ctx->private_data || !stats) {
        return ERROR_INVALID_PARAMETER;
    }
    
    stats_private_t *priv = (stats_private_t *)ctx->private_data;
    
    pthread_mutex_lock(&priv->stats_mutex);
    memcpy(stats, &priv->routing_stats, sizeof(routing_stats_t));
    pthread_mutex_unlock(&priv->stats_mutex);
    
    return ERROR_NONE;
}

error_code_t stats_clear_port(stats_context_t *ctx, port_id_t port_id) {
    if (!ctx || !ctx->private_data || port_id >= MAX_PORTS) {
        return ERROR_INVALID_PARAMETER;
    }
    
    stats_private_t *priv = (stats_private_t *)ctx->private_data;
    
    pthread_mutex_lock(&priv->stats_mutex);
    memset(&priv->port_stats[port_id], 0, sizeof(port_stats_t));
    priv->port_stats[port_id].last_clear = time(NULL);
    pthread_mutex_unlock(&priv->stats_mutex);
    
    LOG_INFO("Cleared statistics for port %u", port_id);
    return ERROR_NONE;
}

error_code_t stats_clear_vlan(stats_context_t *ctx, vlan_id_t vlan_id) {
    if (!ctx || !ctx->private_data || vlan_id >= MAX_VLANS) {
        return ERROR_INVALID_PARAMETER;
    }
    
    stats_private_t *priv = (stats_private_t *)ctx->private_data;
    
    pthread_mutex_lock(&priv->stats_mutex);
    memset(&priv->vlan_stats[vlan_id], 0, sizeof(vlan_stats_t));
    priv->vlan_stats[vlan_id].last_clear = time(NULL);
    pthread_mutex_unlock(&priv->stats_mutex);
    
    LOG_INFO("Cleared statistics for VLAN %u", vlan_id);
    return ERROR_NONE;
}

error_code_t stats_clear_queue(stats_context_t *ctx, port_id_t port_id, uint8_t queue_id) {
    if (!ctx || !ctx->private_data || 
        port_id >= MAX_PORTS || queue_id >= MAX_QUEUES_PER_PORT) {
        return ERROR_INVALID_PARAMETER;
    }
    
    stats_private_t *priv = (stats_private_t *)ctx->private_data;
    
    pthread_mutex_lock(&priv->stats_mutex);
    memset(&priv->queue_stats[port_id][queue_id], 0, sizeof(queue_stats_t));
    priv->queue_stats[port_id][queue_id].last_clear = time(NULL);
    pthread_mutex_unlock(&priv->stats_mutex);
    
    LOG_INFO("Cleared statistics for queue %u on port %u", queue_id, port_id);
    return ERROR_NONE;
}

error_code_t stats_clear_routing(stats_context_t *ctx) {
    if (!ctx || !ctx->private_data) {
        return ERROR_INVALID_PARAMETER;
    }
    
    stats_private_t *priv = (stats_private_t *)ctx->private_data;
    
    pthread_mutex_lock(&priv->stats_mutex);
    memset(&priv->routing_stats, 0, sizeof(routing_stats_t));
    priv->routing_stats.last_clear = time(NULL);
    pthread_mutex_unlock(&priv->stats_mutex);
    
    LOG_INFO("Cleared routing statistics");
    return ERROR_NONE;
}

error_code_t stats_clear_all(stats_context_t *ctx) {
    if (!ctx || !ctx->private_data) {
        return ERROR_INVALID_PARAMETER;
    }
    
    stats_private_t *priv = (stats_private_t *)ctx->private_data;
    time_t current_time = time(NULL);
    
    pthread_mutex_lock(&priv->stats_mutex);
    
    // Clear port and queue stats
    for (int i = 0; i < MAX_PORTS; i++) {
        memset(&priv->port_stats[i], 0, sizeof(port_stats_t));
        priv->port_stats[i].last_clear = current_time;
        
        for (int j = 0; j < MAX_QUEUES_PER_PORT; j++) {
            memset(&priv->queue_stats[i][j], 0, sizeof(queue_stats_t));
            priv->queue_stats[i][j].last_clear = current_time;
        }
    }
    
    // Clear VLAN stats
    for (int i = 0; i < MAX_VLANS; i++) {
        memset(&priv->vlan_stats[i], 0, sizeof(vlan_stats_t));
        priv->vlan_stats[i].last_clear = current_time;
    }
    
    // Clear routing stats
    memset(&priv->routing_stats, 0, sizeof(routing_stats_t));
    priv->routing_stats.last_clear = current_time;
    
    pthread_mutex_unlock(&priv->stats_mutex);
    
    LOG_INFO("Cleared all statistics");
    return ERROR_NONE;
}

error_code_t stats_enable_periodic_collection(stats_context_t *ctx, uint32_t interval_ms) {
    if (!ctx || !ctx->private_data || interval_ms == 0) {
        return ERROR_INVALID_PARAMETER;
    }
    
    stats_private_t *priv = (stats_private_t *)ctx->private_data;
    
    // If collection is already active, stop it first
    if (priv->collection_active) {
        stats_disable_periodic_collection(ctx);
    }
    
    // Set collection parameters
    priv->collection_interval_ms = interval_ms;
    priv->collection_active = true;
    
    // Start collection thread
    if (pthread_create(&priv->collection_thread, NULL, stats_collection_thread, ctx) != 0) {
        priv->collection_active = false;
        return ERROR_INTERNAL;
    }
    
    LOG_INFO("Enabled periodic statistics collection with interval %u ms", interval_ms);
    return ERROR_NONE;
}

error_code_t stats_disable_periodic_collection(stats_context_t *ctx) {
    if (!ctx || !ctx->private_data) {
        return ERROR_INVALID_PARAMETER;
    }
    
    stats_private_t *priv = (stats_private_t *)ctx->private_data;
    
    if (priv->collection_active) {
        // Signal thread to stop
        priv->collection_active = false;
        
        // Wait for thread to terminate
        pthread_join(priv->collection_thread, NULL);
        
        LOG_INFO("Disabled periodic statistics collection");
    }
    
    return ERROR_NONE;
}

error_code_t stats_register_threshold_callback(stats_context_t *ctx, 
                                              const char *stat_type,
                                              uint64_t threshold,
                                              void (*callback)(void *user_data),
                                              void *user_data) {
    if (!ctx || !ctx->private_data || !stat_type || !callback) {
        return ERROR_INVALID_PARAMETER;
    }
    
    stats_private_t *priv = (stats_private_t *)ctx->private_data;
    
    pthread_mutex_lock(&priv->stats_mutex);
    
    // Check if we have space for another callback
    if (priv->callback_count >= MAX_CALLBACKS) {
        pthread_mutex_unlock(&priv->stats_mutex);
        return ERROR_RESOURCE_EXHAUSTED;
    }
    
    // Add the callback
    threshold_callback_t *cb = &priv->callbacks[priv->callback_count];
    strncpy(cb->stat_type, stat_type, sizeof(cb->stat_type) - 1);
    cb->stat_type[sizeof(cb->stat_type) - 1] = '\0';
    cb->threshold = threshold;
    cb->callback = callback;
    cb->user_data = user_data;
    cb->active = true;
    
    priv->callback_count++;
    
    pthread_mutex_unlock(&priv->stats_mutex);
    
    LOG_INFO("Registered threshold callback for %s with threshold %" PRIu64, stat_type, threshold);
    return ERROR_NONE;
}

error_code_t stats_cleanup(stats_context_t *ctx) {
    if (!ctx || !ctx->private_data) {
        return ERROR_INVALID_PARAMETER;
    }
    
    stats_private_t *priv = (stats_private_t *)ctx->private_data;
    
    // Stop collection thread if active
    if (priv->collection_active) {
        stats_disable_periodic_collection(ctx);
    }
    
    // Destroy mutex
    pthread_mutex_destroy(&priv->stats_mutex);
    
    // Free private data
    free(priv);
    ctx->private_data = NULL;
    
    LOG_INFO("Statistics module cleaned up");
    return ERROR_NONE;
}



