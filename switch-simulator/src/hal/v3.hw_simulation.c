/**
 * @file hw_simulation.c
 * @brief Hardware simulation implementation
 */

#include "hal/hw_resources.h"
#include "hal/port.h"
#include "hal/packet.h"
#include "common/logging.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define MODULE_NAME "HW_SIM"

/* Simulated switch hardware data */
static struct {
    uint32_t num_ports;
    port_t *ports;
    packet_queue_t *rx_queues;  /* Receive queues for each port */
    pthread_mutex_t *port_locks;
    bool initialized;
} switch_hw;

/* Hardware simulation thread */
static pthread_t sim_thread;
static bool sim_thread_running = false;

/**
 * @brief Hardware simulation thread function
 * @param arg Thread arguments (unused)
 * @return void* Return value (unused)
 */
static void *hw_simulation_thread(void *arg) {
    LOG_INFO("Hardware simulation thread started");
    
    while (sim_thread_running) {
        /* Simulate packet processing between ports */
        for (uint32_t i = 0; i < switch_hw.num_ports; i++) {
            if (switch_hw.ports[i].status != PORT_STATUS_UP) {
                continue;
            }
            
            pthread_mutex_lock(&switch_hw.port_locks[i]);
            
            /* Process packets from port's tx queue to other ports' rx queues
               In a real implementation, this would involve complex switching logic
               For now, we just implement a simple flooding mechanism */
            packet_t *packet;
            while ((packet = packet_queue_dequeue(&switch_hw.ports[i].tx_queue)) != NULL) {
                /* Simple flooding to all other ports */
                for (uint32_t j = 0; j < switch_hw.num_ports; j++) {
                    if (j != i && switch_hw.ports[j].status == PORT_STATUS_UP) {
                        packet_t *pkt_copy = packet_clone(packet);
                        if (pkt_copy) {
                            pthread_mutex_lock(&switch_hw.port_locks[j]);
                            packet_queue_enqueue(&switch_hw.rx_queues[j], pkt_copy);
                            pthread_mutex_unlock(&switch_hw.port_locks[j]);
                        }
                    }
                }
                
                /* Free the original packet */
                packet_free(packet);
            }
            
            pthread_mutex_unlock(&switch_hw.port_locks[i]);
        }
        
        /* Small delay to prevent 100% CPU usage */
        usleep(1000);  /* 1ms */
    }
    
    LOG_INFO("Hardware simulation thread stopped");
    return NULL;
}

error_code_t hw_init(uint32_t num_ports) {
    if (switch_hw.initialized) {
        LOG_WARN("Hardware already initialized");
        return ERROR_ALREADY_INITIALIZED;
    }
    
    if (num_ports == 0 || num_ports > MAX_SWITCH_PORTS) {
        LOG_ERROR("Invalid number of ports: %u (max: %u)", num_ports, MAX_SWITCH_PORTS);
        return ERROR_INVALID_PARAMETER;
    }
    
    memset(&switch_hw, 0, sizeof(switch_hw));
    
    /* Allocate port array */
    switch_hw.ports = (port_t *)calloc(num_ports, sizeof(port_t));
    if (!switch_hw.ports) {
        LOG_ERROR("Failed to allocate memory for ports");
        return ERROR_MEMORY_ALLOCATION_FAILED;
    }
    
    /* Allocate RX queues */
    switch_hw.rx_queues = (packet_queue_t *)calloc(num_ports, sizeof(packet_queue_t));
    if (!switch_hw.rx_queues) {
        LOG_ERROR("Failed to allocate memory for RX queues");
        free(switch_hw.ports);
        return ERROR_MEMORY_ALLOCATION_FAILED;
    }
    
    /* Allocate port locks */
    switch_hw.port_locks = (pthread_mutex_t *)calloc(num_ports, sizeof(pthread_mutex_t));
    if (!switch_hw.port_locks) {
        LOG_ERROR("Failed to allocate memory for port locks");
        free(switch_hw.rx_queues);
        free(switch_hw.ports);
        return ERROR_MEMORY_ALLOCATION_FAILED;
    }
    
    /* Initialize ports and queues */
    for (uint32_t i = 0; i < num_ports; i++) {
        port_init(&switch_hw.ports[i], i);
        packet_queue_init(&switch_hw.rx_queues[i]);
        pthread_mutex_init(&switch_hw.port_locks[i], NULL);
    }
    
    switch_hw.num_ports = num_ports;
    switch_hw.initialized = true;
    
    LOG_INFO("Hardware initialized with %u ports", num_ports);
    
    /* Start simulation thread */
    sim_thread_running = true;
    if (pthread_create(&sim_thread, NULL, hw_simulation_thread, NULL) != 0) {
        LOG_ERROR("Failed to create simulation thread");
        hw_deinit();
        return ERROR_THREAD_CREATION_FAILED;
    }
    
    return ERROR_NONE;
}

