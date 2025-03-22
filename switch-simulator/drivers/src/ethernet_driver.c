/**
 * @file ethernet_driver.c
 * @brief Implementation of Ethernet driver functionality for the switch simulator
 *
 * This driver provides an abstraction layer for Ethernet operations,
 * allowing the switch simulator to handle Ethernet frames.
 */

#include "../include/ethernet_driver.h"
#include "../../include/common/logging.h"
#include "../../include/common/error_codes.h"
#include "../../include/hal/packet.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/* Constants for Ethernet */
#define ETHERNET_MIN_FRAME_SIZE 64
#define ETHERNET_MAX_FRAME_SIZE 1518
#define ETHERNET_HEADER_SIZE 14
#define ETHERNET_CRC_SIZE 4
#define ETHERNET_MAC_ADDR_LEN 6

/* Private function declarations */
static bool is_frame_valid(const uint8_t *frame, size_t frame_len);
static void process_incoming_frame(ethernet_context_t *ctx, uint8_t *frame, size_t frame_len);

/* Global variables */
static ethernet_callback_t rx_callback = NULL;
static void *rx_callback_context = NULL;

/**
 * Initialize the Ethernet driver
 *
 * @param config Driver configuration parameters
 * @return ETH_SUCCESS on success, error code otherwise
 */
eth_status_t eth_driver_init(ethernet_config_t *config) {
    if (config == NULL) {
        LOG_ERROR("Ethernet driver initialization failed: NULL configuration");
        return ETH_ERROR_INVALID_PARAM;
    }

    LOG_INFO("Initializing Ethernet driver with %d ports", config->num_ports);

    /* Allocate context for the driver */
    ethernet_context_t *ctx = (ethernet_context_t *)malloc(sizeof(ethernet_context_t));
    if (ctx == NULL) {
        LOG_ERROR("Failed to allocate memory for Ethernet driver context");
        return ETH_ERROR_MEMORY;
    }

    /* Initialize context */
    ctx->num_ports = config->num_ports;
    ctx->port_status = (port_status_t *)malloc(config->num_ports * sizeof(port_status_t));
    if (ctx->port_status == NULL) {
        LOG_ERROR("Failed to allocate memory for port status array");
        free(ctx);
        return ETH_ERROR_MEMORY;
    }

    /* Initialize port status */
    for (int i = 0; i < config->num_ports; i++) {
        ctx->port_status[i].link_up = false;
        ctx->port_status[i].speed = ETH_SPEED_1G;
        ctx->port_status[i].duplex = ETH_DUPLEX_FULL;
        memset(ctx->port_status[i].mac_addr, 0, ETHERNET_MAC_ADDR_LEN);
    }

    /* Register with hardware abstraction layer */
    hal_status_t status = hal_register_ethernet_driver(ctx);
    if (status != HAL_SUCCESS) {
        LOG_ERROR("Failed to register Ethernet driver with HAL: %d", status);
        free(ctx->port_status);
        free(ctx);
        return ETH_ERROR_HAL;
    }

    /* Store the driver context */
    config->driver_context = ctx;

    LOG_INFO("Ethernet driver initialized successfully");
    return ETH_SUCCESS;
}

/**
 * Shutdown the Ethernet driver and release resources
 *
 * @param context Driver context previously initialized
 * @return ETH_SUCCESS on success, error code otherwise
 */
eth_status_t eth_driver_shutdown(void *context) {
    if (context == NULL) {
        LOG_ERROR("Ethernet driver shutdown failed: NULL context");
        return ETH_ERROR_INVALID_PARAM;
    }

    ethernet_context_t *ctx = (ethernet_context_t *)context;
    
    /* Unregister from hardware abstraction layer */
    hal_status_t status = hal_unregister_ethernet_driver(ctx);
    if (status != HAL_SUCCESS) {
        LOG_ERROR("Failed to unregister Ethernet driver from HAL: %d", status);
        /* Continue with cleanup despite error */
    }

    /* Free allocated resources */
    free(ctx->port_status);
    free(ctx);

    LOG_INFO("Ethernet driver shutdown completed");
    return ETH_SUCCESS;
}

/**
 * Register a callback function for receiving Ethernet frames
 *
 * @param callback Function to call when a frame is received
 * @param context User context to pass to the callback
 * @return ETH_SUCCESS on success, error code otherwise
 */
eth_status_t eth_register_rx_callback(ethernet_callback_t callback, void *context) {
    if (callback == NULL) {
        LOG_ERROR("Failed to register RX callback: NULL callback function");
        return ETH_ERROR_INVALID_PARAM;
    }

    rx_callback = callback;
    rx_callback_context = context;

    LOG_INFO("Registered Ethernet RX callback function");
    return ETH_SUCCESS;
}

/**
 * Configure an Ethernet port
 *
 * @param context Driver context
 * @param port_id Port identifier
 * @param config Port configuration parameters
 * @return ETH_SUCCESS on success, error code otherwise
 */
