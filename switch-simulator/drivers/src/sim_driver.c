/**
 * @file sim_driver.c
 * @brief Implementation of the switch simulation driver
 *
 * This driver provides simulation capabilities for a network switch,
 * including packet generation, traffic simulation, and network events.
 */

#include "../include/sim_driver.h"
#include "../../include/common/logging.h"
#include "../../include/common/error_codes.h"
#include "../../include/hal/packet.h"
#include "../../include/hal/hw_resources.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

/* Constants for simulation */
#define SIM_MAX_PORTS 64
#define SIM_DEFAULT_TICK_MS 100
#define SIM_DEFAULT_TRAFFIC_RATE 10 /* packets per second */
#define SIM_MAX_PACKET_SIZE 1518
#define SIM_MIN_PACKET_SIZE 64
#define SIM_MAC_ADDR_LEN 6

/* Private function declarations */
static void *sim_worker_thread(void *arg);
static void simulate_traffic(sim_context_t *ctx);
static void generate_random_packet(uint8_t *packet, size_t *packet_size, uint32_t src_port, uint32_t dst_port);
static void simulate_link_events(sim_context_t *ctx);
static uint32_t get_random_port_id(sim_context_t *ctx);

/* Global variables */
static pthread_t sim_thread;
static bool sim_thread_running = false;
static packet_handler_t packet_callback = NULL;
static void *packet_callback_context = NULL;
static link_event_handler_t link_callback = NULL;
static void *link_callback_context = NULL;

/**
 * Initialize the simulation driver
 *
 * @param config Configuration parameters for the simulation
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_driver_init(sim_config_t *config) {
    if (config == NULL) {
        LOG_ERROR("Simulation driver initialization failed: NULL configuration");
        return SIM_ERROR_INVALID_PARAM;
    }

    LOG_INFO("Initializing simulation driver with %d ports", config->num_ports);

    if (config->num_ports > SIM_MAX_PORTS) {
        LOG_ERROR("Number of ports exceeds maximum supported (%d > %d)", 
                 config->num_ports, SIM_MAX_PORTS);
        return SIM_ERROR_INVALID_PARAM;
    }

    /* Allocate simulation context */
    sim_context_t *ctx = (sim_context_t *)malloc(sizeof(sim_context_t));
    if (ctx == NULL) {
        LOG_ERROR("Failed to allocate memory for simulation context");
        return SIM_ERROR_MEMORY;
    }

    /* Initialize context */
    ctx->num_ports = config->num_ports;
    ctx->is_running = false;
    ctx->tick_interval_ms = config->tick_interval_ms > 0 ? 
                           config->tick_interval_ms : SIM_DEFAULT_TICK_MS;
    ctx->traffic_rate = config->traffic_rate > 0 ? 
                       config->traffic_rate : SIM_DEFAULT_TRAFFIC_RATE;
    ctx->link_flap_probability = config->link_flap_probability;

    /* Initialize port statuses */
    ctx->port_status = (sim_port_status_t *)malloc(config->num_ports * sizeof(sim_port_status_t));
    if (ctx->port_status == NULL) {
        LOG_ERROR("Failed to allocate memory for port status array");
        free(ctx);
        return SIM_ERROR_MEMORY;
    }

    /* Set initial port states */
    for (uint32_t i = 0; i < config->num_ports; i++) {
        ctx->port_status[i].link_up = true;
        ctx->port_status[i].traffic_enabled = false;
        
        /* Generate a unique MAC address for each port */
        uint8_t mac[SIM_MAC_ADDR_LEN] = {0x02, 0x00, 0x00, 0x00, (uint8_t)(i >> 8), (uint8_t)i};
        memcpy(ctx->port_status[i].mac_address, mac, SIM_MAC_ADDR_LEN);
    }

    /* Register with hardware simulation layer */
    hw_sim_status_t status = hw_sim_register_driver(ctx);
    if (status != HW_SIM_SUCCESS) {
        LOG_ERROR("Failed to register simulation driver with hardware layer: %d", status);
        free(ctx->port_status);
        free(ctx);
        return SIM_ERROR_HW_SIM;
    }

    /* Initialize random number generator */
    srand((unsigned int)time(NULL));

    /* Store context in the config structure */
    config->sim_context = ctx;

    LOG_INFO("Simulation driver initialized successfully");
    return SIM_SUCCESS;
}

