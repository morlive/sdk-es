/**
 * @file rip.c
 * @brief Implementation of the RIP (Routing Information Protocol)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "common/logging.h"
#include "common/utils.h"
#include "hal/packet.h"
#include "hal/port.h"
#include "l3/routing_table.h"
#include "l3/ip_processing.h"
#include "l3/routing_protocols/rip.h"

#define RIP_VERSION             2
#define RIP_PORT                520
#define RIP_MULTICAST_ADDR      "224.0.0.9"
#define RIP_UPDATE_INTERVAL     30  // seconds
#define RIP_TIMEOUT             180 // seconds
#define RIP_GARBAGE_COLLECTION  120 // seconds
#define RIP_MAX_METRIC          15
#define RIP_INFINITY            16  // unreachable

// RIP command codes
#define RIP_CMD_REQUEST         1
#define RIP_CMD_RESPONSE        2

// Structure for a RIP entry in a RIP packet
typedef struct {
    uint16_t address_family;
    uint16_t route_tag;
    uint32_t ip_address;
    uint32_t subnet_mask;
    uint32_t next_hop;
    uint32_t metric;
} rip_entry_t;

// Structure for a RIP packet header
typedef struct {
    uint8_t command;
    uint8_t version;
    uint16_t zero;
    // followed by rip_entry_t entries
} rip_header_t;

// Structure for a RIP table entry
typedef struct rip_route {
    ip_addr_t destination;
    ip_addr_t subnet_mask;
    ip_addr_t next_hop;
    uint32_t metric;
    uint32_t interface_index;
    time_t last_update;
    bool is_valid;
    struct rip_route *next;
} rip_route_t;

// RIP routing table
static rip_route_t *rip_routes = NULL;

// RIP timer for periodic updates
static time_t last_update_time = 0;

// List of interfaces where RIP is enabled
static uint32_t *rip_enabled_interfaces = NULL;
static uint32_t rip_interface_count = 0;

// Forward declarations
static void rip_process_request(packet_t *packet, uint32_t interface_index);
static void rip_process_response(packet_t *packet, uint32_t interface_index);
static void rip_send_update(uint32_t interface_index, ip_addr_t destination);
static void rip_send_table(uint32_t interface_index, ip_addr_t destination);
static void rip_update_route(ip_addr_t destination, ip_addr_t subnet_mask, 
                             ip_addr_t next_hop, uint32_t metric, 
                             uint32_t interface_index);
static void rip_timeout_routes(void);
static void rip_garbage_collection(void);
static rip_route_t *rip_find_route(ip_addr_t destination, ip_addr_t subnet_mask);
static bool is_rip_enabled_on_interface(uint32_t interface_index);

/**
 * Initialize the RIP protocol
 */
void rip_init(void) {
    LOG_INFO("Initializing RIP protocol");
    
    // Initialize the routing table
    rip_routes = NULL;
    
    // Record the start time for timers
    last_update_time = time(NULL);
    
    // Initialize enabled interfaces array
    rip_enabled_interfaces = NULL;
    rip_interface_count = 0;
    
    LOG_INFO("RIP protocol initialized");
}

/**
 * Enable RIP on an interface
 * 
 * @param interface_index The index of the interface to enable RIP on
 * @return 0 on success, -1 on failure
 */
int rip_enable_on_interface(uint32_t interface_index) {
    LOG_INFO("Enabling RIP on interface %u", interface_index);
    
    // Check if the interface exists
    if (!port_is_valid(interface_index)) {
        LOG_ERROR("Interface %u does not exist", interface_index);
        return -1;
    }
    
    // Check if RIP is already enabled on this interface
    if (is_rip_enabled_on_interface(interface_index)) {
        LOG_WARN("RIP is already enabled on interface %u", interface_index);
        return 0;
    }
    
    // Add the interface to the enabled list
    uint32_t *new_interfaces = realloc(rip_enabled_interfaces, 
                                       (rip_interface_count + 1) * sizeof(uint32_t));
    if (new_interfaces == NULL) {
        LOG_ERROR("Failed to allocate memory for RIP interfaces");
        return -1;
    }
    
    rip_enabled_interfaces = new_interfaces;
    rip_enabled_interfaces[rip_interface_count] = interface_index;
    rip_interface_count++;
    
    // Send a request on this interface to get routes from neighbors
    ip_addr_t multicast_addr;
    if (ip_str_to_addr(RIP_MULTICAST_ADDR, &multicast_addr) != 0) {
        LOG_ERROR("Failed to convert RIP multicast address");
        return -1;
    }
    
    rip_send_table(interface_index, multicast_addr);
    
    LOG_INFO("RIP enabled on interface %u", interface_index);
    return 0;
}

