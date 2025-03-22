/**
 * @file ospf.c
 * @brief Implementation of Open Shortest Path First (OSPF) routing protocol
 *
 * This file implements the OSPF routing protocol for the switching/routing system.
 * It supports OSPFv2 for IPv4 networks, handling neighbor discovery, database 
 * synchronization, route calculation using Dijkstra's algorithm, and LSA flooding.
 *
 * @copyright (c) 2025 Switch Simulator Project
 * @license Proprietary
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <arpa/inet.h>  /* For network byte order conversions */
#include <time.h>

#include "common/error_codes.h"
#include "common/logging.h"
#include "common/types.h"
#include "hal/packet.h"
#include "hal/port.h"
#include "l3/ip.h"
#include "l3/routing_protocols.h"
#include "l3/routing_table.h"
#include "management/stats.h"

/* OSPF Packet Types */
#define OSPF_HELLO                1
#define OSPF_DB_DESCRIPTION       2
#define OSPF_LS_REQUEST           3
#define OSPF_LS_UPDATE            4
#define OSPF_LS_ACK               5

/* OSPF Version */
#define OSPF_VERSION_2            2

/* OSPF Packet Header Length */
#define OSPF_HEADER_LENGTH        24

/* OSPF Router Types and Flags */
#define OSPF_ROUTER_NONE          0x00
#define OSPF_ROUTER_ABR           0x01  /* Area Border Router */
#define OSPF_ROUTER_ASBR          0x02  /* AS Boundary Router */
#define OSPF_ROUTER_VIRTUAL       0x04  /* Virtual Link Endpoint */

/* OSPF Area Types */
#define OSPF_AREA_STANDARD        0
#define OSPF_AREA_STUB            1
#define OSPF_AREA_NSSA            2
#define OSPF_AREA_BACKBONE        0     /* Backbone Area ID: 0.0.0.0 */

/* OSPF Interface Types */
#define OSPF_IFACE_BROADCAST      1
#define OSPF_IFACE_POINT_TO_POINT 2
#define OSPF_IFACE_POINT_TO_MULTI 3
#define OSPF_IFACE_VIRTUAL        4

/* OSPF Neighbor States */
#define OSPF_NBR_DOWN             0
#define OSPF_NBR_ATTEMPT          1
#define OSPF_NBR_INIT             2
#define OSPF_NBR_2WAY             3
#define OSPF_NBR_EXSTART          4
#define OSPF_NBR_EXCHANGE         5
#define OSPF_NBR_LOADING          6
#define OSPF_NBR_FULL             7

/* OSPF Interface States */
#define OSPF_IFACE_DOWN           0
#define OSPF_IFACE_LOOPBACK       1
#define OSPF_IFACE_WAITING        2
#define OSPF_IFACE_POINT_TO_POINT 3
#define OSPF_IFACE_DR_OTHER       4
#define OSPF_IFACE_BACKUP         5
#define OSPF_IFACE_DR             6

/* OSPF LSA Types */
#define OSPF_LSA_ROUTER           1
#define OSPF_LSA_NETWORK          2
#define OSPF_LSA_SUMMARY_IP       3
#define OSPF_LSA_SUMMARY_ASBR     4
#define OSPF_LSA_EXTERNAL         5

/* OSPF Common Timers (in seconds) */
#define OSPF_HELLO_INTERVAL       10
#define OSPF_DEAD_INTERVAL        40
#define OSPF_RETRANSMIT_INTERVAL  5
#define OSPF_LSA_REFRESH_TIME     1800  /* 30 minutes */
#define OSPF_LSA_MAX_AGE          3600  /* 1 hour */

/* OSPF Multicast Addresses (in host byte order) */
#define OSPF_ALLROUTERS_ADDRESS   0xE0000005  /* 224.0.0.5 */
#define OSPF_ALLDROUTERS_ADDRESS  0xE0000006  /* 224.0.0.6 */

/* Maximum number of OSPF areas supported */
#define OSPF_MAX_AREAS            32

/* Maximum number of interfaces per area */
#define OSPF_MAX_INTERFACES       64

/* Maximum number of neighbors per interface */
#define OSPF_MAX_NEIGHBORS        32

/* Maximum number of LSAs in database */
#define OSPF_MAX_LSAS             1024