/**
 * Start the simulation
 *
 * @param context Simulation context previously initialized
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_start(void *context) {
    if (context == NULL) {
        LOG_ERROR("Failed to start simulation: NULL context");
        return SIM_ERROR_INVALID_PARAM;
    }

    sim_context_t *ctx = (sim_context_t *)context;

    /* Check if simulation is already running */
    if (ctx->is_running) {
        LOG_WARN("Simulation is already running");
        return SIM_SUCCESS;
    }

    LOG_INFO("Starting simulation");

    /* Set running flag */
    ctx->is_running = true;
    sim_thread_running = true;

    /* Create worker thread */
    int result = pthread_create(&sim_thread, NULL, sim_worker_thread, ctx);
    if (result != 0) {
        LOG_ERROR("Failed to create simulation worker thread: %d", result);
        ctx->is_running = false;
        sim_thread_running = false;
        return SIM_ERROR_THREAD;
    }

    LOG_INFO("Simulation started successfully");
    return SIM_SUCCESS;
}

/**
 * Stop the simulation
 *
 * @param context Simulation context
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_stop(void *context) {
    if (context == NULL) {
        LOG_ERROR("Failed to stop simulation: NULL context");
        return SIM_ERROR_INVALID_PARAM;
    }

    sim_context_t *ctx = (sim_context_t *)context;

    /* Check if simulation is running */
    if (!ctx->is_running) {
        LOG_WARN("Simulation is not running");
        return SIM_SUCCESS;
    }

    LOG_INFO("Stopping simulation");

    /* Clear running flags */
    ctx->is_running = false;
    sim_thread_running = false;

    /* Wait for thread to terminate */
    pthread_join(sim_thread, NULL);

    LOG_INFO("Simulation stopped successfully");
    return SIM_SUCCESS;
}

/**
 * Shutdown the simulation driver and release resources
 *
 * @param context Simulation context
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_driver_shutdown(void *context) {
    if (context == NULL) {
        LOG_ERROR("Failed to shutdown simulation driver: NULL context");
        return SIM_ERROR_INVALID_PARAM;
    }

    sim_context_t *ctx = (sim_context_t *)context;

    /* Stop the simulation if it's running */
    if (ctx->is_running) {
        sim_status_t status = sim_stop(context);
        if (status != SIM_SUCCESS) {
            LOG_ERROR("Failed to stop simulation: %d", status);
            /* Continue with shutdown despite error */
        }
    }

    /* Unregister from hardware simulation layer */
    hw_sim_status_t status = hw_sim_unregister_driver(ctx);
    if (status != HW_SIM_SUCCESS) {
        LOG_ERROR("Failed to unregister simulation driver: %d", status);
        /* Continue with cleanup despite error */
    }

    /* Free allocated resources */
    free(ctx->port_status);
    free(ctx);

    LOG_INFO("Simulation driver shutdown completed");
    return SIM_SUCCESS;
}

/**
 * Configure simulation parameters
 *
 * @param context Simulation context
 * @param config New configuration parameters
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_configure(void *context, sim_config_t *config) {
    if (context == NULL || config == NULL) {
        LOG_ERROR("Failed to configure simulation: NULL parameter");
        return SIM_ERROR_INVALID_PARAM;
    }

    sim_context_t *ctx = (sim_context_t *)context;

    /* Update configuration parameters */
    ctx->tick_interval_ms = config->tick_interval_ms > 0 ? 
                           config->tick_interval_ms : ctx->tick_interval_ms;
    ctx->traffic_rate = config->traffic_rate > 0 ? 
                       config->traffic_rate : ctx->traffic_rate;
    ctx->link_flap_probability = config->link_flap_probability;

    LOG_INFO("Simulation configured: tick=%d ms, traffic_rate=%d pps, link_flap_prob=%f",
             ctx->tick_interval_ms, ctx->traffic_rate, ctx->link_flap_probability);
    return SIM_SUCCESS;
}

/**
 * Set port state in the simulation
 *
 * @param context Simulation context
 * @param port_id Port identifier
 * @param link_up True to bring port up, false to bring it down
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_set_port_state(void *context, uint32_t port_id, bool link_up) {
    if (context == NULL) {
        LOG_ERROR("Failed to set port state: NULL context");
        return SIM_ERROR_INVALID_PARAM;
    }

    sim_context_t *ctx = (sim_context_t *)context;
    
    if (port_id >= ctx->num_ports) {
        LOG_ERROR("Failed to set port state: Invalid port ID %u", port_id);
        return SIM_ERROR_INVALID_PORT;
    }

    /* Update port state */
    ctx->port_status[port_id].link_up = link_up;

    LOG_INFO("Simulation port %u set to %s", port_id, link_up ? "UP" : "DOWN");

    /* Notify through callback if registered */
    if (link_callback != NULL) {
        link_callback(link_callback_context, port_id, link_up);
    }

    return SIM_SUCCESS;
}

