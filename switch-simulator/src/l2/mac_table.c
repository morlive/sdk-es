/**
 * @file mac_table.c
 * @brief Implementation of MAC address table functionality
 *
 * This file implements the MAC address table for the switch simulator,
 * including functions for adding, removing, searching, and aging MAC entries.
 */

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "common/types.h"
#include "common/error_codes.h"
#include "common/logging.h"
#include "hal/port.h"
#include "l2/mac_table.h"

/**
 * @brief Default size of the MAC table hash
 */
#define MAC_TABLE_DEFAULT_SIZE 1024

/**
 * @brief Default aging time in seconds
 */
#define MAC_DEFAULT_AGING_TIME 300 // 5 minutes

/**
 * @brief Maximum number of MAC entries
 */
#define MAC_TABLE_MAX_ENTRIES 16384

/**
 * @brief Structure for MAC table entry
 */
typedef struct mac_entry {
    mac_address_t mac_addr;      // MAC address
    port_id_t port_id;           // Port ID where this MAC was learned
    vlan_id_t vlan_id;           // VLAN ID for this MAC
    uint32_t last_seen;          // Timestamp when this entry was last used
    bool is_static;              // Whether this is a static entry (doesn't age)
    struct mac_entry *next;      // Pointer to next entry (for hash collisions)
} mac_entry_t;

/**
 * @brief Structure for MAC table
 */
typedef struct {
    mac_entry_t **entries;       // Array of entry pointers (hash table)
    uint32_t size;               // Size of the hash table
    uint32_t count;              // Number of entries in the table
    uint32_t aging_time;         // Aging time in seconds
    uint32_t current_time;       // Current simulated time
} mac_table_internal_t;

/**
 * @brief The global MAC table instance
 */
static mac_table_internal_t g_mac_table;

/**
 * @brief Lock to protect MAC table during concurrent operations
 * In a real implementation, this would be a hardware mutex or spinlock
 */
static volatile int g_mac_table_lock = 0;

/**
 * @brief Acquire lock for MAC table access
 */
static inline void mac_table_acquire_lock(void) {
    // Simulate lock acquisition with atomic operation
    while (__sync_lock_test_and_set(&g_mac_table_lock, 1)) {
        // Spin until acquired
    }
}

/**
 * @brief Release lock for MAC table access
 */
static inline void mac_table_release_lock(void) {
    // Simulate lock release with atomic operation
    __sync_lock_release(&g_mac_table_lock);
}

/**
 * @brief Simple hash function for MAC addresses
 *
 * @param mac MAC address to hash
 * @param vlan VLAN ID to include in hash
 * @return uint32_t Hash value
 */
static uint32_t mac_hash(const mac_address_t mac, vlan_id_t vlan) {
    uint32_t hash = 0;
    
    // Combine all bytes of the MAC address
    for (int i = 0; i < MAC_ADDR_LEN; i++) {
        hash = (hash << 3) ^ (hash >> 29) ^ mac[i];
    }
    
    // Include VLAN ID in the hash
    hash = (hash << 5) ^ (hash >> 27) ^ vlan;
    
    return hash % g_mac_table.size;
}

/**
 * @brief Initialize the MAC table
 *
 * @param size Hash table size (0 for default)
 * @param aging_time Aging time in seconds (0 for default)
 * @return status_t Status code
 */
status_t mac_table_init(uint32_t size, uint32_t aging_time) {
    LOG_INFO("Initializing MAC table");
    
    // Use defaults if parameters are 0
    if (size == 0) {
        size = MAC_TABLE_DEFAULT_SIZE;
    }
    
    if (aging_time == 0) {
        aging_time = MAC_DEFAULT_AGING_TIME;
    }
    
    // Allocate the hash table
    g_mac_table.entries = (mac_entry_t**)calloc(size, sizeof(mac_entry_t*));
    if (g_mac_table.entries == NULL) {
        LOG_ERROR("Failed to allocate memory for MAC table");
        return STATUS_NO_MEMORY;
    }
    
    g_mac_table.size = size;
    g_mac_table.count = 0;
    g_mac_table.aging_time = aging_time;
    g_mac_table.current_time = 0;
    
    LOG_INFO("MAC table initialized with size %u and aging time %u seconds", 
            size, aging_time);
            
    return STATUS_SUCCESS;
}

/**
 * @brief Clean up and free the MAC table resources
 *
 * @return status_t Status code
 */