/* Protocol specific statistics */
typedef struct {
    uint64_t hello_sent;
    uint64_t hello_received;
    uint64_t dd_sent;
    uint64_t dd_received;
    uint64_t ls_req_sent;
    uint64_t ls_req_received;
    uint64_t ls_upd_sent;
    uint64_t ls_upd_received;
    uint64_t ls_ack_sent;
    uint64_t ls_ack_received;
    uint64_t checksum_errors;
    uint64_t malformed_packets;
    uint64_t neighbor_adjacencies;
    uint64_t spf_calculations;
    uint64_t lsa_originations;
    uint64_t lsa_retransmissions;
} ospf_stats_t;

/* OSPF packet header structure */
typedef struct {
    uint8_t  version;
    uint8_t  type;
    uint16_t packet_length;
    uint32_t router_id;
    uint32_t area_id;
    uint16_t checksum;
    uint16_t auth_type;
    uint64_t auth_data;  /* 8 bytes for authentication data */
} ospf_header_t;

/* OSPF hello packet structure */
typedef struct {
    uint32_t network_mask;
    uint16_t hello_interval;
    uint8_t  options;
    uint8_t  router_priority;
    uint32_t router_dead_interval;
    uint32_t designated_router;
    uint32_t backup_designated_router;
    /* Followed by an array of neighbor router IDs */
} ospf_hello_t;

/* OSPF database description packet structure */
typedef struct {
    uint16_t mtu;
    uint8_t  options;
    uint8_t  flags;      /* Init, More, Master/Slave bits */
    uint32_t dd_sequence_number;
    /* Followed by LSA headers */
} ospf_db_desc_t;

/* LSA Header structure */
typedef struct {
    uint16_t ls_age;
    uint8_t  options;
    uint8_t  ls_type;
    uint32_t link_state_id;
    uint32_t advertising_router;
    uint32_t ls_sequence_number;
    uint16_t ls_checksum;
    uint16_t length;
} ospf_lsa_header_t;

/* Router LSA structure */
typedef struct {
    ospf_lsa_header_t header;
    uint16_t flags;
    uint16_t num_links;
    /* Followed by links */
} ospf_router_lsa_t;

/* Router LSA Link entry */
typedef struct {
    uint32_t link_id;
    uint32_t link_data;
    uint8_t  link_type;
    uint8_t  num_tos;
    uint16_t tos_0_metric;
    /* TOS-specific metrics follow if num_tos > 0 */
} ospf_router_link_t;

/* Network LSA structure */
typedef struct {
    ospf_lsa_header_t header;
    uint32_t network_mask;
    /* Followed by attached routers */
} ospf_network_lsa_t;

/* External LSA structure */
typedef struct {
    ospf_lsa_header_t header;
    uint32_t network_mask;
    uint8_t  e_bit_tos;
    uint8_t  unused[3];  /* padding for alignment */
    uint32_t metric;
    uint32_t forwarding_address;
    uint32_t external_route_tag;
} ospf_external_lsa_t;

/* OSPF neighbor structure */
typedef struct {
    uint32_t router_id;
    uint32_t neighbor_ip;
    uint8_t  state;
    uint8_t  priority;
    uint32_t dr;
    uint32_t bdr;
    uint32_t dd_sequence;
    uint8_t  options;
    time_t   last_hello;
    bool     is_master;
    bool     i_have_lst;
    bool     neighbor_has_lst;
    /* Link state request list */
    ospf_lsa_header_t *request_list;
    uint16_t request_list_count;
    /* Link state retransmission list */
    ospf_lsa_header_t *retransmit_list;
    uint16_t retransmit_list_count;
} ospf_neighbor_t;

/* OSPF interface structure */
typedef struct {
    uint32_t interface_id;
    uint32_t ip_address;
    uint32_t network_mask;
    uint8_t  interface_type;
    uint8_t  state;
    uint8_t  priority;
    uint16_t hello_interval;
    uint16_t dead_interval;
    uint16_t retransmit_interval;
    uint32_t dr;
    uint32_t bdr;
    uint32_t area_id;
    uint16_t mtu;
    uint16_t cost;
    time_t   last_hello_sent;
    /* Neighbors */
    ospf_neighbor_t neighbors[OSPF_MAX_NEIGHBORS];
    uint16_t neighbor_count;
} ospf_interface_t;