/**
 * Enable or disable traffic generation on a port
 *
 * @param context Simulation context
 * @param port_id Port identifier
 * @param enable True to enable traffic, false to disable
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_set_traffic_generation(void *context, uint32_t port_id, bool enable) {
    if (context == NULL) {
        LOG_ERROR("Failed to set traffic generation: NULL context");
        return SIM_ERROR_INVALID_PARAM;
    }

    sim_context_t *ctx = (sim_context_t *)context;
    
    if (port_id >= ctx->num_ports) {
        LOG_ERROR("Failed to set traffic generation: Invalid port ID %u", port_id);
        return SIM_ERROR_INVALID_PORT;
    }

    /* Update traffic generation state */
    ctx->port_status[port_id].traffic_enabled = enable;

    LOG_INFO("Traffic generation on port %u %s", port_id, enable ? "enabled" : "disabled");
    return SIM_SUCCESS;
}

/**
 * Register packet handler callback
 *
 * @param callback Function to call when a packet is simulated
 * @param context User context to pass to the callback
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_register_packet_handler(packet_handler_t callback, void *context) {
    if (callback == NULL) {
        LOG_ERROR("Failed to register packet handler: NULL callback");
        return SIM_ERROR_INVALID_PARAM;
    }

    packet_callback = callback;
    packet_callback_context = context;

    LOG_INFO("Packet handler registered");
    return SIM_SUCCESS;
}

/**
 * Register link event handler callback
 *
 * @param callback Function to call when a link state changes
 * @param context User context to pass to the callback
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_register_link_handler(link_event_handler_t callback, void *context) {
    if (callback == NULL) {
        LOG_ERROR("Failed to register link handler: NULL callback");
        return SIM_ERROR_INVALID_PARAM;
    }

    link_callback = callback;
    link_callback_context = context;

    LOG_INFO("Link event handler registered");
    return SIM_SUCCESS;
}

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
                               uint8_t *packet, size_t packet_size) {
    if (context == NULL || packet == NULL) {
        LOG_ERROR("Failed to inject packet: NULL parameter");
        return SIM_ERROR_INVALID_PARAM;
    }

    sim_context_t *ctx = (sim_context_t *)context;
    
    if (port_id >= ctx->num_ports) {
        LOG_ERROR("Failed to inject packet: Invalid port ID %u", port_id);
        return SIM_ERROR_INVALID_PORT;
    }

    if (packet_size < SIM_MIN_PACKET_SIZE || packet_size > SIM_MAX_PACKET_SIZE) {
        LOG_ERROR("Failed to inject packet: Invalid packet size %zu", packet_size);
        return SIM_ERROR_INVALID_PACKET;
    }

    if (!ctx->port_status[port_id].link_up) {
        LOG_WARN("Cannot inject packet: Port %u is down", port_id);
        return SIM_ERROR_PORT_DOWN;
    }

    LOG_DEBUG("Injecting custom packet on port %u, size %zu bytes", port_id, packet_size);

    /* Call packet handler if registered */
    if (packet_callback != NULL) {
        packet_callback(packet_callback_context, port_id, packet, packet_size);
    }

    return SIM_SUCCESS;
}

/**
 * Get the status of a port in the simulation
 *
 * @param context Simulation context
 * @param port_id Port identifier
 * @param status Pointer to store port status
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_get_port_status(void *context, uint32_t port_id, sim_port_status_t *status) {
    if (context == NULL || status == NULL) {
        LOG_ERROR("Failed to get port status: NULL parameter");
        return SIM_ERROR_INVALID_PARAM;
    }

    sim_context_t *ctx = (sim_context_t *)context;
    
    if (port_id >= ctx->num_ports) {
        LOG_ERROR("Failed to get port status: Invalid port ID %u", port_id);
        return SIM_ERROR_INVALID_PORT;
    }

    /* Copy port status */
    memcpy(status, &(ctx->port_status[port_id]), sizeof(sim_port_status_t));

    return SIM_SUCCESS;
}

