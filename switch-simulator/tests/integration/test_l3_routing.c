/**
 * @file test_l3_routing.c
 * @brief Integration tests for L3 routing functionality
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../../include/l3/routing_table.h"
#include "../../include/l3/ip.h"
#include "../../include/l3/arp.h"
#include "../../include/l2/mac_table.h"
#include "../../include/hal/packet.h"
#include "../../include/hal/port.h"
#include "../../include/common/error_codes.h"

#define TEST_PASSED "[ PASSED ] %s\n"
#define TEST_FAILED "[ FAILED ] %s: %s\n"

// Test fixture globals
route_table_t g_route_table;
arp_table_t g_arp_table;
port_info_t g_interfaces[4];

void setup_test_environment() {
    // Initialize routing table
    route_table_init(&g_route_table, 1024);
    
    // Initialize ARP table
    arp_table_init(&g_arp_table, 1024);
    
    // Setup interfaces
    // Interface 0: 192.168.1.1/24
    g_interfaces[0].port_id = 0;
    g_interfaces[0].state = PORT_STATE_UP;
    g_interfaces[0].has_ip = true;
    g_interfaces[0].ip_addr.addr[0] = 192;
    g_interfaces[0].ip_addr.addr[1] = 168;
    g_interfaces[0].ip_addr.addr[2] = 1;
    g_interfaces[0].ip_addr.addr[3] = 1;
    g_interfaces[0].subnet_mask.addr[0] = 255;
    g_interfaces[0].subnet_mask.addr[1] = 255;
    g_interfaces[0].subnet_mask.addr[2] = 255;
    g_interfaces[0].subnet_mask.addr[3] = 0;
    memcpy(g_interfaces[0].mac_addr, (uint8_t[]){0x00, 0x11, 0x22, 0x33, 0x44, 0x00}, 6);
    
    // Interface 1: 192.168.2.1/24
    g_interfaces[1].port_id = 1;
    g_interfaces[1].state = PORT_STATE_UP;
    g_interfaces[1].has_ip = true;
    g_interfaces[1].ip_addr.addr[0] = 192;
    g_interfaces[1].ip_addr.addr[1] = 168;
    g_interfaces[1].ip_addr.addr[2] = 2;
    g_interfaces[1].ip_addr.addr[3] = 1;
    g_interfaces[1].subnet_mask.addr[0] = 255;
    g_interfaces[1].subnet_mask.addr[1] = 255;
    g_interfaces[1].subnet_mask.addr[2] = 255;
    g_interfaces[1].subnet_mask.addr[3] = 0;
    memcpy(g_interfaces[1].mac_addr, (uint8_t[]){0x00, 0x11, 0x22, 0x33, 0x44, 0x01}, 6);
    
    // Interface 2: 10.0.0.1/24
    g_interfaces[2].port_id = 2;
    g_interfaces[2].state = PORT_STATE_UP;
    g_interfaces[2].has_ip = true;
    g_interfaces[2].ip_addr.addr[0] = 10;
    g_interfaces[2].ip_addr.addr[1] = 0;
    g_interfaces[2].ip_addr.addr[2] = 0;
    g_interfaces[2].ip_addr.addr[3] = 1;
    g_interfaces[2].subnet_mask.addr[0] = 255;
    g_interfaces[2].subnet_mask.addr[1] = 255;
    g_interfaces[2].subnet_mask.addr[2] = 255;
    g_interfaces[2].subnet_mask.addr[3] = 0;
    memcpy(g_interfaces[2].mac_addr, (uint8_t[]){0x00, 0x11, 0x22, 0x33, 0x44, 0x02}, 6);
    
    // Add routes
    // Direct routes for connected networks
    ip_addr_t network1 = {192, 168, 1, 0};
    ip_addr_t mask1 = {255, 255, 255, 0};
    ip_addr_t zero_nh = {0, 0, 0, 0};
    
    route_table_add(&g_route_table, &network1, &mask1, &zero_nh, 0, ROUTE_DIRECT);
    
    ip_addr_t network2 = {192, 168, 2, 0};
    ip_addr_t mask2 = {255, 255, 255, 0};
    
    route_table_add(&g_route_table, &network2, &mask2, &zero_nh, 1, ROUTE_DIRECT);
    
    ip_addr_t network3 = {10, 0, 0, 0};
    ip_addr_t mask3 = {255, 255, 255, 0};
    
    route_table_add(&g_route_table, &network3, &mask3, &zero_nh, 2, ROUTE_DIRECT);
    
    // Add a static route to 172.16.0.0/16 via 10.0.0.2
    ip_addr_t network4 = {172, 16, 0, 0};
    ip_addr_t mask4 = {255, 255, 0, 0};
    ip_addr_t next_hop4 = {10, 0, 0, 2};
    
    route_table_add(&g_route_table, &network4, &mask4, &next_hop4, 2, ROUTE_STATIC);
    
    // Add some ARP entries
    ip_addr_t neighbor1 = {192, 168, 1, 100};
    mac_address_t neighbor1_mac = {0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0x01};
    
    arp_table_add(&g_arp_table, &neighbor1, neighbor1_mac);
    
    ip_addr_t neighbor2 = {192, 168, 2, 100};
    mac_address_t neighbor2_mac = {0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0x02};
    
    arp_table_add(&g_arp_table, &neighbor2, neighbor2_mac);
    
    ip_addr_t neighbor3 = {10, 0, 0, 2};
    mac_address_t neighbor3_mac = {0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0x03};
    
    arp_table_add(&g_arp_table, &neighbor3, neighbor3_mac);
}

void teardown_test_environment() {
    route_table_destroy(&g_route_table);
    arp_table_destroy(&g_arp_table);
}

// Helper functions for packet creation and processing
void create_ipv4_packet(packet_t *packet, ip_addr_t *src_ip, ip_addr_t *dst_ip, 
                       mac_address_t src_mac, mac_address_t dst_mac, 
                       uint32_t ingress_port) {
    memset(packet, 0, sizeof(packet_t));
    
    packet->type = PACKET_TYPE_IPV4;
    packet->ingress_port = ingress_port;
    
    memcpy(packet->src_mac, src_mac, 6);
    memcpy(packet->dst_mac, dst_mac, 6);
    
    memcpy(&packet->ip_header.src_ip, src_ip, sizeof(ip_addr_t));
    memcpy(&packet->ip_header.dst_ip, dst_ip, sizeof(ip_addr_t));
    
    packet->ip_header.ttl = 64;
    packet->ip_header.protocol = 6; // TCP
    
    // Simple data payload
    strcpy((char*)packet->payload, "Test Payload");
    packet->payload_len = strlen("Test Payload");
}

uint32_t process_ipv4_packet(packet_t *packet, uint32_t *egress_port, mac_address_t *next_hop_mac) {
    // Check if packet is for us (one of our interfaces)
    bool is_for_us = false;
    for (int i = 0; i < 4; i++) {
        if (g_interfaces[i].has_ip && 
            ip_addr_compare(&packet->ip_header.dst_ip, &g_interfaces[i].ip_addr) == 0) {
            is_for_us = true;
            break;
        }
    }
    
    if (is_for_us) {
        // Packet destined for us - would be processed by upper layer
        return PACKET_FOR_LOCAL_DELIVERY;
    }
    
    // Packet needs to be routed
    
    // Decrement TTL
    if (packet->ip_header.ttl <= 1) {
        // TTL Expired
        return TTL_EXPIRED;
    }
    packet->ip_header.ttl--;
    
    // Lookup route
    route_entry_t route;
    uint32_t result = route_table_lookup(&g_route_table, &packet->ip_header.dst_ip, &route);
    
    if (result != SUCCESS) {
        // No route found
        return NO_ROUTE_TO_HOST;
    }
    
    *egress_port = route.outgoing_interface;
    
    ip_addr_t next_hop;
    
    if (ip_addr_is_zero(&route.next_hop)) {
        // Direct delivery - next hop is destination IP
        memcpy(&next_hop, &packet->ip_header.dst_ip, sizeof(ip_addr_t));
    } else {
        // Indirect - use configured next hop
        memcpy(&next_hop, &route.next_hop, sizeof(ip_addr_t));
    }
    
    // ARP lookup for next hop MAC
    result = arp_table_lookup(&g_arp_table, &next_hop, next_hop_mac);
    
    if (result != SUCCESS) {
        // No ARP entry - would trigger ARP request in real system
        return ARP_ENTRY_NOT_FOUND;
    }
    
    // Set source MAC to outgoing interface MAC
    memcpy(packet->src_mac, g_interfaces[*egress_port].mac_addr, 6);
    // Set destination MAC to next hop MAC
    memcpy(packet->dst_mac, *next_hop_mac, 6);
    
    return SUCCESS;
}

void test_direct_routing() {
    // Test packet from 192.168.1.100 to 192.168.2.100
    packet_t packet;
    ip_addr_t src_ip = {192, 168, 1, 100};
    ip_addr_t dst_ip = {192, 168, 2, 100};
    mac_address_t src_mac = {0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0x01};
    mac_address_t dst_mac = {0x00, 0x11, 0x22, 0x33, 0x44, 0x00}; // Interface 0 MAC
    
    create_ipv4_packet(&packet, &src_ip, &dst_ip, src_mac, dst_mac, 0);
    
    uint32_t egress_port;
    mac_address_t next_hop_mac;
    uint32_t result = process_ipv4_packet(&packet, &egress_port, &next_hop_mac);
    
    assert(result == SUCCESS);
    assert(egress_port == 1); // Should go out interface 1
    
    // Check packet was modified correctly
    assert(packet.ip_header.ttl == 63); // Decremented
    
    // Source MAC should be interface 1 MAC
    assert(memcmp(packet.src_mac, g_interfaces[1].mac_addr, 6) == 0);
    
    // Destination MAC should be neighbor2 MAC
    mac_address_t neighbor2_mac = {0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0x02};
    assert(memcmp(packet.dst_mac, neighbor2_mac, 6) == 0);
    
    printf(TEST_PASSED, "test_direct_routing");
}

void test_indirect_routing() {
    // Test packet from 192.168.1.100 to 172.16.1.1 (via static route)
    packet_t packet;
    ip_addr_t src_ip = {192, 168, 1, 100};
    ip_addr_t dst_ip = {172, 16, 1, 1};
    mac_address_t src_mac = {0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0x01};
    mac_address_t dst_mac = {0x00, 0x11, 0x22, 0x33, 0x44, 0x00}; // Interface 0 MAC
    
    create_ipv4_packet(&packet, &src_ip, &dst_ip, src_mac, dst_mac, 0);
    
    uint32_t egress_port;
    mac_address_t next_hop_mac;
    uint32_t result = process_ipv4_packet(&packet, &egress_port, &next_hop_mac);
    
    assert(result == SUCCESS);
    assert(egress_port == 2); // Should go out interface 2
    
    // Check packet was modified correctly
    assert(packet.ip_header.ttl == 63); // Decremented
    
    // Source MAC should be interface 2 MAC
    assert(memcmp(packet.src_mac, g_interfaces[2].mac_addr, 6) == 0);
    
    // Destination MAC should be neighbor3 MAC (10.0.0.2)
    mac_address_t neighbor3_mac = {0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0x03};
    assert(memcmp(packet.dst_mac, neighbor3_mac, 6) == 0);
    
    printf(TEST_PASSED, "test_indirect_routing");
}

void test_local_delivery() {
    // Test packet to one of our interfaces
    packet_t packet;
    ip_addr_t src_ip = {192, 168, 1, 100};
    ip_addr_t dst_ip = {192, 168, 1, 1}; // Interface 0 IP
    mac_address_t src_mac = {0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0x01};
    mac_address_t dst_mac = {0x00, 0x11, 0x22, 0x33, 0x44, 0x00}; // Interface 0 MAC
    
    create_ipv4_packet(&packet, &src_ip, &dst_ip, src_mac, dst_mac, 0);
    
    uint32_t egress_port;
    mac_address_t next_hop_mac;
    uint32_t result = process_ipv4_packet(&packet, &egress_port, &next_hop_mac);
    
    assert(result == PACKET_FOR_LOCAL_DELIVERY);
    
    printf(TEST_PASSED, "test_local_delivery");
}

void test_ttl_expiry() {
    // Test packet with TTL=1
    packet_t packet;
    ip_addr_t src_ip = {192, 168, 1, 100};
    ip_addr_t dst_ip = {172, 16, 1, 1};
    mac_address_t src_mac = {0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0x01};
    mac_address_t dst_mac = {0x00, 0x11, 0x22, 0x33, 0x44, 0x00};
    
    create_ipv4_packet(&packet, &src_ip, &dst_ip, src_mac, dst_mac, 0);
    packet.ip_header.ttl = 1; // Set TTL to 1
    
    uint32_t egress_port;
    mac_address_t next_hop_mac;
    uint32_t result = process_ipv4_packet(&packet, &egress_port, &next_hop_mac);
    
    assert(result == TTL_EXPIRED);
    
    printf(TEST_PASSED, "test_ttl_expiry");
}

void test_no_route() {
    // Test packet to an IP with no route
    packet_t packet;
    ip_addr_t src_ip = {192, 168, 1, 100};
    ip_addr_t dst_ip = {8, 8, 8, 8}; // No route for this
    mac_address_t src_mac = {0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0x01};
    mac_address_t dst_mac = {0x00, 0x11, 0x22, 0x33, 0x44, 0x00};
    
    create_ipv4_packet(&packet, &src_ip, &dst_ip, src_mac, dst_mac, 0);
    
    uint32_t egress_port;
    mac_address_t next_hop_mac;
    uint32_t result = process_ipv4_packet(&packet, &egress_port, &next_hop_mac);
    
    assert(result == NO_ROUTE_TO_HOST);
    
    printf(TEST_PASSED, "test_no_route");
}

int main() {
    printf("Running L3 Routing integration tests...\n");
    
    setup_test_environment();
    
    test_direct_routing();
    test_indirect_routing();
    test_local_delivery();
    test_ttl_expiry();
    test_no_route();
    
    teardown_test_environment();
    
    printf("All L3 Routing integration tests completed successfully.\n");
    return 0;
}