/**
 * Disable RIP on an interface
 * 
 * @param interface_index The index of the interface to disable RIP on
 * @return 0 on success, -1 on failure
 */
int rip_disable_on_interface(uint32_t interface_index) {
    LOG_INFO("Disabling RIP on interface %u", interface_index);
    
    // Find the interface in the enabled list
    int found_index = -1;
    for (uint32_t i = 0; i < rip_interface_count; i++) {
        if (rip_enabled_interfaces[i] == interface_index) {
            found_index = i;
            break;
        }
    }
    
    if (found_index == -1) {
        LOG_WARN("RIP is not enabled on interface %u", interface_index);
        return 0;
    }
    
    // Remove the interface from the enabled list by shifting
    for (uint32_t i = found_index; i < rip_interface_count - 1; i++) {
        rip_enabled_interfaces[i] = rip_enabled_interfaces[i + 1];
    }
    
    rip_interface_count--;
    
    // Reallocate the memory if needed
    if (rip_interface_count > 0) {
        uint32_t *new_interfaces = realloc(rip_enabled_interfaces, 
                                           rip_interface_count * sizeof(uint32_t));
        if (new_interfaces != NULL) {
            rip_enabled_interfaces = new_interfaces;
        }
    } else {
        free(rip_enabled_interfaces);
        rip_enabled_interfaces = NULL;
    }
    
    LOG_INFO("RIP disabled on interface %u", interface_index);
    return 0;
}

/**
 * Process a RIP packet
 * 
 * @param packet The packet to process
 * @param interface_index The interface the packet was received on
 */
void rip_process_packet(packet_t *packet, uint32_t interface_index) {
    if (packet == NULL) {
        LOG_ERROR("NULL packet passed to rip_process_packet");
        return;
    }
    
    // Check if RIP is enabled on this interface
    if (!is_rip_enabled_on_interface(interface_index)) {
        LOG_DEBUG("Ignoring RIP packet on interface %u (RIP not enabled)", interface_index);
        return;
    }
    
    // Ensure the packet is large enough to contain a RIP header
    if (packet->length < sizeof(rip_header_t)) {
        LOG_ERROR("RIP packet too short");
        return;
    }
    
    // Cast the packet data to a RIP header
    rip_header_t *rip_header = (rip_header_t *)packet->data;
    
    // Check the RIP version
    if (rip_header->version != RIP_VERSION) {
        LOG_ERROR("Unsupported RIP version: %u", rip_header->version);
        return;
    }
    
    // Process based on command
    switch (rip_header->command) {
        case RIP_CMD_REQUEST:
            LOG_DEBUG("Received RIP request");
            rip_process_request(packet, interface_index);
            break;
            
        case RIP_CMD_RESPONSE:
            LOG_DEBUG("Received RIP response");
            rip_process_response(packet, interface_index);
            break;
            
        default:
            LOG_ERROR("Unknown RIP command: %u", rip_header->command);
            break;
    }
}

/**
 * Perform the periodic RIP tasks
 */
void rip_timer_task(void) {
    time_t current_time = time(NULL);
    
    // Check if it's time for a periodic update
    if (current_time - last_update_time >= RIP_UPDATE_INTERVAL) {
        LOG_DEBUG("Sending periodic RIP updates");
        
        // Send updates on all RIP-enabled interfaces
        ip_addr_t multicast_addr;
        if (ip_str_to_addr(RIP_MULTICAST_ADDR, &multicast_addr) == 0) {
            for (uint32_t i = 0; i < rip_interface_count; i++) {
                rip_send_update(rip_enabled_interfaces[i], multicast_addr);
            }
        } else {
            LOG_ERROR("Failed to convert RIP multicast address");
        }
        
        last_update_time = current_time;
    }
    
    // Check for route timeouts and garbage collection
    rip_timeout_routes();
    rip_garbage_collection();
}

