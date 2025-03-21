/**
 * @file sai_route.h
 * @brief Switch Abstraction Interface (SAI) definitions for routing functionality
 *
 * This file contains the API declarations for routing functionality including
 * virtual routers, route entries, next hops, and routing tables.
 */

#ifndef SAI_ROUTE_H
#define SAI_ROUTE_H

#include "../common/types.h"
#include "../common/error_codes.h"
#include "../l3/routing_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of routes in a virtual router
 */
#define SAI_MAX_ROUTES 16384

/**
 * @brief Maximum number of next-hops in an ECMP group
 */
#define SAI_MAX_ECMP_PATHS 64

/**
 * @brief Route entry key 
 */
typedef struct _sai_route_entry_t {
    sai_object_id_t vr_id;          /**< Virtual Router ID */
    sai_ip_prefix_t destination;    /**< IP Prefix destination */
} sai_route_entry_t;

/**
 * @brief Next hop types
 */
typedef enum _sai_next_hop_type_t {
    SAI_NEXT_HOP_TYPE_IP,          /**< IP next hop */
    SAI_NEXT_HOP_TYPE_MPLS,        /**< MPLS next hop */
    SAI_NEXT_HOP_TYPE_TUNNEL,      /**< Tunnel next hop */
    SAI_NEXT_HOP_TYPE_MAX          /**< Max next hop type */
} sai_next_hop_type_t;

/**
 * @brief Route attribute IDs
 */
typedef enum _sai_route_attr_t {
    SAI_ROUTE_ATTR_PACKET_ACTION,      /**< Packet action [sai_packet_action_t] */
    SAI_ROUTE_ATTR_NEXT_HOP_ID,        /**< Next hop or next hop group id [sai_object_id_t] */
    SAI_ROUTE_ATTR_META_DATA,          /**< Route metadata [uint32_t] */
    SAI_ROUTE_ATTR_PRIORITY,           /**< Route priority [uint8_t] */
    SAI_ROUTE_ATTR_IS_TRAP_ENABLED,    /**< Trap enabled for the route [bool] */
    SAI_ROUTE_ATTR_COUNTER_ID,         /**< Statistics counter ID [sai_object_id_t] */
    SAI_ROUTE_ATTR_END
} sai_route_attr_t;

/**
 * @brief Packet actions for routes
 */
typedef enum _sai_packet_action_t {
    SAI_PACKET_ACTION_FORWARD,       /**< Forward the packet */
    SAI_PACKET_ACTION_DROP,          /**< Drop the packet */
    SAI_PACKET_ACTION_TRAP,          /**< Trap packet to CPU */
    SAI_PACKET_ACTION_LOG            /**< Log the packet */
} sai_packet_action_t;

/**
 * @brief Route attributes
 */
typedef struct _sai_route_attr_list_t {
    uint32_t count;                 /**< Number of attributes */
    sai_attribute_t *list;          /**< Array of attributes */
} sai_route_attr_list_t;

/**
 * @brief Create a virtual router
 *
 * @param[out] vr_id Virtual router ID
 * @param[in] attr_count Number of attributes
 * @param[in] attr_list Array of attributes
 *
 * @return #SAI_STATUS_SUCCESS on success, error code on failure
 */
sai_status_t sai_create_virtual_router(
    sai_object_id_t *vr_id,
    uint32_t attr_count,
    const sai_attribute_t *attr_list);

/**
 * @brief Remove a virtual router
 *
 * @param[in] vr_id Virtual router ID
 *
 * @return #SAI_STATUS_SUCCESS on success, error code on failure
 */
sai_status_t sai_remove_virtual_router(
    sai_object_id_t vr_id);

/**
 * @brief Create a route entry
 *
 * @param[in] route_entry Route entry
 * @param[in] attr_count Number of attributes
 * @param[in] attr_list Array of attributes
 *
 * @return #SAI_STATUS_SUCCESS on success, error code on failure
 */
sai_status_t sai_create_route_entry(
    const sai_route_entry_t *route_entry,
    uint32_t attr_count,
    const sai_attribute_t *attr_list);

/**
 * @brief Remove a route entry
 *
 * @param[in] route_entry Route entry
 *
 * @return #SAI_STATUS_SUCCESS on success, error code on failure
 */
sai_status_t sai_remove_route_entry(
    const sai_route_entry_t *route_entry);

/**
 * @brief Set attribute for route entry
 *
 * @param[in] route_entry Route entry
 * @param[in] attr Attribute
 *
 * @return #SAI_STATUS_SUCCESS on success, error code on failure
 */
sai_status_t sai_set_route_entry_attribute(
    const sai_route_entry_t *route_entry,
    const sai_attribute_t *attr);