/* OSPF area structure */
typedef struct {
    uint32_t area_id;
    uint8_t  area_type;
    bool     import_summary;
    /* Interfaces in this area */
    ospf_interface_t interfaces[OSPF_MAX_INTERFACES];
    uint16_t interface_count;
    /* Link state database */
    ospf_lsa_header_t *lsdb;
    uint16_t lsdb_count;
    /* SPF calculation results */
    void *spf_tree;  /* Routing tree from SPF calculation */
    time_t last_spf_calculation;
} ospf_area_t;

/* OSPF global configuration structure */
typedef struct {
    uint32_t router_id;
    uint8_t  router_type;  /* ABR, ASBR flags */
    bool     active;
    uint16_t reference_bandwidth;
    uint16_t spf_calculation_delay;
    uint16_t spf_hold_time;
    uint16_t lsa_arrival_time;
    uint16_t lsa_generation_delay;
    uint16_t lsa_hold_time;
    uint16_t lsa_max_age_time;
    uint16_t lsa_refresh_time;
    uint16_t external_preference;
    bool     rfc1583_compatibility;
    ospf_area_t areas[OSPF_MAX_AREAS];
    uint16_t area_count;
    time_t last_age_check;
    /* Routing information */
    routing_table_t *ospf_routes;
    /* Statistics */
    ospf_stats_t stats;
} ospf_config_t;

/* Global OSPF configuration */
static ospf_config_t g_ospf_config;

/* Forward declarations of static functions */
static error_t ospf_initialize_router(uint32_t router_id);
static error_t ospf_configure_area(uint32_t area_id, uint8_t area_type);
static error_t ospf_configure_interface(uint32_t area_id, uint32_t interface_id, 
                                       uint32_t ip_address, uint32_t mask, 
                                       uint8_t type, uint16_t cost);
static error_t ospf_process_packet(packet_t *packet, uint16_t offset);
static error_t ospf_process_hello(ospf_interface_t *intf, const ospf_header_t *hdr, 
                                 const ospf_hello_t *hello, uint16_t length, 
                                 uint32_t src_ip);
static error_t ospf_process_database_desc(ospf_interface_t *intf, const ospf_header_t *hdr, 
                                        const ospf_db_desc_t *dd, uint16_t length, 
                                        uint32_t src_ip);
static error_t ospf_process_ls_request(ospf_interface_t *intf, const ospf_header_t *hdr, 
                                      const void *ls_req, uint16_t length, 
                                      uint32_t src_ip);
static error_t ospf_process_ls_update(ospf_interface_t *intf, const ospf_header_t *hdr, 
                                     const void *ls_upd, uint16_t length, 
                                     uint32_t src_ip);
static error_t ospf_process_ls_ack(ospf_interface_t *intf, const ospf_header_t *hdr, 
                                  const void *ls_ack, uint16_t length, 
                                  uint32_t src_ip);
static error_t ospf_send_hello(ospf_interface_t *intf);
static error_t ospf_send_database_desc(ospf_interface_t *intf, ospf_neighbor_t *nbr);
static error_t ospf_send_ls_request(ospf_interface_t *intf, ospf_neighbor_t *nbr);
static error_t ospf_send_ls_update(ospf_interface_t *intf, ospf_neighbor_t *nbr);
static error_t ospf_send_ls_ack(ospf_interface_t *intf, const ospf_lsa_header_t *lsa_hdr, 
                               uint32_t dst_ip);
static error_t ospf_calculate_routes(ospf_area_t *area);
static error_t ospf_run_dijkstra(ospf_area_t *area);
static error_t ospf_update_routing_table(void);
static error_t ospf_timer_tick(void);
static error_t ospf_age_lsdb(void);
static void ospf_handle_neighbor_state_change(ospf_interface_t *intf, ospf_neighbor_t *nbr, 
                                             uint8_t new_state);
static ospf_area_t *ospf_find_area(uint32_t area_id);
static ospf_interface_t *ospf_find_interface(ospf_area_t *area, uint32_t interface_id);
static ospf_neighbor_t *ospf_find_neighbor(ospf_interface_t *intf, uint32_t router_id);
static void *ospf_find_lsa(ospf_area_t *area, uint8_t ls_type, uint32_t ls_id, 
                          uint32_t adv_router);
static uint16_t ospf_calculate_checksum(const void *data, size_t len);
static void ospf_log_neighbor_state_change(uint32_t router_id, uint8_t old_state, 
                                          uint8_t new_state);

