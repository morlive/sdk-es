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
