/**
 * @file hw_simulation.c
 * @brief Hardware simulation implementation for switch simulator
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "../../include/hal/port.h"
#include "../../include/hal/packet.h"
#include "../../include/common/logging.h"

/* Private structures and definitions */

/**
 * @brief Internal port information structure
 */
typedef struct {
    port_info_t info;
    bool initialized;
    pthread_mutex_t lock;
} sim_port_t;

/**
 * @brief Simulation state structure
 */
typedef struct {
    bool initialized;
    sim_port_t ports[MAX_PORTS];
    uint32_t port_count;
    pthread_mutex_t global_lock;
} sim_state_t;

/* Static variables */
static sim_state_t g_sim_state = {0};

/* Private function declarations */
static void sim_update_port_state(port_id_t port_id);
static status_t sim_init_port(port_id_t port_id);

/* Implementation */

/**
 * @brief Initialize hardware simulation
 * 
 * @return status_t STATUS_SUCCESS if successful
 */
status_t hw_sim_init(void)
{
    LOG_INFO(LOG_CATEGORY_HAL, "Initializing hardware simulation");
    
    if (g_sim_state.initialized) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Hardware simulation already initialized");
        return STATUS_SUCCESS;
    }
    
    /* Initialize global lock */
    if (pthread_mutex_init(&g_sim_state.global_lock, NULL) != 0) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Failed to initialize global lock");
        return STATUS_FAILURE;
    }
    
    /* Initialize with default port configuration */
    g_sim_state.port_count = 24; /* Default 24 ports for simulation */
    
    /* Initialize all ports */
    for (port_id_t i = 0; i < g_sim_state.port_count; i++) {
        status_t status = sim_init_port(i);
        if (status != STATUS_SUCCESS) {
            LOG_ERROR(LOG_CATEGORY_HAL, "Failed to initialize port %u", i);
            hw_sim_shutdown();
            return status;
        }
    }
    
    g_sim_state.initialized = true;
    LOG_INFO(LOG_CATEGORY_HAL, "Hardware simulation initialized with %u ports", 
             g_sim_state.port_count);
    
    return STATUS_SUCCESS;
}

/**
 * @brief Shutdown hardware simulation
 * 
 * @return status_t STATUS_SUCCESS if successful
 */
status_t hw_sim_shutdown(void)
{
    LOG_INFO(LOG_CATEGORY_HAL, "Shutting down hardware simulation");
    
    if (!g_sim_state.initialized) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Hardware simulation not initialized");
        return STATUS_SUCCESS;
    }
    
    pthread_mutex_lock(&g_sim_state.global_lock);
    
    /* Free resources for all ports */
    for (port_id_t i = 0; i < g_sim_state.port_count; i++) {
        if (g_sim_state.ports[i].initialized) {
            pthread_mutex_destroy(&g_sim_state.ports[i].lock);
            g_sim_state.ports[i].initialized = false;
        }
    }
    
    g_sim_state.initialized = false;
    
    pthread_mutex_unlock(&g_sim_state.global_lock);
    pthread_mutex_destroy(&g_sim_state.global_lock);
    
    LOG_INFO(LOG_CATEGORY_HAL, "Hardware simulation shutdown complete");
    
    return STATUS_SUCCESS;
}

/**
 * @brief Get hardware port information
 * 
 * @param port_id Port identifier
 * @param[out] info Port information structure to fill
 * @return status_t STATUS_SUCCESS if successful
 */
