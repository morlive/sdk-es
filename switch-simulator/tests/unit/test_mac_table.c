/**
 * @file test_mac_table.c
 * @brief Unit tests for MAC table functionality
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../../include/l2/mac_table.h"
#include "../../include/common/error_codes.h"

#define TEST_PASSED "[ PASSED ] %s\n"
#define TEST_FAILED "[ FAILED ] %s: %s\n"

void test_mac_table_init() {
    mac_table_t table;
    uint32_t result = mac_table_init(&table, 1024);
    assert(result == SUCCESS);
    assert(table.size == 0);
    assert(table.capacity == 1024);
    
    mac_table_destroy(&table);
    printf(TEST_PASSED, "test_mac_table_init");
}

void test_mac_entry_add() {
    mac_table_t table;
    mac_address_t mac = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint16_t vlan_id = 100;
    uint32_t port_id = 5;
    
    mac_table_init(&table, 1024);
    
    // Add new entry
    uint32_t result = mac_table_add(&table, mac, vlan_id, port_id, MAC_ENTRY_STATIC);
    assert(result == SUCCESS);
    assert(table.size == 1);
    
    // Add duplicate entry
    result = mac_table_add(&table, mac, vlan_id, port_id, MAC_ENTRY_STATIC);
    assert(result == MAC_ENTRY_EXISTS);
    assert(table.size == 1);
    
    mac_table_destroy(&table);
    printf(TEST_PASSED, "test_mac_entry_add");
}

void test_mac_entry_lookup() {
    mac_table_t table;
    mac_address_t mac = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint16_t vlan_id = 100;
    uint32_t port_id = 5;
    mac_entry_t entry;
    
    mac_table_init(&table, 1024);
    mac_table_add(&table, mac, vlan_id, port_id, MAC_ENTRY_STATIC);
    
    // Lookup existing entry
    uint32_t result = mac_table_lookup(&table, mac, vlan_id, &entry);
    assert(result == SUCCESS);
    assert(entry.port_id == port_id);
    assert(entry.entry_type == MAC_ENTRY_STATIC);
    
    // Lookup non-existing entry
    mac_address_t invalid_mac = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    result = mac_table_lookup(&table, invalid_mac, vlan_id, &entry);
    assert(result == MAC_ENTRY_NOT_FOUND);
    
    mac_table_destroy(&table);
    printf(TEST_PASSED, "test_mac_entry_lookup");
}

void test_mac_entry_delete() {
    mac_table_t table;
    mac_address_t mac = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint16_t vlan_id = 100;
    uint32_t port_id = 5;
    
    mac_table_init(&table, 1024);
    mac_table_add(&table, mac, vlan_id, port_id, MAC_ENTRY_STATIC);
    
    // Delete existing entry
    uint32_t result = mac_table_delete(&table, mac, vlan_id);
    assert(result == SUCCESS);
    assert(table.size == 0);
    
    // Delete non-existing entry
    result = mac_table_delete(&table, mac, vlan_id);
    assert(result == MAC_ENTRY_NOT_FOUND);
    
    mac_table_destroy(&table);
    printf(TEST_PASSED, "test_mac_entry_delete");
}

void test_mac_table_aging() {
    mac_table_t table;
    mac_address_t mac = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint16_t vlan_id = 100;
    uint32_t port_id = 5;
    
    mac_table_init(&table, 1024);
    mac_table_add(&table, mac, vlan_id, port_id, MAC_ENTRY_DYNAMIC);
    
    // Simulate aging
    mac_table_process_aging(&table, 300); // Pass 300 seconds
    
    // Entry should still exist
    mac_entry_t entry;
    uint32_t result = mac_table_lookup(&table, mac, vlan_id, &entry);
    assert(result == SUCCESS);
    
    // Now age out completely
    mac_table_process_aging(&table, 300); // Another 300 seconds (total > aging time)
    
    // Entry should be removed
    result = mac_table_lookup(&table, mac, vlan_id, &entry);
    assert(result == MAC_ENTRY_NOT_FOUND);
    
    mac_table_destroy(&table);
    printf(TEST_PASSED, "test_mac_table_aging");
}

int main() {
    printf("Running MAC Table unit tests...\n");
    
    test_mac_table_init();
    test_mac_entry_add();
    test_mac_entry_lookup();
    test_mac_entry_delete();
    test_mac_table_aging();
    
    printf("All MAC Table tests completed successfully.\n");
    return 0;
}
