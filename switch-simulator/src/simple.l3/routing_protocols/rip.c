#include "include/l3/routing_protocols.h"
#include "include/l3/routing_table.h"
#include "include/common/logging.h"
#include "include/common/error_codes.h"

#define RIP_PORT 520
#define RIP_VERSION 2
#define RIP_MAX_NEIGHBORS 32
#define RIP_UPDATE_INTERVAL 30 // seconds
#define RIP_TIMEOUT 180 // seconds
#define RIP_GARBAGE_COLLECTION 120 // seconds
#define RIP_INFINITY 16

typedef struct {
    uint8_t command;
    uint8_t version;
    uint16_t zero;
} rip_header_t;

typedef struct {
    uint16_t address_family;
    uint16_t route_tag;
    ip_addr_t ip_address;
    ip_addr_t subnet_mask;
    ip_addr_t next_hop;
    uint32_t metric;
} rip_entry_t;

typedef struct {
    ip_addr_t address;
    uint32_t interface_id;
    uint32_t last_update_time;
} rip_neighbor_t;

static rip_neighbor_t rip_neighbors[RIP_MAX_NEIGHBORS];
static uint32_t rip_neighbor_count = 0;
static bool rip_running = false;
static uint32_t rip_timer_id = 0;

// Initialize RIP protocol
error_code_t rip_init(void) {
    LOG_INFO("Initializing RIP protocol");
    rip_neighbor_count = 0;
    memset(rip_neighbors, 0, sizeof(rip_neighbors));
    rip_running = false;
    return ERROR_SUCCESS;
}

// Start RIP protocol
error_code_t rip_start(void) {
    if (rip_running) {
        LOG_WARNING("RIP is already running");
        return ERROR_ALREADY_INITIALIZED;
    }
    
    LOG_INFO("Starting RIP protocol");
    
    // Register for UDP port 520
    error_code_t result = udp_register_port(RIP_PORT, rip_process_packet);
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to register UDP port for RIP");
        return result;
    }
    
    // Send initial request to neighbors
    rip_send_request();
    
    // Start periodic update timer
    rip_timer_id = timer_start(RIP_UPDATE_INTERVAL * 1000, true, rip_timer_callback);
    
    rip_running = true;
    return ERROR_SUCCESS;
}

// Stop RIP protocol
error_code_t rip_stop(void) {
    if (!rip_running) {
        LOG_WARNING("RIP is not running");
        return ERROR_NOT_INITIALIZED;
    }
    
    LOG_INFO("Stopping RIP protocol");
    
    // Unregister UDP port
    udp_unregister_port(RIP_PORT);
    
    // Stop timer
    timer_stop(rip_timer_id);
    
    rip_running = false;
    return ERROR_SUCCESS;
}

// Process incoming RIP packet
static error_code_t rip_process_packet(packet_t *packet) {
    if (!packet || packet->length < sizeof(rip_header_t)) {
        LOG_ERROR("Invalid RIP packet");
        return ERROR_INVALID_PARAMETER;
    }
    
    rip_header_t *header = (rip_header_t *)packet->data;
    
    // Validate RIP version
    if (header->version != RIP_VERSION) {
        LOG_WARNING("Unsupported RIP version: %d", header->version);
        return ERROR_UNSUPPORTED;
    }
    
    // Process based on command
    switch (header->command) {
        case 1: // Request
            return rip_process_request(packet);
            
        case 2: // Response
            return rip_process_response(packet);
            
        default:
            LOG_WARNING("Unknown RIP command: %d", header->command);
            return ERROR_INVALID_PARAMETER;
    }
}

// Send RIP request to all neighbors
static error_code_t rip_send_request(void) {
    LOG_DEBUG("Sending RIP request");
    
    // Create request packet
    uint8_t buffer[sizeof(rip_header_t)];
    rip_header_t *header = (rip_header_t *)buffer;
    
    header->command = 1; // Request
    header->version = RIP_VERSION;
    header->zero = 0;
    
    // Send to all interfaces
    for (uint32_t i = 0; i < get_interface_count(); i++) {
        if (is_interface_up(i)) {
            ip_addr_t broadcast_addr = get_interface_broadcast(i);
            udp_send(broadcast_addr, RIP_PORT, buffer, sizeof(rip_header_t));
        }
    }
    
    return ERROR_SUCCESS;
}

// Timer callback for periodic RIP updates
static void rip_timer_callback(uint32_t timer_id) {
    LOG_DEBUG("RIP timer triggered, sending updates");
    rip_send_updates();
}

// Send RIP updates to all neighbors
static error_code_t rip_send_updates(void) {
    LOG_DEBUG("Sending RIP updates");
    
    // Get all routes to advertise
    route_entry_t routes[MAX_ROUTES];
    uint32_t route_count = 0;
    
    routing_table_get_routes(routes, &route_count);
    
    // Create update packet
    uint32_t packet_size = sizeof(rip_header_t) + route_count * sizeof(rip_entry_t);
    uint8_t *buffer = malloc(packet_size);
    if (!buffer) {
        LOG_ERROR("Failed to allocate memory for RIP update");
        return ERROR_OUT_OF_MEMORY;
    }
    
    rip_header_t *header = (rip_header_t *)buffer;
    rip_entry_t *entries = (rip_entry_t *)(buffer + sizeof(rip_header_t));
    
    header->command = 2; // Response
    header->version = RIP_VERSION;
    header->zero = 0;
    
    // Fill in route entries
    for (uint32_t i = 0; i < route_count; i++) {
        entries[i].address_family = 2; // IP
        entries[i].route_tag = 0;
        memcpy(&entries[i].ip_address, &routes[i].dest_network, sizeof(ip_addr_t));
        memcpy(&entries[i].subnet_mask, &routes[i].subnet_mask, sizeof(ip_addr_t));
        memcpy(&entries[i].next_hop, &routes[i].next_hop, sizeof(ip_addr_t));
        entries[i].metric = routes[i].metric < RIP_INFINITY ? routes[i].metric : RIP_INFINITY;
    }
    
    // Send to all interfaces
    for (uint32_t i = 0; i < get_interface_count(); i++) {
        if (is_interface_up(i)) {
            ip_addr_t broadcast_addr = get_interface_broadcast(i);
            udp_send(broadcast_addr, RIP_PORT, buffer, packet_size);
        }
    }
    
    free(buffer);
    return ERROR_SUCCESS;
}
