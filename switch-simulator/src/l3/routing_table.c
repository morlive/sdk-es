/**
 * @file routing_table.c
 * @brief Implementation of the L3 routing table functionality
 *
 * This file contains the implementation of routing table operations for the
 * switch simulator, including route insertion, deletion, lookup, and management.
 */

#include "l3/routing_table.h"
#include "common/logging.h"
#include "common/error_codes.h"
#include "hal/hw_resources.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

/* Defines */
#define MAX_ROUTES 1024
#define ROUTE_HASH_SIZE 256
#define IPV4_ADDR_LEN 4
#define IPV6_ADDR_LEN 16

/* Private data types */
typedef struct route_entry {
    routing_entry_t info;
    struct route_entry *next;       /* For hash collision resolution */
    struct route_entry *lpm_left;   /* For LPM tree traversal - left child */
    struct route_entry *lpm_right;  /* For LPM tree traversal - right child */
} route_entry_t;

/* Routing table structure */
typedef struct {
    route_entry_t *hash_table[ROUTE_HASH_SIZE];  /* Hash table for O(1) exact lookup */
    route_entry_t *lpm_root_v4;                  /* Root of IPv4 LPM tree */
    route_entry_t *lpm_root_v6;                  /* Root of IPv6 LPM tree */
    route_entry_t *route_pool;                   /* Pre-allocated route entries */
    uint16_t route_count;                        /* Number of routes in the table */
    bool hw_sync_enabled;                        /* Flag indicating if HW sync is enabled */
} routing_table_t;

/* Global variables */
static routing_table_t g_routing_table;
static bool g_routing_initialized = false;

/* Forward declarations of private functions */
static uint32_t hash_ipv4_prefix(const ipv4_addr_t *prefix, uint8_t prefix_len);
static uint32_t hash_ipv6_prefix(const ipv6_addr_t *prefix, uint8_t prefix_len);
static route_entry_t *find_route_exact(const ip_addr_t *prefix, uint8_t prefix_len, ip_addr_type_t type);
static void insert_to_lpm_tree(route_entry_t *entry);
static void remove_from_lpm_tree(route_entry_t *entry);
static route_entry_t *find_route_lpm(const ip_addr_t *addr, ip_addr_type_t type);
static void sync_route_to_hw(const route_entry_t *entry, hw_operation_t operation);
static route_entry_t *allocate_route_entry(void);
static void free_route_entry(route_entry_t *entry);
static bool prefix_match(const ip_addr_t *addr, const ip_addr_t *prefix, uint8_t prefix_len, ip_addr_type_t type);
static uint8_t get_common_prefix_len(const ip_addr_t *addr1, const ip_addr_t *addr2, ip_addr_type_t type);
static bool get_bit_from_prefix(const ip_addr_t *prefix, uint8_t bit_pos, ip_addr_type_t type);

/**
 * @brief Initialize the routing table
 *
 * @return STATUS_SUCCESS if successful, error code otherwise
 */
status_t routing_table_init(void) {
    LOG_INFO("Initializing routing table module");
    
    if (g_routing_initialized) {
        LOG_WARN("Routing table already initialized");
        return STATUS_ALREADY_INITIALIZED;
    }

    /* Clear the routing table structure */
    memset(&g_routing_table, 0, sizeof(g_routing_table));
    
    /* Pre-allocate route entries */
    g_routing_table.route_pool = (route_entry_t *)calloc(MAX_ROUTES, sizeof(route_entry_t));
    if (!g_routing_table.route_pool) {
        LOG_ERROR("Failed to allocate memory for routing table entries");
        return STATUS_NO_MEMORY;
    }
    
    /* Enable HW sync by default */
    g_routing_table.hw_sync_enabled = true;
    
    g_routing_initialized = true;
    LOG_INFO("Routing table initialized successfully, capacity: %d entries", MAX_ROUTES);
    
    return STATUS_SUCCESS;
}

// ========================== new version of routing_table_init ==========================
// /**
//  * @brief Initialize the routing table
//  *
//  * @param table Pointer to the routing table structure to initialize
//  * @return STATUS_SUCCESS if successful, error code otherwise
//  */
// status_t routing_table_init(routing_table_t *table) {
//     if (!table) {
//         LOG_ERROR("Invalid parameter: table pointer is NULL");
//         return STATUS_INVALID_PARAMETER;
//     }
// 
//     LOG_INFO("Initializing routing table");
// 
//     /* Clear the routing table structure */
//     memset(table, 0, sizeof(routing_table_t));
// 
//     /* Pre-allocate route entries */
//     table->route_pool = (route_entry_t *)calloc(MAX_ROUTES, sizeof(route_entry_t));
//     if (!table->route_pool) {
//         LOG_ERROR("Failed to allocate memory for routing table entries");
//         return STATUS_NO_MEMORY;
//     }
// 
//     /* Enable HW sync by default */
//     table->hw_sync_enabled = true;
//     table->route_count = 0;
// 
//     LOG_INFO("Routing table initialized successfully, capacity: %d entries", MAX_ROUTES);
//     return STATUS_SUCCESS;
// }
// =======================================================================================

/**
 * @brief Clean up the routing table resources
 *
 * @return STATUS_SUCCESS if successful, error code otherwise
 */
