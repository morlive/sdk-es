/**
 * @file port.h
 * @brief Port management interface for switch simulator
 */

#ifndef SWITCH_SIM_PORT_H
#define SWITCH_SIM_PORT_H

#include "../common/types.h"
#include "../common/error_codes.h"

/**
 * @brief Port speed definitions
 */
typedef enum {
    PORT_SPEED_10M = 10,         /**< 10 Mbps */
    PORT_SPEED_100M = 100,       /**< 100 Mbps */
    PORT_SPEED_1G = 1000,        /**< 1 Gbps */
    PORT_SPEED_10G = 10000,      /**< 10 Gbps */
    PORT_SPEED_25G = 25000,      /**< 25 Gbps */
    PORT_SPEED_40G = 40000,      /**< 40 Gbps */
    PORT_SPEED_100G = 100000,    /**< 100 Gbps */
    PORT_SPEED_UNKNOWN = 0       /**< Unknown/unspecified speed */
} port_speed_t;

/**
 * @brief Port duplex mode
 */
typedef enum {
    PORT_DUPLEX_HALF = 0,    /**< Half duplex */
    PORT_DUPLEX_FULL,        /**< Full duplex */
    PORT_DUPLEX_UNKNOWN      /**< Unknown/unspecified duplex */
} port_duplex_t;

/**
 * @brief Port operational state
 */
typedef enum {
    PORT_STATE_DOWN = 0,     /**< Port is down */
    PORT_STATE_UP,           /**< Port is up and operational */
    PORT_STATE_TESTING,      /**< Port is in testing mode */
    PORT_STATE_UNKNOWN       /**< Port state is unknown */
} port_state_t;

/**
 * @brief Port type
 */
typedef enum {
    PORT_TYPE_PHYSICAL = 0,  /**< Physical port */
    PORT_TYPE_LAG,           /**< Link Aggregation Group */
    PORT_TYPE_LOOPBACK,      /**< Loopback interface */
    PORT_TYPE_CPU            /**< CPU port */
} port_type_t;

/**
 * @brief Port statistics counters
 */
typedef struct {
    uint64_t rx_packets;     /**< Received packets */
    uint64_t tx_packets;     /**< Transmitted packets */
    uint64_t rx_bytes;       /**< Received bytes */
    uint64_t tx_bytes;       /**< Transmitted bytes */
    uint64_t rx_errors;      /**< Receive errors */
    uint64_t tx_errors;      /**< Transmit errors */
    uint64_t rx_drops;       /**< Dropped received packets */
    uint64_t tx_drops;       /**< Dropped packets during transmission */
    uint64_t rx_unicast;     /**< Received unicast packets */
    uint64_t rx_multicast;   /**< Received multicast packets */
    uint64_t rx_broadcast;   /**< Received broadcast packets */
    uint64_t tx_unicast;     /**< Transmitted unicast packets */
    uint64_t tx_multicast;   /**< Transmitted multicast packets */
    uint64_t tx_broadcast;   /**< Transmitted broadcast packets */
} port_stats_t;

/**
 * @brief Port configuration
 */
typedef struct {
    bool admin_state;        /**< Administrative state (true = enabled) */
    port_speed_t speed;      /**< Port speed */
    port_duplex_t duplex;    /**< Duplex mode */
    bool auto_neg;           /**< Auto-negotiation enabled */
    bool flow_control;       /**< Flow control enabled */
    uint16_t mtu;            /**< Maximum Transmission Unit */
    vlan_id_t pvid;          /**< Port VLAN ID */
} port_config_t;

/**
 * @brief Port information
 */
typedef struct {
    port_id_t id;            /**< Port identifier */
    port_type_t type;        /**< Port type */
    char name[32];           /**< Port name */
    port_config_t config;    /**< Current configuration */
    port_state_t state;      /**< Operational state */
    port_stats_t stats;      /**< Statistics counters */
    mac_addr_t mac_addr;     /**< MAC address associated with port */
} port_info_t;

/**
 * @brief Initialize port subsystem
 * 
 * @return status_t STATUS_SUCCESS if successful
 */
status_t port_init(void);

/**
 * @brief Shutdown port subsystem
 * 
 * @return status_t STATUS_SUCCESS if successful
 */
status_t port_shutdown(void);

/**
 * @brief Get port information
 * 
 * @param port_id Port identifier
 * @param[out] info Port information structure to fill
 * @return status_t STATUS_SUCCESS if successful
 */
status_t port_get_info(port_id_t port_id, port_info_t *info);

/**
 * @brief Set port configuration
 * 
 * @param port_id Port identifier
 * @param config Port configuration to apply
 * @return status_t STATUS_SUCCESS if successful
 */
status_t port_set_config(port_id_t port_id, const port_config_t *config);

/**
 * @brief Set port administrative state
 * 
 * @param port_id Port identifier
 * @param admin_up True to set port administratively up, false for down
 * @return status_t STATUS_SUCCESS if successful
 */
status_t port_set_admin_state(port_id_t port_id, bool admin_up);

/**
 * @brief Get port statistics
 * 
 * @param port_id Port identifier
 * @param[out] stats Statistics structure to fill
 * @return status_t STATUS_SUCCESS if successful
 */
status_t port_get_stats(port_id_t port_id, port_stats_t *stats);

/**
 * @brief Clear port statistics counters
 * 
 * @param port_id Port identifier
 * @return status_t STATUS_SUCCESS if successful
 */
status_t port_clear_stats(port_id_t port_id);

/**
 * @brief Get total number of ports in the system
 * 
 * @param[out] count Pointer to store port count
 * @return status_t STATUS_SUCCESS if successful
 */
status_t port_get_count(uint32_t *count);

/**
 * @brief Get list of all port IDs
 * 
 * @param[out] port_ids Array to store port IDs
 * @param[in,out] count In: max array size; Out: actual number of ports
 * @return status_t STATUS_SUCCESS if successful
 */
status_t port_get_list(port_id_t *port_ids, uint32_t *count);

#endif /* SWITCH_SIM_PORT_H */