/**
 * Add a route to the RIP table and main routing table
 * 
 * @param destination Destination network
 * @param subnet_mask Subnet mask for the destination
 * @param next_hop Next hop IP address
 * @param metric Metric (hop count) for the route
 * @param interface_index Index of the outgoing interface
 */
void rip_add_route(ip_addr_t destination, ip_addr_t subnet_mask, 
                   ip_addr_t next_hop, uint32_t metric, uint32_t interface_index) {
    // Validate the metric
    if (metric > RIP_MAX_METRIC) {
        LOG_WARN("Attempt to add route with metric > 15, setting to infinity");
        metric = RIP_INFINITY;
    }
    
    // Update the RIP route table
    rip_update_route(destination, subnet_mask, next_hop, metric, interface_index);
    
    // Only add to the main routing table if the route is reachable
    if (metric < RIP_INFINITY) {
        // Add to the main routing table
        routing_table_add(destination, subnet_mask, next_hop, RT_PROTO_RIP, 
                         metric, interface_index);
        
        LOG_INFO("Added RIP route to %s/%s via %s (metric %u)",
                 ip_addr_to_str(destination), ip_addr_to_str(subnet_mask),
                 ip_addr_to_str(next_hop), metric);
    }
}

/**
 * Process a RIP request packet
 * 
 * @param packet The request packet to process
 * @param interface_index The interface the packet was received on
 */
static void rip_process_request(packet_t *packet, uint32_t interface_index) {
    rip_header_t *rip_header = (rip_header_t *)packet->data;
    rip_entry_t *entries = (rip_entry_t *)(packet->data + sizeof(rip_header_t));
    
    // Calculate the number of entries
    int entry_count = (packet->length - sizeof(rip_header_t)) / sizeof(rip_entry_t);
    
    // Get the source IP for response
    ip_addr_t source_ip = packet->ip_header.src_addr;
    
    // If we receive a request for a specific route or all routes (indicated by AF=0, Metric=16)
    if (entry_count == 1 && 
        (entries[0].address_family == 0 && entries[0].metric == RIP_INFINITY)) {
        // Send the entire table
        rip_send_table(interface_index, source_ip);
    } else {
        // TODO: Implement specific route request handling
        // For simplicity, we'll send the entire table here too
        rip_send_table(interface_index, source_ip);
    }
}

/**
 * Process a RIP response packet
 * 
 * @param packet The response packet to process
 * @param interface_index The interface the packet was received on
 */
static void rip_process_response(packet_t *packet, uint32_t interface_index) {
    rip_header_t *rip_header = (rip_header_t *)packet->data;
    rip_entry_t *entries = (rip_entry_t *)(packet->data + sizeof(rip_header_t));
    
    // Get the source IP (next hop for these routes)
    ip_addr_t next_hop = packet->ip_header.src_addr;
    
    // Calculate the number of entries
    int entry_count = (packet->length - sizeof(rip_header_t)) / sizeof(rip_entry_t);
    
    // Process each route entry
    for (int i = 0; i < entry_count; i++) {
        // Convert the entry fields to our internal format
        ip_addr_t destination = {entries[i].ip_address};
        ip_addr_t subnet_mask = {entries[i].subnet_mask};
        uint32_t metric = entries[i].metric;
        
        // Add the metric of the interface (usually 1)
        metric += 1;
        
        // Check if the metric is still valid
        if (metric <= RIP_MAX_METRIC) {
            // Update the route
            rip_add_route(destination, subnet_mask, next_hop, metric, interface_index);
        }
    }
}

/**
 * Send RIP updates on an interface
 * 
 * @param interface_index The interface to send updates on
 * @param destination The destination IP address
 */
