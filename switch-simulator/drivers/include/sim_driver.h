/**
 * @file sim_driver.h
 * @brief Simulation driver interface for switch simulator
 */

#ifndef SIM_DRIVER_H
#define SIM_DRIVER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Forward declarations for hardware simulation layer */
typedef enum {
    HW_SIM_SUCCESS = 0,
    HW_SIM_ERROR_GENERAL,
    HW_SIM_ERROR_INVALID_PARAM,
    HW_SIM_ERROR_RESOURCE
} hw_sim_status_t;

hw_sim_status_t hw_sim_register_driver(void *ctx);
hw_sim_status_t hw_sim_unregister_driver(void *ctx);

/* Simulation driver status codes */
typedef enum {
    SIM_SUCCESS = 0,
    SIM_ERROR_GENERAL,
    SIM_ERROR_INVALID_PARAM,
    SIM_ERROR_MEMORY,
    SIM_ERROR_HW_SIM,
    SIM_ERROR_THREAD,
    SIM_ERROR_INVALID_PORT,
    SIM_ERROR_PORT_DOWN,
    SIM_ERROR_INVALID_PACKET,
    SIM_ERROR_NOT_INITIALIZED
} sim_status_t;

/* Port status in simulation */
typedef struct {
    bool link_up;
    bool traffic_enabled;
    uint8_t mac_address[6];
} sim_port_status_t;

/* Simulation statistics */
typedef struct {
    uint64_t packets_generated;
    uint64_t packets_dropped;
    uint64_t link_state_changes;
    uint64_t running_time_ms;
} sim_statistics_t;

/* Simulation driver context */
typedef struct {
    uint32_t num_ports;
    bool is_running;
    uint32_t tick_interval_ms;
    uint32_t traffic_rate;
    double link_flap_probability;
    sim_port_status_t *port_status;
    sim_statistics_t stats;
} sim_context_t;

/* Simulation driver configuration */
typedef struct {
    uint32_t num_ports;
    uint32_t tick_interval_ms;
    uint32_t traffic_rate;
    double link_flap_probability;
    void *sim_context;  /* Filled by sim_driver_init */
} sim_config_t;

/* Callback function for simulated packets */
typedef void (*packet_handler_t)(void *context, uint32_t port_id, 
                                uint8_t *packet, size_t packet_size);

/* Callback function for link state changes */
typedef void (*link_event_handler_t)(void *context, uint32_t port_id, bool link_up);

/**
 * Initialize the simulation driver
 *
 * @param config Configuration parameters for the simulation
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_driver_init(sim_config_t *config);

/**
 * Start the simulation
 *
 * @param context Simulation context previously initialized
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_start(void *context);

/**
 * Stop the simulation
 *
 * @param context Simulation context
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_stop(void *context);

/**
 * Shutdown the simulation driver and release resources
 *
 * @param context Simulation context
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_driver_shutdown(void *context);

/**
 * Configure simulation parameters
 *
 * @param context Simulation context
 * @param config New configuration parameters
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_configure(void *context, sim_config_t *config);

/**
 * Set port state in the simulation
 *
 * @param context Simulation context
 * @param port_id Port identifier
 * @param link_up True to bring port up, false to bring it down
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_set_port_state(void *context, uint32_t port_id, bool link_up);

/**
 * Enable or disable traffic generation on a port
 *
 * @param context Simulation context
 * @param port_id Port identifier
 * @param enable True to enable traffic, false to disable
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_set_traffic_generation(void *context, uint32_t port_id, bool enable);

/**
 * Register packet handler callback
 *
 * @param callback Function to call when a packet is simulated
 * @param context User context to pass to the callback
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_register_packet_handler(packet_handler_t callback, void *context);

/**
 * Register link event handler callback
 *
 * @param callback Function to call when a link state changes
 * @param context User context to pass to the callback
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_register_link_handler(link_event_handler_t callback, void *context);

/**
 * Inject a custom packet into the simulation
 *
 * @param context Simulation context
 * @param port_id Source port for the packet
 * @param packet Packet data
 * @param packet_size Size of the packet in bytes
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_inject_packet(void *context, uint32_t port_id, 
                              uint8_t *packet, size_t packet_size);

/**
 * Get the status of a port in the simulation
 *
 * @param context Simulation context
 * @param port_id Port identifier
 * @param status Pointer to store port status
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_get_port_status(void *context, uint32_t port_id, sim_port_status_t *status);

/**
 * Get simulation statistics
 *
 * @param context Simulation context
 * @param stats Pointer to store statistics
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_get_statistics(void *context, sim_statistics_t *stats);

/**
 * Reset simulation statistics
 *
 * @param context Simulation context
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_reset_statistics(void *context);

/**
 * Set port MAC address in the simulation
 *
 * @param context Simulation context
 * @param port_id Port identifier
 * @param mac_address MAC address (6 bytes)
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_set_port_mac(void *context, uint32_t port_id, const uint8_t *mac_address);

#endif /* SIM_DRIVER_H */
