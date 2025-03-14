/**
 * @file vlan.h
 * @brief VLAN management interface
 */
#ifndef SWITCH_SIM_VLAN_H
#define SWITCH_SIM_VLAN_H

#include "../common/types.h"
#include "../common/error_codes.h"
#include "../hal/port.h"

/**
 * @brief VLAN port membership modes
 */
typedef enum {
    VLAN_PORT_MODE_ACCESS = 0,  /**< Access port - untagged for one VLAN */
    VLAN_PORT_MODE_TRUNK,       /**< Trunk port - tagged for multiple VLANs */
    VLAN_PORT_MODE_HYBRID       /**< Hybrid port - both tagged and untagged VLANs */
} vlan_port_mode_t;

/**
 * @brief VLAN port membership type
 */
typedef enum {
    VLAN_MEMBER_TAGGED = 0,    /**< Tagged member - frames sent with VLAN tag */
    VLAN_MEMBER_UNTAGGED       /**< Untagged member - frames sent without VLAN tag */
} vlan_member_type_t;

/**
 * @brief VLAN entry structure
 */
typedef struct {
    vlan_id_t      vlan_id;            /**< VLAN identifier (1-4095) */
    char           name[32];           /**< VLAN name */
    bool           is_active;          /**< VLAN active state */
    uint64_t       member_ports;       /**< Bitset of member ports (both tagged and untagged) */
    uint64_t       untagged_ports;     /**< Bitset of untagged member ports */
    bool           learning_enabled;   /**< MAC learning enabled on this VLAN */
    bool           stp_enabled;        /**< STP enabled on this VLAN */
} vlan_entry_t;

/**
 * @brief Port VLAN configuration structure
 */
typedef struct {
    vlan_port_mode_t mode;          /**< Port VLAN mode */
    vlan_id_t      pvid;            /**< Port VLAN ID (for ingress untagged frames) */
    vlan_id_t      native_vlan;     /**< Native VLAN for trunk ports (usually same as PVID) */
    bool           accept_untag;    /**< Accept untagged frames */
    bool           accept_tag;      /**< Accept tagged frames */
    bool           ingress_filter;  /**< Enable ingress filtering */
} vlan_port_config_t;

/**
 * @brief Initialize VLAN module
 * 
 * @return status_t STATUS_SUCCESS on success
 */
status_t vlan_init(void);

/**
 * @brief Deinitialize VLAN module
 * 
 * @return status_t STATUS_SUCCESS on success
 */
status_t vlan_deinit(void);

/**
 * @brief Create a new VLAN
 * 
 * @param vlan_id VLAN identifier (1-4095)
 * @param name Optional VLAN name (can be NULL)
 * @return status_t STATUS_SUCCESS on success
 */
status_t vlan_create(vlan_id_t vlan_id, const char *name);

/**
 * @brief Delete a VLAN
 * 
 * @param vlan_id VLAN identifier
 * @return status_t STATUS_SUCCESS on success
 */
status_t vlan_delete(vlan_id_t vlan_id);

/**
 * @brief Get VLAN information
 * 
 * @param vlan_id VLAN identifier
 * @param entry Output parameter to store VLAN entry
 * @return status_t STATUS_SUCCESS on success
 */
status_t vlan_get(vlan_id_t vlan_id, vlan_entry_t *entry);

/**
 * @brief Set VLAN name
 * 
 * @param vlan_id VLAN identifier
 * @param name VLAN name
 * @return status_t STATUS_SUCCESS on success
 */
status_t vlan_set_name(vlan_id_t vlan_id, const char *name);

/**
 * @brief Add port to VLAN
 * 
 * @param vlan_id VLAN identifier
 * @param port_id Port identifier
 * @param member_type Member type (tagged or untagged)
 * @return status_t STATUS_SUCCESS on success
 */
status_t vlan_add_port(vlan_id_t vlan_id, port_id_t port_id, vlan_member_type_t member_type);

/**
 * @brief Remove port from VLAN
 * 
 * @param vlan_id VLAN identifier
 * @param port_id Port identifier
 * @return status_t STATUS_SUCCESS on success
 */
status_t vlan_remove_port(vlan_id_t vlan_id, port_id_t port_id);