/**
 * @brief Initialize the OSPF module
 *
 * Initializes the OSPF protocol module, allocating necessary structures
 * and setting up default configuration values.
 *
 * @return ERROR_OK if successful, appropriate error code otherwise
 */
error_t ospf_init(void)
{
    LOG_INFO("Initializing OSPF routing protocol module");
    
    /* Initialize OSPF global configuration with defaults */
    memset(&g_ospf_config, 0, sizeof(g_ospf_config));
    g_ospf_config.active = false;
    g_ospf_config.reference_bandwidth = 100000;  /* 100 Mbps in Kbps */
    g_ospf_config.spf_calculation_delay = 5;     /* 5 seconds */
    g_ospf_config.spf_hold_time = 10;            /* 10 seconds */
    g_ospf_config.lsa_arrival_time = 1;          /* 1 second */
    g_ospf_config.lsa_generation_delay = 5;      /* 5 seconds */
    g_ospf_config.lsa_hold_time = 7;             /* 7 seconds */
    g_ospf_config.lsa_max_age_time = OSPF_LSA_MAX_AGE;
    g_ospf_config.lsa_refresh_time = OSPF_LSA_REFRESH_TIME;
    g_ospf_config.external_preference = 150;     /* Default external metric */
    g_ospf_config.rfc1583_compatibility = true;
    
    /* Allocate routing table for OSPF routes */
    g_ospf_config.ospf_routes = malloc(sizeof(routing_table_t));
    if (g_ospf_config.ospf_routes == NULL) {
        LOG_ERROR("Failed to allocate OSPF routing table");
        return ERROR_MEMORY_ALLOCATION;
    }
    memset(g_ospf_config.ospf_routes, 0, sizeof(routing_table_t));
    
    /* Initialize statistics */
    memset(&g_ospf_config.stats, 0, sizeof(ospf_stats_t));
    
    LOG_INFO("OSPF module initialized successfully");
    return ERROR_OK;
}

/**
 * @brief Cleanup and shutdown OSPF module
 *
 * Releases all resources allocated by the OSPF module.
 *
 * @return ERROR_OK if successful, appropriate error code otherwise
 */
error_t ospf_cleanup(void)
{
    LOG_INFO("Shutting down OSPF module");
    
    /* Free routing table */
    if (g_ospf_config.ospf_routes != NULL) {
        free(g_ospf_config.ospf_routes);
        g_ospf_config.ospf_routes = NULL;
    }
    
    /* TODO: Clean up other allocated resources like LSA databases */
    
    return ERROR_OK;
}

/**
 * @brief Start the OSPF protocol operations
 *
 * Activates the OSPF protocol with the current configuration.
 * Begins sending OSPF packets and participating in the routing domain.
 *
 * @return ERROR_OK if successful, appropriate error code otherwise
 */
error_t ospf_start(void)
{
    LOG_INFO("Starting OSPF protocol operations");
    
    if (g_ospf_config.router_id == 0) {
        LOG_ERROR("Cannot start OSPF: Router ID not configured");
        return ERROR_INVALID_CONFIG;
    }
    
    if (g_ospf_config.area_count == 0) {
        LOG_ERROR("Cannot start OSPF: No areas configured");
        return ERROR_INVALID_CONFIG;
    }
    
    g_ospf_config.active = true;
    g_ospf_config.last_age_check = time(NULL);
    
    /* Initialize interfaces and start sending Hello packets */
    for (int i = 0; i < g_ospf_config.area_count; i++) {
        ospf_area_t *area = &g_ospf_config.areas[i];
        
        for (int j = 0; j < area->interface_count; j++) {
            ospf_interface_t *intf = &area->interfaces[j];
            intf->state = OSPF_IFACE_WAITING;
            
            /* Send initial Hello */
            ospf_send_hello(intf);
        }
    }
    
    LOG_INFO("OSPF protocol started successfully");
    return ERROR_OK;
}

/**
 * @brief Stop the OSPF protocol operations
 *
 * Deactivates the OSPF protocol, stops sending packets,
 * and withdraws all advertised routes.
 *
 * @return ERROR_OK if successful, appropriate error code otherwise
 */
error_t ospf_stop(void)
{
    LOG_INFO("Stopping OSPF protocol operations");
    
    g_ospf_config.active = false;
    
    /* TODO: Send goodbye messages to neighbors (possibly by sending
       Hello packets with empty neighbor lists) */
    
    /* Clean up routing table */
    /* TODO: Withdraw OSPF routes from the main routing table */
    
    LOG_INFO("OSPF protocol stopped");
    return ERROR_OK;
}

