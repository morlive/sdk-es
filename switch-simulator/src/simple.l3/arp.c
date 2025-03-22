#include "include/l3/ip.h"
#include "include/common/logging.h"
#include "include/common/types.h"
#include "include/common/error_codes.h"

// ARP table structure
typedef struct {
    ip_addr_t ip_addr;
    mac_addr_t mac_addr;
    uint32_t timestamp;
    bool is_static;
} arp_entry_t;

#define ARP_TABLE_SIZE 1024
static arp_entry_t arp_table[ARP_TABLE_SIZE];
static uint32_t arp_entry_count = 0;

// Initialize ARP module
error_code_t arp_init(void) {
    LOG_INFO("Initializing ARP module");
    arp_entry_count = 0;
    memset(arp_table, 0, sizeof(arp_table));
    return ERROR_SUCCESS;
}

// Add an entry to the ARP table
error_code_t arp_add_entry(ip_addr_t ip_addr, mac_addr_t mac_addr, bool is_static) {
    if (arp_entry_count >= ARP_TABLE_SIZE) {
        LOG_ERROR("ARP table is full");
        return ERROR_RESOURCE_FULL;
    }
    
    // Check if entry already exists
    for (uint32_t i = 0; i < arp_entry_count; i++) {
        if (memcmp(&arp_table[i].ip_addr, &ip_addr, sizeof(ip_addr_t)) == 0) {
            // Update existing entry
            memcpy(&arp_table[i].mac_addr, &mac_addr, sizeof(mac_addr_t));
            arp_table[i].timestamp = get_current_time();
            arp_table[i].is_static = is_static;
            LOG_DEBUG("Updated ARP entry for IP %s", ip_to_str(ip_addr));
            return ERROR_SUCCESS;
        }
    }
    
    // Add new entry
    memcpy(&arp_table[arp_entry_count].ip_addr, &ip_addr, sizeof(ip_addr_t));
    memcpy(&arp_table[arp_entry_count].mac_addr, &mac_addr, sizeof(mac_addr_t));
    arp_table[arp_entry_count].timestamp = get_current_time();
    arp_table[arp_entry_count].is_static = is_static;
    arp_entry_count++;
    
    LOG_DEBUG("Added new ARP entry for IP %s", ip_to_str(ip_addr));
    return ERROR_SUCCESS;
}

// Lookup an entry in the ARP table
error_code_t arp_lookup(ip_addr_t ip_addr, mac_addr_t *mac_addr) {
    for (uint32_t i = 0; i < arp_entry_count; i++) {
        if (memcmp(&arp_table[i].ip_addr, &ip_addr, sizeof(ip_addr_t)) == 0) {
            memcpy(mac_addr, &arp_table[i].mac_addr, sizeof(mac_addr_t));
            return ERROR_SUCCESS;
        }
    }
    
    LOG_DEBUG("ARP entry not found for IP %s", ip_to_str(ip_addr));
    return ERROR_NOT_FOUND;
}

// Process incoming ARP packet
error_code_t arp_process_packet(packet_t *packet) {
    // Extract ARP header
    arp_header_t *arp_header = (arp_header_t *)packet->data;
    
    // Process based on ARP operation
    switch (ntohs(arp_header->operation)) {
        case ARP_REQUEST:
            return arp_handle_request(packet);
        
        case ARP_REPLY:
            return arp_handle_reply(packet);
            
        default:
            LOG_WARNING("Unknown ARP operation: %d", ntohs(arp_header->operation));
            return ERROR_INVALID_PARAMETER;
    }
}

// Age out old entries
void arp_aging_task(void) {
    uint32_t current_time = get_current_time();
    
    for (uint32_t i = 0; i < arp_entry_count; i++) {
        if (!arp_table[i].is_static && 
            (current_time - arp_table[i].timestamp) > ARP_AGING_TIMEOUT) {
            // Remove this entry by copying the last entry to this position
            if (i < arp_entry_count - 1) {
                memcpy(&arp_table[i], &arp_table[arp_entry_count - 1], sizeof(arp_entry_t));
            }
            arp_entry_count--;
            i--; // Recheck this position
        }
    }
}