static void rip_send_update(uint32_t interface_index, ip_addr_t destination) {
    // Get the interface IP and subnet
    ip_addr_t interface_ip;
    ip_addr_t interface_subnet;
    
    if (port_get_ip(interface_index, &interface_ip) != 0 ||
        port_get_subnet_mask(interface_index, &interface_subnet) != 0) {
        LOG_ERROR("Failed to get interface IP/subnet for RIP update");
        return;
    }
    
    // Allocate memory for the packet
    // Header + space for multiple entries
    uint32_t max_entries = 25;  // Arbitrary limit for simplicity
    uint32_t packet_size = sizeof(rip_header_t) + max_entries * sizeof(rip_entry_t);
    uint8_t *packet_buffer = malloc(packet_size);
    
    if (packet_buffer == NULL) {
        LOG_ERROR("Failed to allocate memory for RIP update packet");
        return;
    }
    
    // Initialize the header
    rip_header_t *rip_header = (rip_header_t *)packet_buffer;
    rip_header->command = RIP_CMD_RESPONSE;
    rip_header->version = RIP_VERSION;
    rip_header->zero = 0;
    
    // Prepare entries
    rip_entry_t *entries = (rip_entry_t *)(packet_buffer + sizeof(rip_header_t));
    uint32_t entry_count = 0;
    
    // Add entries from the RIP table (with split horizon)
    rip_route_t *current = rip_routes;
    while (current != NULL && entry_count < max_entries) {
        // Split horizon: don't advertise routes back to the interface they came from
        if (current->interface_index != interface_index && current->is_valid) {
            entries[entry_count].address_family = 2;  // IP
            entries[entry_count].route_tag = 0;
            entries[entry_count].ip_address = current->destination.addr;
            entries[entry_count].subnet_mask = current->subnet_mask.addr;
            entries[entry_count].next_hop = 0;  // Indicates to use the packet source
            entries[entry_count].metric = current->metric;
            entry_count++;
        }
        current = current->next;
    }
    
    // Calculate the actual packet size based on the number of entries
    uint32_t actual_size = sizeof(rip_header_t) + entry_count * sizeof(rip_entry_t);
    
    // Create and send the packet
    packet_t rip_packet;
    memset(&rip_packet, 0, sizeof(packet_t));
    
    // Set up IP header fields
    rip_packet.ip_header.src_addr = interface_ip;
    rip_packet.ip_header.dst_addr = destination;
    rip_packet.ip_header.protocol = IP_PROTO_UDP;
    rip_packet.ip_header.ttl = 1;  // RIP packets have TTL of 1
    
    // Set up UDP header fields
    rip_packet.udp_header.src_port = RIP_PORT;
    rip_packet.udp_header.dst_port = RIP_PORT;
    
    // Set the packet data
    rip_packet.data = packet_buffer;
    rip_packet.length = actual_size;
    
    // Send the packet
    if (ip_send_packet(&rip_packet, interface_index) != 0) {
        LOG_ERROR("Failed to send RIP update packet");
    } else {
        LOG_DEBUG("Sent RIP update with %u routes", entry_count);
    }
    
    // Free the packet buffer
    free(packet_buffer);
}

/**
 * Send the entire RIP table to a specific destination
 * 
 * @param interface_index The interface to send the table on
 * @param destination The destination IP address
 */
static void rip_send_table(uint32_t interface_index, ip_addr_t destination) {
    // This is a simplified version of rip_send_update
    // In a real implementation, we might need to split the table into multiple packets
    rip_send_update(interface_index, destination);
}

/**
 * Update a route in the RIP table
 * 
 * @param destination Destination network
 * @param subnet_mask Subnet mask for the destination
 * @param next_hop Next hop IP address
 * @param metric Metric (hop count) for the route
 * @param interface_index Index of the outgoing interface
 */
