/**
 * @file test_routing.c
 * @brief Unit tests for routing table functionality
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../../include/l3/routing_table.h"
#include "../../include/l3/ip.h"
#include "../../include/common/error_codes.h"

#define TEST_PASSED "[ PASSED ] %s\n"
#define TEST_FAILED "[ FAILED ] %s: %s\n"

void test_route_table_init() {
    route_table_t table;
    uint32_t result = route_table_init(&table, 1024);
    assert(result == SUCCESS);
    assert(table.size == 0);
    assert(table.capacity == 1024);
    
    route_table_destroy(&table);
    printf(TEST_PASSED, "test_route_table_init");
}

void test_route_add() {
    route_table_t table;
    ip_addr_t dest_network = {192, 168, 1, 0};
    ip_addr_t subnet_mask = {255, 255, 255, 0};
    ip_addr_t next_hop = {192, 168, 2, 1};
    uint32_t outgoing_interface = 2;
    
    route_table_init(&table, 1024);
    
    // Add new route
    uint32_t result = route_table_add(&table, &dest_network, &subnet_mask, &next_hop, outgoing_interface, ROUTE_STATIC);
    assert(result == SUCCESS);
    assert(table.size == 1);
    
    // Add duplicate route
    result = route_table_add(&table, &dest_network, &subnet_mask, &next_hop, outgoing_interface, ROUTE_STATIC);
    assert(result == ROUTE_EXISTS);
    assert(table.size == 1);
    
    route_table_destroy(&table);
    printf(TEST_PASSED, "test_route_add");
}

void test_route_lookup() {
    route_table_t table;
    ip_addr_t dest_network = {192, 168, 1, 0};
    ip_addr_t subnet_mask = {255, 255, 255, 0};
    ip_addr_t next_hop = {192, 168, 2, 1};
    uint32_t outgoing_interface = 2;
    
    ip_addr_t dest_ip = {192, 168, 1, 100}; // IP in the destination network
    route_entry_t route;
    
    route_table_init(&table, 1024);
    route_table_add(&table, &dest_network, &subnet_mask, &next_hop, outgoing_interface, ROUTE_STATIC);
    
    // Lookup existing route
    uint32_t result = route_table_lookup(&table, &dest_ip, &route);
    assert(result == SUCCESS);
    assert(route.outgoing_interface == outgoing_interface);
    assert(ip_addr_compare(&route.next_hop, &next_hop) == 0);
    
    // Lookup non-existing route
    ip_addr_t invalid_ip = {10, 0, 0, 1};
    result = route_table_lookup(&table, &invalid_ip, &route);
    assert(result == ROUTE_NOT_FOUND);
    
    route_table_destroy(&table);
    printf(TEST_PASSED, "test_route_lookup");
}

void test_route_delete() {
    route_table_t table;
    ip_addr_t dest_network = {192, 168, 1, 0};
    ip_addr_t subnet_mask = {255, 255, 255, 0};
    ip_addr_t next_hop = {192, 168, 2, 1};
    uint32_t outgoing_interface = 2;
    
    route_table_init(&table, 1024);
    route_table_add(&table, &dest_network, &subnet_mask, &next_hop, outgoing_interface, ROUTE_STATIC);
    
    // Delete existing route
    uint32_t result = route_table_delete(&table, &dest_network, &subnet_mask);
    assert(result == SUCCESS);
    assert(table.size == 0);
    
    // Delete non-existing route
    result = route_table_delete(&table, &dest_network, &subnet_mask);
    assert(result == ROUTE_NOT_FOUND);
    
    route_table_destroy(&table);
    printf(TEST_PASSED, "test_route_delete");
}

void test_longest_prefix_match() {
    route_table_t table;
    route_entry_t route;
    
    // Setup several routes with different prefix lengths
    ip_addr_t network1 = {192, 168, 0, 0};
    ip_addr_t mask1 = {255, 255, 0, 0};      // /16
    ip_addr_t nexthop1 = {10, 0, 0, 1};
    
    ip_addr_t network2 = {192, 168, 1, 0};
    ip_addr_t mask2 = {255, 255, 255, 0};    // /24
    ip_addr_t nexthop2 = {10, 0, 0, 2};
    
    ip_addr_t network3 = {192, 168, 1, 128};
    ip_addr_t mask3 = {255, 255, 255, 128};  // /25
    ip_addr_t nexthop3 = {10, 0, 0, 3};
    
    route_table_init(&table, 1024);
    route_table_add(&table, &network1, &mask1, &nexthop1, 1, ROUTE_STATIC);
    route_table_add(&table, &network2, &mask2, &nexthop2, 2, ROUTE_STATIC);
    route_table_add(&table, &network3, &mask3, &nexthop3, 3, ROUTE_STATIC);
    
    // Test IP in 192.168.1.130 - should match the /25 route
    ip_addr_t test_ip1 = {192, 168, 1, 130};
    uint32_t result = route_table_lookup(&table, &test_ip1, &route);
    assert(result == SUCCESS);
    assert(ip_addr_compare(&route.next_hop, &nexthop3) == 0);
    
    // Test IP in 192.168.1.10 - should match the /24 route
    ip_addr_t test_ip2 = {192, 168, 1, 10};
    result = route_table_lookup(&table, &test_ip2, &route);
    assert(result == SUCCESS);
    assert(ip_addr_compare(&route.next_hop, &nexthop2) == 0);
    
    // Test IP in 192.168.2.1 - should match the /16 route
    ip_addr_t test_ip3 = {192, 168, 2, 1};
    result = route_table_lookup(&table, &test_ip3, &route);
    assert(result == SUCCESS);
    assert(ip_addr_compare(&route.next_hop, &nexthop1) == 0);
    
    route_table_destroy(&table);
    printf(TEST_PASSED, "test_longest_prefix_match");
}

int main() {
    printf("Running Routing Table unit tests...\n");
    
    test_route_table_init();
    test_route_add();
    test_route_lookup();
    test_route_delete();
    test_longest_prefix_match();
    
    printf("All Routing Table tests completed successfully.\n");
    return 0;
}