status_t hw_sim_get_port_info(port_id_t port_id, port_info_t *info)
{
    if (!g_sim_state.initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    
    if (port_id >= g_sim_state.port_count || !info) {
        return STATUS_INVALID_PARAMETER;
    }
    
    sim_port_t *port = &g_sim_state.ports[port_id];
    
    pthread_mutex_lock(&port->lock);
    
    /* Copy port information */
    memcpy(info, &port->info, sizeof(port_info_t));
    
    pthread_mutex_unlock(&port->lock);
    
    return STATUS_SUCCESS;
}

/**
 * @brief Set hardware port configuration
 * 
 * @param port_id Port identifier
 * @param config Port configuration to apply
 * @return status_t STATUS_SUCCESS if successful
 */
status_t hw_sim_set_port_config(port_id_t port_id, const port_config_t *config)
{
    if (!g_sim_state.initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    
    if (port_id >= g_sim_state.port_count || !config) {
        return STATUS_INVALID_PARAMETER;
    }
    
    sim_port_t *port = &g_sim_state.ports[port_id];
    
    pthread_mutex_lock(&port->lock);
    
    /* Update port configuration */
    memcpy(&port->info.config, config, sizeof(port_config_t));
    
    /* Update port state based on new configuration */
    sim_update_port_state(port_id);
    
    pthread_mutex_unlock(&port->lock);
    
    LOG_INFO(LOG_CATEGORY_HAL, "Updated configuration for port %u (admin_state=%s, speed=%u)",
             port_id, config->admin_state ? "up" : "down", config->speed);
    
    return STATUS_SUCCESS;
}

/**
 * @brief Simulate packet reception
 * 
 * @param port_id Ingress port ID
 * @param packet Packet buffer with data
 * @return status_t STATUS_SUCCESS if successful
 */
status_t hw_sim_receive_packet(port_id_t port_id, packet_buffer_t *packet)
{
    if (!g_sim_state.initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    
    if (port_id >= g_sim_state.port_count || !packet) {
        return STATUS_INVALID_PARAMETER;
    }
    
    sim_port_t *port = &g_sim_state.ports[port_id];
    
    pthread_mutex_lock(&port->lock);
    
    /* Check if port is up */
    if (port->info.state != PORT_STATE_UP) {
        pthread_mutex_unlock(&port->lock);
        return STATUS_FAILURE;
    }
    
    /* Update port statistics */
    port->info.stats.rx_packets++;
    port->info.stats.rx_bytes += packet->size;
    
    /* Determine packet type for stats */
    if (packet->metadata.dst_mac.addr[0] & 0x01) {
        if (packet->metadata.dst_mac.addr[0] == 0xFF &&
            packet->metadata.dst_mac.addr[1] == 0xFF &&
            packet->metadata.dst_mac.addr[2] == 0xFF &&
            packet->metadata.dst_mac.addr[3] == 0xFF &&
            packet->metadata.dst_mac.addr[4] == 0xFF &&
            packet->metadata.dst_mac.addr[5] == 0xFF) {
            port->info.stats.rx_broadcast++;
        } else {
            port->info.stats.rx_multicast++;
        }
    } else {
        port->info.stats.rx_unicast++;
    }
    
    pthread_mutex_unlock(&port->lock);
    
    /* Set packet metadata */
    packet->metadata.port = port_id;
    packet->metadata.direction = PACKET_DIR_RX;
    packet->metadata.timestamp = (uint32_t)time(NULL);
    
    /* Inject packet into processing pipeline */
    return packet_inject(packet);
}

/**
 * @brief Simulate packet transmission
 * 
 * @param packet Packet buffer to transmit
 * @param port_id Egress port ID
 * @return status_t STATUS_SUCCESS if successful
 */
status_t hw_sim_transmit_packet(packet_buffer_t *packet, port_id_t port_id)
{
    if (!g_sim_state.initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    
    if (port_id >= g_sim_state.port_count || !packet) {
        return STATUS_INVALID_PARAMETER;
    }
    
    sim_port_t *port = &g_sim_state.ports[port_id];
    
    pthread_mutex_lock(&port->lock);
    
    /* Check if port is up */
    if (port->info.state != PORT_STATE_UP) {
        pthread_mutex_unlock(&port->lock);
        packet->metadata.is_dropped = true;
        LOG_DEBUG(LOG_CATEGORY_HAL, "Dropping packet: port %u is down", port_id);
        return STATUS_FAILURE;
    }
    
    /* Check packet size against port MTU */
    if (packet->size > port->info.config.mtu) {
        port->info.stats.tx_drops++;
        pthread_mutex_unlock(&port->lock);
        packet->metadata.is_dropped = true;
        LOG_DEBUG(LOG_CATEGORY_HAL, "Dropping packet: size %u exceeds MTU %u on port %u",
                 packet->size, port->info.config.mtu, port_id);
        return STATUS_FAILURE;
    }
    
    /* Update port statistics */
    port->info.stats.tx_packets++;
    port->info.stats.tx_bytes += packet->size;
    
    /* Determine packet type for stats */
    if (packet->metadata.dst_mac.addr[0] & 0x01) {
        if (packet->metadata.dst_mac.addr[0] == 0xFF &&
            packet->metadata.dst_mac.addr[1] == 0xFF &&
            packet->metadata.dst_mac.addr[2] == 0xFF &&
            packet->metadata.dst_mac.addr[3] == 0xFF &&
            packet->metadata.dst_mac.addr[4] == 0xFF &&
            packet->metadata.dst_mac.addr[5] == 0xFF) {
            port->info.stats.tx_broadcast++;
        } else {
            port->info.stats.tx_multicast++;
        }
    } else {
        port->info.stats.tx_unicast++;
    }
    
    pthread_mutex_unlock(&port->lock);
    
    LOG_TRACE(LOG_CATEGORY_HAL, "Transmitted packet of size %u on port %u",
             packet->size, port_id);
    
    return STATUS_SUCCESS;
}

/**
 * @brief Get the number of ports in hardware
 * 
 * @param[out] count Pointer to store port count
 * @return status_t STATUS_SUCCESS if successful
 */
status_t hw_sim_get_port_count(uint32_t *count)
{
    if (!g_sim_state.initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    
    if (!count) {
        return STATUS_INVALID_PARAMETER;
    }
    
    *count = g_sim_state.port_count;
    return STATUS_SUCCESS;
}

/**
 * @brief Reset all statistics for a port
 * 
 * @param port_id Port identifier
 * @return status_t STATUS_SUCCESS if successful
 */
status_t hw_sim_clear_port_stats(port_id_t port_id)
{
    if (!g_sim_state.initialized) {
        return STATUS_NOT_INITIALIZED;
    }
    
    if (port_id >= g_sim_state.port_count) {
        return STATUS_INVALID_PARAMETER;
    }
    
    sim_port_t *port = &g_sim_state.ports[port_id];
    
    pthread_mutex_lock(&port->lock);
    
    /* Clear all statistics counters */
    memset(&port->info.stats, 0, sizeof(port_stats_t));
    
    pthread_mutex_unlock(&port->lock);
    
    LOG_INFO(LOG_CATEGORY_HAL, "Cleared statistics for port %u", port_id);
    
    return STATUS_SUCCESS;
}

/* Private function implementation */

/**
 * @brief Initialize a port in the simulation
 * 
 * @param port_id Port identifier to initialize
 * @return status_t STATUS_SUCCESS if successful
 */
static status_t sim_init_port(port_id_t port_id)
{
    if (port_id >= MAX_PORTS) {
        return STATUS_INVALID_PARAMETER;
    }
    
    sim_port_t *port = &g_sim_state.ports[port_id];
    
    /* Initialize mutex */
    if (pthread_mutex_init(&port->lock, NULL) != 0) {
        return STATUS_FAILURE;
    }
    
    /* Initialize port data */
    memset(&port->info, 0, sizeof(port_info_t));
    
    port->info.id = port_id;
    port->info.type = PORT_TYPE_PHYSICAL;
    snprintf(port->info.name, sizeof(port->info.name), "Port%u", port_id);
    
    /* Default configuration */
    port->info.config.admin_state = true;  /* Enabled by default */
    port->info.config.speed = PORT_SPEED_1G;
    port->info.config.duplex = PORT_DUPLEX_FULL;
    port->info.config.auto_neg = true;
    port->info.config.flow_control = false;
    port->info.config.mtu = 1500;
    port->info.config.pvid = 1; /* Default VLAN */
    
    /* Initial operational state */
    port->info.state = PORT_STATE_DOWN;
    
    /* Generate a unique MAC address for each port */
    port->info.mac_addr.addr[0] = 0x00;
    port->info.mac_addr.addr[1] = 0x11;
    port->info.mac_addr.addr[2] = 0x22;
    port->info.mac_addr.addr[3] = 0x33;
    port->info.mac_addr.addr[4] = 0x44;
    port->info.mac_addr.addr[5] = (uint8_t)port_id;
    
    /* Mark as initialized */
    port->initialized = true;
    
    /* Update operational state */
    sim_update_port_state(port_id);
    
    return STATUS_SUCCESS;
}

/**
 * @brief Update port operational state based on configuration
 * 
 * @param port_id Port identifier
 */
static void sim_update_port_state(port_id_t port_id)
{
    sim_port_t *port = &g_sim_state.ports[port_id];
    
    /* Port lock must be held by caller */
    
    /* Update operational state based on admin state */
    if (port->info.config.admin_state) {
        /* Simulate link establishment with 80% probability */
        if (rand() % 100 < 80) {
            port->info.state = PORT_STATE_UP;
            LOG_INFO(LOG_CATEGORY_HAL, "Port %u link is UP", port_id);
        } else {
            port->info.state = PORT_STATE_DOWN;
            LOG_INFO(LOG_CATEGORY_HAL, "Port %u link is DOWN despite admin UP", port_id);
        }
    } else {
        port->info.state = PORT_STATE_DOWN;
        LOG_INFO(LOG_CATEGORY_HAL, "Port %u link is DOWN (administratively disabled)", port_id);
    }
}
