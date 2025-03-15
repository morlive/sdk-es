/**
 * @file stp.h
 * @brief Spanning Tree Protocol interface
 */
#ifndef SWITCH_SIM_STP_H
#define SWITCH_SIM_STP_H

#include "../common/types.h"
#include "../common/error_codes.h"
#include "../hal/port.h"

/**
 * @brief STP protocol version
 */
typedef enum {
    STP_VERSION_STP = 0,   /**< IEEE 802.1D Spanning Tree Protocol */
    STP_VERSION_RSTP,      /**< IEEE 802.1w Rapid Spanning Tree Protocol */
    STP_VERSION_MSTP       /**< IEEE 802.1s Multiple Spanning Tree Protocol */
} stp_version_t;

/**
 * @brief STP port states
 */
typedef enum {
    STP_PORT_STATE_DISABLED = 0,  /**< Port is administratively disabled */
    STP_PORT_STATE_BLOCKING,      /**< Port is in blocking state (no forwarding) */
    STP_PORT_STATE_LISTENING,     /**< Port is in listening state (no forwarding) */
    STP_PORT_STATE_LEARNING,      /**< Port is in learning state (no forwarding) */
    STP_PORT_STATE_FORWARDING     /**< Port is in forwarding state */
} stp_port_state_t;

/**
 * @brief STP port roles
 */
typedef enum {
    STP_PORT_ROLE_DISABLED = 0,  /**< Port is disabled */
    STP_PORT_ROLE_ROOT,          /**< Root port */
    STP_PORT_ROLE_DESIGNATED,    /**< Designated port */
    STP_PORT_ROLE_ALTERNATE,     /**< Alternate port */
    STP_PORT_ROLE_BACKUP         /**< Backup port */
} stp_port_role_t;

/**
 * @brief Bridge identifier structure
 */
typedef struct {
    uint16_t priority;          /**< Bridge priority (0-61440 in steps of 4096) */
    mac_addr_t mac_address;     /**< Bridge MAC address */
} bridge_id_t;

/**
 * @brief STP BPDU (Bridge Protocol Data Unit) structure
 */
typedef struct {
    uint16_t protocol_id;       /**< Protocol identifier (0 for STP) */
    uint8_t  protocol_version;  /**< Protocol version */
    uint8_t  bpdu_type;         /**< BPDU type (config=0, tcn=1, rstp=2) */
    uint8_t  flags;             /**< Flags (role, proposal, agreement, etc.) */
    bridge_id_t root_id;        /**< Root bridge identifier */
    uint32_t root_path_cost;    /**< Cost to root bridge */
    bridge_id_t bridge_id;      /**< Sender bridge identifier */
    uint16_t port_id;           /**< Sender port identifier */
    uint16_t message_age;       /**< Age of BPDU in 1/256 seconds */
    uint16_t max_age;           /**< Maximum age in 1/256 seconds */
    uint16_t hello_time;        /**< Hello time in 1/256 seconds */
    uint16_t forward_delay;     /**< Forward delay in 1/256 seconds */
} stp_bpdu_t;

/**
 * @brief STP instance configuration
 */
typedef struct {
    stp_version_t version;       /**< STP protocol version */
    bridge_id_t bridge_id;       /**< Bridge identifier */
    uint16_t hello_time;         /**< Hello time in seconds (1-10) */
    uint16_t max_age;            /**< Maximum age in seconds (6-40) */
    uint16_t forward_delay;      /**< Forward delay in seconds (4-30) */
    uint16_t transmit_hold;      /**< Max BPDUs per hello time */
    bool     enabled;            /**< Enable/disable STP */
} stp_config_t;

/**
 * @brief STP port configuration
 */
typedef struct {
    uint16_t port_priority;      /**< Port priority (0-240 in steps of 16) */
    uint32_t path_cost;          /**< Port path cost (1-200000000) */
    bool     edge_port;          /**< Edge port configuration (true for edge ports) */
    bool     bpdu_guard;         /**< BPDU guard enabled/disabled */
    bool     root_guard;         /**< Root guard enabled/disabled */
    bool     enabled;            /**< Port STP enabled/disabled */
} stp_port_config_t;

/**
 * @brief STP port statistics
 */
typedef struct {
    uint32_t bpdus_received;       /**< Number of BPDUs received */
    uint32_t bpdus_transmitted;    /**< Number of BPDUs transmitted */
    uint32_t topology_changes;     /**< Number of topology changes detected */
    uint32_t bpdu_guard_triggers;  /**< Number of BPDU guard triggers */
    uint32_t root_guard_triggers;  /**< Number of root guard triggers */
} stp_port_stats_t;

