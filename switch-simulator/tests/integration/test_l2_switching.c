/**
 * @file test_l2_switching.c
 * @brief Integration tests for L2 switching functionality
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../../include/l2/mac_table.h"
#include "../../include/l2/vlan.h"
#include "../../include/hal/packet.h"
#include "../../include/hal/port.h"
#include "../../include/common/error_codes.h"

#define TEST_PASSED "[ PASSED ] %s\n"
#define TEST_FAILED "[ FAILED ] %s: %s\n"

// Test fixture globals
mac_table_t g_mac_table;
vlan_table_t g_vlan_table;
port_info_t g_ports[8];

void setup_test_environment() {
    // Initialize the MAC table
    mac_table_init(&g_mac_table, 1024);
    
    // Initialize the VLAN table
    vlan_table_init(&g_vlan_table, 4096);
    
    // Create VLANs
    vlan_create(&g_vlan_table, 1, "default");
    vlan_create(&g_vlan_table, 10, "data");
    vlan_create(&g_vlan_table, 20, "voice");
    
    // Setup ports
    for (int i = 0; i < 8; i++) {
        g_ports[i].port_id = i;
        g_ports[i].state = PORT_STATE_UP;
        g_ports[i].speed = PORT_SPEED_1G;
        g_ports[i].duplex = PORT_DUPLEX_FULL;
    }
    
    // Configure VLAN port memberships
    // Ports 0, 1 are in VLAN 10 as untagged
    vlan_add_port(&g_vlan_table, 10, 0, VLAN_PORT_UNTAGGED);
    vlan_add_port(&g_vlan_table, 10, 1, VLAN_PORT_UNTAGGED);
    
    // Ports 2, 3 are in VLAN 20 as untagged
    vlan_add_port(&g_vlan_table, 20, 2, VLAN_PORT_UNTAGGED);
    vlan_add_port(&g_vlan_table, 20, 3, VLAN_PORT_UNTAGGED);
    
    // Port 4 is a trunk port for both VLANs
    vlan_add_port(&g_vlan_table, 10, 4, VLAN_PORT_TAGGED);
    vlan_add_port(&g_vlan_table, 20, 4, VLAN_PORT_TAGGED);
}

void teardown_test_environment() {
    mac_table_destroy(&g_mac_table);
    vlan_table_destroy(&g_vlan_table);
}

// Helper function to create a test packet
void create_test_packet(packet_t *packet, uint8_t *src_mac, uint8_t *dst_mac, uint16_t vlan_id, uint32_t ingress_port) {
    memset(packet, 0, sizeof(packet_t));
    
    memcpy(packet->src_mac, src_mac, 6);
    memcpy(packet->dst_mac, dst_mac, 6);
    packet->vlan_id = vlan_id;
    packet->ingress_port = ingress_port;
    
    if (vlan_id != 0) {
        packet->has_vlan_tag = true;
    }
}

// Helper function to process L2 packet and get egress port
uint32_t process_l2_packet(packet_t *packet, uint32_t *egress_port) {
    // VLAN classification (if no VLAN tag)
    if (!packet->has_vlan_tag) {
        uint32_t result = vlan_classify_packet(&g_vlan_table, packet);
        if (result != SUCCESS) {
            return result;
        }
    }
    
    // MAC learning
    mac_address_t src_mac;
    memcpy(src_mac, packet->src_mac, 6);
    mac_table_add(&g_mac_table, src_mac, packet->vlan_id, packet->ingress_port, MAC_ENTRY_DYNAMIC);
    
    // MAC lookup for forwarding decision
    mac_address_t dst_mac;
    memcpy(dst_mac, packet->dst_mac, 6);
    
    // Check if it's a broadcast
    bool is_broadcast = true;
    for (int i = 0; i < 6; i++) {
        if (dst_mac[i] != 0xFF) {
            is_broadcast = false;
            break;
        }
    }
    
    if (is_broadcast) {
        // For simplicity, we just return a flood indication
        *egress_port = PORT_FLOOD;
        return SUCCESS;
    }
    
    // Unicast lookup
    mac_entry_t entry;
    uint32_t result = mac_table_lookup(&g_mac_table, dst_mac, packet->vlan_id, &entry);
    
    if (result == SUCCESS) {
        *egress_port = entry.port_id;
        return SUCCESS;
    } else {
        // Unknown unicast, flood in VLAN
        *egress_port = PORT_FLOOD;
        return SUCCESS;
    }
}

void test_mac_learning_and_forwarding() {
    uint8_t mac1[6] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t mac2[6] = {0x00, 0x06, 0x07, 0x08, 0x09, 0x0A};
    
    // Create a packet from MAC1 to MAC2 on port 0 (VLAN 10)
    packet_t packet;
    create_test_packet(&packet, mac1, mac2, 0, 0);
    
    // Process the packet - should learn MAC1 and flood (MAC2 unknown)
    uint32_t egress_port;
    uint32_t result = process_l2_packet(&packet, &egress_port);
    
    assert(result == SUCCESS);
    assert(egress_port == PORT_FLOOD);
    
    // Check if MAC1 was learned correctly
    mac_address_t src_mac;
    memcpy(src_mac, mac1, 6);
    mac_entry_t entry;
    result = mac_table_lookup(&g_mac_table, src_mac, 10, &entry); // VLAN 10 assigned during classification
    
    assert(result == SUCCESS);
    assert(entry.port_id == 0);
    
    // Now create a packet from MAC2 to MAC1 on port 1 (same VLAN)
    create_test_packet(&packet, mac2, mac1, 0, 1);
    
    // Process this packet - should learn MAC2 and forward to port 0 (MAC1 known)
    result = process_l2_packet(&packet, &egress_port);
    
    assert(result == SUCCESS);
    assert(egress_port == 0);
    
    // Check if MAC2 was learned correctly
    memcpy(src_mac, mac2, 6);
    result = mac_table_lookup(&g_mac_table, src_mac, 10, &entry);
    
    assert(result == SUCCESS);
    assert(entry.port_id == 1);
    
    printf(TEST_PASSED, "test_mac_learning_and_forwarding");
}

void test_vlan_isolation() {
    uint8_t mac1[6] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05}; // In VLAN 10
    uint8_t mac2[6] = {0x00, 0x06, 0x07, 0x08, 0x09, 0x0A}; // In VLAN 10
    uint8_t mac3[6] = {0x00, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F}; // In VLAN 20
    
    // Learn MAC addresses in their respective VLANs
    mac_table_add(&g_mac_table, mac1, 10, 0, MAC_ENTRY_DYNAMIC);
    mac_table_add(&g_mac_table, mac2, 10, 1, MAC_ENTRY_DYNAMIC);
    mac_table_add(&g_mac_table, mac3, 20, 2, MAC_ENTRY_DYNAMIC);
    
    // Test packet from VLAN 10 to VLAN 10
    packet_t packet;
    create_test_packet(&packet, mac1, mac2, 10, 0);
    
    uint32_t egress_port;
    uint32_t result = process_l2_packet(&packet, &egress_port);
    
    assert(result == SUCCESS);
    assert(egress_port == 1); // Should forward to port 1
    
    // Test packet from VLAN 10 to VLAN 20
    create_test_packet(&packet, mac1, mac3, 10, 0);
    
    result = process_l2_packet(&packet, &egress_port);
    
    assert(result == SUCCESS);
    assert(egress_port == PORT_FLOOD); // MAC3 not found in VLAN 10
    
    // Test packet in VLAN 20
    create_test_packet(&packet, mac3, mac1, 20, 2);
    
    result = process_l2_packet(&packet, &egress_port);
    
    assert(result == SUCCESS);
    assert(egress_port == PORT_FLOOD); // MAC1 not found in VLAN 20
    
    printf(TEST_PASSED, "test_vlan_isolation");
}

void test_trunk_port_forwarding() {
    uint8_t mac1[6] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05}; // Port 0, VLAN 10
    uint8_t mac2[6] = {0x00, 0x06, 0x07, 0x08, 0x09, 0x0A}; // Port 4 (trunk)
    
    // Learn MAC addresses
    mac_table_add(&g_mac_table, mac1, 10, 0, MAC_ENTRY_DYNAMIC);
    mac_table_add(&g_mac_table, mac2, 10, 4, MAC_ENTRY_DYNAMIC);
    
    // Test packet from access port to trunk port
    packet_t packet;
    create_test_packet(&packet, mac1, mac2, 0, 0); // No VLAN tag on ingress from access port
    
    uint32_t egress_port;
    uint32_t result = process_l2_packet(&packet, &egress_port);
    
    assert(result == SUCCESS);
    assert(egress_port == 4); // Should forward to trunk port
    
    // VLAN tag handling on egress
    result = vlan_process_egress(&g_vlan_table, &packet, egress_port);
    assert(result == SUCCESS);
    assert(packet.has_vlan_tag == true); // Should add tag when going to trunk
    
    // Test packet from trunk port to access port
    create_test_packet(&packet, mac2, mac1, 10, 4); // With VLAN tag on ingress from trunk
    
    result = process_l2_packet(&packet, &egress_port);
    
    assert(result == SUCCESS);
    assert(egress_port == 0); // Should forward to access port
    
    // VLAN tag handling on egress
    result = vlan_process_egress(&g_vlan_table, &packet, egress_port);
    assert(result == SUCCESS);
    assert(packet.has_vlan_tag == false); // Should remove tag when going to access port
    
    printf(TEST_PASSED, "test_trunk_port_forwarding");
}

void test_broadcast_handling() {
    uint8_t src_mac[6] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t bcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    // Create a broadcast packet in VLAN 10
    packet_t packet;
    create_test_packet(&packet, src_mac, bcast_mac, 0, 0); // From port 0 (VLAN 10)
    
    uint32_t egress_port;
    uint32_t result = process_l2_packet(&packet, &egress_port);
    
    assert(result == SUCCESS);
    assert(egress_port == PORT_FLOOD); // Should flood in VLAN
    
    // Verify the packet would be sent to correct ports within VLAN 10
    uint32_t eligible_ports[8] = {0};
    result = vlan_get_flood_ports(&g_vlan_table, packet.vlan_id, packet.ingress_port, eligible_ports, 8);
    
    assert(result == SUCCESS);
    assert(eligible_ports[0] == 0); // Source port, should be 0 (don't send back)
    assert(eligible_ports[1] == 1); // Port 1 in VLAN 10, should receive
    assert(eligible_ports[2] == 0); // Port 2 not in VLAN 10, should not receive
    assert(eligible_ports[3] == 0); // Port 3 not in VLAN 10, should not receive
    assert(eligible_ports[4] == 1); // Port 4 (trunk) in VLAN 10, should receive
    
    printf(TEST_PASSED, "test_broadcast_handling");
}

int main() {
    printf("Running L2 Switching integration tests...\n");
    
    setup_test_environment();
    
    test_mac_learning_and_forwarding();
    test_vlan_isolation();
    test_trunk_port_forwarding();
    test_broadcast_handling();
    
    teardown_test_environment();
    
    printf("All L2 Switching integration tests completed successfully.\n");
    return 0;
}

















