/**
 * @brief Get attribute for route entry
 *
 * @param[in] route_entry Route entry
 * @param[in] attr_count Number of attributes
 * @param[inout] attr_list Array of attributes
 *
 * @return #SAI_STATUS_SUCCESS on success, error code on failure
 */
sai_status_t sai_get_route_entry_attribute(
    const sai_route_entry_t *route_entry,
    uint32_t attr_count,
    sai_attribute_t *attr_list);

/**
 * @brief Create a next hop
 *
 * @param[out] next_hop_id Next hop ID
 * @param[in] attr_count Number of attributes
 * @param[in] attr_list Array of attributes
 *
 * @return #SAI_STATUS_SUCCESS on success, error code on failure
 */
sai_status_t sai_create_next_hop(
    sai_object_id_t *next_hop_id,
    uint32_t attr_count,
    const sai_attribute_t *attr_list);

/**
 * @brief Remove a next hop
 *
 * @param[in] next_hop_id Next hop ID
 *
 * @return #SAI_STATUS_SUCCESS on success, error code on failure
 */
sai_status_t sai_remove_next_hop(
    sai_object_id_t next_hop_id);

/**
 * @brief Create a next hop group for ECMP
 *
 * @param[out] next_hop_group_id Next hop group ID
 * @param[in] attr_count Number of attributes
 * @param[in] attr_list Array of attributes
 *
 * @return #SAI_STATUS_SUCCESS on success, error code on failure
 */
sai_status_t sai_create_next_hop_group(
    sai_object_id_t *next_hop_group_id,
    uint32_t attr_count,
    const sai_attribute_t *attr_list);

/**
 * @brief Remove a next hop group
 *
 * @param[in] next_hop_group_id Next hop group ID
 *
 * @return #SAI_STATUS_SUCCESS on success, error code on failure
 */
sai_status_t sai_remove_next_hop_group(
    sai_object_id_t next_hop_group_id);

/**
 * @brief Add next hop to a next hop group
 *
 * @param[in] next_hop_group_id Next hop group ID
 * @param[in] next_hop_id Next hop ID to add
 *
 * @return #SAI_STATUS_SUCCESS on success, error code on failure
 */
sai_status_t sai_add_next_hop_to_group(
    sai_object_id_t next_hop_group_id,
    sai_object_id_t next_hop_id);

/**
 * @brief Remove next hop from a next hop group
 *
 * @param[in] next_hop_group_id Next hop group ID
 * @param[in] next_hop_id Next hop ID to remove
 *
 * @return #SAI_STATUS_SUCCESS on success, error code on failure
 */
sai_status_t sai_remove_next_hop_from_group(
    sai_object_id_t next_hop_group_id,
    sai_object_id_t next_hop_id);

/**
 * @brief Get route entry statistics
 *
 * @param[in] route_entry Route entry
 * @param[in] counter_ids List of counter IDs
 * @param[in] number_of_counters Number of counters in the list
 * @param[out] counters List of resulting counter values
 *
 * @return #SAI_STATUS_SUCCESS on success, error code on failure
 */
sai_status_t sai_get_route_stats(
    const sai_route_entry_t *route_entry,
    const sai_stat_id_t *counter_ids,
    uint32_t number_of_counters,
    uint64_t *counters);

/**
 * @brief Clear route entry statistics
 *
 * @param[in] route_entry Route entry
 * @param[in] counter_ids List of counter IDs
 * @param[in] number_of_counters Number of counters in the list
 *
 * @return #SAI_STATUS_SUCCESS on success, error code on failure
 */
sai_status_t sai_clear_route_stats(
    const sai_route_entry_t *route_entry,
    const sai_stat_id_t *counter_ids,
    uint32_t number_of_counters);

/**
 * @brief Route methods table
 */
typedef struct _sai_route_api_t {
    sai_create_virtual_router_fn         create_virtual_router;
    sai_remove_virtual_router_fn         remove_virtual_router;
    sai_create_route_entry_fn            create_route_entry;
    sai_remove_route_entry_fn            remove_route_entry;
    sai_set_route_entry_attribute_fn     set_route_entry_attribute;
    sai_get_route_entry_attribute_fn     get_route_entry_attribute;
    sai_create_next_hop_fn               create_next_hop;
    sai_remove_next_hop_fn               remove_next_hop;
    sai_create_next_hop_group_fn         create_next_hop_group;
    sai_remove_next_hop_group_fn         remove_next_hop_group;
    sai_add_next_hop_to_group_fn         add_next_hop_to_group;
    sai_remove_next_hop_from_group_fn    remove_next_hop_from_group;
    sai_get_route_stats_fn               get_route_stats;
    sai_clear_route_stats_fn             clear_route_stats;
} sai_route_api_t;

#ifdef __cplusplus
}
#endif

#endif /* SAI_ROUTE_H */