/**
 * @brief Configure the local router ID for OSPF
 *
 * Sets the router ID for this OSPF instance. The router ID is a 32-bit 
 * identifier, usually represented as an IPv4 address.
 *
 * @param router_id The 32-bit router ID to assign
 * @return ERROR_OK if successful, appropriate error code otherwise
 */
error_t ospf_set_router_id(uint32_t router_id)
{
    if (router_id == 0) {
        LOG_ERROR("Invalid router ID (0.0.0.0) specified");
        return ERROR_INVALID_PARAM;
    }
    
    LOG_INFO("Setting OSPF router ID to %u.%u.%u.%u", 
            (router_id >> 24) & 0xFF, 
            (router_id >> 16) & 0xFF, 
            (router_id >> 8) & 0xFF, 
            router_id & 0xFF);
    
    g_ospf_config.router_id = router_id;
    
    return ERROR_OK;
}

/**
 * @brief Create and configure an OSPF area
 *
 * Adds a new area to the OSPF domain. Each area has its own link-state
 * database and can have specific properties like being a stub area.
 *
 * @param area_id The 32-bit area ID (usually in IP address format)
 * @param area_type The type of area (standard, stub, NSSA)
 * @return ERROR_OK if successful, appropriate error code otherwise
 */
error_t ospf_create_area(uint32_t area_id, uint8_t area_type)
{
    LOG_INFO("Creating OSPF area %u.%u.%u.%u of type %u", 
            (area_id >> 24) & 0xFF, 
            (area_id >> 16) & 0xFF, 
            (area_id >> 8) & 0xFF, 
            area_id & 0xFF,
            area_type);
    
    if (g_ospf_config.area_count >= OSPF_MAX_AREAS) {
        LOG_ERROR("Cannot create area: maximum number of areas reached");
        return ERROR_RESOURCE_LIMIT;
    }
    
    /* Check if area already exists */
    for (int i = 0; i < g_ospf_config.area_count; i++) {
        if (g_ospf_config.areas[i].area_id == area_id) {
            LOG_ERROR("Area %u.%u.%u.%u already exists", 
                    (area_id >> 24) & 0xFF, 
                    (area_id >> 16) & 0xFF, 
                    (area_id >> 8) & 0xFF, 
                    area_id & 0xFF);
            return ERROR_ALREADY_EXISTS;
        }
    }
    
    /* Create new area */
    ospf_area_t *new_area = &g_ospf_config.areas[g_ospf_config.area_count];
    memset(new_area, 0, sizeof(ospf_area_t));
    
    new_area->area_id = area_id;
    new_area->area_type = area_type;
    new_area->import_summary = (area_type != OSPF_AREA_STUB);
    
    g_ospf_config.area_count++;
    
    LOG_INFO("OSPF area %u.%u.%u.%u created successfully", 
            (area_id >> 24) & 0xFF, 
            (area_id >> 16) & 0xFF, 
            (area_id >> 8) & 0xFF, 
            area_id & 0xFF);
    
    return ERROR_OK;
}

/**
 * @brief Configure an interface for OSPF
 *
 * Adds an interface to an OSPF area with the specified parameters.
 *
 * @param area_id The 32-bit area ID this interface belongs to
 * @param interface_id The interface identifier in the system
 * @param ip_address The primary IP address of the interface
 * @param mask The subnet mask of the interface
 * @param type The OSPF interface type (broadcast, P2P, etc.)
 * @param cost The OSPF cost/metric for this interface
 * @return ERROR_OK if successful, appropriate error code otherwise
 */