eth_status_t eth_configure_port(void *context, uint32_t port_id, port_config_t *config) {
    if (context == NULL || config == NULL) {
        LOG_ERROR("Failed to configure port: NULL parameter");
        return ETH_ERROR_INVALID_PARAM;
    }

    ethernet_context_t *ctx = (ethernet_context_t *)context;
    
    if (port_id >= ctx->num_ports) {
        LOG_ERROR("Failed to configure port: Invalid port ID %u", port_id);
        return ETH_ERROR_INVALID_PORT;
    }

    /* Update port configuration */
    ctx->port_status[port_id].speed = config->speed;
    ctx->port_status[port_id].duplex = config->duplex;
    memcpy(ctx->port_status[port_id].mac_addr, config->mac_addr, ETHERNET_MAC_ADDR_LEN);

    /* Apply configuration to hardware (simulated) */
    hal_status_t status = hal_configure_ethernet_port(port_id, config);
    if (status != HAL_SUCCESS) {
        LOG_ERROR("Failed to apply port configuration to HAL: %d", status);
        return ETH_ERROR_HAL;
    }

    LOG_INFO("Port %u configured: Speed=%d, Duplex=%d", 
             port_id, config->speed, config->duplex);
    return ETH_SUCCESS;
}

/**
 * Set port state (up/down)
 *
 * @param context Driver context
 * @param port_id Port identifier
 * @param link_up True to bring port up, false to bring it down
 * @return ETH_SUCCESS on success, error code otherwise
 */
eth_status_t eth_set_port_state(void *context, uint32_t port_id, bool link_up) {
    if (context == NULL) {
        LOG_ERROR("Failed to set port state: NULL context");
        return ETH_ERROR_INVALID_PARAM;
    }

    ethernet_context_t *ctx = (ethernet_context_t *)context;
    
    if (port_id >= ctx->num_ports) {
        LOG_ERROR("Failed to set port state: Invalid port ID %u", port_id);
        return ETH_ERROR_INVALID_PORT;
    }

    /* Update port state */
    ctx->port_status[port_id].link_up = link_up;

    /* Apply state change to hardware (simulated) */
    hal_status_t status = hal_set_ethernet_port_state(port_id, link_up);
    if (status != HAL_SUCCESS) {
        LOG_ERROR("Failed to apply port state to HAL: %d", status);
        return ETH_ERROR_HAL;
    }

    LOG_INFO("Port %u set to %s", port_id, link_up ? "UP" : "DOWN");
    return ETH_SUCCESS;
}

/**
 * Get port status
 *
 * @param context Driver context
 * @param port_id Port identifier
 * @param status Pointer to store port status
 * @return ETH_SUCCESS on success, error code otherwise
 */
eth_status_t eth_get_port_status(void *context, uint32_t port_id, port_status_t *status) {
    if (context == NULL || status == NULL) {
        LOG_ERROR("Failed to get port status: NULL parameter");
        return ETH_ERROR_INVALID_PARAM;
    }

    ethernet_context_t *ctx = (ethernet_context_t *)context;
    
    if (port_id >= ctx->num_ports) {
        LOG_ERROR("Failed to get port status: Invalid port ID %u", port_id);
        return ETH_ERROR_INVALID_PORT;
    }

    /* Copy status information */
    memcpy(status, &(ctx->port_status[port_id]), sizeof(port_status_t));
    
    return ETH_SUCCESS;
}

/**
 * Send an Ethernet frame
 *
 * @param context Driver context
 * @param port_id Port identifier
 * @param frame Pointer to frame data
 * @param frame_len Length of the frame in bytes
 * @return ETH_SUCCESS on success, error code otherwise
 */
eth_status_t eth_send_frame(void *context, uint32_t port_id, uint8_t *frame, size_t frame_len) {
    if (context == NULL || frame == NULL) {
        LOG_ERROR("Failed to send frame: NULL parameter");
        return ETH_ERROR_INVALID_PARAM;
    }

    ethernet_context_t *ctx = (ethernet_context_t *)context;
    
    if (port_id >= ctx->num_ports) {
        LOG_ERROR("Failed to send frame: Invalid port ID %u", port_id);
        return ETH_ERROR_INVALID_PORT;
    }

    if (!ctx->port_status[port_id].link_up) {
        LOG_WARN("Cannot send frame: Port %u is down", port_id);
        return ETH_ERROR_PORT_DOWN;
    }

    /* Validate frame */
    if (!is_frame_valid(frame, frame_len)) {
        LOG_ERROR("Failed to send frame: Invalid Ethernet frame");
        return ETH_ERROR_INVALID_FRAME;
    }

    /* Send frame through HAL */
    hal_status_t status = hal_send_ethernet_frame(port_id, frame, frame_len);
    if (status != HAL_SUCCESS) {
        LOG_ERROR("Failed to send frame through HAL: %d", status);
        return ETH_ERROR_HAL;
    }

    LOG_DEBUG("Frame sent on port %u, length %zu bytes", port_id, frame_len);
    return ETH_SUCCESS;
}

