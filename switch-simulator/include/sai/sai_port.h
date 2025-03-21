/**
 * @file sai_port.h
 * @brief Switch Abstraction Interface (SAI) for port management
 *
 * This file defines the SAI interface for port operations including port creation,
 * deletion, configuration, and status retrieval. It provides an abstraction layer
 * between the application and the underlying hardware.
 */

#ifndef SAI_PORT_H
#define SAI_PORT_H

#include "../common/types.h"
#include "../common/error_codes.h"

/**
 * @brief Maximum number of ports supported by the switch
 */
#define SAI_MAX_PORTS 64

/**
 * @brief Port operational states
 */
typedef enum sai_port_oper_status_e {
    SAI_PORT_OPER_STATUS_UNKNOWN,       /**< Port operational state unknown */
    SAI_PORT_OPER_STATUS_UP,            /**< Port is operationally up */
    SAI_PORT_OPER_STATUS_DOWN,          /**< Port is operationally down */
    SAI_PORT_OPER_STATUS_TESTING,       /**< Port is in testing mode */
    SAI_PORT_OPER_STATUS_NOT_PRESENT    /**< Port is not present */
} sai_port_oper_status_t;

/**
 * @brief Port administrative states
 */
typedef enum sai_port_admin_state_e {
    SAI_PORT_ADMIN_STATE_UNKNOWN,       /**< Port administrative state unknown */
    SAI_PORT_ADMIN_STATE_UP,            /**< Port is administratively enabled */
    SAI_PORT_ADMIN_STATE_DOWN           /**< Port is administratively disabled */
} sai_port_admin_state_t;

/**
 * @brief Port speed modes in Mbps
 */
typedef enum sai_port_speed_e {
    SAI_PORT_SPEED_10,          /**< 10 Mbps */
    SAI_PORT_SPEED_100,         /**< 100 Mbps */
    SAI_PORT_SPEED_1000,        /**< 1 Gbps */
    SAI_PORT_SPEED_10000,       /**< 10 Gbps */
    SAI_PORT_SPEED_25000,       /**< 25 Gbps */
    SAI_PORT_SPEED_40000,       /**< 40 Gbps */
    SAI_PORT_SPEED_100000,      /**< 100 Gbps */
    SAI_PORT_SPEED_UNKNOWN      /**< Unknown speed */
} sai_port_speed_t;

/**
 * @brief Port duplex modes
 */
typedef enum sai_port_duplex_e {
    SAI_PORT_DUPLEX_HALF,       /**< Half duplex mode */
    SAI_PORT_DUPLEX_FULL,       /**< Full duplex mode */
    SAI_PORT_DUPLEX_UNKNOWN     /**< Unknown duplex mode */
} sai_port_duplex_t;

/**
 * @brief Port flow control modes
 */
typedef enum sai_port_flow_control_e {
    SAI_PORT_FLOW_CONTROL_DISABLE,         /**< Flow control disabled */
    SAI_PORT_FLOW_CONTROL_TX_ONLY,         /**< Transmit pause frames only */
    SAI_PORT_FLOW_CONTROL_RX_ONLY,         /**< Receive pause frames only */
    SAI_PORT_FLOW_CONTROL_BOTH_ENABLE      /**< Both transmit and receive pause frames */
} sai_port_flow_control_t;

/**
 * @brief Port media type
 */
typedef enum sai_port_media_type_e {
    SAI_PORT_MEDIA_TYPE_UNKNOWN,        /**< Unknown media type */
    SAI_PORT_MEDIA_TYPE_FIBER,          /**< Fiber media */
    SAI_PORT_MEDIA_TYPE_COPPER          /**< Copper media */
} sai_port_media_type_t;

/**
 * @brief Port type
 */
typedef enum sai_port_type_e {
    SAI_PORT_TYPE_LOGICAL,              /**< Logical port */
    SAI_PORT_TYPE_CPU,                  /**< CPU port */
    SAI_PORT_TYPE_FABRIC                /**< Fabric port */
} sai_port_type_t;

