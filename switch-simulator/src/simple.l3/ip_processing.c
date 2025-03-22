#include "include/l3/ip.h"
#include "include/l3/routing_table.h"
#include "include/common/logging.h"
#include "include/common/error_codes.h"
#include "include/hal/packet.h"

// Initialize IP processing module
error_code_t ip_init(void) {
    LOG_INFO("Initializing IP processing module");
    return ERROR_SUCCESS;
}

// Process incoming IP packet
error_code_t ip_process_packet(packet_t *packet) {
    // Validate packet
    if (!packet || packet->length < sizeof(ip_header_t)) {
        LOG_ERROR("Invalid packet pointer or insufficient packet length");
        return ERROR_INVALID_PARAMETER;
    }
    
    // Extract IP header
    ip_header_t *ip_header = (ip_header_t *)packet->data;
    
    // Basic header validation
    if (ip_header->version != 4) {
        LOG_WARNING("Unsupported IP version: %d", ip_header->version);
        return ERROR_UNSUPPORTED;
    }
    
    // Verify header checksum
    uint16_t original_checksum = ip_header->checksum;
    ip_header->checksum = 0;
    uint16_t calculated_checksum = calculate_ip_checksum(ip_header, ip_header->ihl * 4);
    ip_header->checksum = original_checksum;
    
    if (original_checksum != calculated_checksum) {
        LOG_WARNING("IP header checksum mismatch: expected 0x%04x, calculated 0x%04x", 
                   original_checksum, calculated_checksum);
        return ERROR_CHECKSUM_FAILURE;
    }
    
    // Check if packet is destined for this device
    if (is_own_ip_address(ip_header->dst_addr)) {
        return ip_process_local_packet(packet);
    } else {
        return ip_forward_packet(packet);
    }
}

// Process a packet destined for this device
static error_code_t ip_process_local_packet(packet_t *packet) {
    ip_header_t *ip_header = (ip_header_t *)packet->data;
    
    // Process based on protocol
    switch (ip_header->protocol) {
        case IP_PROTOCOL_ICMP:
            return icmp_process_packet(packet);
            
        case IP_PROTOCOL_TCP:
        case IP_PROTOCOL_UDP:
            return ip_deliver_to_upper_layer(packet);
            
        default:
            LOG_WARNING("Unsupported IP protocol: %d", ip_header->protocol);
            return ERROR_UNSUPPORTED;
    }
}

// Forward a packet to another destination
static error_code_t ip_forward_packet(packet_t *packet) {
    ip_header_t *ip_header = (ip_header_t *)packet->data;
    
    // Check TTL
    if (ip_header->ttl <= 1) {
        // TTL expired, send ICMP Time Exceeded message
        LOG_DEBUG("TTL expired for packet to %s", ip_to_str(ip_header->dst_addr));
        return icmp_send_time_exceeded(packet);
    }
    
    // Decrement TTL
    ip_header->ttl--;
    
    // Recalculate checksum
    ip_header->checksum = 0;
    ip_header->checksum = calculate_ip_checksum(ip_header, ip_header->ihl * 4);
    
    // Lookup next hop in routing table
    ip_addr_t next_hop;
    uint32_t output_interface;
    error_code_t result = routing_table_lookup(ip_header->dst_addr, &next_hop, &output_interface);
    
    if (result != ERROR_SUCCESS) {
        // No route found, send ICMP Destination Unreachable
        LOG_DEBUG("No route to %s", ip_to_str(ip_header->dst_addr));
        return icmp_send_dest_unreachable(packet, ICMP_DEST_NETWORK_UNREACHABLE);
    }
    
    // Lookup MAC address of next hop
    mac_addr_t next_hop_mac;
    result = arp_lookup(next_hop, &next_hop_mac);
    
    if (result != ERROR_SUCCESS) {
        // No ARP entry found, queue packet and send ARP request
        LOG_DEBUG("No ARP entry for %s, queuing packet", ip_to_str(next_hop));
        ip_queue_packet(packet, next_hop, output_interface);
        return arp_send_request(next_hop, output_interface);
    }
    
    // Update Ethernet header
    ethernet_header_t *eth_header = (ethernet_header_t *)(packet->data - sizeof(ethernet_header_t));
    memcpy(eth_header->dst_mac, next_hop_mac, sizeof(mac_addr_t));
    memcpy(eth_header->src_mac, get_interface_mac(output_interface), sizeof(mac_addr_t));
    
    // Send packet out of the interface
    return hal_send_packet(packet, output_interface);
}