/**
 * Get simulation statistics
 *
 * @param context Simulation context
 * @param stats Pointer to store statistics
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_get_statistics(void *context, sim_statistics_t *stats) {
    if (context == NULL || stats == NULL) {
        LOG_ERROR("Failed to get statistics: NULL parameter");
        return SIM_ERROR_INVALID_PARAM;
    }

    sim_context_t *ctx = (sim_context_t *)context;

    /* Copy simulation statistics */
    stats->packets_generated = ctx->stats.packets_generated;
    stats->packets_dropped = ctx->stats.packets_dropped;
    stats->link_state_changes = ctx->stats.link_state_changes;
    stats->running_time_ms = ctx->stats.running_time_ms;

    return SIM_SUCCESS;
}

/**
 * Reset simulation statistics
 *
 * @param context Simulation context
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_reset_statistics(void *context) {
    if (context == NULL) {
        LOG_ERROR("Failed to reset statistics: NULL context");
        return SIM_ERROR_INVALID_PARAM;
    }

    sim_context_t *ctx = (sim_context_t *)context;

    /* Reset all statistics counters */
    memset(&(ctx->stats), 0, sizeof(sim_statistics_t));

    LOG_INFO("Simulation statistics reset");
    return SIM_SUCCESS;
}

/**
 * Set port MAC address in the simulation
 *
 * @param context Simulation context
 * @param port_id Port identifier
 * @param mac_address MAC address (6 bytes)
 * @return SIM_SUCCESS on success, error code otherwise
 */
sim_status_t sim_set_port_mac(void *context, uint32_t port_id, const uint8_t *mac_address) {
    if (context == NULL || mac_address == NULL) {
        LOG_ERROR("Failed to set port MAC: NULL parameter");
        return SIM_ERROR_INVALID_PARAM;
    }

    sim_context_t *ctx = (sim_context_t *)context;
    
    if (port_id >= ctx->num_ports) {
        LOG_ERROR("Failed to set port MAC: Invalid port ID %u", port_id);
        return SIM_ERROR_INVALID_PORT;
    }

    /* Update MAC address */
    memcpy(ctx->port_status[port_id].mac_address, mac_address, SIM_MAC_ADDR_LEN);

    LOG_INFO("Port %u MAC address set to %02x:%02x:%02x:%02x:%02x:%02x", 
             port_id,
             mac_address[0], mac_address[1], mac_address[2],
             mac_address[3], mac_address[4], mac_address[5]);
    return SIM_SUCCESS;
}

/* Private functions */

/**
 * Simulation worker thread function
 *
 * @param arg Thread argument (simulation context)
 * @return NULL when thread terminates
 */
static void *sim_worker_thread(void *arg) {
    sim_context_t *ctx = (sim_context_t *)arg;
    
    LOG_INFO("Simulation worker thread started");

    /* Simulation loop */
    uint64_t start_time = (uint64_t)time(NULL) * 1000; /* Milliseconds since epoch */
    
    while (sim_thread_running && ctx->is_running) {
        /* Update running time */
        uint64_t current_time = (uint64_t)time(NULL) * 1000;
        ctx->stats.running_time_ms = current_time - start_time;
        
        /* Simulate network traffic */
        simulate_traffic(ctx);
        
        /* Simulate link state changes */
        simulate_link_events(ctx);
        
        /* Sleep for tick interval */
        usleep(ctx->tick_interval_ms * 1000); /* Convert ms to µs */
    }
    
    LOG_INFO("Simulation worker thread terminated");
    return NULL;
}

/**
 * Simulate network traffic
 *
 * @param ctx Simulation context
 */
static void simulate_traffic(sim_context_t *ctx) {
    /* Calculate how many packets to generate this tick */
    uint32_t packets_per_tick = (ctx->traffic_rate * ctx->tick_interval_ms) / 1000;
    
    /* Ensure at least one packet if traffic_rate > 0 */
    if (ctx->traffic_rate > 0 && packets_per_tick == 0) {
        /* Generate packet probabilistically */
        if ((rand() % 1000) < ((ctx->traffic_rate * ctx->tick_interval_ms) % 1000)) {
            packets_per_tick = 1;
        }
    }
    
    /* Generate packets */
    for (uint32_t i = 0; i < packets_per_tick; i++) {
        /* Select source port with traffic enabled and link up */
        uint32_t src_port = get_random_port_id(ctx);
        if (src_port >= ctx->num_ports) {
            /* No eligible source ports */
            continue;
        }
        
        if (!ctx->port_status[src_port].traffic_enabled || !ctx->port_status[src_port].link_up) {
            continue;
        }
        
        /* Select random destination port */
        uint32_t dst_port;
        do {
            dst_port = rand() % ctx->num_ports;
        } while (dst_port == src_port); /* Avoid sending to self */
        
        /* Generate random packet */
        uint8_t packet[SIM_MAX_PACKET_SIZE];
        size_t packet_size;
        generate_random_packet(packet, &packet_size, src_port, dst_port);
        
        /* Increment stats */
        ctx->stats.packets_generated++;
        
        /* Call packet handler if registered */
        if (packet_callback != NULL) {
            packet_callback(packet_callback_context, src_port, packet, packet_size);
        }
    }
}