/**
 * @brief Port statistics counter IDs
 */
typedef enum sai_port_stat_counter_id_e {
    SAI_PORT_STAT_IF_IN_OCTETS,            /**< Ingress octets counter */
    SAI_PORT_STAT_IF_IN_UCAST_PKTS,        /**< Ingress unicast packets counter */
    SAI_PORT_STAT_IF_IN_NON_UCAST_PKTS,    /**< Ingress non-unicast packets counter */
    SAI_PORT_STAT_IF_IN_DISCARDS,          /**< Ingress discarded packets counter */
    SAI_PORT_STAT_IF_IN_ERRORS,            /**< Ingress error packets counter */
    SAI_PORT_STAT_IF_OUT_OCTETS,           /**< Egress octets counter */
    SAI_PORT_STAT_IF_OUT_UCAST_PKTS,       /**< Egress unicast packets counter */
    SAI_PORT_STAT_IF_OUT_NON_UCAST_PKTS,   /**< Egress non-unicast packets counter */
    SAI_PORT_STAT_IF_OUT_DISCARDS,         /**< Egress discarded packets counter */
    SAI_PORT_STAT_IF_OUT_ERRORS            /**< Egress error packets counter */
} sai_port_stat_counter_id_t;

/**
 * @brief Port attribute ID
 */
typedef enum sai_port_attr_id_e {
    SAI_PORT_ATTR_TYPE,                   /**< Port type (READ-ONLY) [sai_port_type_t] */
    SAI_PORT_ATTR_OPER_STATUS,            /**< Port operational state (READ-ONLY) [sai_port_oper_status_t] */
    SAI_PORT_ATTR_ADMIN_STATE,            /**< Port admin state [sai_port_admin_state_t] */
    SAI_PORT_ATTR_MEDIA_TYPE,             /**< Media Type [sai_port_media_type_t] */
    SAI_PORT_ATTR_SPEED,                  /**< Port speed in Mbps [sai_port_speed_t] */
    SAI_PORT_ATTR_DUPLEX,                 /**< Port duplex mode [sai_port_duplex_t] */
    SAI_PORT_ATTR_AUTO_NEG_MODE,          /**< Auto negotiation mode [bool] */
    SAI_PORT_ATTR_FLOW_CONTROL_MODE,      /**< Flow control mode [sai_port_flow_control_t] */
    SAI_PORT_ATTR_MTU,                    /**< Maximum Transmission Unit [uint32_t] */
    SAI_PORT_ATTR_HW_LANE_LIST,           /**< Hardware lane list [uint32_t list] */
    SAI_PORT_ATTR_INTERNAL_LOOPBACK_MODE, /**< Internal loopback mode [bool] */
    SAI_PORT_ATTR_FEC_MODE,               /**< Forward Error Correction mode [bool] */
    SAI_PORT_ATTR_MAX                     /**< Max attribute ID */
} sai_port_attr_id_t;

/**
 * @brief Port attribute structure
 */
typedef struct sai_port_attr_s {
    sai_port_attr_id_t id;        /**< Attribute ID */
    union {
        sai_port_type_t type;                /**< Port type */
        sai_port_oper_status_t oper_status;  /**< Port operational status */
        sai_port_admin_state_t admin_state;  /**< Port administrative state */
        sai_port_media_type_t media_type;    /**< Port media type */
        sai_port_speed_t speed;              /**< Port speed */
        sai_port_duplex_t duplex;            /**< Port duplex mode */
        bool auto_neg;                       /**< Auto-negotiation mode */
        sai_port_flow_control_t flow_control;/**< Flow control mode */
        uint32_t mtu;                        /**< Maximum Transmission Unit */
        struct {
            uint32_t count;                  /**< Number of hardware lanes */
            uint32_t *list;                  /**< List of hardware lanes */
        } hw_lane_list;                      /**< Hardware lane list */
        bool internal_loopback;              /**< Internal loopback mode */
        bool fec_mode;                       /**< Forward Error Correction mode */
    } value;                                 /**< Attribute value */
} sai_port_attr_t;

