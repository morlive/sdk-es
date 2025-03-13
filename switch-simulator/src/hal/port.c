/**
 * @file port.c
 * @brief Port management implementation for switch simulator
 */

#include <string.h>
#include <stdlib.h>
#include "../../include/hal/port.h"
#include "../../include/common/logging.h"

/* Forward declarations for hardware simulation functions */
extern status_t hw_sim_init(void);
extern status_t hw_sim_shutdown(void);
extern status_t hw_sim_get_port_info(port_id_t port_id, port_info_t *info);
extern status_t hw_sim_set_port_config(port_id_t port_id, const port_config_t *config);
extern status_t hw_sim_get_port_count(uint32_t *count);
extern status_t hw_sim_clear_port_stats(port_id_t port_id);

/* Static variables */
static bool g_port_initialized = false;

/**
 * @brief Initialize port subsystem
 * 
 * @return status_t STATUS_SUCCESS if successful
 */
status_t port_init(void)
{
    LOG_INFO(LOG_CATEGORY_HAL, "Initializing port subsystem");
    
    if (g_port_initialized) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Port subsystem already initialized");
        return STATUS_SUCCESS;
    }
    
    /* Initialize hardware simulation */
    status_t status = hw_sim_init();
    if (status != STATUS_SUCCESS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to initialize hardware simulation");
        return status;
    }
    
    g_port_initialized = true;
    LOG_INFO(LOG_CATEGORY_HAL, "Port subsystem initialized successfully");
    
    return STATUS_SUCCESS;
}

/**
 * @brief Shutdown port subsystem
 * 
 * @return status_t STATUS_SUCCESS if successful
 */
status_t port_shutdown(void)
{
    LOG_INFO(LOG_CATEGORY_HAL, "Shutting down port subsystem");
    
    if (!g_port_initialized) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Port subsystem not initialized");
        return STATUS_SUCCESS;
    }
    
    /* Shutdown hardware simulation */
    status_t status = hw_sim_shutdown();
    if (status != STATUS_SUCCESS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to shutdown hardware simulation");
        return status;
    }
    
    g_port_initialized = false;
    LOG_INFO(LOG_CATEGORY_HAL, "Port subsystem shutdown successfully");
    
    return STATUS_SUCCESS;
}

/**
 * @brief Get port information
 * 
 * @param port_id Port identifier
 * @param[out] info Port information structure to fill
 * @return status_t STATUS_SUCCESS if successful
 */
