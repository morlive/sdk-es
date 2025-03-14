/**
 * @file mac_table.h
 * @brief MAC address table management interface
 */
#ifndef SWITCH_SIM_MAC_TABLE_H
#define SWITCH_SIM_MAC_TABLE_H

#include "../common/types.h"
#include "../common/error_codes.h"
#include "../hal/packet.h"
#include "../hal/port.h"

/**
 * @brief MAC address entry types
 */
typedef enum {
    MAC_ENTRY_TYPE_DYNAMIC = 0,  /**< Dynamically learned MAC address */
    MAC_ENTRY_TYPE_STATIC,       /**< Statically configured MAC address */
    MAC_ENTRY_TYPE_MANAGEMENT    /**< Management MAC address (e.g. CPU port) */
} mac_entry_type_t;

/**
 * @brief MAC table entry aging state
 */
typedef enum {
    MAC_AGING_ACTIVE = 0,  /**< Entry is subject to aging */
    MAC_AGING_DISABLED     /**< Entry is not subject to aging */
} mac_aging_state_t;

/**
 * @brief MAC table entry structure
 */
typedef struct {
    mac_addr_t     mac_addr;      /**< MAC address */
    vlan_id_t      vlan_id;       /**< VLAN ID */
    port_id_t      port_id;       /**< Port ID where MAC was learned */
    mac_entry_type_t type;        /**< Entry type */
    mac_aging_state_t aging;      /**< Aging state */
    uint32_t       age_timestamp;  /**< Last time this entry was used (for aging) */
} mac_table_entry_t;

/**
 * @brief MAC learning configuration
 */
typedef struct {
    bool           learning_enabled;  /**< Global MAC learning enabled/disabled */
    uint32_t       aging_time;        /**< MAC entry aging time in seconds, 0 = no aging */
    uint32_t       max_entries;       /**< Maximum number of entries in the table */
    bool           move_detection;    /**< Enable/disable MAC move detection */
} mac_table_config_t;

/**
 * @brief Initialize the MAC address table
 * 
 * @param config Configuration parameters
 * @return status_t STATUS_SUCCESS on success
 */
status_t mac_table_init(mac_table_config_t *config);

/**
 * @brief Deinitialize the MAC address table
 * 
 * @return status_t STATUS_SUCCESS on success
 */
status_t mac_table_deinit(void);

/**
 * @brief Add a static entry to the MAC table
 * 
 * @param mac_addr MAC address
 * @param vlan_id VLAN ID
 * @param port_id Port ID
 * @return status_t STATUS_SUCCESS on success, error code otherwise
 */
status_t mac_table_add_static_entry(mac_addr_t mac_addr, vlan_id_t vlan_id, port_id_t port_id);

/**
 * @brief Delete an entry from the MAC table
 * 
 * @param mac_addr MAC address
 * @param vlan_id VLAN ID
 * @return status_t STATUS_SUCCESS on success, error code otherwise
 */
status_t mac_table_delete_entry(mac_addr_t mac_addr, vlan_id_t vlan_id);

/**
 * @brief Look up a MAC address in the table
 * 
 * @param mac_addr MAC address to look up
 * @param vlan_id VLAN ID
 * @param entry Output parameter to store the entry if found
 * @return status_t STATUS_SUCCESS if found, STATUS_NOT_FOUND otherwise
 */
status_t mac_table_lookup(mac_addr_t mac_addr, vlan_id_t vlan_id, mac_table_entry_t *entry);

/**
 * @brief Process MAC learning for an incoming packet
 * 
 * @param packet Packet information
 * @param port_id Port on which packet was received
 * @return status_t STATUS_SUCCESS on success, error code otherwise
 */
status_t mac_table_learn(packet_info_t *packet, port_id_t port_id);

/**
 * @brief Get destination port for forwarding a packet
 * 
 * @param dst_mac Destination MAC address
 * @param vlan_id VLAN ID
 * @param port_id Output parameter to store destination port
 * @return status_t STATUS_SUCCESS if found, STATUS_NOT_FOUND otherwise
 */
status_t mac_table_get_port(mac_addr_t dst_mac, vlan_id_t vlan_id, port_id_t *port_id);

/**
 * @brief Clear all dynamic entries in the MAC table
 * 
 * @return status_t STATUS_SUCCESS on success
 */
status_t mac_table_clear_dynamic(void);

/**
 * @brief Clear all entries (both static and dynamic) in the MAC table
 * 
 * @return status_t STATUS_SUCCESS on success
 */
status_t mac_table_clear_all(void);

/**
 * @brief Process MAC aging - remove entries that have expired
 * 
 * @return status_t STATUS_SUCCESS on success
 */
status_t mac_table_process_aging(void);

/**
 * @brief Get number of entries currently in the MAC table
 * 
 * @param count Output parameter to store entry count
 * @return status_t STATUS_SUCCESS on success
 */
status_t mac_table_get_count(uint32_t *count);

/**
 * @brief Get all entries in the MAC table
 * 
 * @param entries Array to store MAC table entries
 * @param max_entries Size of the entries array
 * @param count Output parameter to store number of entries returned
 * @return status_t STATUS_SUCCESS on success
 */
status_t mac_table_get_entries(mac_table_entry_t *entries, uint32_t max_entries, uint32_t *count);

/**
 * @brief Configure MAC learning on a specific port
 * 
 * @param port_id Port identifier
 * @param enable Enable/disable MAC learning
 * @return status_t STATUS_SUCCESS on success
 */
status_t mac_table_configure_port_learning(port_id_t port_id, bool enable);

/**
 * @brief Register callback for MAC address change events
 * 
 * @param callback Function to call when MAC events occur
 * @param user_data User data to pass to callback
 * @return status_t STATUS_SUCCESS on success
 */
typedef void (*mac_event_callback_t)(mac_table_entry_t *entry, bool is_added, void *user_data);
status_t mac_table_register_event_callback(mac_event_callback_t callback, void *user_data);

#endif /* SWITCH_SIM_MAC_TABLE_H */
