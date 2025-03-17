/**
 * @file mac_learning.c
 * @brief Implementation of MAC address learning functionality
 *
 * This file implements the MAC address learning mechanism for the switch simulator,
 * handling packet processing for MAC learning, MAC table updates, and related operations.
 */

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "common/types.h"
#include "common/error_codes.h"
#include "common/logging.h"
#include "hal/port.h"
#include "hal/packet.h"
#include "l2/mac_table.h"
#include "l2/vlan.h"
#include "l2/stp.h"

/**
 * @brief Maximum number of MAC addresses that can be learned per second per port
 */
#define MAC_LEARNING_RATE_LIMIT 100

/**
 * @brief Default MAC learning state
 */
#define MAC_LEARNING_DEFAULT_ENABLED true

/**
 * @brief MAC learning statistics structure
 */
typedef struct {
    uint32_t total_learned;          // Total MACs learned
    uint32_t total_moved;            // Total port moves detected
    uint32_t rate_limited;           // Number of rate-limited MACs
    uint32_t last_reset_time;        // Time of last stats reset
} mac_learning_stats_internal_t;

/**
 * @brief Structure to track learning rate per port
 */
typedef struct {
    uint32_t learned_count;          // Number of MACs learned in current interval
    uint32_t last_interval_time;     // Start time of current interval
    bool rate_limited;               // Whether port is currently rate limited
} port_learning_rate_t;

/**
 * @brief Global MAC learning configuration and state
 */
typedef struct {
    bool initialized;                 // Whether MAC learning is initialized
    bool learning_enabled;            // Global MAC learning enable/disable state
    bool *port_learning_enabled;      // Per-port MAC learning enable/disable state
    port_learning_rate_t *port_rates; // Per-port learning rate tracking
    mac_learning_stats_internal_t stats; // Learning statistics
    uint32_t current_time;           // Current simulation time in seconds
    uint32_t num_ports;              // Number of ports in the system
} mac_learning_state_t;

/**
 * @brief The global MAC learning state instance
 */
static mac_learning_state_t g_mac_learning;

/**
 * @brief Lock to protect MAC learning during concurrent operations
 * In a real implementation, this would be a hardware mutex or spinlock
 */
static volatile int g_mac_learning_lock = 0;

/**
 * @brief Acquire lock for MAC learning operations
 */
static inline void mac_learning_acquire_lock(void) {
    // Simulate lock acquisition with atomic operation
    while (__sync_lock_test_and_set(&g_mac_learning_lock, 1)) {
        // Spin until acquired
    }
}

/**
 * @brief Release lock for MAC learning operations
 */
static inline void mac_learning_release_lock(void) {
    // Simulate lock release with atomic operation
    __sync_lock_release(&g_mac_learning_lock);
}

/**
 * @brief Reset learning rate statistics for a port
 *
 * @param port_id Port ID
 * @param current_time Current time
 */
static void reset_port_learning_rate(port_id_t port_id, uint32_t current_time) {
    if (is_port_valid(port_id) && port_id < g_mac_learning.num_ports) {
        g_mac_learning.port_rates[port_id].learned_count = 0;
        g_mac_learning.port_rates[port_id].last_interval_time = current_time;
        g_mac_learning.port_rates[port_id].rate_limited = false;
    }
}

/**
 * @brief Check if MAC learning is enabled for a port
 *
 * @param port_id Port ID
 * @return bool True if learning is enabled, false otherwise
 */
static bool is_learning_enabled_for_port(port_id_t port_id) {
    if (!g_mac_learning.initialized || !g_mac_learning.learning_enabled) {
        return false;
    }
    
    if (!is_port_valid(port_id) || port_id >= g_mac_learning.num_ports) {
        return false;
    }
    
    return g_mac_learning.port_learning_enabled[port_id];
}

/**
 * @brief Check if a port is currently rate limited for MAC learning
 *
 * @param port_id Port ID
 * @param current_time Current time
 * @return bool True if rate limited, false otherwise
 */