status_t port_get_info(port_id_t port_id, port_info_t *info)
{
    if (!g_port_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    
    if (!info) {
        return STATUS_INVALID_PARAMETER;
    }
    
    /* Get information from hardware simulation */
    status_t status = hw_sim_get_port_info(port_id, info);
    if (status != STATUS_SUCCESS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to get information for port %u", port_id);
    } else {
        LOG_DEBUG(LOG_CATEGORY_HAL, "Retrieved information for port %u (%s)", 
                 port_id, info->name);
    }
    
    return status;
}

/**
 * @brief Set port configuration
 * 
 * @param port_id Port identifier
 * @param config Port configuration to apply
 * @return status_t STATUS_SUCCESS if successful
 */
status_t port_set_config(port_id_t port_id, const port_config_t *config)
{
    if (!g_port_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    
    if (!config) {
        return STATUS_INVALID_PARAMETER;
    }
    
    /* Set configuration in hardware simulation */
    status_t status = hw_sim_set_port_config(port_id, config);
    if (status != STATUS_SUCCESS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to set configuration for port %u", port_id);
    } else {
        LOG_INFO(LOG_CATEGORY_HAL, "Set configuration for port %u (admin_state=%s, speed=%u)",
                port_id, config->admin_state ? "up" : "down", config->speed);
    }
    
    return status;
}

/**
 * @brief Set port administrative state
 * 
 * @param port_id Port identifier
 * @param admin_up True to set port administratively up, false for down
 * @return status_t STATUS_SUCCESS if successful
 */
status_t port_set_admin_state(port_id_t port_id, bool admin_up)
{
    if (!g_port_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    
    /* Get current configuration */
    port_info_t info;
    status_t status = hw_sim_get_port_info(port_id, &info);
    if (status != STATUS_SUCCESS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to get current configuration for port %u", port_id);
        return status;
    }
    
    /* Update admin state only */
    port_config_t config = info.config;
    config.admin_state = admin_up;
    
    /* Apply updated configuration */
    status = hw_sim_set_port_config(port_id, &config);
    if (status != STATUS_SUCCESS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to set admin state for port %u", port_id);
    } else {
        LOG_INFO(LOG_CATEGORY_HAL, "Set admin state for port %u to %s",
                port_id, admin_up ? "up" : "down");
    }
    
    return status;
}

/**
 * @brief Get port statistics
 * 
 * @param port_id Port identifier
 * @param[out] stats Statistics structure to fill
 * @return status_t STATUS_SUCCESS if successful
 */
status_t port_get_stats(port_id_t port_id, port_stats_t *stats)
{
    if (!g_port_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    
    if (!stats) {
        return STATUS_INVALID_PARAMETER;
    }
    
    /* Get port information which includes stats */
    port_info_t info;
    status_t status = hw_sim_get_port_info(port_id, &info);
    if (status != STATUS_SUCCESS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to get statistics for port %u", port_id);
        return status;
    }
    
    /* Copy statistics */
    memcpy(stats, &info.stats, sizeof(port_stats_t));
    
    LOG_DEBUG(LOG_CATEGORY_HAL, "Retrieved statistics for port %u (rx: %lu, tx: %lu)",
             port_id, stats->rx_packets, stats->tx_packets);
    
    return STATUS_SUCCESS;
}

/**
 * @brief Clear port statistics counters
 * 
 * @param port_id Port identifier
 * @return status_t STATUS_SUCCESS if successful
 */
status_t port_clear_stats(port_id_t port_id)
{
    if (!g_port_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    
    /* Clear statistics in hardware simulation */
    status_t status = hw_sim_clear_port_stats(port_id);
    if (status != STATUS_SUCCESS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to clear statistics for port %u", port_id);
    } else {
        LOG_INFO(LOG_CATEGORY_HAL, "Cleared statistics for port %u", port_id);
    }
    
    return status;
}

/**
 * @brief Get total number of ports in the system
 * 
 * @param[out] count Pointer to store port count
 * @return status_t STATUS_SUCCESS if successful
 */
status_t port_get_count(uint32_t *count)
{
    if (!g_port_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    
    if (!count) {
        return STATUS_INVALID_PARAMETER;
    }
    
    /* Get count from hardware simulation */
    status_t status = hw_sim_get_port_count(count);
    if (status != STATUS_SUCCESS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to get port count");
    } else {
        LOG_DEBUG(LOG_CATEGORY_HAL, "Retrieved port count: %u", *count);
    }
    
    return status;
}

/**
 * @brief Get list of all port IDs
 * 
 * @param[out] port_ids Array to store port IDs
 * @param[in,out] count In: max array size; Out: actual number of ports
 * @return status_t STATUS_SUCCESS if successful
 */
status_t port_get_list(port_id_t *port_ids, uint32_t *count)
{
    if (!g_port_initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    
    if (!port_ids || !count || *count == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    
    /* Get total port count */
    uint32_t total_ports;
    status_t status = hw_sim_get_port_count(&total_ports);
    if (status != STATUS_SUCCESS) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to get port count for list");
        return status;
    }
    
    /* Check array size */
    if (*count < total_ports) {
        *count = total_ports;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    /* Fill port IDs (in our simple implementation, ports are 0..N-1) */
    for (uint32_t i = 0; i < total_ports; i++) {
        port_ids[i] = (port_id_t)i;
    }
    
    *count = total_ports;
    
    LOG_DEBUG(LOG_CATEGORY_HAL, "Retrieved list of %u ports", total_ports);
    
    return STATUS_SUCCESS;
}
