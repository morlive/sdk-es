/**
 * @file sai_vlan.h
 * @brief Switch Abstraction Interface (SAI) for VLAN management
 *
 * This file defines the SAI interface for VLAN operations including VLAN creation,
 * deletion, member management, and attribute configuration. It provides an abstraction
 * layer between the application and the underlying hardware for VLAN functionality.
 */

#ifndef SAI_VLAN_H
#define SAI_VLAN_H

#include "../common/types.h"
#include "../common/error_codes.h"
#include "sai_port.h"

/**
 * @brief Maximum number of VLANs supported by the switch
 */
#define SAI_MAX_VLANS 4094

/**
 * @brief VLAN ID type
 */
typedef uint16_t sai_vlan_id_t;

/**
 * @brief VLAN member tagging modes
 */
typedef enum sai_vlan_tagging_mode_e {
    SAI_VLAN_TAGGING_MODE_UNTAGGED,    /**< Untagged mode (strip VLAN tag) */
    SAI_VLAN_TAGGING_MODE_TAGGED,      /**< Tagged mode (keep VLAN tag) */
    SAI_VLAN_TAGGING_MODE_PRIORITY     /**< Priority tagged (tag with VLAN 0) */
} sai_vlan_tagging_mode_t;

/**
 * @brief VLAN attribute IDs
 */
typedef enum sai_vlan_attr_id_e {
    SAI_VLAN_ATTR_MAX_LEARNED_ADDRESSES,  /**< Maximum MAC addresses that can be learned on this VLAN [uint32_t] */
    SAI_VLAN_ATTR_STP_INSTANCE,           /**< STP instance ID [uint32_t] */
    SAI_VLAN_ATTR_LEARN_DISABLE,          /**< Disable MAC learning on this VLAN [bool] */
    SAI_VLAN_ATTR_IPV4_MCAST_LOOKUP_KEY,  /**< IPv4 multicast lookup key type [uint32_t] */
    SAI_VLAN_ATTR_IPV6_MCAST_LOOKUP_KEY,  /**< IPv6 multicast lookup key type [uint32_t] */
    SAI_VLAN_ATTR_UNKNOWN_UNICAST_FLOOD,  /**< Control for unknown unicast flood [bool] */
    SAI_VLAN_ATTR_UNKNOWN_MCAST_FLOOD,    /**< Control for unknown multicast flood [bool] */
    SAI_VLAN_ATTR_BROADCAST_FLOOD,        /**< Control for broadcast flood [bool] */
    SAI_VLAN_ATTR_PORT_LIST,              /**< List of ports in VLAN [sai_port_id_t array] */
    SAI_VLAN_ATTR_MEMBER_COUNT,           /**< Number of ports in VLAN (READ-ONLY) [uint32_t] */
    SAI_VLAN_ATTR_MAX                     /**< Max attribute ID */
} sai_vlan_attr_id_t;

/**
 * @brief VLAN attribute structure
 */
typedef struct sai_vlan_attr_s {
    sai_vlan_attr_id_t id;  /**< Attribute ID */
    union {
        uint32_t max_learned_addresses;    /**< Maximum learnable MAC addresses */
        uint32_t stp_instance;             /**< STP instance ID */
        bool learn_disable;                /**< MAC learning disable flag */
        uint32_t ipv4_mcast_lookup_key;    /**< IPv4 multicast lookup key type */
        uint32_t ipv6_mcast_lookup_key;    /**< IPv6 multicast lookup key type */
        bool unknown_unicast_flood;        /**< Unknown unicast flood control */
        bool unknown_mcast_flood;          /**< Unknown multicast flood control */
        bool broadcast_flood;              /**< Broadcast flood control */
        struct {
            uint32_t count;                /**< Number of ports */
            sai_port_id_t *list;           /**< List of port IDs */
        } port_list;                       /**< List of ports in VLAN */
        uint32_t member_count;             /**< Number of ports in VLAN */
    } value;                             /**< Attribute value */
} sai_vlan_attr_t;

/**
 * @brief VLAN member attribute IDs
 */
typedef enum sai_vlan_member_attr_id_e {
    SAI_VLAN_MEMBER_ATTR_VLAN_ID,       /**< VLAN ID [sai_vlan_id_t] */
    SAI_VLAN_MEMBER_ATTR_PORT_ID,       /**< Port ID [sai_port_id_t] */
    SAI_VLAN_MEMBER_ATTR_TAGGING_MODE,  /**< Tagging mode [sai_vlan_tagging_mode_t] */
    SAI_VLAN_MEMBER_ATTR_MAX            /**< Max attribute ID */
} sai_vlan_member_attr_id_t;

/**
 * @brief VLAN member attribute structure
 */
typedef struct sai_vlan_member_attr_s {
    sai_vlan_member_attr_id_t id;  /**< Attribute ID */
    union {
        sai_vlan_id_t vlan_id;              /**< VLAN ID */
        sai_port_id_t port_id;              /**< Port ID */
        sai_vlan_tagging_mode_t tagging_mode; /**< Tagging mode */
    } value;                              /**< Attribute value */
} sai_vlan_member_attr_t;

/**
 * @brief VLAN member ID type
 */
typedef uint32_t sai_vlan_member_id_t;

/**
 * @brief VLAN statistics counter IDs
 */