status_t mac_table_cleanup(void) {
    LOG_INFO("Cleaning up MAC table");
    
    if (g_mac_table.entries == NULL) {
        return STATUS_SUCCESS;  // Already cleaned up
    }
    
    mac_table_acquire_lock();
    
    // Free all entries
    for (uint32_t i = 0; i < g_mac_table.size; i++) {
        mac_entry_t *entry = g_mac_table.entries[i];
        while (entry != NULL) {
            mac_entry_t *next = entry->next;
            free(entry);
            entry = next;
        }
    }
    
    // Free the hash table
    free(g_mac_table.entries);
    g_mac_table.entries = NULL;
    g_mac_table.count = 0;
    
    mac_table_release_lock();
    
    LOG_INFO("MAC table cleanup complete");
    
    return STATUS_SUCCESS;
}

/**
 * @brief Add or update a MAC entry in the table
 *
 * @param mac MAC address
 * @param port_id Port ID
 * @param vlan_id VLAN ID
 * @param is_static Whether this is a static entry
 * @return status_t Status code
 */
status_t mac_table_add(const mac_address_t mac, port_id_t port_id, 
                       vlan_id_t vlan_id, bool is_static) {
    if (g_mac_table.entries == NULL) {
        LOG_ERROR("MAC table not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (!is_port_valid(port_id)) {
        LOG_ERROR("Invalid port ID: %u", port_id);
        return STATUS_INVALID_PARAMETER;
    }
    
    mac_table_acquire_lock();
    
    // Check if we've reached maximum capacity
    if (g_mac_table.count >= MAC_TABLE_MAX_ENTRIES) {
        mac_table_release_lock();
        LOG_ERROR("MAC table is full");
        return STATUS_TABLE_FULL;
    }
    
    uint32_t hash = mac_hash(mac, vlan_id);
    mac_entry_t *entry = g_mac_table.entries[hash];
    mac_entry_t *prev = NULL;
    
    // Look for existing entry
    while (entry != NULL) {
        if (memcmp(entry->mac_addr, mac, MAC_ADDR_LEN) == 0 && 
            entry->vlan_id == vlan_id) {
            // Found existing entry, update it
            entry->port_id = port_id;
            entry->last_seen = g_mac_table.current_time;
            
            // If entry is being changed from dynamic to static
            if (!entry->is_static && is_static) {
                entry->is_static = true;
            }
            
            mac_table_release_lock();
            LOG_DEBUG("Updated MAC entry: %02x:%02x:%02x:%02x:%02x:%02x on port %u VLAN %u",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], port_id, vlan_id);
            return STATUS_SUCCESS;
        }
        prev = entry;
        entry = entry->next;
    }
    
    // Create new entry
    mac_entry_t *new_entry = (mac_entry_t*)malloc(sizeof(mac_entry_t));
    if (new_entry == NULL) {
        mac_table_release_lock();
        LOG_ERROR("Failed to allocate memory for MAC entry");
        return STATUS_NO_MEMORY;
    }
    
    // Initialize the new entry
    memcpy(new_entry->mac_addr, mac, MAC_ADDR_LEN);
    new_entry->port_id = port_id;
    new_entry->vlan_id = vlan_id;
    new_entry->last_seen = g_mac_table.current_time;
    new_entry->is_static = is_static;
    new_entry->next = NULL;
    
    // Add to hash table
    if (prev == NULL) {
        g_mac_table.entries[hash] = new_entry;
    } else {
        prev->next = new_entry;
    }
    
    g_mac_table.count++;
    
    mac_table_release_lock();
    
    LOG_DEBUG("Added new MAC entry: %02x:%02x:%02x:%02x:%02x:%02x on port %u VLAN %u %s",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], 
             port_id, vlan_id, is_static ? "(static)" : "");
             
    return STATUS_SUCCESS;
}

/**
 * @brief Remove a MAC entry from the table
 *
 * @param mac MAC address
 * @param vlan_id VLAN ID
 * @return status_t Status code
 */
status_t mac_table_remove(const mac_address_t mac, vlan_id_t vlan_id) {
    if (g_mac_table.entries == NULL) {
        LOG_ERROR("MAC table not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    mac_table_acquire_lock();
    
    uint32_t hash = mac_hash(mac, vlan_id);
    mac_entry_t *entry = g_mac_table.entries[hash];
    mac_entry_t *prev = NULL;
    
    while (entry != NULL) {
        if (memcmp(entry->mac_addr, mac, MAC_ADDR_LEN) == 0 && 
            entry->vlan_id == vlan_id) {
            // Found the entry, remove it
            if (prev == NULL) {
                g_mac_table.entries[hash] = entry->next;
            } else {
                prev->next = entry->next;
            }
            
            free(entry);
            g_mac_table.count--;
            
            mac_table_release_lock();
            
            LOG_DEBUG("Removed MAC entry: %02x:%02x:%02x:%02x:%02x:%02x VLAN %u",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], vlan_id);
            return STATUS_SUCCESS;
        }
        
        prev = entry;
        entry = entry->next;
    }
    
    mac_table_release_lock();
    
    LOG_DEBUG("MAC entry not found for removal: %02x:%02x:%02x:%02x:%02x:%02x VLAN %u",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], vlan_id);
    return STATUS_NOT_FOUND;
}