error_t ospf_add_interface(uint32_t area_id, uint32_t interface_id, 
                          uint32_t ip_address, uint32_t mask, 
                          uint8_t type, uint16_t cost)
{
    LOG_INFO("Adding interface %u to OSPF area %u.%u.%u.%u", 
            interface_id,
            (area_id >> 24) & 0xFF, 
            (area_id >> 16) & 0xFF, 
            (area_id >> 8) & 0xFF, 
            area_id & 0xFF);
    
    /* Find the area */
    ospf_area_t *area = ospf_find_area(area_id);
    if (area == NULL) {
        LOG_ERROR("Cannot add interface: area %u.%u.%u.%u does not exist", 
                 (area_id >> 24) & 0xFF, 
                 (area_id >> 16) & 0xFF, 
                 (area_id >> 8) & 0xFF, 
                 area_id & 0xFF);
        return ERROR_NOT_FOUND;
    }
    
    if (area->interface_count >= OSPF_MAX_INTERFACES) {
        LOG_ERROR("Cannot add interface: maximum number of interfaces reached for area");
        return ERROR_RESOURCE_LIMIT;
    }
    
    /* Check if interface already exists in this area */
    for (int i = 0; i < area->interface_count; i++) {
        if (area->interfaces[i].interface_id == interface_id) {
            LOG_ERROR("Interface %u already exists in area %u.%u.%u.%u", 
                     interface_id,
                     (area_id >> 24) & 0xFF, 
                     (area_id >> 16) & 0xFF, 
                     (area_id >> 8) & 0xFF, 
                     area_id & 0xFF);
            return ERROR_ALREADY_EXISTS;
        }
    }
    
    /* Add new interface */
    ospf_interface_t *new_intf = &area->interfaces[area->interface_count];
    memset(new_intf, 0, sizeof(ospf_interface_t));
    
    new_intf->interface_id = interface_id;
    new_intf->ip_address = ip_address;
    new_intf->network_mask = mask;
    new_intf->interface_type = type;
    new_intf->state = OSPF_IFACE_DOWN;
    new_intf->priority = 1;  /* Default priority */
    new_intf->hello_interval = OSPF_HELLO_INTERVAL;
    new_intf->dead_interval = OSPF_DEAD_INTERVAL;
    new_intf->retransmit_interval = OSPF_RETRANSMIT_INTERVAL;
    new_intf->dr = 0;
    new_intf->bdr = 0;
    new_intf->area_id = area_id;
    new_intf->cost = cost;
    
    /* TODO: Get actual MTU from system */
    new_intf->mtu = 1500;
    
    area->interface_count++;
    
    LOG_INFO("Interface %u added to OSPF area %u.%u.%u.%u successfully", 
            interface_id,
            (area_id >> 24) & 0xFF, 
            (area_id >> 16) & 0xFF, 
            (area_id >> 8) & 0xFF, 
            area_id & 0xFF);
    
    return ERROR_OK;
}

/**
 * @brief Process an incoming OSPF packet
 *
 * Handles an OSPF packet received from the network, dispatching it to the
 * appropriate handler based on packet type.
 *
 * @param packet The packet containing the OSPF message
 * @param offset The offset into the packet where the OSPF header begins
 * @return ERROR_OK if successful, appropriate error code otherwise
 */
error_t ospf_process_packet(packet_t *packet, uint16_t offset)
{
    if (!g_ospf_config.active) {
        LOG_DEBUG("Dropping OSPF packet: protocol not active");
        return ERROR_NOT_ACTIVE;
    }
    
    if (packet == NULL || packet->data == NULL) {
        LOG_ERROR("Invalid packet pointer");
        return ERROR_INVALID_PARAM;
    }
    
    if (offset + OSPF_HEADER_LENGTH > packet->length) {
        LOG_ERROR("Packet too short for OSPF header");
        g_ospf_config.stats.malformed_packets++;
        return ERROR_PACKET_TOO_SMALL;
    }
    
    /* Extract the OSPF header */
    ospf_header_t *hdr = (ospf_header_t *)(packet->data + offset);
    
    /* Validate packet length */
    uint16_t packet_length = ntohs(hdr->packet_length);
    if (offset + packet_length > packet->length) {
        LOG_ERROR("OSPF packet length exceeds actual packet size");
        g_ospf_config.stats.malformed_packets++;
        return ERROR_PACKET_TOO_SMALL;
    }
    
    /* Verify OSPF version */
    if (hdr->version != OSPF_VERSION_2) {
        LOG_ERROR("Unsupported OSPF version: %u", hdr->version);
        g_ospf_config.stats.malformed_packets++;
        return ERROR_UNSUPPORTED_VERSION;
    }
    
    /* Calculate and verify checksum */
    uint16_t original_checksum = hdr->checksum;
    hdr->checksum = 0;
    uint16_t calculated_checksum = ospf_calculate_checksum(hdr, packet_length);
    hdr->checksum = original_checksum;
    
    if (original_checksum != calculated_checksum
