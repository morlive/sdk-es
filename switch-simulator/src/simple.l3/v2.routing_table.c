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

status_t routing_table_init(void) {

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

status_t routing_table_deinit(void) {
status_t routing_table_add_route(const routing_entry_t *route) {
status_t routing_table_delete_route(const ip_addr_t *prefix, uint8_t prefix_len, ip_addr_type_t type) {
status_t routing_table_lookup(const ip_addr_t *dest_ip, ip_addr_type_t type, routing_entry_t *route_info) {
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
status_t routing_table_get_stats(routing_table_stats_t *stats) {
status_t routing_table_flush(void) {
static uint32_t hash_ipv4_prefix(const ipv4_addr_t *prefix, uint8_t prefix_len) {
static uint32_t hash_ipv6_prefix(const ipv6_addr_t *prefix, uint8_t prefix_len) {
static route_entry_t *find_route_exact(const ip_addr_t *prefix, uint8_t prefix_len, ip_addr_type_t type) {
static void insert_to_lpm_tree(route_entry_t *entry) {
static void remove_from_lpm_tree(route_entry_t *entry) {
static route_entry_t *find_route_lpm(const ip_addr_t *addr, ip_addr_type_t type) {
static void sync_route_to_hw(const route_entry_t *entry, hw_operation_t operation) {
static route_entry_t *allocate_route_entry(void) {
static void free_route_entry(route_entry_t *entry) {
static bool prefix_match(const ip_addr_t *addr, const ip_addr_t *prefix, uint8_t prefix_len, ip_addr_type_t type) {
static uint8_t get_common_prefix_len(const ip_addr_t *addr1, const ip_addr_t *addr2, ip_addr_type_t type) {
static bool get_bit_from_prefix(const ip_addr_t *prefix, uint8_t bit_pos, ip_addr_type_t type) {
status_t routing_add_route(const ip_addr_t *prefix, uint8_t prefix_len, ip_addr_type_t type,
                         const ip_addr_t *next_hop, uint16_t interface_index,
                         uint16_t metric, route_source_t route_source) {
status_t routing_remove_route(const ip_addr_t *prefix, uint8_t prefix_len, ip_addr_type_t type) {
status_t routing_lookup(const ip_addr_t *dest_addr, ip_addr_type_t type, routing_entry_t *route_info) {
static uint32_t hash_ipv4_prefix(const ipv4_addr_t *prefix, uint8_t prefix_len) {
static uint32_t hash_ipv6_prefix(const ipv6_addr_t *prefix, uint8_t prefix_len) {
static route_entry_t *find_route_exact(const ip_addr_t *prefix, uint8_t prefix_len, ip_addr_type_t type) {
static void insert_to_lpm_tree(route_entry_t *entry) {
static void remove_from_lpm_tree(route_entry_t *entry) {
static route_entry_t *find_route_lpm(const ip_addr_t *addr, ip_addr_type_t type) {
static void sync_route_to_hw(const route_entry_t *entry, hw_operation_t operation) {
static route_entry_t *allocate_route_entry(void) {
static void free_route_entry(route_entry_t *entry) {
status_t routing_table_cleanup(void) {
status_t routing_set_hw_sync(bool enable) {
status_t routing_get_stats(routing_table_stats_t *stats) {