/**
 * @brief Look up a MAC address in the table
 *
 * @param mac MAC address to look up
 * @param vlan_id VLAN ID
 * @param port_id Pointer to store the port ID if found
 * @return status_t Status code
 */
status_t mac_table_lookup(const mac_address_t mac, vlan_id_t vlan_id, 
                         port_id_t *port_id) {
    if (g_mac_table.entries == NULL) {
        LOG_ERROR("MAC table not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (port_id == NULL) {
        LOG_ERROR("NULL port_id pointer");
        return STATUS_INVALID_PARAMETER;
    }
    
    mac_table_acquire_lock();
    
    uint32_t hash = mac_hash(mac, vlan_id);
    mac_entry_t *entry = g_mac_table.entries[hash];
    
    while (entry != NULL) {
        if (memcmp(entry->mac_addr, mac, MAC_ADDR_LEN) == 0 && 
            entry->vlan_id == vlan_id) {
            // Found the entry
            *port_id = entry->port_id;
            entry->last_seen = g_mac_table.current_time;  // Update timestamp
            
            mac_table_release_lock();
            
            LOG_DEBUG("MAC lookup found: %02x:%02x:%02x:%02x:%02x:%02x VLAN %u -> port %u",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], vlan_id, *port_id);
            return STATUS_SUCCESS;
        }
        
        entry = entry->next;
    }
    
    mac_table_release_lock();
    
    LOG_DEBUG("MAC lookup not found: %02x:%02x:%02x:%02x:%02x:%02x VLAN %u",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], vlan_id);
    return STATUS_NOT_FOUND;
}

/**
 * @brief Flush all entries from the MAC table
 *
 * @param vlan_id VLAN ID to flush (0 for all VLANs)
 * @param port_id Port ID to flush (PORT_ID_INVALID for all ports)
 * @param flush_static Whether to flush static entries too
 * @return status_t Status code
 */
