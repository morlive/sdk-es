/**
 * @file bsp.h
 * @brief Board Support Package main interface header
 *
 * This file defines the interfaces for the BSP (Board Support Package)
 * which provides hardware abstraction for the switch simulator.
 */

#ifndef BSP_H
#define BSP_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Error codes specific to BSP operations
 */
typedef enum {
    BSP_SUCCESS = 0,
    BSP_ERROR_INVALID_PARAM,
    BSP_ERROR_NOT_INITIALIZED,
    BSP_ERROR_RESOURCE_UNAVAILABLE,
    BSP_ERROR_IO,
    BSP_ERROR_TIMEOUT,
    BSP_ERROR_NOT_SUPPORTED,
    BSP_ERROR_UNKNOWN
} bsp_error_t;

/**
 * @brief Board type definitions
 */
typedef enum {
    BSP_BOARD_TYPE_GENERIC,
    BSP_BOARD_TYPE_SMALL,      // Small switch (e.g., 8 ports)
    BSP_BOARD_TYPE_MEDIUM,     // Medium switch (e.g., 24 ports)
    BSP_BOARD_TYPE_LARGE,      // Large switch (e.g., 48 ports)
    BSP_BOARD_TYPE_DATACENTER  // High-density datacenter switch
} bsp_board_type_t;

/**
 * @brief Port speed definitions
 */
typedef enum {
    BSP_PORT_SPEED_10M = 10,
    BSP_PORT_SPEED_100M = 100,
    BSP_PORT_SPEED_1G = 1000,
    BSP_PORT_SPEED_10G = 10000,
    BSP_PORT_SPEED_25G = 25000,
    BSP_PORT_SPEED_40G = 40000,
    BSP_PORT_SPEED_100G = 100000
} bsp_port_speed_t;

/**
 * @brief Port duplex mode
 */
typedef enum {
    BSP_PORT_DUPLEX_HALF,
    BSP_PORT_DUPLEX_FULL
} bsp_port_duplex_t;

/**
 * @brief Port status structure
 */
typedef struct {
    bool link_up;
    bsp_port_speed_t speed;
    bsp_port_duplex_t duplex;
    bool flow_control_enabled;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_errors;
    uint64_t tx_errors;
} bsp_port_status_t;

/**
 * @brief Board configuration structure
 */
typedef struct {
    bsp_board_type_t board_type;
    uint32_t num_ports;
    uint32_t cpu_frequency_mhz;
    uint32_t memory_size_mb;
    bool has_layer3_support;
    bool has_qos_support;
    bool has_acl_support;
    const char* board_name;
} bsp_config_t;

/**
 * @brief Resource allocation handle
 */
typedef void* bsp_resource_handle_t;

/**
 * @brief Initialize the Board Support Package
 *
 * @param config Board configuration
 * @return bsp_error_t Error code
 */
bsp_error_t bsp_init(const bsp_config_t* config);

/**
 * @brief Deinitialize the Board Support Package and release resources
 *
 * @return bsp_error_t Error code
 */
bsp_error_t bsp_deinit(void);

/**
 * @brief Get the board configuration
 *
 * @param config Pointer to store the current configuration
 * @return bsp_error_t Error code
 */
bsp_error_t bsp_get_config(bsp_config_t* config);

/**
 * @brief Reset the board to its initial state
 *
 * @param hard_reset If true, perform a hard reset; otherwise, soft reset
 * @return bsp_error_t Error code
 */
bsp_error_t bsp_reset(bool hard_reset);

/**
 * @brief Initialize a specific port
 *
 * @param port_id Port identifier
 * @param speed Initial port speed
 * @param duplex Initial duplex mode
 * @return bsp_error_t Error code
 */
bsp_error_t bsp_port_init(uint32_t port_id, bsp_port_speed_t speed, bsp_port_duplex_t duplex);

/**
 * @brief Get port status
 *
 * @param port_id Port identifier
 * @param status Pointer to store port status
 * @return bsp_error_t Error code
 */
bsp_error_t bsp_port_get_status(uint32_t port_id, bsp_port_status_t* status);

/**
 * @brief Enable or disable a port
 *
 * @param port_id Port identifier
 * @param enable If true, enable the port; otherwise, disable
 * @return bsp_error_t Error code
 */
bsp_error_t bsp_port_set_enabled(uint32_t port_id, bool enable);

/**
 * @brief Register a callback for port status changes
 *
 * @param port_id Port identifier
 * @param callback Function to call when port status changes
 * @param user_data User data to pass to the callback
 * @return bsp_error_t Error code
 */
bsp_error_t bsp_port_register_callback(uint32_t port_id, 
                                      void (*callback)(uint32_t port_id, bsp_port_status_t status, void* user_data),
                                      void* user_data);

/**
 * @brief Allocate a hardware resource
 *
 * @param resource_type Type of resource to allocate
 * @param size Size of the resource (interpretation depends on type)
 * @param handle Pointer to store the resource handle
 * @return bsp_error_t Error code
 */
bsp_error_t bsp_allocate_resource(uint32_t resource_type, uint32_t size, bsp_resource_handle_t* handle);

/**
 * @brief Free a previously allocated hardware resource
 *
 * @param handle Resource handle
 * @return bsp_error_t Error code
 */
bsp_error_t bsp_free_resource(bsp_resource_handle_t handle);

/**
 * @brief Get system timestamp in microseconds
 *
 * @return uint64_t Current timestamp
 */
uint64_t bsp_get_timestamp_us(void);

/**
 * @brief Check if board is initialized
 *
 * @return bool True if initialized, false otherwise
 */
bool bsp_is_initialized(void);

#endif /* BSP_H */