static bool is_port_rate_limited(port_id_t port_id, uint32_t current_time) {
    if (!is_port_valid(port_id) || port_id >= g_mac_learning.num_ports) {
        return true; // Invalid ports are considered rate limited
    }
    
    port_learning_rate_t *rate = &g_mac_learning.port_rates[port_id];
    
    // Reset counters if we're in a new interval (1 second)
    if (current_time - rate->last_interval_time >= 1) {
        reset_port_learning_rate(port_id, current_time);
    }
    
    // Check if we've exceeded the rate limit
    if (rate->learned_count >= MAC_LEARNING_RATE_LIMIT) {
        if (!rate->rate_limited) {
            rate->rate_limited = true;
            LOG_WARN("MAC learning rate limit reached on port %u", port_id);
            g_mac_learning.stats.rate_limited++;
        }
        return true;
    }
    
    return false;
}

/**
 * @brief Update port learning rate counters
 *
 * @param port_id Port ID
 * @param current_time Current time
 */
static void update_port_learning_rate(port_id_t port_id, uint32_t current_time) {
    if (is_port_valid(port_id) && port_id < g_mac_learning.num_ports) {
        port_learning_rate_t *rate = &g_mac_learning.port_rates[port_id];
        
        // Reset counters if we're in a new interval
        if (current_time - rate->last_interval_time >= 1) {
            reset_port_learning_rate(port_id, current_time);
        }
        
        // Increment the learned count
        rate->learned_count++;
    }
}

/**
 * @brief Initialize MAC learning functionality
 *
 * @param num_ports Number of ports in the system
 * @return status_t Status code
 */
status_t mac_learning_init(uint32_t num_ports) {
    LOG_INFO("Initializing MAC learning");
    
    if (g_mac_learning.initialized) {
        LOG_WARN("MAC learning already initialized");
        return STATUS_ALREADY_EXISTS;
    }
    
    if (num_ports == 0) {
        LOG_ERROR("Invalid number of ports: %u", num_ports);
        return STATUS_INVALID_PARAMETER;
    }
    
    // Allocate per-port arrays
    g_mac_learning.port_learning_enabled = (bool*)calloc(num_ports, sizeof(bool));
    g_mac_learning.port_rates = (port_learning_rate_t*)calloc(num_ports, sizeof(port_learning_rate_t));
    
    if (g_mac_learning.port_learning_enabled == NULL || g_mac_learning.port_rates == NULL) {
        LOG_ERROR("Failed to allocate memory for MAC learning");
        mac_learning_cleanup();
        return STATUS_NO_MEMORY;
    }
    
    // Initialize default values
    g_mac_learning.num_ports = num_ports;
    g_mac_learning.learning_enabled = MAC_LEARNING_DEFAULT_ENABLED;
    g_mac_learning.current_time = 0;
    
    // Enable learning on all ports by default
    for (uint32_t i = 0; i < num_ports; i++) {
        g_mac_learning.port_learning_enabled[i] = MAC_LEARNING_DEFAULT_ENABLED;
        reset_port_learning_rate(i, 0);
    }
    
    // Reset statistics
    memset(&g_mac_learning.stats, 0, sizeof(mac_learning_stats_internal_t));
    
    g_mac_learning.initialized = true;
    
    LOG_INFO("MAC learning initialized for %u ports", num_ports);
    return STATUS_SUCCESS;
}

/**
 * @brief Clean up MAC learning resources
 *
 * @return status_t Status code
 */
status_t mac_learning_cleanup(void) {
    LOG_INFO("Cleaning up MAC learning");
    
    mac_learning_acquire_lock();
    
    // Free allocated resources
    if (g_mac_learning.port_learning_enabled != NULL) {
        free(g_mac_learning.port_learning_enabled);
        g_mac_learning.port_learning_enabled = NULL;
    }
    
    if (g_mac_learning.port_rates != NULL) {
        free(g_mac_learning.port_rates);
        g_mac_learning.port_rates = NULL;
    }
    
    g_mac_learning.initialized = false;
    
    mac_learning_release_lock();
    
    LOG_INFO("MAC learning cleanup complete");
    return STATUS_SUCCESS;
}

/**
 * @brief Enable or disable MAC learning globally
 *
 * @param enable True to enable, false to disable
 * @return status_t Status code
 */