/**
 * Generate a random Ethernet packet
 *
 * @param packet Buffer to store the packet
 * @param packet_size Pointer to store the packet size
 * @param src_port Source port ID
 * @param dst_port Destination port ID (for MAC address)
 */
static void generate_random_packet(uint8_t *packet, size_t *packet_size, uint32_t src_port, uint32_t dst_port) {
    /* Determine random packet size */
    size_t size = SIM_MIN_PACKET_SIZE + rand() % (SIM_MAX_PACKET_SIZE - SIM_MIN_PACKET_SIZE + 1);
    *packet_size = size;
    
    /* Clear packet buffer */
    memset(packet, 0, size);
    
    /* Set destination MAC (use port's MAC) */
    uint8_t dst_mac[SIM_MAC_ADDR_LEN] = {0x02, 0x00, 0x00, 0x00, (uint8_t)(dst_port >> 8), (uint8_t)dst_port};
    memcpy(packet, dst_mac, SIM_MAC_ADDR_LEN);
    
    /* Set source MAC (use port's MAC) */
    uint8_t src_mac[SIM_MAC_ADDR_LEN] = {0x02, 0x00, 0x00, 0x00, (uint8_t)(src_port >> 8), (uint8_t)src_port};
    memcpy(packet + SIM_MAC_ADDR_LEN, src_mac, SIM_MAC_ADDR_LEN);
    
    /* Set EtherType (IPv4 = 0x0800) */
    packet[12] = 0x08;
    packet[13] = 0x00;
    
    /* Fill the rest with random data */
    for (size_t i = 14; i < size; i++) {
        packet[i] = (uint8_t)(rand() % 256);
    }
}

/**
 * Simulate link state changes based on configuration
 *
 * @param ctx Simulation context
 */
static void simulate_link_events(sim_context_t *ctx) {
    /* Skip if link flap probability is zero */
    if (ctx->link_flap_probability <= 0.0) {
        return;
    }
    
    /* Check each port for potential link state change */
    for (uint32_t i = 0; i < ctx->num_ports; i++) {
        /* Determine if link should flap based on probability */
        if ((double)rand() / RAND_MAX < ctx->link_flap_probability) {
            /* Toggle link state */
            bool new_state = !ctx->port_status[i].link_up;
            ctx->port_status[i].link_up = new_state;
            
            /* Update statistics */
            ctx->stats.link_state_changes++;
            
            LOG_INFO("Simulated link flap: Port %u is now %s", i, new_state ? "UP" : "DOWN");
            
            /* Notify through callback if registered */
            if (link_callback != NULL) {
                link_callback(link_callback_context, i, new_state);
            }
        }
    }
}

/**
 * Get a random port ID with traffic enabled and link up
 *
 * @param ctx Simulation context
 * @return Port ID or ctx->num_ports if no eligible port found
 */
static uint32_t get_random_port_id(sim_context_t *ctx) {
    /* Count eligible ports */
    uint32_t eligible_ports = 0;
    for (uint32_t i = 0; i < ctx->num_ports; i++) {
        if (ctx->port_status[i].traffic_enabled && ctx->port_status[i].link_up) {
            eligible_ports++;
        }
    }
    
    if (eligible_ports == 0) {
        return ctx->num_ports; /* No eligible ports */
    }
    
    /* Select a random eligible port */
    uint32_t selected = rand() % eligible_ports;
    uint32_t count = 0;
    
    for (uint32_t i = 0; i < ctx->num_ports; i++) {
        if (ctx->port_status[i].traffic_enabled && ctx->port_status[i].link_up) {
            if (count == selected) {
                return i;
            }
            count++;
        }
    }
    
    return ctx->num_ports; /* Should never reach here */
}
