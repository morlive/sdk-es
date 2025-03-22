#include "include/l3/routing_protocols.h"
#include "include/l3/routing_table.h"
#include "include/common/logging.h"
#include "include/common/error_codes.h"

#define OSPF_PROTOCOL_NUMBER 89
#define OSPF_VERSION 2
#define OSPF_MAX_NEIGHBORS 100
#define OSPF_HELLO_INTERVAL 10 // seconds
#define OSPF_DEAD_INTERVAL 40 // seconds
#define OSPF_MAX_AREAS 10

typedef enum {
    OSPF_HELLO = 1,
    OSPF_DATABASE_DESCRIPTION = 2,
    OSPF_LINK_STATE_REQUEST = 3,
    OSPF_LINK_STATE_UPDATE = 4,
    OSPF_LINK_STATE_ACK = 5
} ospf_packet_type_t;

typedef enum {
    OSPF_NEIGHBOR_DOWN = 0,
    OSPF_NEIGHBOR_INIT = 1,
    OSPF_NEIGHBOR_2WAY = 2,
    OSPF_NEIGHBOR_EXSTART = 3,
    OSPF_NEIGHBOR_EXCHANGE = 4,
    OSPF_NEIGHBOR_LOADING = 5,
    OSPF_NEIGHBOR_FULL = 6
} ospf_neighbor_state_t;

typedef struct {
    uint8_t version;
    uint8_t type;
    uint16_t packet_length;
    ip_addr_t router_id;
    ip_addr_t area_id;
    uint16_t checksum;
    uint16_t auth_type;
    uint8_t auth_data[8];
} ospf_header_t;

typedef struct {
    ip_addr_t router_id;
    ip_addr_t address;
    uint32_t interface_id;
    ospf_neighbor_state_t state;
    uint32_t last_hello_time;
    uint32_t dd_sequence_num;
    bool is_dr;
    bool is_bdr;
} ospf_neighbor_t;

static ospf_neighbor_t ospf_neighbors[OSPF_MAX_NEIGHBORS];
static uint32_t ospf_neighbor_count = 0;
static bool ospf_running = false;
static uint32_t ospf_hello_timer_id = 0;
static ip_addr_t ospf_router_id;

// Initialize OSPF protocol
error_code_t ospf_init(void) {
    LOG_INFO("Initializing OSPF protocol");
    ospf_neighbor_count = 0;
    memset(ospf_neighbors, 0, sizeof(ospf_neighbors));
    ospf_running = false;
    
    // Generate router ID (use highest IP address of any interface)
    ip_addr_t highest_ip = {0};
    for (uint32_t i = 0; i < get_interface_count(); i++) {
        ip_addr_t ip = get_interface_ip(i);
        if (ip_compare(ip, highest_ip) > 0) {
            highest_ip = ip;
        }
    }
    
    ospf_router_id = highest_ip;
    LOG_INFO("OSPF router ID set to %s", ip_to_str(ospf_router_id));
    
    return ERROR_SUCCESS;
}

// Start OSPF protocol
error_code_t ospf_start(void) {
    if (ospf_running) {
        LOG_WARNING("OSPF is already running");
        return ERROR_ALREADY_INITIALIZED;
    }
    
    LOG_INFO("Starting OSPF protocol");
    
    // Register for raw IP protocol 89
    error_code_t result = ip_register_protocol(OSPF_PROTOCOL_NUMBER, ospf_process_packet);
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to register IP protocol for OSPF");
        return result;
    }
    
    // Start hello packet timer
    ospf_hello_timer_id = timer_start(OSPF_HELLO_INTERVAL * 1000, true, ospf_hello_timer_callback);
    
    // Send initial hello packets
    ospf_send_hello_all_interfaces();
    
    ospf_running = true;
    return ERROR_SUCCESS;
}

// Stop OSPF protocol
error_code_t ospf_stop(void) {
    if (!ospf_running) {
        LOG_WARNING("OSPF is not running");
        return ERROR_NOT_INITIALIZED;
    }
    
    LOG_INFO("Stopping OSPF protocol");
    
    // Unregister IP protocol
    ip_unregister_protocol(OSPF_PROTOCOL_NUMBER);
    
    // Stop timer
    timer_stop(ospf_hello_timer_id);
    
    ospf_running = false;
    return ERROR_SUCCESS;
}

// Process incoming OSPF packet
static error_code_t ospf_process_packet(packet_t *packet) {
    if (!packet || packet->length < sizeof(ospf_header_t)) {
        LOG_ERROR("Invalid OSPF packet");
        return ERROR_INVALID_PARAMETER;
    }
    
    ospf_header_t *header = (ospf_header_t *)packet->data;
    
    // Validate OSPF version
    if (header->version != OSPF_VERSION) {
        LOG_WARNING("Unsupported OSPF version: %d", header->version);
        return ERROR_UNSUPPORTED;
    }
    
    // Verify checksum
    uint16_t original_checksum = header->checksum;
    header->checksum = 0;
    uint16_t calculated_checksum = calculate_checksum(packet->data, packet->length);
    header->checksum = original_checksum;
    
    if (original_checksum != calculated_checksum) {
        LOG_WARNING("OSPF checksum mismatch");
        return ERROR_CHECKSUM_FAILURE;
    }
    
    // Process based on packet type
    switch (header->type) {
        case OSPF_HELLO:
            return ospf_process_hello(packet);
            
        case OSPF_DATABASE_DESCRIPTION:
            return ospf_process_database_description(packet);
            
        case OSPF_LINK_STATE_REQUEST:
            return ospf_process_link_state_request(packet);
            
        case OSPF_LINK_STATE_UPDATE:
            return ospf_process_link_state_update(packet);
            
        case OSPF_LINK_STATE_ACK:
            return ospf_process_link_state_ack(packet);
            
        default:
            LOG_WARNING("Unknown OSPF packet type: %d", header->type);
            return ERROR_INVALID_PARAMETER;
    }
}

// Send hello packets on all interfaces
static error_code_t ospf_send_hello_all_interfaces(void) {
    LOG_DEBUG("Sending OSPF hello packets on all interfaces");
    
    for (uint32_t i = 0; i < get_interface_count(); i++) {
        if (is_interface_up(i) && is_ospf_enabled_on_interface(i)) {
            ospf_send_hello(i);
        }
    }
    
    return ERROR_SUCCESS;
}

// Send hello packet on a specific interface
static error_code_t ospf_send_hello