status_t mac_table_flush(vlan_id_t vlan_id, port_id_t port_id, bool flush_static) {
    if (g_mac_table.entries == NULL) {
        LOG_ERROR("MAC table not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    mac_table_acquire_lock();
    
    uint32_t flushed = 0;
    
    // Iterate through all hash buckets
    for (uint32_t i = 0; i < g_mac_table.size; i++) {
        mac_entry_t *entry = g_mac_table.entries[i];
        mac_entry_t *prev = NULL;
        
        while (entry != NULL) {
            bool should_flush = true;
            
            // Check if this entry should be flushed based on criteria
            if (vlan_id != 0 && entry->vlan_id != vlan_id) {
                should_flush = false;
            }
            
            if (port_id != PORT_ID_INVALID && entry->port_id != port_id) {
                should_flush = false;
            }
            
            if (!flush_static && entry->is_static) {
                should_flush = false;
            }
            
            if (should_flush) {
                // Remove this entry
                mac_entry_t *to_remove = entry;
                
                if (prev == NULL) {
                    g_mac_table.entries[i] = entry->next;
                    entry = entry->next;
                } else {
                    prev->next = entry->next;
                    entry = entry->next;
                }
                
                free(to_remove);
                g_mac_table.count--;
                flushed++;
            } else {
                // Keep this entry and move to next
                prev = entry;
                entry = entry->next;
            }
        }
    }
    
    mac_table_release_lock();
    
    LOG_INFO("Flushed %u MAC table entries", flushed);
    return STATUS_SUCCESS;
}

/**
 * @brief Process aging of MAC table entries
 *
 * Removes entries that have exceeded the aging time threshold.
 * This function should be called periodically by the system.
 *
 * @param current_time Current system time in seconds
 * @return status_t Status code
 */
status_t mac_table_process_aging(uint32_t current_time) {
    if (g_mac_table.entries == NULL) {
        LOG_ERROR("MAC table not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    mac_table_acquire_lock();
    
    // Update the current time
    g_mac_table.current_time = current_time;
    
    uint32_t aged_out = 0;
    
    // Iterate through all hash buckets
    for (uint32_t i = 0; i < g_mac_table.size; i++) {
        mac_entry_t *entry = g_mac_table.entries[i];
        mac_entry_t *prev = NULL;
        
        while (entry != NULL) {
            // Skip static entries, they don't age
            if (entry->is_static) {
                prev = entry;
                entry = entry->next;
                continue;
            }
            
            // Check if this entry has aged out
            uint32_t age = current_time - entry->last_seen;
            if (age > g_mac_table.aging_time) {
                // Remove this entry
                mac_entry_t *to_remove = entry;
                
                if (prev == NULL) {
                    g_mac_table.entries[i] = entry->next;
                    entry = entry->next;
                } else {
                    prev->next = entry->next;
                    entry = entry->next;
                }
                
                LOG_DEBUG("Aged out MAC entry: %02x:%02x:%02x:%02x:%02x:%02x VLAN %u Port %u",
                         to_remove->mac_addr[0], to_remove->mac_addr[1], 
                         to_remove->mac_addr[2], to_remove->mac_addr[3], 
                         to_remove->mac_addr[4], to_remove->mac_addr[5], 
                         to_remove->vlan_id, to_remove->port_id);
                
                free(to_remove);
                g_mac_table.count--;
                aged_out++;
            } else {
                // Keep this entry and move to next
                prev = entry;
                entry = entry->next;
            }
        }
    }
    
    mac_table_release_lock();
    
    if (aged_out > 0) {
        LOG_DEBUG("Aged out %u MAC table entries", aged_out);
    }
    
    return STATUS_SUCCESS;
}

/**
 * @brief Get MAC table statistics
 *
 * @param stats Pointer to statistics structure to fill
 * @return status_t Status code
 */
status_t mac_table_get_stats(mac_table_stats_t *stats) {
    if (g_mac_table.entries == NULL) {
        LOG_ERROR("MAC table not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (stats == NULL) {
        LOG_ERROR("NULL stats pointer");
        return STATUS_INVALID_PARAMETER;
    }
    
    mac_table_acquire_lock();
    
    // Count static and dynamic entries
    uint32_t static_count = 0;
    uint32_t dynamic_count = 0;
    
    for (uint32_t i = 0; i < g_mac_table.size; i++) {
        mac_entry_t *entry = g_mac_table.entries[i];
        
        while (entry != NULL) {
            if (entry->is_static) {
                static_count++;
            } else {
                dynamic_count++;
            }
            entry = entry->next;
        }
    }
    
    // Fill statistics
    stats->total_entries = g_mac_table.count;
    stats->static_entries = static_count;
    stats->dynamic_entries = dynamic_count;
    stats->table_size = g_mac_table.size;
    stats->aging_time = g_mac_table.aging_time;
    
    mac_table_release_lock();
    
    return STATUS_SUCCESS;
}

/**
 * @brief Set MAC table aging time
 *
 * @param aging_time New aging time in seconds
 * @return status_t Status code
 */
status_t mac_table_set_aging_time(uint32_t aging_time) {
    if (g_mac_table.entries == NULL) {
        LOG_ERROR("MAC table not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (aging_time == 0) {
        LOG_ERROR("Invalid aging time: %u", aging_time);
        return STATUS_INVALID_PARAMETER;
    }
    
    mac_table_acquire_lock();
    g_mac_table.aging_time = aging_time;
    mac_table_release_lock();
    
    LOG_INFO("MAC table aging time set to %u seconds", aging_time);
    return STATUS_SUCCESS;
}

/**
 * @brief Iterate through MAC table entries
 *
 * This function allows the caller to iterate through all MAC table entries
 * and perform an action on each one via the callback function.
 *
 * @param callback Function to call for each entry
 * @param user_data User data to pass to the callback
 * @return status_t Status code
 */
status_t mac_table_iterate(mac_table_iter_cb_t callback, void *user_data) {
    if (g_mac_table.entries == NULL) {
        LOG_ERROR("MAC table not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (callback == NULL) {
        LOG_ERROR("NULL callback function");
        return STATUS_INVALID_PARAMETER;
    }
    
    mac_table_acquire_lock();
    
    mac_entry_info_t info;
    bool continue_iteration = true;
    
    // Iterate through all hash buckets
    for (uint32_t i = 0; i < g_mac_table.size && continue_iteration; i++) {
        mac_entry_t *entry = g_mac_table.entries[i];
        
        while (entry != NULL && continue_iteration) {
            // Fill entry info for callback
            memcpy(info.mac_addr, entry->mac_addr, MAC_ADDR_LEN);
            info.port_id = entry->port_id;
            info.vlan_id = entry->vlan_id;
            info.is_static = entry->is_static;
            info.age = g_mac_table.current_time - entry->last_seen;
            
            // Call the callback function
            continue_iteration = callback(&info, user_data);
            
            entry = entry->next;
        }
    }
    
    mac_table_release_lock();
    
    return STATUS_SUCCESS;
}