/**
 * @brief Initialize spanning tree protocol
 * 
 * @param config STP configuration
 * @return status_t STATUS_SUCCESS on success
 */
status_t stp_init(stp_config_t *config);

/**
 * @brief Deinitialize spanning tree protocol
 * 
 * @return status_t STATUS_SUCCESS on success
 */
status_t stp_deinit(void);

/**
 * @brief Enable spanning tree protocol
 * 
 * @return status_t STATUS_SUCCESS on success
 */
status_t stp_enable(void);

/**
 * @brief Disable spanning tree protocol
 * 
 * @return status_t STATUS_SUCCESS on success
 */
status_t stp_disable(void);

/**
 * @brief Configure STP parameters
 * 
 * @param config STP configuration
 * @return status_t STATUS_SUCCESS on success
 */
status_t stp_set_config(stp_config_t *config);

/**
 * @brief Get current STP configuration
 * 
 * @param config Output parameter to store STP configuration
 * @return status_t STATUS_SUCCESS on success
 */
status_t stp_get_config(stp_config_t *config);

/**
 * @brief Configure STP on a port
 * 
 * @param port_id Port identifier
 * @param config Port STP configuration
 * @return status_t STATUS_SUCCESS on success
 */
status_t stp_set_port_config(port_id_t port_id, stp_port_config_t *config);

/**
 * @brief Get STP configuration for a port
 * 
 * @param port_id Port identifier
 * @param config Output parameter to store port STP configuration
 * @return status_t STATUS_SUCCESS on success
 */
status_t stp_get_port_config(port_id_t port_id, stp_port_config_t *config);

/**
 * @brief Get current STP state for a port
 * 
 * @param port_id Port identifier
 * @param state Output parameter to store port STP state
 * @return status_t STATUS_SUCCESS on success
 */
status_t stp_get_port_state(port_id_t port_id, stp_port_state_t *state);

/**
 * @brief Get current STP role for a port
 * 
 * @param port_id Port identifier
 * @param role Output parameter to store port STP role
 * @return status_t STATUS_SUCCESS on success
 */
status_t stp_get_port_role(port_id_t port_id, stp_port_role_t *role);

/**
 * @brief Get STP statistics for a port
 * 
 * @param port_id Port identifier
 * @param stats Output parameter to store port STP statistics
 * @return status_t STATUS_SUCCESS on success
 */
status_t stp_get_port_stats(port_id_t port_id, stp_port_stats_t *stats);

/**
 * @brief Clear STP statistics for a port
 * 
 * @param port_id Port identifier
 * @return status_t STATUS_SUCCESS on success
 */
status_t stp_clear_port_stats(port_id_t port_id);

 /**
 * @brief Process received BPDU
 *
 * @param port_id Port identifier where BPDU was received
 * @param bpdu BPDU packet data
 * @return status_t STATUS_SUCCESS on success
 */
//status_t stp_process_bpdu(port_id_t port_id, stp_bpdu_t *bpdu);
status_t stp_process_bpdu(port_id_t port_id, const stp_bpdu_t *bpdu);

/**
 * @brief Manually trigger a topology change notification
 *
 * @param port_id Port identifier
 * @return status_t STATUS_SUCCESS on success
 */
status_t stp_trigger_topology_change(port_id_t port_id);

/**
 * @brief Check if forwarding is allowed on a port based on STP state
 *
 * @param port_id Port identifier
 * @param allowed Output parameter set to true if forwarding is allowed
 * @return status_t STATUS_SUCCESS on success
 */
status_t stp_is_forwarding_allowed(port_id_t port_id, bool *allowed);

/**
 * @brief Register callback for STP state change events
 *
 * @param callback Function to call when STP state changes
 * @param user_data User data to pass to callback
 * @return status_t STATUS_SUCCESS on success
 */
typedef void (*stp_state_change_callback_t)(port_id_t port_id,
                                           stp_port_state_t old_state,
                                           stp_port_state_t new_state,
                                           void *user_data);
status_t stp_register_state_change_callback(stp_state_change_callback_t callback, void *user_data);

/**
 * @brief Run STP periodic tasks (should be called periodically)
 *
 * Handles transmitting BPDUs, aging timers, etc.
 *
 * @return status_t STATUS_SUCCESS on success
 */
status_t stp_periodic_tasks(void);

#endif /* SWITCH_SIM_STP_H */