typedef enum sai_vlan_stat_counter_id_e {
    SAI_VLAN_STAT_IN_OCTETS,           /**< Ingress octets counter */
    SAI_VLAN_STAT_IN_PACKETS,          /**< Ingress packets counter */
    SAI_VLAN_STAT_IN_UCAST_PKTS,       /**< Ingress unicast packets counter */
    SAI_VLAN_STAT_IN_NON_UCAST_PKTS,   /**< Ingress non-unicast packets counter */
    SAI_VLAN_STAT_IN_DISCARDS,         /**< Ingress discarded packets counter */
    SAI_VLAN_STAT_IN_ERRORS,           /**< Ingress error packets counter */
    SAI_VLAN_STAT_OUT_OCTETS,          /**< Egress octets counter */
    SAI_VLAN_STAT_OUT_PACKETS,         /**< Egress packets counter */
    SAI_VLAN_STAT_OUT_UCAST_PKTS,      /**< Egress unicast packets counter */
    SAI_VLAN_STAT_OUT_NON_UCAST_PKTS,  /**< Egress non-unicast packets counter */
    SAI_VLAN_STAT_OUT_DISCARDS,        /**< Egress discarded packets counter */
    SAI_VLAN_STAT_OUT_ERRORS           /**< Egress error packets counter */
} sai_vlan_stat_counter_id_t;

/**
 * @brief Initialize the SAI VLAN module
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_vlan_init(void);

/**
 * @brief Create a VLAN
 *
 * @param[in] vlan_id     VLAN ID to create
 * @param[in] attr_count  Number of attributes
 * @param[in] attr_list   Array of attributes for VLAN creation
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_create_vlan(
    sai_vlan_id_t vlan_id,
    uint32_t attr_count,
    const sai_vlan_attr_t *attr_list
);

/**
 * @brief Remove a VLAN
 *
 * @param[in] vlan_id    VLAN ID to be removed
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_remove_vlan(
    sai_vlan_id_t vlan_id
);

/**
 * @brief Set VLAN attribute
 *
 * @param[in] vlan_id    VLAN ID
 * @param[in] attr       Attribute to be set
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_set_vlan_attribute(
    sai_vlan_id_t vlan_id,
    const sai_vlan_attr_t *attr
);

/**
 * @brief Get VLAN attribute
 *
 * @param[in]  vlan_id     VLAN ID
 * @param[in]  attr_count  Number of attributes
 * @param[out] attr_list   Array of attributes to be retrieved
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_get_vlan_attributes(
    sai_vlan_id_t vlan_id,
    uint32_t attr_count,
    sai_vlan_attr_t *attr_list
);

/**
 * @brief Get VLAN statistics
 *
 * @param[in]  vlan_id        VLAN ID
 * @param[in]  counter_ids    Array of counter IDs to retrieve
 * @param[in]  counter_count  Number of counters in the array
 * @param[out] counters       Array to store counter values
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_get_vlan_stats(
    sai_vlan_id_t vlan_id,
    const sai_vlan_stat_counter_id_t *counter_ids,
    uint32_t counter_count,
    uint64_t *counters
);

/**
 * @brief Clear VLAN statistics
 *
 * @param[in] vlan_id        VLAN ID
 * @param[in] counter_ids    Array of counter IDs to clear
 * @param[in] counter_count  Number of counters in the array
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_clear_vlan_stats(
    sai_vlan_id_t vlan_id,
    const sai_vlan_stat_counter_id_t *counter_ids,
    uint32_t counter_count
);

/**
 * @brief Create a VLAN member
 *
 * @param[out] vlan_member_id  Pointer to store the created VLAN member ID
 * @param[in]  attr_count      Number of attributes
 * @param[in]  attr_list       Array of attributes for VLAN member creation
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_create_vlan_member(
    sai_vlan_member_id_t *vlan_member_id,
    uint32_t attr_count,
    const sai_vlan_member_attr_t *attr_list
);

/**
 * @brief Remove a VLAN member
 *
 * @param[in] vlan_member_id  VLAN member ID to be removed
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_remove_vlan_member(
    sai_vlan_member_id_t vlan_member_id
);

/**
 * @brief Set VLAN member attribute
 *
 * @param[in] vlan_member_id  VLAN member ID
 * @param[in] attr            Attribute to be set
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_set_vlan_member_attribute(
    sai_vlan_member_id_t vlan_member_id,
    const sai_vlan_member_attr_t *attr
);

/**
 * @brief Get VLAN member attribute
 *
 * @param[in]  vlan_member_id  VLAN member ID
 * @param[in]  attr_count      Number of attributes
 * @param[out] attr_list       Array of attributes to be retrieved
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_get_vlan_member_attributes(
    sai_vlan_member_id_t vlan_member_id,
    uint32_t attr_count,
    sai_vlan_member_attr_t *attr_list
);

/**
 * @brief Get VLAN members of a specific VLAN
 *
 * @param[in]  vlan_id             VLAN ID
 * @param[out] vlan_member_count   Pointer to store the number of VLAN members
 * @param[out] vlan_member_list    Array to store VLAN member IDs
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_get_vlan_members(
    sai_vlan_id_t vlan_id,
    uint32_t *vlan_member_count,
    sai_vlan_member_id_t *vlan_member_list
);

/**
 * @brief Get VLAN member by port ID and VLAN ID
 *
 * @param[in]  vlan_id         VLAN ID
 * @param[in]  port_id         Port ID
 * @param[out] vlan_member_id  Pointer to store VLAN member ID
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_get_vlan_member_by_port(
    sai_vlan_id_t vlan_id,
    sai_port_id_t port_id,
    sai_vlan_member_id_t *vlan_member_id
);

/**
 * @brief Get all VLANs configured on the switch
 *
 * @param[out] vlan_count  Pointer to store the number of VLANs
 * @param[out] vlan_list   Array to store VLAN IDs
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_get_vlan_list(
    uint32_t *vlan_count,
    sai_vlan_id_t *vlan_list
);

#endif /* SAI_VLAN_H */