status_t mac_learning_set_global_state(bool enable) {
    if (!g_mac_learning.initialized) {
        LOG_ERROR("MAC learning not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    mac_learning_acquire_lock();
    g_mac_learning.learning_enabled = enable;
    mac_learning_release_lock();
    
    LOG_INFO("MAC learning globally %s", enable ? "enabled" : "disabled");
    return STATUS_SUCCESS;
}

/**
 * @brief Enable or disable MAC learning on a specific port
 *
 * @param port_id Port ID
 * @param enable True to enable, false to disable
 * @return status_t Status code
 */
status_t mac_learning_set_port_state(port_id_t port_id, bool enable) {
    if (!g_mac_learning.initialized) {
        LOG_ERROR("MAC learning not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (!is_port_valid(port_id) || port_id >= g_mac_learning.num_ports) {
        LOG_ERROR("Invalid port ID: %u", port_id);
        return STATUS_INVALID_PARAMETER;
    }
    
    mac_learning_acquire_lock();
    g_mac_learning.port_learning_enabled[port_id] = enable;
    mac_learning_release_lock();
    
    LOG_INFO("MAC learning %s on port %u", enable ? "enabled" : "disabled", port_id);
    return STATUS_SUCCESS;
}

/**
 * @brief Process a packet for MAC learning
 *
 * @param packet Packet to process
 * @param port_id Ingress port ID
 * @param current_time Current time
 * @return status_t Status code
 */
status_t mac_learning_process_packet(const packet_buffer_t *packet, port_id_t port_id, uint32_t current_time) {
    if (!g_mac_learning.initialized) {
        LOG_ERROR("MAC learning not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (packet == NULL) {
        LOG_ERROR("NULL packet pointer");
        return STATUS_INVALID_PARAMETER;
    }
    
    if (!is_port_valid(port_id) || port_id >= g_mac_learning.num_ports) {
        LOG_ERROR("Invalid port ID: %u", port_id);
        return STATUS_INVALID_PARAMETER;
    }
    
    mac_learning_acquire_lock();
    
    // Update current time
    g_mac_learning.current_time = current_time;
    
    // Check if learning is enabled for this port
    if (!is_learning_enabled_for_port(port_id)) {
        mac_learning_release_lock();
        return STATUS_SUCCESS; // Learning disabled, but not an error
    }
    
    // Extract Ethernet header from packet
    ethernet_header_t eth_header;
    status_t status = packet_get_ethernet_header(packet, &eth_header);
    if (status != STATUS_SUCCESS) {
        mac_learning_release_lock();
        LOG_ERROR("Failed to extract Ethernet header from packet");
        return status;
    }
    
    // Get VLAN ID from packet
    vlan_id_t vlan_id = VLAN_ID_DEFAULT;
    status = packet_get_vlan_id(packet, &vlan_id);
    if (status != STATUS_SUCCESS && status != STATUS_NOT_FOUND) {
        mac_learning_release_lock();
        LOG_ERROR("Failed to get VLAN ID from packet");
        return status;
    }
    
    // Don't learn from multicast/broadcast source MACs
    if (eth_header.src_mac[0] & 0x01) {
        mac_learning_release_lock();
        LOG_DEBUG("Skipping learning for multicast/broadcast source MAC");
        return STATUS_SUCCESS;
    }
    
    // Check if port is in forwarding state for this VLAN
    stp_port_state_t stp_state;
    status = stp_get_port_state(port_id, vlan_id, &stp_state);
    if (status == STATUS_SUCCESS && stp_state != STP_PORT_STATE_FORWARDING) {
        mac_learning_release_lock();
        LOG_DEBUG("Skipping learning on port %u in non-forwarding STP state", port_id);
        return STATUS_SUCCESS;
    }
    
    // Check rate limiting
    if (is_port_rate_limited(port_id, current_time)) {
        mac_learning_release_lock();
        return STATUS_SUCCESS; // Rate limited, but not an error
    }
    
    // Check if MAC already exists in table for this VLAN
    port_id_t existing_port;
    status = mac_table_lookup(eth_header.src_mac, vlan_id, &existing_port);
    
    if (status == STATUS_SUCCESS) {
        // MAC already exists
        if (existing_port != port_id) {
            // MAC has moved to a different port
            LOG_INFO("MAC %02x:%02x:%02x:%02x:%02x:%02x moved from port %u to port %u on VLAN %u",
                    eth_header.src_mac[0], eth_header.src_mac[1], eth_header.src_mac[2],
                    eth_header.src_mac[3], eth_header.src_mac[4], eth_header.src_mac[5],
                    existing_port, port_id, vlan_id);
            
            // Update MAC table with new port
            status = mac_table_add(eth_header.src_mac, port_id, vlan_id, false);
            if (status == STATUS_SUCCESS) {
                g_mac_learning.stats.total_moved++;
            }
        }
        // If on same port, MAC table entry was already updated by mac_table_lookup
    }
    else if (status == STATUS_NOT_FOUND)
    {
      // New MAC address - learn it
      status = mac_table_add(eth_header.src_mac, port_id, vlan_id, false);
      if (status == STATUS_SUCCESS) {
          LOG_DEBUG("Learned new MAC %02x:%02x:%02x:%02x:%02x:%02x on port %u VLAN %u",
                  eth_header.src_mac[0], eth_header.src_mac[1], eth_header.src_mac[2],
                  eth_header.src_mac[3], eth_header.src_mac[4], eth_header.src_mac[5],
                  port_id, vlan_id);

          // Update statistics
          g_mac_learning.stats.total_learned++;
          update_port_learning_rate(port_id, current_time);
      } else if (status == STATUS_TABLE_FULL) {
          LOG_WARN("Failed to learn MAC: MAC table is full");
      } else {
          LOG_ERROR("Failed to learn MAC: error %d", status);
      }
    }
    else {
      // Some other error during lookup
      LOG_ERROR("MAC table lookup failed with error %d", status);
  }

  mac_learning_release_lock();
  return status;
}

/**
 * @brief Reset MAC learning statistics
 *
 * @return status_t Status code
 */
status_t mac_learning_reset_stats(void) {
    if (!g_mac_learning.initialized) {
        LOG_ERROR("MAC learning not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    mac_learning_acquire_lock();

    // Reset statistics
    g_mac_learning.stats.total_learned = 0;
    g_mac_learning.stats.total_moved = 0;
    g_mac_learning.stats.rate_limited = 0;
    g_mac_learning.stats.last_reset_time = g_mac_learning.current_time;

    mac_learning_release_lock();

    LOG_INFO("MAC learning statistics reset");
    return STATUS_SUCCESS;
}

/**
 * @brief Get MAC learning statistics
 *
 * @param stats Pointer to statistics structure to fill
 * @return status_t Status code
 */
status_t mac_learning_get_stats(mac_learning_stats_t *stats) {
    if (!g_mac_learning.initialized) {
        LOG_ERROR("MAC learning not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    if (stats == NULL) {
        LOG_ERROR("NULL stats pointer");
        return STATUS_INVALID_PARAMETER;
    }

    mac_learning_acquire_lock();

    // Fill statistics structure
    stats->total_learned = g_mac_learning.stats.total_learned;
    stats->total_moved = g_mac_learning.stats.total_moved;
    stats->rate_limited = g_mac_learning.stats.rate_limited;
    stats->learning_enabled = g_mac_learning.learning_enabled;

    mac_learning_release_lock();

    return STATUS_SUCCESS;
}

/**
 * @brief Get MAC learning state for a specific port
 *
 * @param port_id Port ID
 * @param enabled Pointer to store the enabled state
 * @return status_t Status code
 */
status_t mac_learning_get_port_state(port_id_t port_id, bool *enabled) {
    if (!g_mac_learning.initialized) {
        LOG_ERROR("MAC learning not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    if (!is_port_valid(port_id) || port_id >= g_mac_learning.num_ports) {
        LOG_ERROR("Invalid port ID: %u", port_id);
        return STATUS_INVALID_PARAMETER;
    }

    if (enabled == NULL) {
        LOG_ERROR("NULL enabled pointer");
        return STATUS_INVALID_PARAMETER;
    }

    mac_learning_acquire_lock();
    *enabled = g_mac_learning.port_learning_enabled[port_id];
    mac_learning_release_lock();

    return STATUS_SUCCESS;
}

/**
 * @brief Process MAC learning aging based on MAC table aging
 *
 * This function should be called periodically to ensure MAC table
 * entries are properly aged out.
 *
 * @param current_time Current system time in seconds
 * @return status_t Status code
 */
status_t mac_learning_process_aging(uint32_t current_time) {
    if (!g_mac_learning.initialized) {
        LOG_ERROR("MAC learning not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    mac_learning_acquire_lock();

    // Update current time
    g_mac_learning.current_time = current_time;

    // Reset rate limiting counters if needed
    for (port_id_t port_id = 0; port_id < g_mac_learning.num_ports; port_id++) {
        port_learning_rate_t *rate = &g_mac_learning.port_rates[port_id];
        if (current_time - rate->last_interval_time >= 1) {
            reset_port_learning_rate(port_id, current_time);
        }
    }

    mac_learning_release_lock();

    // Call MAC table aging process to age out entries
    status_t status = mac_table_process_aging(current_time);
    if (status != STATUS_SUCCESS) {
        LOG_ERROR("MAC table aging process failed: %d", status);
    }

    return status;
}

/**
 * @brief Flush MAC learning entries for a port or VLAN
 *
 * @param vlan_id VLAN ID to flush (0 for all VLANs)
 * @param port_id Port ID to flush (PORT_ID_INVALID for all ports)
 * @return status_t Status code
 */
status_t mac_learning_flush(vlan_id_t vlan_id, port_id_t port_id) {
    if (!g_mac_learning.initialized) {
        LOG_ERROR("MAC learning not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    if (port_id != PORT_ID_INVALID && (port_id >= g_mac_learning.num_ports || !is_port_valid(port_id))) {
        LOG_ERROR("Invalid port ID: %u", port_id);
        return STATUS_INVALID_PARAMETER;
    }

    // Only flush dynamic entries (learned ones)
    status_t status = mac_table_flush(vlan_id, port_id, false);
    if (status != STATUS_SUCCESS) {
        LOG_ERROR("Failed to flush MAC entries: %d", status);
        return status;
    }

    LOG_INFO("Flushed dynamic MAC entries for VLAN %u, port %u",
            vlan_id ? vlan_id : VLAN_ID_ALL, port_id != PORT_ID_INVALID ? port_id : PORT_ID_ALL);

    return STATUS_SUCCESS;
}

/**
 * @brief Handle port state change events
 *
 * This function is called when a port's operational state changes,
 * to update MAC learning behavior accordingly.
 *
 * @param port_id Port ID
 * @param is_up Whether port is now up
 * @return status_t Status code
 */
status_t mac_learning_handle_port_state_change(port_id_t port_id, bool is_up) {
    if (!g_mac_learning.initialized) {
        LOG_ERROR("MAC learning not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    if (!is_port_valid(port_id) || port_id >= g_mac_learning.num_ports) {
        LOG_ERROR("Invalid port ID: %u", port_id);
        return STATUS_INVALID_PARAMETER;
    }

    status_t status = STATUS_SUCCESS;

    // If port went down, flush learned MACs on this port
    if (!is_up) {
        LOG_INFO("Port %u went down, flushing dynamic MAC entries", port_id);
        status = mac_learning_flush(0, port_id);
    }

    return status;
}

/**
 * @brief Handle VLAN state change events
 *
 * This function is called when a VLAN's state changes,
 * to update MAC learning behavior accordingly.
 *
 * @param vlan_id VLAN ID
 * @param is_active Whether VLAN is now active
 * @return status_t Status code
 */
status_t mac_learning_handle_vlan_state_change(vlan_id_t vlan_id, bool is_active) {
    if (!g_mac_learning.initialized) {
        LOG_ERROR("MAC learning not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    status_t status = STATUS_SUCCESS;

    // If VLAN was disabled, flush all learned MACs in this VLAN
    if (!is_active) {
        LOG_INFO("VLAN %u became inactive, flushing dynamic MAC entries", vlan_id);
        status = mac_learning_flush(vlan_id, PORT_ID_INVALID);
    }

    return status;
}