status_t routing_table_deinit(void) {
    LOG_INFO("Deinitializing routing table module");
    
    if (!g_routing_initialized) {
        LOG_WARN("Routing table not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    /* Free the route pool */
    free(g_routing_table.route_pool);
    
    /* Reset the initialized flag */
    g_routing_initialized = false;
    
    LOG_INFO("Routing table deinitialized successfully");
    
    return STATUS_SUCCESS;
}

/**
 * @brief Add a route to the routing table
 *
 * @param route Pointer to the route information
 * @return STATUS_SUCCESS if successful, error code otherwise
 */
status_t routing_table_add_route(const routing_entry_t *route) {
    route_entry_t *entry;
    uint32_t hash_index;
    
    if (!g_routing_initialized) {
        LOG_ERROR("Routing table not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (route == NULL) {
        LOG_ERROR("NULL route pointer provided to routing_table_add_route");
        return STATUS_INVALID_PARAM;
    }
    
    if (g_routing_table.route_count >= MAX_ROUTES) {
        LOG_ERROR("Routing table is full (%d entries)", MAX_ROUTES);
        return STATUS_TABLE_FULL;
    }
    
    /* Check if the route already exists */
    if (find_route_exact(&route->prefix, route->prefix_len, route->addr_type)) {
        LOG_WARN("Route already exists");
        return STATUS_ALREADY_EXISTS;
    }
    
    /* Allocate a new route entry */
    entry = allocate_route_entry();
    if (!entry) {
        LOG_ERROR("Failed to allocate route entry");
        return STATUS_NO_MEMORY;
    }
    
    /* Copy the route information */
    memcpy(&entry->info, route, sizeof(routing_entry_t));
    
    /* Calculate the hash index */
    if (route->addr_type == IP_ADDR_TYPE_V4) {
        hash_index = hash_ipv4_prefix(&route->prefix.v4, route->prefix_len) % ROUTE_HASH_SIZE;
    } else {
        hash_index = hash_ipv6_prefix(&route->prefix.v6, route->prefix_len) % ROUTE_HASH_SIZE;
    }
    
    /* Insert into the hash table (at the beginning of the chain) */
    entry->next = g_routing_table.hash_table[hash_index];
    g_routing_table.hash_table[hash_index] = entry;
    
    /* Insert into the LPM tree */
    insert_to_lpm_tree(entry);
    
    /* Increment the route count */
    g_routing_table.route_count++;
    
    LOG_INFO("Added route to %s/%d via next hop %s (metric %d, interface %d)",
             (route->addr_type == IP_ADDR_TYPE_V4) ? 
                 inet_ntoa(*((struct in_addr *)&route->prefix.v4)) : "IPv6",
             route->prefix_len,
             (route->addr_type == IP_ADDR_TYPE_V4) ? 
                 inet_ntoa(*((struct in_addr *)&route->next_hop.v4)) : "IPv6",
             route->metric,
             route->egress_if);
    
    /* Synchronize with hardware if enabled */
    if (g_routing_table.hw_sync_enabled) {
        sync_route_to_hw(entry, HW_OPERATION_ADD);
    }
    
    return STATUS_SUCCESS;
}

/**
 * @brief Delete a route from the routing table
 *
 * @param prefix IP address prefix
 * @param prefix_len Prefix length
 * @param type IP address type (IPv4 or IPv6)
 * @return STATUS_SUCCESS if successful, error code otherwise
 */
status_t routing_table_delete_route(const ip_addr_t *prefix, uint8_t prefix_len, ip_addr_type_t type) {
    route_entry_t *entry, *prev;
    uint32_t hash_index;
    
    if (!g_routing_initialized) {
        LOG_ERROR("Routing table not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (prefix == NULL) {
        LOG_ERROR("NULL prefix pointer provided to routing_table_delete_route");
        return STATUS_INVALID_PARAM;
    }
    
    /* Calculate the hash index */
    if (type == IP_ADDR_TYPE_V4) {
        hash_index = hash_ipv4_prefix(&prefix->v4, prefix_len) % ROUTE_HASH_SIZE;
    } else {
        hash_index = hash_ipv6_prefix(&prefix->v6, prefix_len) % ROUTE_HASH_SIZE;
    }
    
    /* Find the route in the hash table */
    prev = NULL;
    entry = g_routing_table.hash_table[hash_index];
    
    while (entry) {
        if (entry->info.addr_type == type && 
            entry->info.prefix_len == prefix_len &&
            memcmp(&entry->info.prefix, prefix, 
                  (type == IP_ADDR_TYPE_V4 ? IPV4_ADDR_LEN : IPV6_ADDR_LEN)) == 0) {
            /* Found the route */
            break;
        }
        prev = entry;
        entry = entry->next;
    }
    
    if (!entry) {
        LOG_WARN("Route not found");
        return STATUS_NOT_FOUND;
    }
    
    /* Remove from the hash table */
    if (prev) {
        prev->next = entry->next;
    } else {
        g_routing_table.hash_table[hash_index] = entry->next;
    }
    
    /* Remove from the LPM tree */
    remove_from_lpm_tree(entry);
    
    LOG_INFO("Deleted route to %s/%d",
             (type == IP_ADDR_TYPE_V4) ? 
                 inet_ntoa(*((struct in_addr *)&prefix->v4)) : "IPv6",
             prefix_len);
    
    /* Synchronize with hardware if enabled */
    if (g_routing_table.hw_sync_enabled) {
        sync_route_to_hw(entry, HW_OPERATION_DELETE);
    }
    
    /* Free the route entry */
    free_route_entry(entry);
    
    /* Decrement the route count */
    g_routing_table.route_count--;
    
    return STATUS_SUCCESS;
}

/**
 * @brief Look up a route based on destination IP
 *
 * @param dest_ip Destination IP address
 * @param type IP address type (IPv4 or IPv6)
 * @param route_info Pointer to store the found route information
 * @return STATUS_SUCCESS if successful, error code otherwise
 */
status_t routing_table_lookup(const ip_addr_t *dest_ip, ip_addr_type_t type, routing_entry_t *route_info) {
    route_entry_t *entry;
    
    if (!g_routing_initialized) {
        LOG_ERROR("Routing table not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (dest_ip == NULL || route_info == NULL) {
        LOG_ERROR("NULL pointer provided to routing_table_lookup");
        return STATUS_INVALID_PARAM;
    }
    
    /* Find the longest prefix match */
    entry = find_route_lpm(dest_ip, type);
    if (!entry) {
        LOG_DEBUG("No matching route found for %s",
                 (type == IP_ADDR_TYPE_V4) ? 
                     inet_ntoa(*((struct in_addr *)&dest_ip->v4)) : "IPv6");
        return STATUS_NOT_FOUND;
    }
    
    /* Copy the route information */
    memcpy(route_info, &entry->info, sizeof(routing_entry_t));
    
    LOG_DEBUG("Found route to %s via next hop %s (metric %d, interface %d)",
             (type == IP_ADDR_TYPE_V4) ? 
                 inet_ntoa(*((struct in_addr *)&dest_ip->v4)) : "IPv6",
             (type == IP_ADDR_TYPE_V4) ? 
                 inet_ntoa(*((struct in_addr *)&entry->info.next_hop.v4)) : "IPv6",
             entry->info.metric,
             entry->info.egress_if);
    
    return STATUS_SUCCESS;
}

/**
 * @brief Get all routes in the routing table
 *
 * @param routes Array to store the routes
 * @param max_routes Maximum number of routes to retrieve
 * @param num_routes Pointer to store the actual number of routes retrieved
 * @return STATUS_SUCCESS if successful, error code otherwise
 */
status_t routing_table_get_all_routes(routing_entry_t *routes, uint16_t max_routes, uint16_t *num_routes) {
    uint16_t count = 0;
    uint32_t i;
    route_entry_t *entry;
    
    if (!g_routing_initialized) {
        LOG_ERROR("Routing table not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (routes == NULL || num_routes == NULL) {
        LOG_ERROR("NULL pointer provided to routing_table_get_all_routes");
        return STATUS_INVALID_PARAM;
    }
    
    /* Iterate through the hash table */
    for (i = 0; i < ROUTE_HASH_SIZE && count < max_routes; i++) {
        entry = g_routing_table.hash_table[i];
        while (entry && count < max_routes) {
            /* Copy the route information */
            memcpy(&routes[count], &entry->info, sizeof(routing_entry_t));
            count++;
            entry = entry->next;
        }
    }
    
    *num_routes = count;
    
    LOG_INFO("Retrieved %d routes (table contains %d routes)", count, g_routing_table.route_count);
    
    return STATUS_SUCCESS;
}

/**
 * @brief Enable or disable hardware synchronization
 *
 * @param enable True to enable HW sync, false to disable
 * @return STATUS_SUCCESS if successful, error code otherwise
 */
status_t routing_table_set_hw_sync(bool enable) {
    if (!g_routing_initialized) {
        LOG_ERROR("Routing table not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    g_routing_table.hw_sync_enabled = enable;
    
    LOG_INFO("Hardware synchronization %s", enable ? "enabled" : "disabled");
    
    return STATUS_SUCCESS;
}

/**
 * @brief Get routing table statistics
 *
 * @param stats Pointer to store the statistics
 * @return STATUS_SUCCESS if successful, error code otherwise
 */
status_t routing_table_get_stats(routing_table_stats_t *stats) {
    if (!g_routing_initialized) {
        LOG_ERROR("Routing table not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    if (stats == NULL) {
        LOG_ERROR("NULL pointer provided to routing_table_get_stats");
        return STATUS_INVALID_PARAM;
    }
    
    stats->total_routes = g_routing_table.route_count;
    stats->max_routes = MAX_ROUTES;
    stats->hw_sync_enabled = g_routing_table.hw_sync_enabled;
    
    return STATUS_SUCCESS;
}

/**
 * @brief Flush all routes from the routing table
 *
 * @return STATUS_SUCCESS if successful, error code otherwise
 */
status_t routing_table_flush(void) {
    uint32_t i;
    route_entry_t *entry, *next;
    
    if (!g_routing_initialized) {
        LOG_ERROR("Routing table not initialized");
        return STATUS_NOT_INITIALIZED;
    }
    
    LOG_INFO("Flushing routing table (%d entries)", g_routing_table.route_count);
    
    /* Iterate through the hash table and free all entries */
    for (i = 0; i < ROUTE_HASH_SIZE; i++) {
        entry = g_routing_table.hash_table[i];
        while (entry) {
            next = entry->next;
            
            /* Synchronize with hardware if enabled */
            if (g_routing_table.hw_sync_enabled) {
                sync_route_to_hw(entry, HW_OPERATION_DELETE);
            }
            
            free_route_entry(entry);
            entry = next;
        }
        g_routing_table.hash_table[i] = NULL;
    }
    
    /* Reset the LPM tree roots */
    g_routing_table.lpm_root_v4 = NULL;
    g_routing_table.lpm_root_v6 = NULL;
    
    /* Reset the route count */
    g_routing_table.route_count = 0;
    
    LOG_INFO("Routing table flushed successfully");
    
    return STATUS_SUCCESS;
}

/**
 * @brief Hash function for IPv4 prefixes
 *
 * @param prefix IPv4 address prefix
 * @param prefix_len Prefix length
 * @return Hash value
 */
static uint32_t hash_ipv4_prefix(const ipv4_addr_t *prefix, uint8_t prefix_len) {
    uint32_t hash;
    uint32_t addr = prefix->addr;
    
    /* Apply mask to the address based on prefix length */
    if (prefix_len < 32) {
        uint32_t mask = 0xFFFFFFFF << (32 - prefix_len);
        addr &= mask;
    }
    
    /* Jenkins hash function */
    hash = addr;
    hash = (hash + 0x7ed55d16) + (hash << 12);
    hash = (hash ^ 0xc761c23c) ^ (hash >> 19);
    hash = (hash + 0x165667b1) + (hash << 5);
    hash = (hash + 0xd3a2646c) ^ (hash << 9);
    hash = (hash + 0xfd7046c5) + (hash << 3);
    hash = (hash ^ 0xb55a4f09) ^ (hash >> 16);
    
    /* Include prefix length in hash */
    hash ^= prefix_len;
    
    return hash;
}

/**
 * @brief Hash function for IPv6 prefixes
 *
 * @param prefix IPv6 address prefix
 * @param prefix_len Prefix length
 * @return Hash value
 */
static uint32_t hash_ipv6_prefix(const ipv6_addr_t *prefix, uint8_t prefix_len) {
    uint32_t hash = 0;
    uint8_t i;
    uint8_t bytes_to_hash = (prefix_len + 7) / 8; /* Ceiling of prefix_len / 8 */
    
    /* Use at most 16 bytes */
    if (bytes_to_hash > 16) {
        bytes_to_hash = 16;
    }
    
    /* Apply XOR to all the bytes */
    for (i = 0; i < bytes_to_hash; i++) {
        hash ^= ((uint32_t)prefix->addr[i] << ((i % 4) * 8));
        
        /* Every 4 bytes, mix the hash */
        if ((i % 4) == 3 || i == bytes_to_hash - 1) {
            hash = (hash + 0x7ed55d16) + (hash << 12);
            hash = (hash ^ 0xc761c23c) ^ (hash >> 19);
            hash = (hash + 0x165667b1) + (hash << 5);
            hash = (hash + 0xd3a2646c) ^ (hash << 9);
            hash = (hash + 0xfd7046c5) + (hash << 3);
            hash = (hash ^ 0xb55a4f09) ^ (hash >> 16);
        }
    }
    
    /* Include prefix length in hash */
    hash ^= prefix_len;
    
    return hash;
}

/**
 * @brief Find a route with exact match
 *
 * @param prefix IP address prefix
 * @param prefix_len Prefix length
 * @param type IP address type (IPv4 or IPv6)
 * @return Pointer to the route entry if found, NULL otherwise
 */
static route_entry_t *find_route_exact(const ip_addr_t *prefix, uint8_t prefix_len, ip_addr_type_t type) {
    route_entry_t *entry;
    uint32_t hash_index;
    
    /* Calculate the hash index */
    if (type == IP_ADDR_TYPE_V4) {
        hash_index = hash_ipv4_prefix(&prefix->v4, prefix_len) % ROUTE_HASH_SIZE;
    } else {
        hash_index = hash_ipv6_prefix(&prefix->v6, prefix_len) % ROUTE_HASH_SIZE;
    }
    
    /* Search in the hash chain */
    entry = g_routing_table.hash_table[hash_index];
    while (entry) {
        if (entry->info.addr_type == type && 
            entry->info.prefix_len == prefix_len &&
            memcmp(&entry->info.prefix, prefix, 
                  (type == IP_ADDR_TYPE_V4 ? IPV4_ADDR_LEN : IPV6_ADDR_LEN)) == 0) {
            /* Found the route */
            return entry;
        }
        entry = entry->next;
    }
    
    /* Route not found */
    return NULL;
}

/**
 * @brief Insert a route entry into the LPM tree
 *
 * @param entry Route entry to insert
 */
static void insert_to_lpm_tree(route_entry_t *entry) {
    route_entry_t **tree_root;
    route_entry_t **current;
    uint8_t bit_pos;
    bool bit_value;
    
    /* Select the appropriate tree based on address type */
    if (entry->info.addr_type == IP_ADDR_TYPE_V4) {
        tree_root = &g_routing_table.lpm_root_v4;
    } else {
        tree_root = &g_routing_table.lpm_root_v6;
    }
    
    /* Start from the root */
    current = tree_root;
    
    /* Traverse the tree based on prefix bits */
    for (bit_pos = 0; bit_pos < entry->info.prefix_len; bit_pos++) {
        if (*current == NULL) {
            /* Create a new node */
            *current = entry;
            return;
        }
        
        /* Get the bit value at the current position */
        bit_value = get_bit_from_prefix(&entry->info.prefix, bit_pos, entry->info.addr_type);
        
        /* Follow the appropriate child */
        if (bit_value) {
            current = &((*current)->lpm_right);
        } else {
            current = &((*current)->lpm_left);
        }
    }
    
    /* Insert the entry at the current position */
    *current = entry;
}

/**
 * @brief Remove a route entry from the LPM tree
 *
 * @param entry Route entry to remove
 */
static void remove_from_lpm_tree(route_entry_t *entry) {
    /* For simplicity, we just clear the LPM tree links */
    entry->lpm_left = NULL;
    entry->lpm_right = NULL;
    
    /* For a real implementation, we would need to rebuild the tree */
    /* Since this is a simulator, this approach is acceptable */
    /* In a production system, a more sophisticated data structure would be used */
    
    /* Clear the LPM tree roots */
    g_routing_table.lpm_root_v4 = NULL;
    g_routing_table.lpm_root_v6 = NULL;
    
    /* Rebuild the LPM trees */
    uint32_t i;
    route_entry_t *current;
    for (i = 0; i < ROUTE_HASH_SIZE; i++) {
        current = g_routing_table.hash_table[i];
        while (current) {
            if (current != entry) {
                insert_to_lpm_tree(current);
            }
            current = current->next;
        }
    }
}

/**
 * @brief Find a route using longest prefix match
 *
 * @param addr IP address to match
 * @param type IP address type (IPv4 or IPv6)
 * @return Pointer to the route entry if found, NULL otherwise
 */
static route_entry_t *find_route_lpm(const ip_addr_t *addr, ip_addr_type_t type) {
    route_entry_t *current;
    route_entry_t *best_match = NULL;
    uint8_t bit_pos = 0;
    bool bit_value;
    
    /* Select the appropriate tree based on address type */
    if (type == IP_ADDR_TYPE_V4) {
        current = g_routing_table.lpm_root_v4;
    } else {
        current = g_routing_table.lpm_root_v6;
    }
    
    /* Traverse the tree */
    while (current) {
        /* Check if the current node matches the address */
        if (prefix_match(addr, &current->info.prefix, current->info.prefix_len, type)) {
            /* Update the best match if this is a better match */
            if (best_match == NULL || current->info.prefix_len > best_match->info.prefix_len) {
                best_match = current;
            }
        }
        
        /* Get the bit value at the current position */
        if (bit_pos < (type == IP_ADDR_TYPE_V4 ? 32 : 128)) {
            bit_value = get_bit_from_prefix(addr, bit_pos, type);
            
            /* Follow the appropriate child */
            if (bit_value) {
                current = current->lpm_right;
            } else {
                current = current->lpm_left;
            }
            
            bit_pos++;
        } else {
            /* Reached the end of the address bits */
            break;
        }
    }
    
    return best_match;
}

/**
 * @brief Synchronize a route with the hardware
 *
 * @param entry Route entry to synchronize
 * @param operation Hardware operation (add or delete)
 */
static void sync_route_to_hw(const route_entry_t *entry, hw_operation_t operation) {
    hw_resource_t hw_resource;
    
    /* Set up the hardware resource */
    hw_resource.type = HW_RESOURCE_TYPE_ROUTE;
    memcpy(&hw_resource.route_entry, &entry->info, sizeof(routing_entry_t));
    
    /* Update the hardware */
    if (operation == HW_OPERATION_ADD) {
        hw_resource_add(&hw_resource);
        LOG_DEBUG("Added route to hardware: %s/%d",
                 (entry->info.addr_type == IP_ADDR_TYPE_V4) ? 
                     inet_ntoa(*((struct in_addr *)&entry->info.prefix.v4)) : "IPv6",
                 entry->info.prefix_len);
    } else if (operation == HW_OPERATION_DELETE) {
        hw_resource_delete(&hw_resource);
        LOG_DEBUG("Deleted route from hardware: %s/%d",
                 (entry->info.addr_type == IP_ADDR_TYPE_V4) ? 
                     inet_ntoa(*((struct in_addr *)&entry->info.prefix.v4)) : "IPv6",
                 entry->info.prefix_len);
    }
}

/**
 * @brief Allocate a route entry from the pool
 *
 * @return Pointer to the allocated entry, NULL if none available
 */
static route_entry_t *allocate_route_entry(void) {
    uint16_t i;
    
    /* Find a free entry in the pool */
    for (i = 0; i < MAX_ROUTES; i++) {
        /* Check if the entry is free */
        if (g_routing_table.route_pool[i].info.addr_type == IP_ADDR_TYPE_INVALID) {
            /* Clear the entry */
            memset(&g_routing_table.route_pool[i], 0, sizeof(route_entry_t));
            return &g_routing_table.route_pool[i];
        }
    }
    
    /* No free entries available */
    return NULL;
}

/**
 * @brief Free a route entry back to the pool
 *
 * @param entry Route entry to free
 */
static void free_route_entry(route_entry_t *entry) {
    /* Mark the entry as free */
    entry->info.addr_type = IP_ADDR_TYPE_INVALID;
    
    /* Clear the entry */
    memset(entry, 0, sizeof(route_entry_t));
}

/**
 * @brief Check if an address matches a prefix
 *
 * @param addr IP address to check
 * @param prefix IP address prefix to match against
 * @param prefix_len Prefix length
 * @param type IP address type (IPv4 or IPv6)
 * @return True if the address matches the prefix, false otherwise
 */
static bool prefix_match(const ip_addr_t *addr, const ip_addr_t *prefix, uint8_t prefix_len, ip_addr_type_t type) {
    uint8_t bytes_to_check;
    uint8_t bits_in_last_byte;
    uint8_t i;
    uint8_t mask;
    
    if (type == IP_ADDR_TYPE_V4) {
        /* Check full bytes */
        bytes_to_check = prefix_len / 8;
        if (bytes_to_check > 0 && memcmp(&addr->v4, &prefix->v4, bytes_to_check) != 0) {
            return false;
        }
        
        /* Check partial byte if needed */
        bits_in_last_byte = prefix_len % 8;
        if (bits_in_last_byte > 0) {
            mask = 0xFF << (8 - bits_in_last_byte);
            if ((((uint8_t *)&addr->v4)[bytes_to_check] & mask) != 
                (((uint8_t *)&prefix->v4)[bytes_to_check] & mask)) {
                return false;
            }
        }
    } else {
        /* Check full bytes */
        bytes_to_check = prefix_len / 8;
        if (bytes_to_check > 0 && memcmp(addr->v6.addr, prefix->v6.addr, bytes_to_check) != 0) {
            return false;
        }
        
        /* Check partial byte if needed */
        bits_in_last_byte = prefix_len % 8;
        if (bits_in_last_byte > 0) {
            mask = 0xFF << (8 - bits_in_last_byte);
            if ((addr->v6.addr[bytes_to_check] & mask) !=
                (prefix->v6.addr[bytes_to_check] & mask)) {
                return false;
            }
        }
    }

    return true;
}

/**
 * @brief Get the number of common prefix bits between two IP addresses
 *
 * @param addr1 First IP address
 * @param addr2 Second IP address
 * @param type IP address type (IPv4 or IPv6)
 * @return Number of common prefix bits
 */
static uint8_t get_common_prefix_len(const ip_addr_t *addr1, const ip_addr_t *addr2, ip_addr_type_t type) {
    uint8_t common_bits = 0;
    uint8_t i, j;
    uint8_t byte1, byte2;
    uint8_t max_bytes = (type == IP_ADDR_TYPE_V4) ? IPV4_ADDR_LEN : IPV6_ADDR_LEN;

    for (i = 0; i < max_bytes; i++) {
        if (type == IP_ADDR_TYPE_V4) {
            byte1 = ((uint8_t *)&addr1->v4)[i];
            byte2 = ((uint8_t *)&addr2->v4)[i];
        } else {
            byte1 = addr1->v6.addr[i];
            byte2 = addr2->v6.addr[i];
        }

        if (byte1 == byte2) {
            common_bits += 8;
        } else {
            /* Find the number of common bits in the differing byte */
            for (j = 0; j < 8; j++) {
                if ((byte1 & (0x80 >> j)) == (byte2 & (0x80 >> j))) {
                    common_bits++;
                } else {
                    return common_bits;
                }
            }
        }
    }

    return common_bits;
}

/**
 * @brief Get a specific bit from an IP prefix
 *
 * @param prefix IP address prefix
 * @param bit_pos Bit position (0 is most significant)
 * @param type IP address type (IPv4 or IPv6)
 * @return The value of the bit (true for 1, false for 0)
 */
static bool get_bit_from_prefix(const ip_addr_t *prefix, uint8_t bit_pos, ip_addr_type_t type) {
    uint8_t byte_pos;
    uint8_t bit_in_byte;
    uint8_t byte_value;

    if (type == IP_ADDR_TYPE_V4) {
        if (bit_pos >= 32) {
            return false;  /* Out of range for IPv4 */
        }

        byte_pos = bit_pos / 8;
        bit_in_byte = 7 - (bit_pos % 8);  /* Bit 0 is the MSB in the byte */

        byte_value = ((uint8_t *)&prefix->v4)[byte_pos];
        return (byte_value & (1 << bit_in_byte)) != 0;
    } else {
        if (bit_pos >= 128) {
            return false;  /* Out of range for IPv6 */
        }

        byte_pos = bit_pos / 8;
        bit_in_byte = 7 - (bit_pos % 8);  /* Bit 0 is the MSB in the byte */

        byte_value = prefix->v6.addr[byte_pos];
        return (byte_value & (1 << bit_in_byte)) != 0;
    }
}

/**
 * @brief Insert a route into the routing table
 *
 * @param prefix IP address prefix for the route
 * @param prefix_len Prefix length
 * @param type IP address type (IPv4 or IPv6)
 * @param next_hop Next hop IP address
 * @param interface_index Outgoing interface index
 * @param metric Route metric
 * @param route_source Source of the route
 * @return STATUS_SUCCESS if successful, error code otherwise
 */
status_t routing_add_route(const ip_addr_t *prefix, uint8_t prefix_len, ip_addr_type_t type,
                         const ip_addr_t *next_hop, uint16_t interface_index,
                         uint16_t metric, route_source_t route_source) {
    uint32_t hash;
    route_entry_t *new_entry;

    if (!g_routing_initialized) {
        LOG_ERROR("Routing table not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    if (!prefix || !next_hop) {
        LOG_ERROR("Invalid parameters for routing_add_route");
        return STATUS_INVALID_PARAMS;
    }

    /* Validate prefix length */
    if ((type == IP_ADDR_TYPE_V4 && prefix_len > 32) ||
        (type == IP_ADDR_TYPE_V6 && prefix_len > 128)) {
        LOG_ERROR("Invalid prefix length for route: %u", prefix_len);
        return STATUS_INVALID_PARAMS;
    }

    /* Check if route already exists */
    if (find_route_exact(prefix, prefix_len, type) != NULL) {
        LOG_WARN("Route already exists in the routing table");
        return STATUS_ALREADY_EXISTS;
    }

    /* Check if we've reached the maximum number of routes */
    if (g_routing_table.route_count >= MAX_ROUTES) {
        LOG_ERROR("Routing table full, cannot add more routes");
        return STATUS_RESOURCE_EXCEEDED;
    }

    /* Allocate a new route entry */
    new_entry = allocate_route_entry();
    if (!new_entry) {
        LOG_ERROR("Failed to allocate new route entry");
        return STATUS_NO_MEMORY;
    }

    /* Set route information */
    memcpy(&new_entry->info.prefix, prefix, sizeof(ip_addr_t));
    memcpy(&new_entry->info.next_hop, next_hop, sizeof(ip_addr_t));
    new_entry->info.prefix_len = prefix_len;
    new_entry->info.interface_index = interface_index;
    new_entry->info.metric = metric;
    new_entry->info.addr_type = type;
    new_entry->info.source = route_source;
    new_entry->info.flags = 0;  /* No flags by default */

    /* Insert into hash table for exact match lookup */
    if (type == IP_ADDR_TYPE_V4) {
        hash = hash_ipv4_prefix(&prefix->v4, prefix_len);
    } else {
        hash = hash_ipv6_prefix(&prefix->v6, prefix_len);
    }

    hash %= ROUTE_HASH_SIZE;
    new_entry->next = g_routing_table.hash_table[hash];
    g_routing_table.hash_table[hash] = new_entry;

    /* Insert into LPM tree for longest prefix match lookup */
    insert_to_lpm_tree(new_entry);

    /* Increment route count */
    g_routing_table.route_count++;

    LOG_INFO("Added route: %s/%u via interface %u",
             ip_addr_to_str(prefix, type), prefix_len, interface_index);

    /* Sync to hardware if enabled */
    if (g_routing_table.hw_sync_enabled) {
        sync_route_to_hw(new_entry, HW_OP_ADD);
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Remove a route from the routing table
 *
 * @param prefix IP address prefix for the route
 * @param prefix_len Prefix length
 * @param type IP address type (IPv4 or IPv6)
 * @return STATUS_SUCCESS if successful, error code otherwise
 */
status_t routing_remove_route(const ip_addr_t *prefix, uint8_t prefix_len, ip_addr_type_t type) {
    uint32_t hash;
    route_entry_t *entry, *prev = NULL;

    if (!g_routing_initialized) {
        LOG_ERROR("Routing table not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    if (!prefix) {
        LOG_ERROR("Invalid parameter for routing_remove_route");
        return STATUS_INVALID_PARAMS;
    }

    /* Calculate hash for the route */
    if (type == IP_ADDR_TYPE_V4) {
        hash = hash_ipv4_prefix(&prefix->v4, prefix_len);
    } else {
        hash = hash_ipv6_prefix(&prefix->v6, prefix_len);
    }

    hash %= ROUTE_HASH_SIZE;

    /* Find the route in the hash table */
    for (entry = g_routing_table.hash_table[hash]; entry != NULL; prev = entry, entry = entry->next) {
        if (entry->info.addr_type == type && entry->info.prefix_len == prefix_len &&
            memcmp(&entry->info.prefix, prefix, sizeof(ip_addr_t)) == 0) {

            /* Remove from hash table */
            if (prev == NULL) {
                g_routing_table.hash_table[hash] = entry->next;
            } else {
                prev->next = entry->next;
            }

            /* Remove from LPM tree */
            remove_from_lpm_tree(entry);

            /* Sync to hardware if enabled */
            if (g_routing_table.hw_sync_enabled) {
                sync_route_to_hw(entry, HW_OP_REMOVE);
            }

            LOG_INFO("Removed route: %s/%u",
                     ip_addr_to_str(prefix, type), prefix_len);

            /* Free the route entry */
            free_route_entry(entry);

            /* Decrement route count */
            g_routing_table.route_count--;

            return STATUS_SUCCESS;
        }
    }

    LOG_WARN("Route not found for deletion: %s/%u",
             ip_addr_to_str(prefix, type), prefix_len);

    return STATUS_NOT_FOUND;
}

/**
 * @brief Look up the best matching route for an IP address
 *
 * @param dest_addr Destination IP address to look up
 * @param type IP address type (IPv4 or IPv6)
 * @param[out] route_info Pointer to store route information if found
 * @return STATUS_SUCCESS if a route is found, error code otherwise
 */
status_t routing_lookup(const ip_addr_t *dest_addr, ip_addr_type_t type, routing_entry_t *route_info) {
    route_entry_t *entry;

    if (!g_routing_initialized) {
        LOG_ERROR("Routing table not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    if (!dest_addr || !route_info) {
        LOG_ERROR("Invalid parameters for routing_lookup");
        return STATUS_INVALID_PARAMS;
    }

    /* Perform longest prefix match lookup */
    entry = find_route_lpm(dest_addr, type);
    if (!entry) {
        LOG_DEBUG("No route found for destination: %s",
                  ip_addr_to_str(dest_addr, type));
        return STATUS_NOT_FOUND;
    }

    /* Copy route information */
    memcpy(route_info, &entry->info, sizeof(routing_entry_t));

    LOG_DEBUG("Route found for %s: via %s, interface %u",
              ip_addr_to_str(dest_addr, type),
              ip_addr_to_str(&entry->info.next_hop, type),
              entry->info.interface_index);

    return STATUS_SUCCESS;
}

/**
 * @brief Get hash value for an IPv4 prefix
 *
 * @param prefix IPv4 address prefix
 * @param prefix_len Prefix length
 * @return Hash value
 */
static uint32_t hash_ipv4_prefix(const ipv4_addr_t *prefix, uint8_t prefix_len) {
    uint32_t hash;
    uint32_t masked_prefix;
    uint32_t mask;

    /* Create a mask based on prefix length */
    if (prefix_len == 0) {
        mask = 0;
    } else if (prefix_len >= 32) {
        mask = 0xFFFFFFFF;
    } else {
        mask = ~((1UL << (32 - prefix_len)) - 1);
    }

    /* Apply mask to the prefix */
    masked_prefix = ntohl(prefix->addr) & mask;

    /* Simple hash function */
    hash = masked_prefix ^ prefix_len;
    hash = ((hash >> 16) ^ hash) * 0x45d9f3b;
    hash = ((hash >> 16) ^ hash) * 0x45d9f3b;
    hash = (hash >> 16) ^ hash;

    return hash;
}

/**
 * @brief Get hash value for an IPv6 prefix
 *
 * @param prefix IPv6 address prefix
 * @param prefix_len Prefix length
 * @return Hash value
 */
static uint32_t hash_ipv6_prefix(const ipv6_addr_t *prefix, uint8_t prefix_len) {
    uint32_t hash = 0;
    uint8_t i;
    uint8_t bytes_to_hash = (prefix_len + 7) / 8;  /* Ceiling of prefix_len/8 */

    /* Limit bytes to hash to IPv6 address size */
    if (bytes_to_hash > IPV6_ADDR_LEN) {
        bytes_to_hash = IPV6_ADDR_LEN;
    }

    /* Hash the significant bytes and the prefix length */
    for (i = 0; i < bytes_to_hash; i++) {
        hash = (hash << 5) - hash + prefix->addr[i];
    }

    hash = (hash << 5) - hash + prefix_len;

    return hash;
}

/**
 * @brief Find a route entry with exact match
 *
 * @param prefix IP address prefix to match
 * @param prefix_len Prefix length
 * @param type IP address type (IPv4 or IPv6)
 * @return Pointer to the matching route entry, or NULL if not found
 */
static route_entry_t *find_route_exact(const ip_addr_t *prefix, uint8_t prefix_len, ip_addr_type_t type) {
    uint32_t hash;
    route_entry_t *entry;

    /* Calculate hash for the route */
    if (type == IP_ADDR_TYPE_V4) {
        hash = hash_ipv4_prefix(&prefix->v4, prefix_len);
    } else {
        hash = hash_ipv6_prefix(&prefix->v6, prefix_len);
    }

    hash %= ROUTE_HASH_SIZE;

    /* Search in the hash bucket */
    for (entry = g_routing_table.hash_table[hash]; entry != NULL; entry = entry->next) {
        if (entry->info.addr_type == type && entry->info.prefix_len == prefix_len &&
            memcmp(&entry->info.prefix, prefix, sizeof(ip_addr_t)) == 0) {
            return entry;
        }
    }

    return NULL;  /* Not found */
}

/**
 * @brief Insert a route entry into the LPM (Longest Prefix Match) tree
 *
 * @param entry Route entry to insert
 */
static void insert_to_lpm_tree(route_entry_t *entry) {
    route_entry_t **root;
    route_entry_t *current, *parent = NULL;
    bool go_right;
    uint8_t bit_pos = 0;

    /* Select the appropriate LPM tree root based on address type */
    if (entry->info.addr_type == IP_ADDR_TYPE_V4) {
        root = &g_routing_table.lpm_root_v4;
    } else {
        root = &g_routing_table.lpm_root_v6;
    }

    /* If the tree is empty, make this entry the root */
    if (*root == NULL) {
        *root = entry;
        entry->lpm_left = NULL;
        entry->lpm_right = NULL;
        return;
    }

    /* Traverse the tree to find the insertion point */
    current = *root;
    while (current != NULL) {
        parent = current;

        /* If we've checked all bits in the prefix, stop */
        if (bit_pos >= entry->info.prefix_len) {
            break;
        }

        /* Decide which branch to take based on the current bit */
        go_right = get_bit_from_prefix(&entry->info.prefix, bit_pos, entry->info.addr_type);

        if (go_right) {
            current = current->lpm_right;
        } else {
            current = current->lpm_left;
        }

        bit_pos++;
    }

    /* Insert the new entry */
    if (go_right) {
        parent->lpm_right = entry;
    } else {
        parent->lpm_left = entry;
    }

    entry->lpm_left = NULL;
    entry->lpm_right = NULL;
}

/**
 * @brief Remove a route entry from the LPM tree
 *
 * @param entry Route entry to remove
 */
static void remove_from_lpm_tree(route_entry_t *entry) {
    /* This is a simplified implementation.
     * In a production system, this would need to handle
     * the rearrangement of the LPM tree after removal.
     * For the simulator, we'll assume this is a managed tree
     * where nodes can be marked as deleted without restructuring.
     */

    /* Mark the entry as removed by clearing its children */
    entry->lpm_left = NULL;
    entry->lpm_right = NULL;

    /* In a full implementation, we would need to:
     * 1. Find the entry in the tree
     * 2. Handle the case where the entry has 0, 1, or 2 children
     * 3. Rebalance the tree if necessary
     */

    LOG_DEBUG("Removed route entry from LPM tree (simplified implementation)");
}

/**
 * @brief Find the best matching route using Longest Prefix Match
 *
 * @param addr IP address to match
 * @param type IP address type (IPv4 or IPv6)
 * @return Pointer to the best matching route entry, or NULL if not found
 */
static route_entry_t *find_route_lpm(const ip_addr_t *addr, ip_addr_type_t type) {
    route_entry_t *current;
    route_entry_t *best_match = NULL;
    bool go_right;
    uint8_t bit_pos = 0;

    /* Select the appropriate LPM tree root based on address type */
    if (type == IP_ADDR_TYPE_V4) {
        current = g_routing_table.lpm_root_v4;
    } else {
        current = g_routing_table.lpm_root_v6;
    }

    /* Traverse the tree to find the longest prefix match */
    while (current != NULL) {
        /* Check if the current node's prefix matches the address */
        if (prefix_match(addr, &current->info.prefix, current->info.prefix_len, type)) {
            /* Update best match if this prefix is longer */
            if (best_match == NULL || current->info.prefix_len > best_match->info.prefix_len) {
                best_match = current;
            }
        }

        /* If we've reached a leaf node, stop */
        if (bit_pos >=
            ((type == IP_ADDR_TYPE_V4) ? 32 : 128)) {
            break;
        }

        /* Decide which branch to take based on the current bit in the address */
        go_right = get_bit_from_prefix(addr, bit_pos, type);

        if (go_right) {
            current = current->lpm_right;
        } else {
            current = current->lpm_left;
        }

        bit_pos++;
    }

    return best_match;
}

/**
 * @brief Synchronize a route entry with hardware
 *
 * @param entry Route entry to synchronize
 * @param operation Hardware operation (add/remove/update)
 */
static void sync_route_to_hw(const route_entry_t *entry, hw_operation_t operation) {
    hw_route_t hw_route;

    /* Convert route entry to hardware format */
    memset(&hw_route, 0, sizeof(hw_route_t));

    hw_route.prefix = entry->info.prefix;
    hw_route.prefix_len = entry->info.prefix_len;
    hw_route.addr_type = entry->info.addr_type;
    hw_route.next_hop = entry->info.next_hop;
    hw_route.interface_index = entry->info.interface_index;
    hw_route.metric = entry->info.metric;

    /* Perform hardware operation */
    switch (operation) {
        case HW_OP_ADD:
            LOG_DEBUG("Adding route to hardware: %s/%u",
                     ip_addr_to_str(&entry->info.prefix, entry->info.addr_type),
                     entry->info.prefix_len);
            hw_add_route(&hw_route);
            break;

        case HW_OP_REMOVE:
            LOG_DEBUG("Removing route from hardware: %s/%u",
                     ip_addr_to_str(&entry->info.prefix, entry->info.addr_type),
                     entry->info.prefix_len);
            hw_remove_route(&hw_route);
            break;

        case HW_OP_UPDATE:
            LOG_DEBUG("Updating route in hardware: %s/%u",
                     ip_addr_to_str(&entry->info.prefix, entry->info.addr_type),
                     entry->info.prefix_len);
            hw_update_route(&hw_route);
            break;

        default:
            LOG_ERROR("Unknown hardware operation: %d", operation);
            break;
    }
}

/**
 * @brief Allocate a route entry from the pre-allocated pool
 *
 * @return Pointer to an available route entry, or NULL if none available
 */
static route_entry_t *allocate_route_entry(void) {
    uint16_t i;

    for (i = 0; i < MAX_ROUTES; i++) {
        /* Check if entry is not in use (all zeroes) */
        if (g_routing_table.route_pool[i].info.prefix_len == 0 &&
            g_routing_table.route_pool[i].next == NULL) {

            /* Clear the entry before returning it */
            memset(&g_routing_table.route_pool[i], 0, sizeof(route_entry_t));
            return &g_routing_table.route_pool[i];
        }
    }

    LOG_ERROR("No available route entries in pool");
    return NULL;
}

/**
 * @brief Free a route entry back to the pool
 *
 * @param entry Route entry to free
 */
static void free_route_entry(route_entry_t *entry) {
    /* Clear the entry to mark it as available */
    memset(entry, 0, sizeof(route_entry_t));
}

/**
 * @brief Clean up the routing table module resources
 *
 * @return STATUS_SUCCESS if successful, error code otherwise
 */
status_t routing_table_cleanup(void) {
    if (!g_routing_initialized) {
        LOG_WARN("Routing table not initialized, nothing to clean up");
        return STATUS_NOT_INITIALIZED;
    }

    LOG_INFO("Cleaning up routing table module");

    /* Free the route pool */
    if (g_routing_table.route_pool) {
        free(g_routing_table.route_pool);
        g_routing_table.route_pool = NULL;
    }

    /* Reset the routing table structure */
    memset(&g_routing_table, 0, sizeof(g_routing_table));

    g_routing_initialized = false;

    LOG_INFO("Routing table cleanup completed");

    return STATUS_SUCCESS;
}

/**
 * @brief Enable or disable hardware synchronization for routes
 *
 * @param enable True to enable HW sync, false to disable
 * @return STATUS_SUCCESS if successful, error code otherwise
 */
status_t routing_set_hw_sync(bool enable) {
    if (!g_routing_initialized) {
        LOG_ERROR("Routing table not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    g_routing_table.hw_sync_enabled = enable;

    LOG_INFO("Hardware synchronization for routes %s",
             enable ? "enabled" : "disabled");

    return STATUS_SUCCESS;
}

/**
 * @brief Get statistics about the routing table
 *
 * @param[out] stats Pointer to store routing table statistics
 * @return STATUS_SUCCESS if successful, error code otherwise
 */
status_t routing_get_stats(routing_table_stats_t *stats) {
    uint16_t i;
    uint16_t ipv4_count = 0;
    uint16_t ipv6_count = 0;

    if (!g_routing_initialized) {
        LOG_ERROR("Routing table not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    if (!stats) {
        LOG_ERROR("Invalid parameter for routing_get_stats");
        return STATUS_INVALID_PARAMS;
    }

    /* Count IPv4 and IPv6 routes */
    for (i = 0; i < MAX_ROUTES; i++) {
        if (g_routing_table.route_pool[i].info.prefix_len > 0) {
            if (g_routing_table.route_pool[i].info.addr_type == IP_ADDR_TYPE_V4) {
                ipv4_count++;
            } else {
                ipv6_count++;
            }
        }
    }

    /* Fill in statistics */
    stats->total_routes = g_routing_table.route_count;
    stats->ipv4_routes = ipv4_count;
    stats->ipv6_routes = ipv6_count;
    stats->max_routes = MAX_ROUTES;
    stats->hw_sync_enabled = g_routing_table.hw_sync_enabled;

    return STATUS_SUCCESS;
}
