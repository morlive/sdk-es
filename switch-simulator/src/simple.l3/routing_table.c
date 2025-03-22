#include "include/l3/routing_table.h"
#include "include/common/logging.h"
#include "include/common/error_codes.h"

#define MAX_ROUTES 1024

typedef struct {
    ip_addr_t dest_network;
    ip_addr_t subnet_mask;
    ip_addr_t next_hop;
    uint32_t interface_id;
    uint32_t metric;
    route_type_t type;
    bool is_active;
} route_entry_t;

static route_entry_t routing_table[MAX_ROUTES];
static uint32_t route_count = 0;

// Initialize routing table
error_code_t routing_table_init(void) {
    LOG_INFO("Initializing routing table");
    route_count = 0;
    memset(routing_table, 0, sizeof(routing_table));
    return ERROR_SUCCESS;
}

// Add a route to the routing table
error_code_t routing_table_add_route(ip_addr_t dest_network, ip_addr_t subnet_mask, 
                                    ip_addr_t next_hop, uint32_t interface_id,
                                    uint32_t metric, route_type_t type) {
    if (route_count >= MAX_ROUTES) {
        LOG_ERROR("Routing table is full");
        return ERROR_RESOURCE_FULL;
    }
    
    // Check if route already exists
    for (uint32_t i = 0; i < route_count; i++) {
        if (memcmp(&routing_table[i].dest_network, &dest_network, sizeof(ip_addr_t)) == 0 &&
            memcmp(&routing_table[i].subnet_mask, &subnet_mask, sizeof(ip_addr_t)) == 0) {
            
            // Update existing route
            memcpy(&routing_table[i].next_hop, &next_hop, sizeof(ip_addr_t));
            routing_table[i].interface_id = interface_id;
            routing_table[i].metric = metric;
            routing_table[i].type = type;
            routing_table[i].is_active = true;
            
            LOG_DEBUG("Updated route to %s/%s", ip_to_str(dest_network), ip_to_str(subnet_mask));
            return ERROR_SUCCESS;
        }
    }
    
    // Add new route
    memcpy(&routing_table[route_count].dest_network, &dest_network, sizeof(ip_addr_t));
    memcpy(&routing_table[route_count].subnet_mask, &subnet_mask, sizeof(ip_addr_t));
    memcpy(&routing_table[route_count].next_hop, &next_hop, sizeof(ip_addr_t));
    routing_table[route_count].interface_id = interface_id;
    routing_table[route_count].metric = metric;
    routing_table[route_count].type = type;
    routing_table[route_count].is_active = true;
    
    route_count++;
    
    LOG_DEBUG("Added new route to %s/%s", ip_to_str(dest_network), ip_to_str(subnet_mask));
    return ERROR_SUCCESS;
}

// Lookup a route in the routing table
error_code_t routing_table_lookup(ip_addr_t dest_ip, ip_addr_t *next_hop, uint32_t *interface_id) {
    uint32_t longest_prefix_match = 0;
    int32_t best_route_index = -1;
    
    // Find the longest prefix match
    for (uint32_t i = 0; i < route_count; i++) {
        if (!routing_table[i].is_active) {
            continue;
        }
        
        // Check if destination matches this network
        ip_addr_t masked_dest;
        for (int j = 0; j < 4; j++) {
            masked_dest.bytes[j] = dest_ip.bytes[j] & routing_table[i].subnet_mask.bytes[j];
        }
        
        if (memcmp(&masked_dest, &routing_table[i].dest_network, sizeof(ip_addr_t)) == 0) {
            // Calculate prefix length
            uint32_t prefix_length = 0;
            for (int j = 0; j < 4; j++) {
                uint8_t mask = routing_table[i].subnet_mask.bytes[j];
                while (mask) {
                    prefix_length += (mask & 0x01);
                    mask >>= 1;
                }
            }
            
            // Check if this is a better match
            if (prefix_length > longest_prefix_match) {
                longest_prefix_match = prefix_length;
                best_route_index = i;
            }
            // If prefix lengths are equal, choose the one with lower metric
            else if (prefix_length == longest_prefix_match && 
                     routing_table[i].metric < routing_table[best_route_index].metric) {
                best_route_index = i;
            }
        }
    }
    
    if (best_route_index >= 0) {
        memcpy(next_hop, &routing_table[best_route_index].next_hop, sizeof(ip_addr_t));
        *interface_id = routing_table[best_route_index].interface_id;
        return ERROR_SUCCESS;
    }
    
    LOG_DEBUG("No route found for destination %s", ip_to_str(dest_ip));
    return ERROR_NOT_FOUND;
}

// Delete a route from the routing table
error_code_t routing_table_delete_route(ip_addr_t dest_network, ip_addr_t subnet_mask) {
    for (uint32_t i = 0; i < route_count; i++) {
        if (memcmp(&routing_table[i].dest_network, &dest_network, sizeof(ip_addr_t)) == 0 &&
            memcmp(&routing_table[i].subnet_mask, &subnet_mask, sizeof(ip_addr_t)) == 0) {
            
            // Mark route as inactive
            routing_table[i].is_active = false;
            
            LOG_DEBUG("Deleted route to %s/%s", ip_to_str(dest_network), ip_to_str(subnet_mask));
            return ERROR_SUCCESS;
        }
    }
    
    LOG_WARNING("Route to %s/%s not found for deletion", ip_to_str(dest_network), ip_to_str(subnet_mask));
    return ERROR_NOT_FOUND;
}