/**
 * @brief Set VLAN active state
 * 
 * @param vlan_id VLAN identifier
 * @param active Set to true to activate VLAN
 * @return status_t STATUS_SUCCESS on success
 */
status_t vlan_set_active(vlan_id_t vlan_id, bool active);

/**
 * @brief Enable/disable MAC learning on VLAN
 * 
 * @param vlan_id VLAN identifier
 * @param enable Set to true to enable learning
 * @return status_t STATUS_SUCCESS on success
 */
status_t vlan_set_learning(vlan_id_t vlan_id, bool enable);

/**
 * @brief Enable/disable STP on VLAN
 * 
 * @param vlan_id VLAN identifier
 * @param enable Set to true to enable STP
 * @return status_t STATUS_SUCCESS on success
 */
status_t vlan_set_stp(vlan_id_t vlan_id, bool enable);

/**
 * @brief Configure port VLAN parameters
 * 
 * @param port_id Port identifier
 * @param config Port VLAN configuration
 * @return status_t STATUS_SUCCESS on success
 */
status_t vlan_set_port_config(port_id_t port_id, vlan_port_config_t *config);

/**
 * @brief Get port VLAN configuration
 * 
 * @param port_id Port identifier
 * @param config Output parameter to store port VLAN configuration
 * @return status_t STATUS_SUCCESS on success
 */
status_t vlan_get_port_config(port_id_t port_id, vlan_port_config_t *config);

/**
 * @brief Get all VLANs configured on the system
 * 
 * @param entries Array to store VLAN entries
 * @param max_entries Size of the entries array
 * @param count Output parameter to store number of entries returned
 * @return status_t STATUS_SUCCESS on success
 */
status_t vlan_get_all(vlan_entry_t *entries, uint32_t max_entries, uint32_t *count);

/**
 * @brief Get all VLANs that include a specific port
 * 
 * @param port_id Port identifier
 * @param vlan_ids Array to store VLAN IDs
 * @param max_vlans Size of the vlan_ids array
 * @param count Output parameter to store number of VLANs returned
 * @return status_t STATUS_SUCCESS on success
 */
status_t vlan_get_by_port(port_id_t port_id, vlan_id_t *vlan_ids, uint32_t max_vlans, uint32_t *count);

/**
 * @brief Process a packet for VLAN handling (tagging/untagging)
 * 
 * @param packet_info Packet information
 * @param in_port Input port
 * @param out_port Output port
 * @param out_vlan_id Output parameter to store egress VLAN ID
 * @param tag_action Output parameter to indicate required tag action
 * @return status_t STATUS_SUCCESS if packet should be forwarded
 */
typedef enum {
    VLAN_TAG_ACTION_NONE = 0,   /**< Don't modify VLAN tag */
    VLAN_TAG_ACTION_ADD,        /**< Add VLAN tag */
    VLAN_TAG_ACTION_REMOVE,     /**< Remove VLAN tag */
    VLAN_TAG_ACTION_REPLACE     /**< Replace VLAN tag */
} vlan_tag_action_t;

status_t vlan_process_packet(packet_info_t *packet_info, 
                           port_id_t in_port, 
                           port_id_t out_port,
                           vlan_id_t *out_vlan_id,
                           vlan_tag_action_t *tag_action);

/**
 * @brief Register callback for VLAN events
 * 
 * @param callback Function to call when VLAN events occur
 * @param user_data User data to pass to callback
 * @return status_t STATUS_SUCCESS on success
 */
typedef enum {
    VLAN_EVENT_CREATE = 0,      /**< VLAN created */
    VLAN_EVENT_DELETE,          /**< VLAN deleted */
    VLAN_EVENT_PORT_ADDED,      /**< Port added to VLAN */
    VLAN_EVENT_PORT_REMOVED,    /**< Port removed from VLAN */
    VLAN_EVENT_CONFIG_CHANGE    /**< VLAN configuration changed */
} vlan_event_type_t;

typedef void (*vlan_event_callback_t)(vlan_id_t vlan_id, 
                                    vlan_event_type_t event_type, 
                                    port_id_t port_id,  /* Valid for port events */
                                    void *user_data);
                                    
status_t vlan_register_event_callback(vlan_event_callback_t callback, void *user_data);

#endif /* SWITCH_SIM_VLAN_H */