/**
 * Receive frame callback from HAL
 * This is called by the HAL when a frame is received
 *
 * @param context Driver context
 * @param port_id Port identifier
 * @param frame Pointer to frame data
 * @param frame_len Length of the frame in bytes
 */
void eth_receive_frame_callback(void *context, uint32_t port_id, uint8_t *frame, size_t frame_len) {
    if (context == NULL || frame == NULL) {
        LOG_ERROR("Received invalid frame callback parameters");
        return;
    }

    ethernet_context_t *ctx = (ethernet_context_t *)context;
    
    if (port_id >= ctx->num_ports) {
        LOG_ERROR("Received frame on invalid port ID %u", port_id);
        return;
    }

    if (!ctx->port_status[port_id].link_up) {
        LOG_DEBUG("Ignoring frame on port %u (port is down)", port_id);
        return;
    }

    /* Validate frame */
    if (!is_frame_valid(frame, frame_len)) {
        LOG_WARN("Received invalid Ethernet frame on port %u", port_id);
        return;
    }

    LOG_DEBUG("Received frame on port %u, length %zu bytes", port_id, frame_len);

    /* Process the frame */
    process_incoming_frame(ctx, frame, frame_len);

    /* Call user callback if registered */
    if (rx_callback != NULL) {
        rx_callback(rx_callback_context, port_id, frame, frame_len);
    }
}

/**
 * Start the Ethernet driver
 *
 * @param context Driver context
 * @return ETH_SUCCESS on success, error code otherwise
 */
eth_status_t eth_start(void *context) {
    if (context == NULL) {
        LOG_ERROR("Failed to start Ethernet driver: NULL context");
        return ETH_ERROR_INVALID_PARAM;
    }

    ethernet_context_t *ctx = (ethernet_context_t *)context;

    /* Start the HAL Ethernet subsystem */
    hal_status_t status = hal_start_ethernet();
    if (status != HAL_SUCCESS) {
        LOG_ERROR("Failed to start Ethernet HAL: %d", status);
        return ETH_ERROR_HAL;
    }

    LOG_INFO("Ethernet driver started");
    return ETH_SUCCESS;
}

/**
 * Stop the Ethernet driver
 *
 * @param context Driver context
 * @return ETH_SUCCESS on success, error code otherwise
 */
eth_status_t eth_stop(void *context) {
    if (context == NULL) {
        LOG_ERROR("Failed to stop Ethernet driver: NULL context");
        return ETH_ERROR_INVALID_PARAM;
    }

    ethernet_context_t *ctx = (ethernet_context_t *)context;

    /* Stop the HAL Ethernet subsystem */
    hal_status_t status = hal_stop_ethernet();
    if (status != HAL_SUCCESS) {
        LOG_ERROR("Failed to stop Ethernet HAL: %d", status);
        return ETH_ERROR_HAL;
    }

    LOG_INFO("Ethernet driver stopped");
    return ETH_SUCCESS;
}

/* Private functions */

/**
 * Validates an Ethernet frame
 *
 * @param frame Pointer to frame data
 * @param frame_len Length of the frame in bytes
 * @return true if frame is valid, false otherwise
 */
static bool is_frame_valid(const uint8_t *frame, size_t frame_len) {
    /* Check frame length */
    if (frame_len < ETHERNET_MIN_FRAME_SIZE || frame_len > ETHERNET_MAX_FRAME_SIZE) {
        return false;
    }

    /* Additional validation could be performed here:
     * - Check CRC
     * - Validate MAC addresses
     * - Check EtherType
     */

    return true;
}

/**
 * Process an incoming Ethernet frame
 *
 * @param ctx Ethernet driver context
 * @param frame Pointer to frame data
 * @param frame_len Length of the frame in bytes
 */
static void process_incoming_frame(ethernet_context_t *ctx, uint8_t *frame, size_t frame_len) {
    /* Extract source and destination MAC addresses */
    uint8_t *dst_mac = frame;
    uint8_t *src_mac = frame + ETHERNET_MAC_ADDR_LEN;
    
    /* Extract EtherType (next 2 bytes after src MAC) */
    uint16_t ethertype = (frame[12] << 8) | frame[13];

    LOG_DEBUG("Processing frame: src=%02x:%02x:%02x:%02x:%02x:%02x, dst=%02x:%02x:%02x:%02x:%02x:%02x, type=0x%04x",
              src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5],
              dst_mac[0], dst_mac[1], dst_mac[2], dst_mac[3], dst_mac[4], dst_mac[5],
              ethertype);

    /* Additional processing could be done here */
}