/**
 * @brief Port counter structure
 */
typedef struct sai_port_counters_s {
    uint64_t if_in_octets;         /**< Ingress octets counter */
    uint64_t if_in_ucast_pkts;     /**< Ingress unicast packets counter */
    uint64_t if_in_non_ucast_pkts; /**< Ingress non-unicast packets counter */
    uint64_t if_in_discards;       /**< Ingress discarded packets counter */
    uint64_t if_in_errors;         /**< Ingress error packets counter */
    uint64_t if_out_octets;        /**< Egress octets counter */
    uint64_t if_out_ucast_pkts;    /**< Egress unicast packets counter */
    uint64_t if_out_non_ucast_pkts;/**< Egress non-unicast packets counter */
    uint64_t if_out_discards;      /**< Egress discarded packets counter */
    uint64_t if_out_errors;        /**< Egress error packets counter */
} sai_port_counters_t;

/**
 * @brief Port ID type
 */
typedef uint32_t sai_port_id_t;

/**
 * @brief Initialize the SAI port module
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_port_init(void);

/**
 * @brief Create a port
 *
 * @param[out] port_id       Pointer to store the created port ID
 * @param[in]  attr_count    Number of attributes
 * @param[in]  attr_list     Array of attributes for port creation
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_create_port(
    sai_port_id_t *port_id,
    uint32_t attr_count,
    const sai_port_attr_t *attr_list
);

/**
 * @brief Remove a port
 *
 * @param[in] port_id    Port ID to be removed
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_remove_port(
    sai_port_id_t port_id
);

/**
 * @brief Set port attribute
 *
 * @param[in] port_id    Port ID
 * @param[in] attr       Attribute to be set
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_set_port_attribute(
    sai_port_id_t port_id,
    const sai_port_attr_t *attr
);

/**
 * @brief Get port attribute
 *
 * @param[in]  port_id    Port ID
 * @param[in]  attr_count Number of attributes
 * @param[out] attr_list  Array of attributes to be retrieved
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_get_port_attributes(
    sai_port_id_t port_id,
    uint32_t attr_count,
    sai_port_attr_t *attr_list
);

/**
 * @brief Get port statistics
 *
 * @param[in]  port_id         Port ID
 * @param[in]  counter_ids     Array of counter IDs to retrieve
 * @param[in]  counter_count   Number of counters in the array
 * @param[out] counters        Array to store counter values
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_get_port_stats(
    sai_port_id_t port_id,
    const sai_port_stat_counter_id_t *counter_ids,
    uint32_t counter_count,
    uint64_t *counters
);

/**
 * @brief Clear port statistics
 *
 * @param[in] port_id         Port ID
 * @param[in] counter_ids     Array of counter IDs to clear
 * @param[in] counter_count   Number of counters in the array
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_clear_port_stats(
    sai_port_id_t port_id,
    const sai_port_stat_counter_id_t *counter_ids,
    uint32_t counter_count
);

/**
 * @brief Get port state
 *
 * @param[in]  port_id     Port ID
 * @param[out] oper_status Pointer to store operational status
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_get_port_state(
    sai_port_id_t port_id,
    sai_port_oper_status_t *oper_status
);

/**
 * @brief Get all port IDs
 *
 * @param[out] port_count  Pointer to store the number of ports
 * @param[out] port_list   Array to store port IDs
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_get_port_list(
    uint32_t *port_count,
    sai_port_id_t *port_list
);

/**
 * @brief Port state change notification handler type
 *
 * @param[in] port_id      Port ID
 * @param[in] oper_status  New operational status
 */
typedef void (*sai_port_state_change_notification_fn)(
    sai_port_id_t port_id,
    sai_port_oper_status_t oper_status
);

/**
 * @brief Register for port state change notifications
 *
 * @param[in] handler    Notification handler function
 *
 * @return SAI_STATUS_SUCCESS on success or error code on failure
 */
sai_status_t sai_register_port_state_change_notification(
    sai_port_state_change_notification_fn handler
);

#endif /* SAI_PORT_H */