error_code_t hw_deinit(void) {
    if (!switch_hw.initialized) {
        LOG_WARN("Hardware not initialized");
        return ERROR_NOT_INITIALIZED;
    }
    
    /* Stop simulation thread */
    if (sim_thread_running) {
        sim_thread_running = false;
        pthread_join(sim_thread, NULL);
    }
    
    /* Free resources */
    for (uint32_t i = 0; i < switch_hw.num_ports; i++) {
        packet_queue_deinit(&switch_hw.ports[i].tx_queue);
        packet_queue_deinit(&switch_hw.rx_queues[i]);
        pthread_mutex_destroy(&switch_hw.port_locks[i]);
    }
    
    free(switch_hw.port_locks);
    free(switch_hw.rx_queues);
    free(switch_hw.ports);
    
    memset(&switch_hw, 0, sizeof(switch_hw));
    
    LOG_INFO("Hardware deinitialized");
    
    return ERROR_NONE;
}

error_code_t hw_get_port(uint32_t port_id, port_t **port) {
    if (!switch_hw.initialized) {
        LOG_ERROR("Hardware not initialized");
        return ERROR_NOT_INITIALIZED;
    }
    
    if (port_id >= switch_hw.num_ports) {
        LOG_ERROR("Invalid port ID: %u (max: %u)", port_id, switch_hw.num_ports - 1);
        return ERROR_INVALID_PARAMETER;
    }
    
    if (port == NULL) {
        LOG_ERROR("Null port pointer");
        return ERROR_INVALID_PARAMETER;
    }
    
    *port = &switch_hw.ports[port_id];
    
    return ERROR_NONE;
}

error_code_t hw_receive_packet(uint32_t port_id, packet_t **packet) {
    if (!switch_hw.initialized) {
        LOG_ERROR("Hardware not initialized");
        return ERROR_NOT_INITIALIZED;
    }
    
    if (port_id >= switch_hw.num_ports) {
        LOG_ERROR("Invalid port ID: %u (max: %u)", port_id, switch_hw.num_ports - 1);
        return ERROR_INVALID_PARAMETER;
    }
    
    if (packet == NULL) {
        LOG_ERROR("Null packet pointer");
        return ERROR_INVALID_PARAMETER;
    }
    
    if (switch_hw.ports[port_id].status != PORT_STATUS_UP) {
        /* Port is down, no packets available */
        *packet = NULL;
        return ERROR_NONE;
    }
    
    pthread_mutex_lock(&switch_hw.port_locks[port_id]);
    *packet = packet_queue_dequeue(&switch_hw.rx_queues[port_id]);
    pthread_mutex_unlock(&switch_hw.port_locks[port_id]);
    
    return ERROR_NONE;
}

error_code_t hw_send_packet(uint32_t port_id, packet_t *packet) {
    if (!switch_hw.initialized) {
        LOG_ERROR("Hardware not initialized");
        return ERROR_NOT_INITIALIZED;
    }
    
    if (port_id >= switch_hw.num_ports) {
        LOG_ERROR("Invalid port ID: %u (max: %u)", port_id, switch_hw.num_ports - 1);
        return ERROR_INVALID_PARAMETER;
    }
    
    if (packet == NULL) {
        LOG_ERROR("Null packet");
        return ERROR_INVALID_PARAMETER;
    }
    
    if (switch_hw.ports[port_id].status != PORT_STATUS_UP) {
        LOG_WARN("Dropping packet on port %u: Port down", port_id);
        packet_free(packet);
        return ERROR_PORT_DOWN;
    }
    
    pthread_mutex_lock(&switch_hw.port_locks[port_id]);
    error_code_t result = packet_queue_enqueue(&switch_hw.ports[port_id].tx_queue, packet);
    pthread_mutex_unlock(&switch_hw.port_locks[port_id]);
    
    return result;
}

error_code_t hw_get_port_stats(uint32_t port_id, port_stats_t *stats) {
    if (!switch_hw.initialized) {
        LOG_ERROR("Hardware not initialized");
        return ERROR_NOT_INITIALIZED;
    }
    
    if (port_id >= switch_hw.num_ports) {
        LOG_ERROR("Invalid port ID: %u (max: %u)", port_id, switch_hw.num_ports - 1);
        return ERROR_INVALID_PARAMETER;
    }
    
    if (stats == NULL) {
        LOG_ERROR("Null stats pointer");
        return ERROR_INVALID_PARAMETER;
    }
    
    pthread_mutex_lock(&switch_hw.port_locks[port_id]);
    memcpy(stats, &switch_hw.ports[port_id].stats, sizeof(port_stats_t));
    pthread_mutex_unlock(&switch_hw.port_locks[port_id]);
    
    return ERROR_NONE;
}
