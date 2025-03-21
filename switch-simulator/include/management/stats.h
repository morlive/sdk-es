/**
 * @file stats.h
 * @brief Statistics collection interface for the switch simulator
 *
 * This header defines interfaces for collecting, retrieving and managing
 * statistics for ports, queues, VLANs, and other switch entities.
 */

#ifndef STATS_H
#define STATS_H

#include <stdint.h>
#include <time.h>
#include "../common/error_codes.h"
#include "../common/types.h"
#include "../hal/port.h"

/**
 * @brief Port statistics structure
 */
typedef struct {
    uint64_t rx_packets;        /**< Total received packets */
    uint64_t tx_packets;        /**< Total transmitted packets */
    uint64_t rx_bytes;          /**< Total received bytes */
    uint64_t tx_bytes;          /**< Total transmitted bytes */
    uint64_t rx_errors;         /**< Receive errors */
    uint64_t tx_errors;         /**< Transmit errors */
    uint64_t rx_drops;          /**< Dropped received packets */
    uint64_t tx_drops;          /**< Dropped transmit packets */
    uint64_t rx_unicast;        /**< Received unicast packets */
    uint64_t tx_unicast;        /**< Transmitted unicast packets */
    uint64_t rx_broadcast;      /**< Received broadcast packets */
    uint64_t tx_broadcast;      /**< Transmitted broadcast packets */
    uint64_t rx_multicast;      /**< Received multicast packets */
    uint64_t tx_multicast;      /**< Transmitted multicast packets */
    uint64_t collisions;        /**< Collision count */
    time_t last_clear;          /**< Timestamp of last counter clear */
} port_stats_t;

/**
 * @brief VLAN statistics structure
 */
typedef struct {
    uint64_t rx_packets;        /**< Received packets for this VLAN */
    uint64_t tx_packets;        /**< Transmitted packets for this VLAN */
    uint64_t rx_bytes;          /**< Received bytes for this VLAN */
    uint64_t tx_bytes;          /**< Transmitted bytes for this VLAN */
    time_t last_clear;          /**< Timestamp of last counter clear */
} vlan_stats_t;

/**
 * @brief Queue statistics structure
 */
typedef struct {
    uint64_t enqueued;          /**< Packets added to queue */
    uint64_t dequeued;          /**< Packets removed from queue */
    uint64_t dropped;           /**< Packets dropped due to queue full */
    uint64_t current_depth;     /**< Current queue depth */
    uint64_t max_depth;         /**< Maximum queue depth reached */
    time_t last_clear;          /**< Timestamp of last counter clear */
} queue_stats_t;

/**
 * @brief L3 routing statistics structure
 */
typedef struct {
    uint64_t routed_packets;    /**< Total routed packets */
    uint64_t routed_bytes;      /**< Total routed bytes */
    uint64_t routing_failures;  /**< Number of routing failures */
    uint64_t arp_requests;      /**< ARP requests sent */
    uint64_t arp_replies;       /**< ARP replies received */
    time_t last_clear;          /**< Timestamp of last counter clear */
} routing_stats_t;

/**
 * @brief Statistics context structure
 */
typedef struct stats_context_s {
    void *private_data;         /**< Private data for statistics implementation */
} stats_context_t;

/**
 * @brief Initialize statistics module
 * 
 * @param ctx Pointer to statistics context to initialize
 * @return error_code_t Error code
 */
error_code_t stats_init(stats_context_t *ctx);

/**
 * @brief Get port statistics
 * 
 * @param ctx Statistics context
 * @param port_id Port identifier
 * @param stats Pointer to statistics structure to fill
 * @return error_code_t Error code
 */
error_code_t stats_get_port(stats_context_t *ctx, port_id_t port_id, port_stats_t *stats);

/**
 * @brief Get VLAN statistics
 * 
 * @param ctx Statistics context
 * @param vlan_id VLAN identifier
 * @param stats Pointer to statistics structure to fill
 * @return error_code_t Error code
 */
error_code_t stats_get_vlan(stats_context_t *ctx, vlan_id_t vlan_id, vlan_stats_t *stats);

/**
 * @brief Get queue statistics
 * 
 * @param ctx Statistics context
 * @param port_id Port identifier
 * @param queue_id Queue identifier
 * @param stats Pointer to statistics structure to fill
 * @return error_code_t Error code
 */
error_code_t stats_get_queue(stats_context_t *ctx, port_id_t port_id, 
                             uint8_t queue_id, queue_stats_t *stats);

/**
 * @brief Get routing statistics
 * 
 * @param ctx Statistics context
 * @param stats Pointer to statistics structure to fill
 * @return error_code_t Error code
 */
error_code_t stats_get_routing(stats_context_t *ctx, routing_stats_t *stats);

/**
 * @brief Clear port statistics
 * 
 * @param ctx Statistics context
 * @param port_id Port identifier
 * @return error_code_t Error code
 */
error_code_t stats_clear_port(stats_context_t *ctx, port_id_t port_id);

/**
 * @brief Clear VLAN statistics
 * 
 * @param ctx Statistics context
 * @param vlan_id VLAN identifier
 * @return error_code_t Error code
 */
error_code_t stats_clear_vlan(stats_context_t *ctx, vlan_id_t vlan_id);

/**
 * @brief Clear queue statistics
 * 
 * @param ctx Statistics context
 * @param port_id Port identifier
 * @param queue_id Queue identifier
 * @return error_code_t Error code
 */
error_code_t stats_clear_queue(stats_context_t *ctx, port_id_t port_id, uint8_t queue_id);

/**
 * @brief Clear routing statistics
 * 
 * @param ctx Statistics context
 * @return error_code_t Error code
 */
error_code_t stats_clear_routing(stats_context_t *ctx);

/**
 * @brief Clear all statistics
 * 
 * @param ctx Statistics context
 * @return error_code_t Error code
 */
error_code_t stats_clear_all(stats_context_t *ctx);

/**
 * @brief Enable periodic statistics collection
 * 
 * @param ctx Statistics context
 * @param interval_ms Collection interval in milliseconds
 * @return error_code_t Error code
 */
error_code_t stats_enable_periodic_collection(stats_context_t *ctx, uint32_t interval_ms);

/**
 * @brief Disable periodic statistics collection
 * 
 * @param ctx Statistics context
 * @return error_code_t Error code
 */
error_code_t stats_disable_periodic_collection(stats_context_t *ctx);

/**
 * @brief Register a callback for statistics threshold events
 * 
 * @param ctx Statistics context
 * @param stat_type Type of statistic to monitor
 * @param threshold Threshold value
 * @param callback Function to call when threshold is crossed
 * @param user_data User data to pass to the callback
 * @return error_code_t Error code
 */
error_code_t stats_register_threshold_callback(stats_context_t *ctx, 
                                              const char *stat_type,
                                              uint64_t threshold,
                                              void (*callback)(void *user_data),
                                              void *user_data);

/**
 * @brief Clean up statistics resources
 * 
 * @param ctx Statistics context to clean up
 * @return error_code_t Error code
 */
error_code_t stats_cleanup(stats_context_t *ctx);

#endif /* STATS_H */
