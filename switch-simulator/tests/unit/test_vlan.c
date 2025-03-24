/**
 * @file test_vlan.c
 * @brief Unit tests for VLAN functionality
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../../include/l2/vlan.h"
#include "../../include/common/error_codes.h"

#define TEST_PASSED "[ PASSED ] %s\n"
#define TEST_FAILED "[ FAILED ] %s: %s\n"

void test_vlan_create() {
    vlan_table_t vlan_table;
    uint32_t result = vlan_table_init(&vlan_table, 4096);
    assert(result == SUCCESS);
    
    // Create a new VLAN
    uint16_t vlan_id = 100;
    char vlan_name[] = "test_vlan";
    result = vlan_create(&vlan_table, vlan_id, vlan_name);
    assert(result == SUCCESS);
    
    // Try to create an existing VLAN
    result = vlan_create(&vlan_table, vlan_id, "another_name");
    assert(result == VLAN_EXISTS);
    
    // Try to create an invalid VLAN ID
    result = vlan_create(&vlan_table, 4097, "invalid_vlan");
    assert(result == INVALID_VLAN_ID);
    
    vlan_table_destroy(&vlan_table);
    printf(TEST_PASSED, "test_vlan_create");
}

void test_vlan_delete() {
    vlan_table_t vlan_table;
    vlan_table_init(&vlan_table, 4096);
    
    // Create a VLAN
    uint16_t vlan_id = 100;
    vlan_create(&vlan_table, vlan_id, "test_vlan");
    
    // Delete existing VLAN
    uint32_t result = vlan_delete(&vlan_table, vlan_id);
    assert(result == SUCCESS);
    
    // Try to delete non-existing VLAN
    result = vlan_delete(&vlan_table, vlan_id);
    assert(result == VLAN_NOT_FOUND);
    
    // Try to delete default VLAN
    result = vlan_delete(&vlan_table, 1);
    assert(result == VLAN_OPERATION_NOT_PERMITTED);
    
    vlan_table_destroy(&vlan_table);
    printf(TEST_PASSED, "test_vlan_delete");
}

void test_port_vlan_membership() {
    vlan_table_t vlan_table;
    vlan_table_init(&vlan_table, 4096);
    
    uint16_t vlan_id = 100;
    uint32_t port_id = 5;
    
    vlan_create(&vlan_table, vlan_id, "test_vlan");
    
    // Add port to VLAN
    uint32_t result = vlan_add_port(&vlan_table, vlan_id, port_id, VLAN_PORT_UNTAGGED);
    assert(result == SUCCESS);
    
    // Verify port membership
    vlan_port_info_t port_info;
    result = vlan_get_port_info(&vlan_table, vlan_id, port_id, &port_info);
    assert(result == SUCCESS);
    assert(port_info.membership == VLAN_PORT_UNTAGGED);
    
    // Change port membership
    result = vlan_add_port(&vlan_table, vlan_id, port_id, VLAN_PORT_TAGGED);
    assert(result == SUCCESS);
    
    // Verify updated port membership
    result = vlan_get_port_info(&vlan_table, vlan_id, port_id, &port_info);
    assert(result == SUCCESS);
    assert(port_info.membership == VLAN_PORT_TAGGED);
    
    // Remove port from VLAN
    result = vlan_remove_port(&vlan_table, vlan_id, port_id);
    assert(result == SUCCESS);
    
    // Verify port is removed
    result = vlan_get_port_info(&vlan_table, vlan_id, port_id, &port_info);
    assert(result == VLAN_PORT_NOT_MEMBER);
    
    vlan_table_destroy(&vlan_table);
    printf(TEST_PASSED, "test_port_vlan_membership");
}

void test_vlan_port_operations() {
    vlan_table_t vlan_table;
    vlan_table_init(&vlan_table, 4096);
    
    // Create multiple VLANs
    vlan_create(&vlan_table, 100, "vlan_100");
    vlan_create(&vlan_table, 200, "vlan_200");
    vlan_create(&vlan_table, 300, "vlan_300");
    
    uint32_t port_id = 5;
    
    // Add port to multiple VLANs with different memberships
    uint32_t result;
    result = vlan_add_port(&vlan_table, 100, port_id, VLAN_PORT_UNTAGGED);
    assert(result == SUCCESS);
    
    result = vlan_add_port(&vlan_table, 200, port_id, VLAN_PORT_TAGGED);
    assert(result == SUCCESS);
    
    result = vlan_add_port(&vlan_table, 300, port_id, VLAN_PORT_TAGGED);
    assert(result == SUCCESS);
    
    // Get all VLANs for a port
    vlan_port_membership_t memberships[10];
    uint32_t count = 0;
    result = vlan_get_port_memberships(&vlan_table, port_id, memberships, 10, &count);
    assert(result == SUCCESS);
    assert(count == 3);
    
    // Verify memberships
    bool found_vlan100 = false;
    bool found_vlan200 = false;
    bool found_vlan300 = false;
    
    for (int i = 0; i < count; i++) {
        if (memberships[i].vlan_id == 100) {
            assert(memberships[i].membership == VLAN_PORT_UNTAGGED);
            found_vlan100 = true;
        } else if (memberships[i].vlan_id == 200) {
            assert(memberships[i].membership == VLAN_PORT_TAGGED);
            found_vlan200 = true;
        } else if (memberships[i].vlan_id == 300) {
            assert(memberships[i].membership == VLAN_PORT_TAGGED);
            found_vlan300 = true;
        }
    }
    
    assert(found_vlan100 && found_vlan200 && found_vlan300);
    
    vlan_table_destroy(&vlan_table);
    printf(TEST_PASSED, "test_vlan_port_operations");
}

void test_vlan_packet_processing() {
    vlan_table_t vlan_table;
    vlan_table_init(&vlan_table, 4096);
    
    // Setup VLAN configuration
    vlan_create(&vlan_table, 100, "data_vlan");
    vlan_create(&vlan_table, 200, "voice_vlan");
    
    // Port 1: Access port in VLAN 100
    vlan_add_port(&vlan_table, 100, 1, VLAN_PORT_UNTAGGED);
    
    // Port 2: Trunk port allowing VLAN 100, 200
    vlan_add_port(&vlan_table, 100, 2, VLAN_PORT_TAGGED);
    vlan_add_port(&vlan_table, 200, 2, VLAN_PORT_TAGGED);
    
    // Port 3: Access port in VLAN 200
    vlan_add_port(&vlan_table, 200, 3, VLAN_PORT_UNTAGGED);
    
    // Test packet classification
    packet_t untagged_packet;
    memset(&untagged_packet, 0, sizeof(packet_t));
    untagged_packet.ingress_port = 1;
    
    uint32_t result = vlan_classify_packet(&vlan_table, &untagged_packet);
    assert(result == SUCCESS);
    assert(untagged_packet.vlan_id == 100);
    
    // Test packet with VLAN tag
    packet_t tagged_packet;
    memset(&tagged_packet, 0, sizeof(packet_t));
    tagged_packet.ingress_port = 2;
    tagged_packet.has_vlan_tag = true;
    tagged_packet.vlan_id = 200;
    
    result = vlan_process_packet(&vlan_table, &tagged_packet);
    assert(result == SUCCESS);
    
    // Test egress VLAN handling
    result = vlan_process_egress(&vlan_table, &untagged_packet, 2);
    assert(result == SUCCESS);
    assert(untagged_packet.has_vlan_tag == true); // Should be tagged on trunk port
    
    result = vlan_process_egress(&vlan_table, &tagged_packet, 3);
    assert(result == SUCCESS);
    assert(tagged_packet.has_vlan_tag == false); // Should be untagged on access port
    
    vlan_table_destroy(&vlan_table);
    printf(TEST_PASSED, "test_vlan_packet_processing");
}

int main() {
    printf("Running VLAN unit tests...\n");
    
    test_vlan_create();
    test_vlan_delete();
    test_port_vlan_membership();
    test_vlan_port_operations();
    test_vlan_packet_processing();
    
    printf("All VLAN tests completed successfully.\n");
    return 0;
}