static void rip_update_route(ip_addr_t destination, ip_addr_t subnet_mask, 
                             ip_addr_t next_hop, uint32_t metric, 
                             uint32_t interface_index) {
    // Find if the route already exists
    rip_route_t *route = rip_find_route(destination, subnet_mask);
    
    if (route != NULL) {
        // Route exists, update it if the new metric is better or from the same next hop
        if (metric < route->metric || ip_addr_equal(next_hop, route->next_hop)) {
            route->next_hop = next_hop;
            route->metric = metric;
            route->interface_index = interface_index;
            route->last_update = time(NULL);
            route->is_valid = (metric < RIP_INFINITY);
            
            // Update the main routing table
            if (route->is_valid) {
                routing_table_update(destination, subnet_mask, next_hop, 
                                    metric, interface_index);
            } else {
                routing_table_remove(destination, subnet_mask);
            }
        }
    } else {
        // Route doesn't exist, create a new one
        rip_route_t *new_route = malloc(sizeof(rip_route_t));
        if (new_route == NULL) {
            LOG_ERROR("Failed to allocate memory for new RIP route");
            return;
        }
        
        new_route->destination = destination;
        new_route->subnet_mask = subnet_mask;
        new_route->next_hop = next_hop;
        new_route->metric = metric;
        new_route->interface_index = interface_index;
        new_route->last_update = time(NULL);
        new_route->is_valid = (metric < RIP_INFINITY);
        new_route->next = rip_routes;
        rip_routes = new_route;
        
        // Add to the main routing table if valid
        if (new_route->is_valid) {
            routing_table_add(destination, subnet_mask, next_hop, RT_PROTO_RIP, 
                             metric, interface_index);
        }
    }
}

/**
 * Handle route timeouts
 */
static void rip_timeout_routes(void) {
    time_t current_time = time(NULL);
    rip_route_t *current = rip_routes;
    
    while (current != NULL) {
        // Check if the route has timed out
        if (current->is_valid && 
            (current_time - current->last_update) > RIP_TIMEOUT) {
            LOG_INFO("RIP route to %s timed out", ip_addr_to_str(current->destination));
            
            // Mark the route as invalid and set metric to infinity
            current->is_valid = false;
            current->metric = RIP_INFINITY;
            
            // Remove from the main routing table
            routing_table_remove(current->destination, current->subnet_mask);
            
            // Record the time when the route became invalid for garbage collection
            current->last_update = current_time;
        }
        
        current = current->next;
    }
}

/**
 * Perform garbage collection on invalid routes
 */
static void rip_garbage_collection(void) {
    time_t current_time = time(NULL);
    rip_route_t *current = rip_routes;
    rip_route_t *prev = NULL;
    
    while (current != NULL) {
        // Check if the invalid route should be removed
        if (!current->is_valid && 
            (current_time - current->last_update) > RIP_GARBAGE_COLLECTION) {
            LOG_INFO("Removing expired RIP route to %s", 
                     ip_addr_to_str(current->destination));
            
            // Remove the route
            rip_route_t *to_remove = current;
            
            if (prev == NULL) {
                // This is the first route in the list
                rip_routes = current->next;
                current = rip_routes;
            } else {
                // This is not the first route
                prev->next = current->next;
                current = current->next;
            }
            
            free(to_remove);
        } else {
            // Move to the next route
            prev = current;
            current = current->next;
        }
    }
}

/**
 * Find a route in the RIP table
 * 
 * @param destination Destination network to find
 * @param subnet_mask Subnet mask for the destination
 * @return Pointer to the route if found, NULL otherwise
 */
static rip_route_t *rip_find_route(ip_addr_t destination, ip_addr_t subnet_mask) {
    rip_route_t *current = rip_routes;
    
    while (current != NULL) {
        if (ip_addr_equal(current->destination, destination) && 
            ip_addr_equal(current->subnet_mask, subnet_mask)) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

/**
 * Check if RIP is enabled on an interface
 * 
 * @param interface_index The interface to check
 * @return true if RIP is enabled, false otherwise
 */
static bool is_rip_enabled_on_interface(uint32_t interface_index) {
    for (uint32_t i = 0; i < rip_interface_count; i++) {
        if (rip_enabled_interfaces[i] == interface_index) {
            return true;
        }
    }
    return false;
}

/**
 * Clean up RIP resources
 */
void rip_cleanup(void) {
    LOG_INFO("Cleaning up RIP resources");
    
    // Free the routing table
    rip_route_t *current = rip_routes;
    while (current != NULL) {
        rip_route_t *next = current->next;
        free(current);
        current = next;
    }
    rip_routes = NULL;
    
    // Free the interfaces array
    free(rip_enabled_interfaces);
    rip_enabled_interfaces = NULL;
    rip_interface_count = 0;
    
    LOG_INFO("RIP resources cleaned up");
}
