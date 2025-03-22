/**
 * @file ip_processing.c
 * @brief Implementation of IP packet processing functionality
 *
 * This file implements the processing of IP packets in the switching/routing system.
 * It handles IPv4 and IPv6 packet processing, header validation, fragmentation,
 * time-to-live management, and forwarding decision logic.
 *
 * @copyright (c) 2025 Switch Simulator Project
 * @license Proprietary
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <arpa/inet.h>  /* For network byte order conversions */

#include "common/error_codes.h"
#include "common/logging.h"
#include "common/types.h"
#include "hal/packet.h"
#include "hal/port.h"
#include "l3/ip.h"
#include "l3/routing_table.h"
#include "l3/arp.h"
#include "management/stats.h"

/* Constants for IP processing */
#define IP_VERSION_4             4
#define IP_VERSION_6             6
#define IPV4_HEADER_MIN_LEN      20      /* Minimum IPv4 header length in bytes */
#define IPV4_HEADER_MAX_LEN      60      /* Maximum IPv4 header length in bytes */
#define IPV6_HEADER_LEN          40      /* Fixed IPv6 header length in bytes */
#define IP_FLAG_DF               0x4000  /* Don't Fragment flag */
#define IP_FLAG_MF               0x2000  /* More Fragments flag */
#define IP_FRAG_OFFSET_MASK      0x1FFF  /* Fragment offset mask */
#define IP_FRAGMENT_UNIT         8       /* Fragment offset unit size in bytes */
#define TTL_DEFAULT              64      /* Default TTL value for originated packets */
#define TTL_THRESHOLD            1       /* Minimum TTL value to forward packet */
#define IPV6_HOP_LIMIT_DEFAULT   64      /* Default hop limit for IPv6 */
#define IPV6_HOP_LIMIT_THRESHOLD 1       /* Minimum hop limit to forward IPv6 packet */

/* IP Protocol Numbers (common ones) */
#define IP_PROTO_ICMP            1
#define IP_PROTO_IGMP            2
#define IP_PROTO_TCP             6
#define IP_PROTO_UDP             17
#define IP_PROTO_IPV6            41
#define IP_PROTO_ICMPV6          58
#define IP_PROTO_OSPF            89

/* IPv6 Extension Header Types */
#define IPV6_EXT_HOP_BY_HOP      0
#define IPV6_EXT_ROUTING         43
#define IPV6_EXT_FRAGMENT        44
#define IPV6_EXT_ESP             50
#define IPV6_EXT_AUTH            51
#define IPV6_EXT_DEST_OPTS       60

/* IP packet processing statistics */
typedef struct {
    uint64_t packets_processed;
    uint64_t bytes_processed;
    uint64_t ipv4_packets;
    uint64_t ipv6_packets;
    uint64_t fragmented_packets;
    uint64_t reassembled_packets;
    uint64_t ttl_exceeded;
    uint64_t header_errors;
    uint64_t forwarded_packets;
    uint64_t local_delivered;
    uint64_t dropped_packets;
} ip_stats_t;

/* Global statistics structure */
static ip_stats_t g_ip_stats;

/* Maximum Transmission Unit table */
static uint16_t g_port_mtu_table[MAX_PORTS];

/* IPv4 Fragment reassembly context */
typedef struct ipv4_frag_entry {
    ipv4_addr_t src_addr;
    ipv4_addr_t dst_addr;
    uint16_t ident;
    uint8_t protocol;
    uint32_t arrival_time;
    uint32_t total_length;
    uint16_t fragment_flags;
    uint8_t *reassembled_data;
    uint16_t fragments_received;
    bool fragments[MAX_FRAGMENTS];
    struct ipv4_frag_entry *next;
} ipv4_frag_entry_t;

/* IPv6 Fragment reassembly context */
typedef struct ipv6_frag_entry {
    ipv6_addr_t src_addr;
    ipv6_addr_t dst_addr;
    uint32_t ident;
    uint8_t next_header;
    uint32_t arrival_time;
    uint32_t total_length;
    uint8_t *reassembled_data;
    uint16_t fragments_received;
    bool fragments[MAX_FRAGMENTS];
    struct ipv6_frag_entry *next;
} ipv6_frag_entry_t;

/* Fragment reassembly tables */
static ipv4_frag_entry_t *g_ipv4_frag_table = NULL;
static ipv6_frag_entry_t *g_ipv6_frag_table = NULL;

/* IPv6 extension header processing context */
typedef struct {
    uint8_t current_header;
    uint16_t current_offset;
    uint8_t next_header;
    bool has_fragment_header;
    bool has_routing_header;
    uint8_t routing_type;
    uint8_t segments_left;
} ipv6_ext_headers_ctx_t;

/* Forward declarations */
static error_t process_ipv4_packet(packet_t *packet, uint16_t *offset);
static error_t process_ipv6_packet(packet_t *packet, uint16_t *offset);
static error_t validate_ipv4_header(const ipv4_header_t *header, uint16_t packet_len);
static error_t validate_ipv6_header(const ipv6_header_t *header, uint16_t packet_len);
static error_t process_ipv4_options(const ipv4_header_t *header, packet_t *packet);
static error_t process_ipv6_extension_headers(packet_t *packet, uint16_t *offset, ipv6_ext_headers_ctx_t *ctx);
static error_t fragment_ipv4_packet(packet_t *packet, uint16_t mtu, port_id_t egress_port);
static error_t fragment_ipv6_packet(packet_t *packet, uint16_t mtu, port_id_t egress_port);
static ipv4_frag_entry_t *find_ipv4_frag_entry(const ipv4_header_t *header);
static ipv6_frag_entry_t *find_ipv6_frag_entry(const ipv6_addr_t *src, const ipv6_addr_t *dst, uint32_t ident);
static error_t reassemble_ipv4_fragments(ipv4_frag_entry_t *entry, packet_t **reassembled);
static error_t reassemble_ipv6_fragments(ipv6_frag_entry_t *entry, packet_t **reassembled);
static void cleanup_stale_fragments(void);
static error_t forward_ip_packet(packet_t *packet, const route_entry_t *route);
static error_t deliver_to_local_stack(packet_t *packet, uint8_t protocol);
static uint16_t calculate_ipv4_checksum(const void *data, size_t len);
static bool is_local_address(const void *addr, bool is_ipv6);

/**
 * @brief Initialize the IP processing module
 *
 * Sets up the IP packet processing subsystem, initializes statistics counters,
 * and configures default MTU values for all ports.
 *
 * @return ERROR_SUCCESS on successful initialization
 *         ERROR_INIT_FAILED if initialization fails
 */
error_t ip_processing_init(void) {
    LOG_INFO("Initializing IP Processing module");
    
    /* Initialize statistics */
    memset(&g_ip_stats, 0, sizeof(g_ip_stats));
    
    /* Initialize default MTU for all ports */
    for (int i = 0; i < MAX_PORTS; i++) {
        g_port_mtu_table[i] = DEFAULT_MTU;
    }
    
    /* Register statistics with the stats collector */
    stats_register_counter("ip.packets_processed", &g_ip_stats.packets_processed);
    stats_register_counter("ip.bytes_processed", &g_ip_stats.bytes_processed);
    stats_register_counter("ip.ipv4_packets", &g_ip_stats.ipv4_packets);
    stats_register_counter("ip.ipv6_packets", &g_ip_stats.ipv6_packets);
    stats_register_counter("ip.fragmented_packets", &g_ip_stats.fragmented_packets);
    stats_register_counter("ip.reassembled_packets", &g_ip_stats.reassembled_packets);
    stats_register_counter("ip.ttl_exceeded", &g_ip_stats.ttl_exceeded);
    stats_register_counter("ip.header_errors", &g_ip_stats.header_errors);
    stats_register_counter("ip.forwarded_packets", &g_ip_stats.forwarded_packets);
    stats_register_counter("ip.local_delivered", &g_ip_stats.local_delivered);
    stats_register_counter("ip.dropped_packets", &g_ip_stats.dropped_packets);
    
    LOG_INFO("IP Processing module initialized successfully");
    return ERROR_SUCCESS;
}

/**
 * @brief Shutdown the IP processing module
 *
 * Cleans up resources used by the IP processing subsystem.
 *
 * @return ERROR_SUCCESS on successful shutdown
 */
error_t ip_processing_shutdown(void) {
    LOG_INFO("Shutting down IP Processing module");
    
    /* Free fragment reassembly tables */
    ipv4_frag_entry_t *ipv4_entry = g_ipv4_frag_table;
    while (ipv4_entry) {
        ipv4_frag_entry_t *next = ipv4_entry->next;
        if (ipv4_entry->reassembled_data) {
            free(ipv4_entry->reassembled_data);
        }
        free(ipv4_entry);
        ipv4_entry = next;
    }
    
    ipv6_frag_entry_t *ipv6_entry = g_ipv6_frag_table;
    while (ipv6_entry) {
        ipv6_frag_entry_t *next = ipv6_entry->next;
        if (ipv6_entry->reassembled_data) {
            free(ipv6_entry->reassembled_data);
        }
        free(ipv6_entry);
        ipv6_entry = next;
    }
    
    g_ipv4_frag_table = NULL;
    g_ipv6_frag_table = NULL;
    
    LOG_INFO("IP Processing module shutdown complete");
    return ERROR_SUCCESS;
}

/**
 * @brief Process an IP packet
 *
 * Main entry point for IP packet processing. Determines IP version and
 * calls the appropriate processing function.
 *
 * @param packet Pointer to the packet structure
 * @param offset Pointer to offset where IP header starts (updated after processing)
 * @return ERROR_SUCCESS on successful processing
 *         Various error codes on failure
 */
error_t ip_process_packet(packet_t *packet, uint16_t *offset) {
    error_t status;
    uint8_t version;
    
    if (!packet || !offset) {
        LOG_ERROR("Invalid parameters: packet=%p, offset=%p", packet, offset);
        return ERROR_INVALID_PARAMETER;
    }
    
    if (*offset + sizeof(uint8_t) > packet->length) {
        LOG_ERROR("Packet too short for IP header");
        g_ip_stats.header_errors++;
        g_ip_stats.dropped_packets++;
        return ERROR_PACKET_TOO_SHORT;
    }
    
    /* Get IP version */
    version = (packet->data[*offset] >> 4) & 0x0F;
    
    /* Update statistics */
    g_ip_stats.packets_processed++;
    g_ip_stats.bytes_processed += packet->length - *offset;
    
    /* Process according to IP version */
    switch (version) {
        case IP_VERSION_4:
            g_ip_stats.ipv4_packets++;
            status = process_ipv4_packet(packet, offset);
            break;
            
        case IP_VERSION_6:
            g_ip_stats.ipv6_packets++;
            status = process_ipv6_packet(packet, offset);
            break;
            
        default:
            LOG_ERROR("Unsupported IP version: %d", version);
            g_ip_stats.header_errors++;
            g_ip_stats.dropped_packets++;
            status = ERROR_UNSUPPORTED_PROTOCOL;
            break;
    }
    
    return status;
}

/**
 * @brief Set MTU for a specific port
 *
 * @param port_id Port identifier
 * @param mtu MTU value to set
 * @return ERROR_SUCCESS on success
 *         ERROR_INVALID_PARAMETER if port_id is invalid
 */
error_t ip_set_port_mtu(port_id_t port_id, uint16_t mtu) {
    if (port_id >= MAX_PORTS) {
        LOG_ERROR("Invalid port ID: %d", port_id);
        return ERROR_INVALID_PARAMETER;
    }
    
    if (mtu < MIN_MTU || mtu > MAX_MTU) {
        LOG_ERROR("Invalid MTU value: %d (min=%d, max=%d)", mtu, MIN_MTU, MAX_MTU);
        return ERROR_INVALID_PARAMETER;
    }
    
    LOG_INFO("Setting MTU for port %d to %d", port_id, mtu);
    g_port_mtu_table[port_id] = mtu;
    
    return ERROR_SUCCESS;
}

/**
 * @brief Get MTU for a specific port
 *
 * @param port_id Port identifier
 * @param mtu Pointer to store the MTU value
 * @return ERROR_SUCCESS on success
 *         ERROR_INVALID_PARAMETER if parameters are invalid
 */
error_t ip_get_port_mtu(port_id_t port_id, uint16_t *mtu) {
    if (port_id >= MAX_PORTS || !mtu) {
        LOG_ERROR("Invalid parameters: port_id=%d, mtu=%p", port_id, mtu);
        return ERROR_INVALID_PARAMETER;
    }
    
    *mtu = g_port_mtu_table[port_id];
    return ERROR_SUCCESS;
}

/**
 * @brief Get IP processing statistics
 *
 * @param stats Pointer to structure to store the statistics
 * @return ERROR_SUCCESS on success
 *         ERROR_INVALID_PARAMETER if stats is NULL
 */
error_t ip_get_statistics(ip_statistics_t *stats) {
    if (!stats) {
        LOG_ERROR("Invalid parameter: stats=%p", stats);
        return ERROR_INVALID_PARAMETER;
    }
    
    stats->packets_processed = g_ip_stats.packets_processed;
    stats->bytes_processed = g_ip_stats.bytes_processed;
    stats->ipv4_packets = g_ip_stats.ipv4_packets;
    stats->ipv6_packets = g_ip_stats.ipv6_packets;
    stats->fragmented_packets = g_ip_stats.fragmented_packets;
    stats->reassembled_packets = g_ip_stats.reassembled_packets;
    stats->ttl_exceeded = g_ip_stats.ttl_exceeded;
    stats->header_errors = g_ip_stats.header_errors;
    stats->forwarded_packets = g_ip_stats.forwarded_packets;
    stats->local_delivered = g_ip_stats.local_delivered;
    stats->dropped_packets = g_ip_stats.dropped_packets;
    
    return ERROR_SUCCESS;
}

/**
 * @brief Create an IP packet
 *
 * Creates a new IP packet with the specified parameters.
 *
 * @param src_addr Source IP address
 * @param dst_addr Destination IP address
 * @param protocol Protocol (TCP, UDP, etc.)
 * @param ttl Time-to-live value
 * @param data Payload data
 * @param data_len Length of payload data
 * @param is_ipv6 Whether to create IPv6 packet (false for IPv4)
 * @param packet Pointer to store the created packet
 * @return ERROR_SUCCESS on success
 *         Various error codes on failure
 */
error_t ip_create_packet(const void *src_addr, const void *dst_addr, uint8_t protocol,
                         uint8_t ttl, const uint8_t *data, uint16_t data_len,
                         bool is_ipv6, packet_t **packet) {
    if (!src_addr || !dst_addr || (!data && data_len > 0) || !packet) {
        LOG_ERROR("Invalid parameters");
        return ERROR_INVALID_PARAMETER;
    }
    
    uint16_t header_size = is_ipv6 ? IPV6_HEADER_LEN : IPV4_HEADER_MIN_LEN;
    uint16_t total_size = header_size + data_len;
    
    /* Allocate packet */
    *packet = (packet_t *)malloc(sizeof(packet_t));
    if (!*packet) {
        LOG_ERROR("Failed to allocate packet structure");
        return ERROR_OUT_OF_MEMORY;
    }
    
    /* Allocate packet data */
    (*packet)->data = (uint8_t *)malloc(total_size);
    if (!(*packet)->data) {
        LOG_ERROR("Failed to allocate packet data");
        free(*packet);
        *packet = NULL;
        return ERROR_OUT_OF_MEMORY;
    }
    
    (*packet)->length = total_size;
    (*packet)->ingress_port = PORT_CPU;
    (*packet)->metadata.vlan_id = DEFAULT_VLAN;
    (*packet)->metadata.priority = 0;
    
    if (is_ipv6) {
        /* Construct IPv6 header */
        ipv6_header_t *ipv6_hdr = (ipv6_header_t *)(*packet)->data;
        
        ipv6_hdr->version_tc_fl = htonl((IP_VERSION_6 << 28) | 0);  /* Version 6, Traffic Class 0, Flow Label 0 */
        ipv6_hdr->payload_len = htons(data_len);
        ipv6_hdr->next_header = protocol;
        ipv6_hdr->hop_limit = ttl;
        
        memcpy(&ipv6_hdr->src_addr, src_addr, sizeof(ipv6_addr_t));
        memcpy(&ipv6_hdr->dst_addr, dst_addr, sizeof(ipv6_addr_t));
        
        /* Copy payload data */
        if (data && data_len > 0) {
            memcpy((*packet)->data + IPV6_HEADER_LEN, data, data_len);
        }
    } else {
        /* Construct IPv4 header */
        ipv4_header_t *ipv4_hdr = (ipv4_header_t *)(*packet)->data;
        
        ipv4_hdr->version_ihl = (IP_VERSION_4 << 4) | (IPV4_HEADER_MIN_LEN / 4);  /* Version 4, IHL 5 (20 bytes) */
        ipv4_hdr->tos = 0;
        ipv4_hdr->total_length = htons(total_size);
        ipv4_hdr->id = htons(rand() & 0xFFFF);  /* Random ID */
        ipv4_hdr->flags_fragment = 0;
        ipv4_hdr->ttl = ttl;
        ipv4_hdr->protocol = protocol;
        ipv4_hdr->header_checksum = 0;
        
        ipv4_hdr->src_addr = *(ipv4_addr_t *)src_addr;
        ipv4_hdr->dst_addr = *(ipv4_addr_t *)dst_addr;
        
        /* Calculate header checksum */
        ipv4_hdr->header_checksum = calculate_ipv4_checksum(ipv4_hdr, IPV4_HEADER_MIN_LEN);
        
        /* Copy payload data */
        if (data && data_len > 0) {
            memcpy((*packet)->data + IPV4_HEADER_MIN_LEN, data, data_len);
        }
    }
    
    return ERROR_SUCCESS;
}

/**
 * @brief Process an IPv4 packet
 *
 * Processes an IPv4 packet, validates headers, handles fragmentation,
 * decrements TTL, and forwards/delivers the packet.
 *
 * @param packet Pointer to the packet structure
 * @param offset Pointer to offset where IPv4 header starts (updated after processing)
 * @return ERROR_SUCCESS on successful processing
 *         Various error codes on failure
 */
static error_t process_ipv4_packet(packet_t *packet, uint16_t *offset) {
    error_t status;
    ipv4_header_t *header;
    uint16_t header_len;
    uint16_t packet_len;
    route_entry_t route;
    bool is_fragment = false;
    
    if (*offset + sizeof(ipv4_header_t) > packet->length) {
        LOG_ERROR("Packet too short for IPv4 header");
        g_ip_stats.header_errors++;
        g_ip_stats.dropped_packets++;
        return ERROR_PACKET_TOO_SHORT;
    }
    
    header = (ipv4_header_t *)(packet->data + *offset);
    header_len = (header->version_ihl & 0x0F) * 4;
    packet_len = ntohs(header->total_length);
    
    /* Validate header */
    status = validate_ipv4_header(header, packet->length - *offset);
    if (status != ERROR_SUCCESS) {
        LOG_ERROR("IPv4 header validation failed: error=%d", status);
        g_ip_stats.header_errors++;
        g_ip_stats.dropped_packets++;
        return status;
    }
    
    /* Process IP options if present */
    if (header_len > IPV4_HEADER_MIN_LEN) {
        status = process_ipv4_options(header, packet);
        if (status != ERROR_SUCCESS) {
            LOG_ERROR("IPv4 options processing failed: error=%d", status);
            g_ip_stats.dropped_packets++;
            return status;
        }
    }
    
    /* Check if packet is a fragment */
    uint16_t frag_info = ntohs(header->flags_fragment);
    is_fragment = (frag_info & (IP_FLAG_MF | IP_FRAG_OFFSET_MASK)) != 0;
    
    if (is_fragment) {
        /* Handle IP fragmentation */
        ipv4_frag_entry_t *frag_entry = find_ipv4_frag_entry(header);
        
        if (!frag_entry) {
            /* Create new fragment entry */
            frag_entry = (ipv4_frag_entry_t *)malloc(sizeof(ipv4_frag_entry_t));
            if (!frag_entry) {
                LOG_ERROR("Failed to allocate IPv4 fragment entry");
                g_ip_stats.dropped_packets++;
                return ERROR_OUT_OF_MEMORY;
            }
            
            /* Initialize fragment entry */
            memset(frag_entry, 0, sizeof(ipv4_frag_entry_t));
            frag_entry->src_addr = header->src_addr;
            frag_entry->dst_addr = header->dst_addr;
            frag_entry->ident = ntohs(header->id);
            frag_entry->protocol = header->protocol;
            frag_entry->arrival_time = get_current_time();
            frag_entry->next = g_ipv4_frag_table;
            g_ipv4_frag_table = frag_entry;
        }
        
        /* Process fragment */
        uint16_t frag_offset = (frag_info & IP_FRAG_OFFSET_MASK) * IP_FRAGMENT_UNIT;
        uint16_t data_len = ntohs(header->total_length) - header_len;
        
        /* Update total length if this is the last fragment */
        if ((frag_info & IP_FLAG_MF) == 0) {
            frag_entry->total_length = frag_offset + data_len;
        }
        
        /* Mark fragment as received */
        if (frag_offset / IP_FRAGMENT_UNIT < MAX_FRAGMENTS) {
            frag_entry->fragments[frag_offset / IP_FRAGMENT_UNIT] = true;
            frag_entry->fragments_received++;
        }
        
        /* Copy fragment data */
        if (!frag_entry->reassembled_data) {
            frag_entry->reassembled_data = (uint8_t *)malloc(MAX_PACKET_SIZE);
            if (!frag_entry->reassembled_data) {
                LOG_ERROR("Failed to allocate memory for reassembled data");
                g_ip_stats.dropped_packets++;
                return ERROR_OUT_OF_MEMORY;
            }
            memset(frag_entry->reassembled_data, 0, MAX_PACKET_SIZE);
        }
        
        /* Copy fragment data to reassembly buffer */
        memcpy(frag_entry->reassembled_data + frag_offset, 
               packet->data + *offset + header_len, 
               data_len);
        
        /* Check if we have all fragments */
        packet_t *reassembled = NULL;
        status = reassemble_ipv4_fragments(frag_entry, &reassembled);
        
        if (status == ERROR_SUCCESS && reassembled) {
            g_ip_stats.reassembled_packets++;
            /* Process the reassembled packet */
            uint16_t new_offset = 0;
            status = ip_process_packet(reassembled, &new_offset);
            packet_free(reassembled);
            return status;
        } else if (status == ERROR_IN_PROGRESS) {
            /* Still waiting for more fragments */
            return ERROR_SUCCESS;
        } else {
            LOG_ERROR("Failed to reassemble IPv4 fragments: error=%d", status);
            g_ip_stats.dropped_packets++;
            return status;
        }
    }
    
    /* Check if TTL has expired */
    if (header->ttl <= TTL_THRESHOLD) {
        LOG_DEBUG("TTL expired for packet from %d.%d.%d.%d to %d.%d.%d.%d",
                 header->src_addr.bytes[0], header->src_addr.bytes[1], 
                 header->src_addr.bytes[2], header->src_addr.bytes[3],
                 header->dst_addr.bytes[0], header->dst_addr.bytes[1], 
                 header->dst_addr.bytes[2], header->dst_addr.bytes[3]);
        g_ip_stats.ttl_exceeded++;
        g_ip_stats.dropped_packets++;
        /* Send ICMP Time Exceeded message back to source */
        /* This would be implemented in a separate ICMP module */
        return ERROR_TTL_EXPIRED;
    }
    
    /* Decrement TTL */
    header->ttl--;
    
    /* Recalculate checksum */
    header->header_checksum = 0;
    header->header_checksum = calculate_ipv4_checksum(header, header_len);
    
    /* Check if packet is destined for local system */
    if (is_local_address(&header->dst_addr, false)) {
        g_ip_stats.local_delivered++;
        return deliver_to_local_stack(packet, header->protocol);
    }
    
    /* Look up route */
    status = routing_table_lookup(&header->dst_addr, false, &route);
    if (status != ERROR_SUCCESS) {
        LOG_ERROR("No route found for %d.%d.%d.%d",
                 header->dst_addr.bytes[0], header->dst_addr.bytes[1], 
                 header->dst_addr.bytes[2], header->dst_addr.bytes[3]);
        g_ip_stats.dropped_packets++;
        return ERROR_NO_ROUTE;
    }
    
    /* Forward packet */
    *offset += header_len;  /* Update offset to point after IP header */
    g_ip_stats.forwarded_packets++;
    return forward_ip_packet(packet, &route);
}

/**
 * @brief Process an IPv6 packet
 *
 * Processes an IPv6 packet, validates headers, handles extension headers,
 * decrements hop limit, and forwards/delivers the packet.
 *
 * @param packet Pointer to the packet structure
 * @param offset Pointer to offset where IPv6 header starts (updated after processing)
 * @return ERROR_SUCCESS on successful processing
 *         Various error codes on failure
 */
static error_t process_ipv6_packet(packet_t *packet, uint16_t *offset) {
    error_t status;
    ipv6_header_t *header;
    uint16_t payload_len;
    route_entry_t route;
    ipv6_ext_headers_ctx_t ext_headers_ctx;
    
    if (*offset + sizeof(ipv6_header_t) > packet->length) {
        LOG_ERROR("Packet too short for IPv6 header");
        g_ip_stats.header_errors++;
        g_ip_stats.dropped_packets++;
        return ERROR_PACKET_TOO_SHORT;
    }
    
    header = (ipv6_header_t *)(packet->data + *offset);
    payload_len = ntohs(header->payload_len);
    
    /* Validate header */
    status = validate_ipv6_header(header, packet->length - *offset);
    if (status != ERROR_SUCCESS) {
        LOG_ERROR("IPv6 header validation failed: error=%d", status);
        g_ip_stats.header_errors++;
        g_ip_stats.dropped_packets++;
        return status;
    }
    
    /* Check if Hop Limit has expired */
    if (header->hop_limit <= IPV6_HOP_LIMIT_THRESHOLD) {
        LOG_DEBUG("Hop Limit expired for IPv6 packet");
        g_ip_stats.ttl_exceeded++;
        g_ip_stats.dropped_packets++;
        /* Send ICMPv6 Time Exceeded message back to source */
        /* This would be implemented in a separate ICMPv6 module */
        return ERROR_TTL_EXPIRED;
    }
    
    /* Decrement Hop Limit */
    header->hop_limit--;
    
    /* Process extension headers */
    memset(&ext_headers_ctx, 0, sizeof(ext_headers_ctx));
    ext_headers_ctx.current_header = header->next_hdr;
    ext_headers_ctx.current_offset = *offset + sizeof(ipv6_header_t);

    status = process_ipv6_extension_headers(packet, &ext_headers_ctx.current_offset, &ext_headers_ctx);
    if (status != ERROR_SUCCESS) {
        LOG_ERROR("Error processing IPv6 extension headers: error=%d", status);
        g_ip_stats.header_errors++;
        g_ip_stats.dropped_packets++;
        return status;
    }

    /* Update offset to point to the transport layer header */
    *offset = ext_headers_ctx.current_offset;

    /* Check if packet is destined for us */
    if (is_local_address(&header->dst_addr, true)) {
        LOG_DEBUG("IPv6 packet destined for local delivery");
        g_ip_stats.local_delivered++;
        return deliver_to_local_stack(packet, ext_headers_ctx.next_header);
    }

    /* Lookup route for destination */
    status = routing_table_lookup_ipv6(&header->dst_addr, &route);
    if (status != ERROR_SUCCESS) {
        LOG_DEBUG("No route found for IPv6 destination");
        g_ip_stats.dropped_packets++;
        /* Send ICMPv6 Destination Unreachable message */
        return ERROR_NO_ROUTE;
    }

    /* If the packet exceeds the MTU of the outgoing interface, fragment it */
    if (packet->length > g_port_mtu_table[route.egress_port]) {
        /* Only fragment if there's no "Don't Fragment" flag
         * For IPv6, a Fragmentation header would be generated
         */
        if (!ext_headers_ctx.has_fragment_header) {
            LOG_DEBUG("IPv6 packet needs fragmentation");
            g_ip_stats.fragmented_packets++;
            status = fragment_ipv6_packet(packet, g_port_mtu_table[route.egress_port], route.egress_port);
            if (status != ERROR_SUCCESS) {
                LOG_ERROR("IPv6 fragmentation failed: error=%d", status);
                g_ip_stats.dropped_packets++;
                return status;
            }
        } else {
            /* Can't fragment - need to send ICMPv6 Packet Too Big message */
            LOG_DEBUG("IPv6 packet too big and can't be fragmented");
            g_ip_stats.dropped_packets++;
            return ERROR_PACKET_TOO_BIG;
        }
    }

    /* Forward the packet */
    g_ip_stats.forwarded_packets++;
    return forward_ip_packet(packet, &route);
}

/**
 * @brief Validate an IPv4 header
 *
 * Checks the IPv4 header for correct version, length, and checksum.
 *
 * @param header Pointer to the IPv4 header
 * @param packet_len Total length of the packet buffer from the header
 * @return ERROR_SUCCESS if header is valid
 *         ERROR_INVALID_HEADER if header fails validation
 */
static error_t validate_ipv4_header(const ipv4_header_t *header, uint16_t packet_len) {
    uint16_t header_len;
    uint16_t total_len;
    uint16_t calculated_checksum;

    /* Validate IP version */
    if ((header->version_ihl >> 4) != IP_VERSION_4) {
        LOG_ERROR("Invalid IPv4 version: %d", header->version_ihl >> 4);
        return ERROR_INVALID_HEADER;
    }

    /* Validate header length */
    header_len = (header->version_ihl & 0x0F) * 4;
    if (header_len < IPV4_HEADER_MIN_LEN || header_len > IPV4_HEADER_MAX_LEN) {
        LOG_ERROR("Invalid IPv4 header length: %d", header_len);
        return ERROR_INVALID_HEADER;
    }

    /* Validate total length */
    total_len = ntohs(header->total_len);
    if (total_len < header_len || total_len > packet_len) {
        LOG_ERROR("IPv4 total length invalid: total=%d, header=%d, packet=%d",
                 total_len, header_len, packet_len);
        return ERROR_INVALID_HEADER;
    }

    /* Validate checksum */
    calculated_checksum = calculate_ipv4_checksum(header, header_len);
    if (calculated_checksum != 0) {
        LOG_ERROR("IPv4 header checksum failed: calculated=0x%04x", calculated_checksum);
        return ERROR_INVALID_CHECKSUM;
    }

    return ERROR_SUCCESS;
}

/**
 * @brief Validate an IPv6 header
 *
 * Checks the IPv6 header for correct version and length.
 *
 * @param header Pointer to the IPv6 header
 * @param packet_len Total length of the packet buffer from the header
 * @return ERROR_SUCCESS if header is valid
 *         ERROR_INVALID_HEADER if header fails validation
 */
static error_t validate_ipv6_header(const ipv6_header_t *header, uint16_t packet_len) {
    uint16_t payload_len;

    /* Validate IP version */
    if ((ntohl(header->version_tc_flowlabel) >> 28) != IP_VERSION_6) {
        LOG_ERROR("Invalid IPv6 version: %d", ntohl(header->version_tc_flowlabel) >> 28);
        return ERROR_INVALID_HEADER;
    }

    /* Validate payload length */
    payload_len = ntohs(header->payload_len);
    if (payload_len + sizeof(ipv6_header_t) > packet_len) {
        LOG_ERROR("IPv6 payload length exceeds packet buffer: payload=%d, packet=%d",
                 payload_len, packet_len);
        return ERROR_INVALID_HEADER;
    }

    return ERROR_SUCCESS;
}

/**
 * @brief Process IPv4 options in an IPv4 header
 *
 * Processes any IPv4 header options and performs required actions.
 *
 * @param header Pointer to the IPv4 header
 * @param packet Pointer to the packet structure
 * @return ERROR_SUCCESS if options processed successfully
 *         Various error codes on failure
 */
static error_t process_ipv4_options(const ipv4_header_t *header, packet_t *packet) {
    uint8_t header_len = (header->version_ihl & 0x0F) * 4;
    uint8_t options_len = header_len - IPV4_HEADER_MIN_LEN;
    const uint8_t *options = (const uint8_t *)header + IPV4_HEADER_MIN_LEN;
    uint8_t i = 0;

    /* If no options, return success */
    if (options_len == 0) {
        return ERROR_SUCCESS;
    }

    LOG_DEBUG("Processing IPv4 options, length=%d", options_len);

    /* Process options */
    while (i < options_len) {
        uint8_t opt_type = options[i];
        uint8_t opt_len;

        /* Handle one-byte options */
        if (opt_type == 0) {  /* End of options */
            break;
        } else if (opt_type == 1) {  /* No operation */
            i++;
            continue;
        }

        /* Ensure we can read option length */
        if (i + 1 >= options_len) {
            LOG_ERROR("Malformed IPv4 option: truncated at option type");
            return ERROR_INVALID_OPTION;
        }

        opt_len = options[i + 1];

        /* Validate option length */
        if (opt_len < 2 || i + opt_len > options_len) {
            LOG_ERROR("Malformed IPv4 option: invalid length %d", opt_len);
            return ERROR_INVALID_OPTION;
        }

        /* Process specific options */
        switch (opt_type) {
            case 7:  /* Record Route */
                /* We would implement recording of the route here */
                LOG_DEBUG("IPv4 Record Route option");
                break;
            case 68:  /* Time Stamp */
                /* We would implement timestamp processing here */
                LOG_DEBUG("IPv4 Time Stamp option");
                break;
            case 131:  /* Loose Source Routing */
            case 137:  /* Strict Source Routing */
                LOG_DEBUG("IPv4 Source Routing option type=%d", opt_type);
                /* Source routing processing would be implemented here */
                break;
            default:
                LOG_DEBUG("Unhandled IPv4 option: type=%d, len=%d", opt_type, opt_len);
                break;
        }

        i += opt_len;
    }

    return ERROR_SUCCESS;
}

/**
 * @brief Process IPv6 extension headers
 *
 * Processes the chain of IPv6 extension headers, updating the context
 * with information about the headers encountered.
 *
 * @param packet Pointer to the packet structure
 * @param offset Pointer to the current offset in the packet (updated)
 * @param ctx Pointer to the extension headers context structure
 * @return ERROR_SUCCESS if extension headers processed successfully
 *         Various error codes on failure
 */
static error_t process_ipv6_extension_headers(packet_t *packet, uint16_t *offset, ipv6_ext_headers_ctx_t *ctx) {
    uint8_t current_header = ctx->current_header;
    uint16_t current_offset = *offset;
    bool done = false;

    while (!done) {
        switch (current_header) {
            case IPV6_EXT_HOP_BY_HOP:
            case IPV6_EXT_ROUTING:
            case IPV6_EXT_DEST_OPTS: {
                /* These headers have a common format: next header, hdr len, data */
                if (current_offset + 2 > packet->length) {
                    LOG_ERROR("IPv6 extension header truncated");
                    return ERROR_INVALID_HEADER;
                }

                uint8_t next_header = packet->data[current_offset];
                uint8_t hdr_ext_len = packet->data[current_offset + 1];
                uint16_t total_len = (hdr_ext_len + 1) * 8;  /* Length in 8-octet units, excluding first 8 */

                if (current_offset + total_len > packet->length) {
                    LOG_ERROR("IPv6 extension header exceeds packet length");
                    return ERROR_INVALID_HEADER;
                }

                /* Handle specific extension header types */
                if (current_header == IPV6_EXT_ROUTING) {
                    ctx->has_routing_header = true;
                    if (total_len >= 4) {  /* Ensure we can read routing data */
                        ctx->routing_type = packet->data[current_offset + 2];
                        ctx->segments_left = packet->data[current_offset + 3];
                    }
                }

                current_header = next_header;
                current_offset += total_len;
                break;
            }

            case IPV6_EXT_FRAGMENT: {
                if (current_offset + 8 > packet->length) {
                    LOG_ERROR("IPv6 fragment header truncated");
                    return ERROR_INVALID_HEADER;
                }

                ctx->has_fragment_header = true;
                uint8_t next_header = packet->data[current_offset];
                /* Fragment header processing would be implemented here */

                current_header = next_header;
                current_offset += 8;  /* Fragment header is always 8 bytes */
                break;
            }

            case IPV6_EXT_AUTH: {
                if (current_offset + 4 > packet->length) {
                    LOG_ERROR("IPv6 authentication header truncated");
                    return ERROR_INVALID_HEADER;
                }

                uint8_t next_header = packet->data[current_offset];
                uint8_t auth_len = packet->data[current_offset + 1];
                uint16_t total_len = (auth_len + 2) * 4;  /* Length in 4-octet units + 2 */

                if (current_offset + total_len > packet->length) {
                    LOG_ERROR("IPv6 authentication header exceeds packet length");
                    return ERROR_INVALID_HEADER;
                }

                current_header = next_header;
                current_offset += total_len;
                break;
            }

            case IPV6_EXT_ESP:
                /* ESP header requires special processing and marks end of readable headers */
                LOG_DEBUG("IPv6 ESP header encountered - cannot process further headers");
                done = true;
                break;

            default:
                /* This is not an extension header, so it's the upper layer protocol */
                done = true;
                break;
        }
    }

    /* Update context with final values */
    ctx->next_header = current_header;
    *offset = current_offset;

    return ERROR_SUCCESS;
}

/**
 * @brief Fragment an IPv4 packet
 *
 * Fragments an IPv4 packet to fit within the specified MTU.
 *
 * @param packet Pointer to the packet to fragment
 * @param mtu Maximum transmission unit of the outgoing interface
 * @param egress_port ID of the egress port
 * @return ERROR_SUCCESS if fragmentation successful
 *         ERROR_CANNOT_FRAGMENT if packet has DF flag set
 *         Other error codes on failure
 */
static error_t fragment_ipv4_packet(packet_t *packet, uint16_t mtu, port_id_t egress_port) {
    ipv4_header_t *header;
    uint16_t offset = 0;
    uint16_t header_len;
    uint16_t total_len;
    uint16_t data_offset;
    uint16_t max_data_per_fragment;
    uint16_t fragment_offset = 0;
    uint16_t flags_frag_offset;
    uint8_t *data_start;

    /* Get IPv4 header */
    header = (ipv4_header_t *)(packet->data + offset);
    header_len = (header->version_ihl & 0x0F) * 4;
    total_len = ntohs(header->total_len);

    /* Check if Don't Fragment flag is set */
    flags_frag_offset = ntohs(header->flags_fragment_offset);
    if (flags_frag_offset & IP_FLAG_DF) {
        LOG_DEBUG("Cannot fragment IPv4 packet with DF flag set");
        return ERROR_CANNOT_FRAGMENT;
    }

    /* Calculate maximum data per fragment (ensure multiple of 8 bytes) */
    max_data_per_fragment = (mtu - header_len) & ~0x7;
    if (max_data_per_fragment < 8) {
        LOG_ERROR("MTU too small for IPv4 fragmentation");
        return ERROR_MTU_TOO_SMALL;
    }

    data_offset = offset + header_len;
    data_start = packet->data + data_offset;

    /* Create and send fragments */
    while (fragment_offset < total_len - header_len) {
        packet_t *fragment = NULL;
        ipv4_header_t *frag_header;
        uint16_t data_len;
        uint16_t frag_total_len;

        /* Calculate fragment size */
        data_len = (fragment_offset + max_data_per_fragment <= total_len - header_len) ?
                   max_data_per_fragment : (total_len - header_len - fragment_offset);
        frag_total_len = header_len + data_len;

        /* Create a new packet for the fragment */
        fragment = packet_alloc(frag_total_len);
        if (!fragment) {
            LOG_ERROR("Failed to allocate memory for IPv4 fragment");
            return ERROR_OUT_OF_MEMORY;
        }

        /* Copy the header */
        memcpy(fragment->data, header, header_len);
        frag_header = (ipv4_header_t *)(fragment->data);

        /* Update header fields for the fragment */
        frag_header->total_len = htons(frag_total_len);

        /* Set More Fragments flag for all but the last fragment */
        flags_frag_offset = ntohs(header->flags_fragment_offset) & ~(IP_FLAG_MF | IP_FRAG_OFFSET_MASK);
        if (fragment_offset + data_len < total_len - header_len) {
            flags_frag_offset |= IP_FLAG_MF;
        } else if (ntohs(header->flags_fragment_offset) & IP_FLAG_MF) {
            /* Preserve the MF flag from the original packet if this is the last fragment */
            flags_frag_offset |= IP_FLAG_MF;
        }

        /* Set fragment offset (in 8-byte units) */
        flags_frag_offset |= (fragment_offset / 8) & IP_FRAG_OFFSET_MASK;
        frag_header->flags_fragment_offset = htons(flags_frag_offset);

        /* Clear checksum and recalculate */
        frag_header->header_checksum = 0;
        frag_header->header_checksum = calculate_ipv4_checksum(frag_header, header_len);

        /* Copy data for this fragment */
        memcpy(fragment->data + header_len, data_start + fragment_offset, data_len);

        /* Send the fragment */
        port_transmit(egress_port, fragment);
        packet_free(fragment);

        /* Move to next fragment */
        fragment_offset += data_len;
    }

    return ERROR_SUCCESS;
}

/**
 * @brief Fragment an IPv6 packet
 *
 * Creates a fragmentation header and fragments an IPv6 packet to fit within the specified MTU.
 *
 * @param packet Pointer to the packet to fragment
 * @param mtu Maximum transmission unit of the outgoing interface
 * @param egress_port ID of the egress port
 * @return ERROR_SUCCESS if fragmentation successful
 *         ERROR_NOT_IMPLEMENTED for features not yet implemented
 *         Other error codes on failure
 */
static error_t fragment_ipv6_packet(packet_t *packet, uint16_t mtu, port_id_t egress_port) {
    /* IPv6 fragmentation is more complex due to extension headers
     * For this example, we'll provide a simpler implementation
     */
    LOG_ERROR("IPv6 fragmentation not fully implemented");
    return ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Calculate IPv4 header checksum
 *
 * Calculates the standard Internet checksum over the IPv4 header.
 *
 * @param data Pointer to the data to checksum
 * @param len Length of the data in bytes
 * @return The 16-bit checksum value
 */
static uint16_t calculate_ipv4_checksum(const void *data, size_t len) {
    const uint16_t *ptr = (const uint16_t *)data;
    uint32_t sum = 0;

    /* Sum up 16-bit words */
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }

    /* Add left-over byte if present */
    if (len > 0) {
        sum += *((const uint8_t *)ptr);
    }

    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    /* Take one's complement */
    return (uint16_t)~sum;
}

/**
 * @brief Check if an IP address is a local address
 *
 * Determines if the specified IP address belongs to this device.
 *
 * @param addr Pointer to the IP address (IPv4 or IPv6)
 * @param is_ipv6 Flag indicating if the address is IPv6 (true) or IPv4 (false)
 * @return true if the address is local, false otherwise
 */
static bool is_local_address(const void *addr, bool is_ipv6) {
    /* Implementation would check against configured interface addresses */
    /* This is a simplified placeholder */

    if (is_ipv6) {
        const ipv6_addr_t *ipv6_addr = (const ipv6_addr_t *)addr;
        /* Check for loopback address (::1) */
        static const ipv6_addr_t ipv6_loopback = {
            .bytes = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}
        };

        if (memcmp(ipv6_addr, &ipv6_loopback, sizeof(ipv6_addr_t)) == 0) {
            return true;
        }

        /* Integration with interface management would be needed here */
        return false;
    } else {
        const ipv4_addr_t *ipv4_addr = (const ipv4_addr_t *)addr;
        uint32_t addr_val = ipv4_addr->addr;

        /* Check for loopback addresses (127.0.0.0/8) */
        if ((addr_val & 0xFF000000) == 0x7F000000) {
            return true;
        }

        /* Integration with interface management would be needed here */
        return false;
    }
}

/**
 * @brief Forward an IP packet
 *
 * Forwards an IP packet according to the routing entry.
 *
 * @param packet Pointer to the packet to forward
 * @param route Pointer to the route entry
 * @return ERROR_SUCCESS if forwarding successful
 *         Various error codes on failure
 */
static error_t forward_ip_packet(packet_t *packet, const route_entry_t *route) {
    error_t status;
    mac_addr_t next_hop_mac;

    /* Get next hop MAC address via ARP for IPv4 or Neighbor Discovery for IPv6 */
    if (route->is_ipv6) {
        /* This would use IPv6 Neighbor Discovery */
        LOG_DEBUG("IPv6 next hop resolution not implemented");
        return ERROR_NOT_IMPLEMENTED;
    } else {
        /* Resolve next hop MAC via ARP */
        status = arp_resolve_next_hop(&route->next_hop.ipv4, &next_hop_mac);
        if (status != ERROR_SUCCESS) {
            LOG_DEBUG("ARP resolution failed for next hop");
            /* Queue packet for ARP resolution */
            return ERROR_ARP_PENDING;
        }
    }

    /* Update Ethernet header with next hop MAC */
    ethernet_header_t *eth_header = (ethernet_header_t *)packet->data;
    memcpy(eth_header->dst_mac.addr, next_hop_mac.addr, sizeof(mac_addr_t));

    /* Send packet out the egress port */
    LOG_DEBUG("Forwarding IP packet to port %d", route->egress_port);
    return port_transmit(route->egress_port, packet);
}

/**
 * @brief Deliver packet to local protocol stack
 *
 * Delivers a packet to the appropriate local protocol handler.
 *
 * @param packet Pointer to the packet to deliver
 * @param protocol The IP protocol number
 * @return ERROR_SUCCESS if delivery successful
 *         ERROR_UNSUPPORTED_PROTOCOL if no handler for protocol
 *         Various error codes on failure
 */
static error_t deliver_to_local_stack(packet_t *packet, uint8_t protocol) {
    /* This would integrate with upper layer protocol handlers */
    LOG_DEBUG("Delivering packet to local stack, protocol=%d", protocol);

    switch (protocol) {
        case IP_PROTO_ICMP:
            /* Deliver to ICMP handler */
            LOG_DEBUG("Delivering to ICMP handler");
            break;

        case IP_PROTO_ICMPV6:
            /* Deliver to ICMPv6 handler */
            LOG_DEBUG("Delivering to ICMPv6 handler");
            break;

        case IP_PROTO_TCP:
            /* Deliver to TCP handler */
            LOG_DEBUG("Delivering to TCP handler");
            break;

        case IP_PROTO_UDP:
            /* Deliver to UDP handler */
            LOG_DEBUG("Delivering to UDP handler");
            break;

        case IP_PROTO_OSPF:
            /* Deliver to OSPF handler */
            LOG_DEBUG("Delivering to OSPF handler");
            break;

        default:
            LOG_DEBUG("Unsupported protocol: %d", protocol);
            return ERROR_UNSUPPORTED_PROTOCOL;
    }

    /* In a real implementation, we would call the appropriate protocol handler */
    return ERROR_SUCCESS;
}

/**
 * @brief Process an IP packet
 *
 * Main entry point for IP packet processing. Determines IP version
 * and dispatches to the appropriate handler.
 *
 * @param packet Pointer to the packet structure
 * @param offset Offset where IP header starts (updated after processing)
 * @return ERROR_SUCCESS on successful processing
 *         Various error codes on failure
 */
error_t ip_process_packet(packet_t *packet, uint16_t offset) {
    uint8_t version;
    error_t status;

    if (offset + 1 > packet->length) {
        LOG_ERROR("Packet too short for IP header");
        g_ip_stats.dropped_packets++;
        return ERROR_PACKET_TOO_SHORT;
    }

    /* Get IP version */
    version = (packet->data[offset] >> 4);

    /* Update statistics */
    g_ip_stats.packets_processed++;
    g_ip_stats.bytes_processed += packet->length - offset;

    LOG_DEBUG("Processing IP packet: version=%d, len=%d", version, packet->length - offset);

    /* Dispatch based on IP version */
    switch (version) {
        case IP_VERSION_4:
            g_ip_stats.ipv4_packets++;
            status = process_ipv4_packet(packet, &offset);
            break;

        case IP_VERSION_6:
            g_ip_stats.ipv6_packets++;
            status = process_ipv6_packet(packet, &offset);
            break;

        default:
            LOG_ERROR("Unsupported IP version: %d", version);
            g_ip_stats.dropped_packets++;
            return ERROR_UNSUPPORTED_VERSION;
    }

    return status;
}

/**
 * @brief Set MTU for a specific port
 *
 * Sets the Maximum Transmission Unit for the specified port.
 *
 * @param port_id ID of the port
 * @param mtu MTU value to set
 * @return ERROR_SUCCESS if MTU set successfully
 *         ERROR_INVALID_PORT if port ID is invalid
 *         ERROR_INVALID_PARAMETER if MTU is invalid
 */
error_t ip_set_port_mtu(port_id_t port_id, uint16_t mtu) {
    if (port_id >= MAX_PORTS) {
        LOG_ERROR("Invalid port ID: %d", port_id);
        return ERROR_INVALID_PORT;
    }

    if (mtu < MIN_MTU || mtu > MAX_MTU) {
        LOG_ERROR("Invalid MTU value: %d", mtu);
        return ERROR_INVALID_PARAMETER;
    }

    LOG_INFO("Setting MTU for port %d to %d", port_id, mtu);
    g_port_mtu_table[port_id] = mtu;

    return ERROR_SUCCESS;
}

/**
 * @brief Get MTU for a specific port
 *
 * Gets the Maximum Transmission Unit for the specified port.
 *
 * @param port_id ID of the port
 * @param mtu Pointer to store the MTU value
 * @return ERROR_SUCCESS if MTU retrieved successfully
 *         ERROR_INVALID_PORT if port ID is invalid
 *         ERROR_NULL_POINTER if mtu pointer is NULL
 */
error_t ip_get_port_mtu(port_id_t port_id, uint16_t *mtu) {
    if (port_id >= MAX_PORTS) {
        LOG_ERROR("Invalid port ID: %d", port_id);
        return ERROR_INVALID_PORT;
    }

    if (!mtu) {
        LOG_ERROR("NULL pointer for MTU output parameter");
        return ERROR_NULL_POINTER;
    }

    *mtu = g_port_mtu_table[port_id];

    return ERROR_SUCCESS;
}

/**
 * @brief Find an IPv4 fragment entry
 *
 * Searches for a fragment entry matching the header's identification fields.
 *
 * @param header Pointer to the IPv4 header
 * @return Pointer to the fragment entry if found, NULL otherwise
 */
static ipv4_frag_entry_t *find_ipv4_frag_entry(const ipv4_header_t *header) {
    ipv4_frag_entry_t *entry = g_ipv4_frag_table;

    while (entry) {
        if (entry->src_addr.addr == header->src_addr.addr &&
            entry->dst_addr.addr == header->dst_addr.addr &&
            entry->ident == ntohs(header->identification) &&
            entry->protocol == header->protocol) {
            return entry;
        }
        entry = entry->next;
    }

    return NULL;
}

/**
 * @brief Find an IPv6 fragment entry
 *
 * Searches for a fragment entry matching the source, destination, and identification.
 *
 * @param src Pointer to the source IPv6 address
 * @param dst Pointer to the destination IPv6 address
 * @param ident Fragment identification
 * @return Pointer to the fragment entry if found, NULL otherwise
 */
static ipv6_frag_entry_t *find_ipv6_frag_entry(const ipv6_addr_t *src, const ipv6_addr_t *dst, uint32_t ident) {
    ipv6_frag_entry_t *entry = g_ipv6_frag_table;

    while (entry) {
        if (memcmp(&entry->src_addr, src, sizeof(ipv6_addr_t)) == 0 &&
            memcmp(&entry->dst_addr, dst, sizeof(ipv6_addr_t)) == 0 &&
            entry->ident == ident) {
            return entry;
        }
        entry = entry->next;
    }

    return NULL;
}

/**
 * @brief Clean up stale fragment entries
 *
 * Removes expired fragment entries from the reassembly tables.
 */
static void cleanup_stale_fragments(void) {
    uint32_t current_time = get_system_time_ms();
    uint32_t timeout = FRAGMENT_REASSEMBLY_TIMEOUT;

    // Clean up IPv4 fragment entries
    ipv4_frag_entry_t *prev_ipv4 = NULL;
    ipv4_frag_entry_t *curr_ipv4 = g_ipv4_frag_table;

    while (curr_ipv4) {
        if ((current_time - curr_ipv4->arrival_time) > timeout) {
            ipv4_frag_entry_t *to_remove = curr_ipv4;

            // Update the linked list
            if (prev_ipv4) {
                prev_ipv4->next = curr_ipv4->next;
            } else {
                g_ipv4_frag_table = curr_ipv4->next;
            }

            curr_ipv4 = curr_ipv4->next;

            // Free the resources
            if (to_remove->reassembled_data) {
                free(to_remove->reassembled_data);
            }
            free(to_remove);

            LOG_DEBUG("Removed stale IPv4 fragment entry");
        } else {
            prev_ipv4 = curr_ipv4;
            curr_ipv4 = curr_ipv4->next;
        }
    }

    // Clean up IPv6 fragment entries
    ipv6_frag_entry_t *prev_ipv6 = NULL;
    ipv6_frag_entry_t *curr_ipv6 = g_ipv6_frag_table;

    while (curr_ipv6) {
        if ((current_time - curr_ipv6->arrival_time) > timeout) {
            ipv6_frag_entry_t *to_remove = curr_ipv6;

            // Update the linked list
            if (prev_ipv6) {
                prev_ipv6->next = curr_ipv6->next;
            } else {
                g_ipv6_frag_table = curr_ipv6->next;
            }

            curr_ipv6 = curr_ipv6->next;

            // Free the resources
            if (to_remove->reassembled_data) {
                free(to_remove->reassembled_data);
            }
            free(to_remove);

            LOG_DEBUG("Removed stale IPv6 fragment entry");
        } else {
            prev_ipv6 = curr_ipv6;
            curr_ipv6 = curr_ipv6->next;
        }
    }
}

/**
 * @brief Reassemble IPv4 fragments
 *
 * Attempts to reassemble IPv4 fragments into a complete packet.
 *
 * @param entry Pointer to the fragment entry containing fragment information
 * @param reassembled Packet to store the reassembled data
 * @return ERROR_SUCCESS if reassembly is successful or in progress
 *         ERROR_REASSEMBLY_FAILED if reassembly fails
 */
static error_t reassemble_ipv4_fragments(ipv4_frag_entry_t *entry, packet_t *reassembled) {
    if (!entry || !reassembled) {
        return ERROR_INVALID_PARAMETER;
    }

    // Check if all fragments are received
    bool complete = true;
    uint16_t max_offset = 0;

    for (int i = 0; i < MAX_FRAGMENTS; i++) {
        if (entry->fragments[i]) {
            uint16_t offset = i * IP_FRAGMENT_UNIT;
            if (offset > max_offset) {
                max_offset = offset;
            }
        } else if (i * IP_FRAGMENT_UNIT < entry->total_length) {
            // This fragment is missing and is within the expected packet length
            complete = false;
            break;
        }
    }

    // If the last fragment has the MF flag cleared, we know the total length
    if (!(entry->fragment_flags & IP_FLAG_MF) && complete) {
        // All fragments received, reassemble the packet

        // Allocate memory for the reassembled packet if not already done
        if (!entry->reassembled_data) {
            entry->reassembled_data = (uint8_t *)malloc(entry->total_length);
            if (!entry->reassembled_data) {
                LOG_ERROR("Failed to allocate memory for reassembled IPv4 packet");
                return ERROR_MEMORY_ALLOCATION_FAILED;
            }
        }

        // Copy the fragments to the reassembled packet
        ipv4_header_t *header = (ipv4_header_t *)entry->reassembled_data;

        // Set up basic header fields
        header->version_ihl = (IP_VERSION_4 << 4) | (IPV4_HEADER_MIN_LEN / 4);
        header->tos = 0;
        header->total_length = htons(entry->total_length);
        header->identification = htons(entry->ident);
        header->flags_fragment_offset = 0; // No fragmentation in reassembled packet
        header->ttl = TTL_DEFAULT;
        header->protocol = entry->protocol;
        header->header_checksum = 0;
        header->src_addr = entry->src_addr;
        header->dst_addr = entry->dst_addr;

        // Recalculate the checksum
        header->header_checksum = calculate_ipv4_checksum(header, IPV4_HEADER_MIN_LEN);

        // Copy the reassembled data to the output packet
        packet_reset(reassembled);
        if (packet_append_data(reassembled, entry->reassembled_data, entry->total_length) != ERROR_SUCCESS) {
            LOG_ERROR("Failed to append reassembled IPv4 data to packet");
            return ERROR_PACKET_OPERATION_FAILED;
        }

        // Update statistics
        g_ip_stats.reassembled_packets++;

        LOG_DEBUG("Successfully reassembled IPv4 packet (ID: %u)", entry->ident);
        return ERROR_SUCCESS;
    }

    // Reassembly still in progress
    return ERROR_REASSEMBLY_IN_PROGRESS;
}

/**
 * @brief Reassemble IPv6 fragments
 *
 * Attempts to reassemble IPv6 fragments into a complete packet.
 *
 * @param entry Pointer to the fragment entry containing fragment information
 * @param reassembled Packet to store the reassembled data
 * @return ERROR_SUCCESS if reassembly is successful or in progress
 *         ERROR_REASSEMBLY_FAILED if reassembly fails
 */
static error_t reassemble_ipv6_fragments(ipv6_frag_entry_t *entry, packet_t *reassembled) {
    if (!entry || !reassembled) {
        return ERROR_INVALID_PARAMETER;
    }

    // Check if all fragments are received
    bool complete = true;
    uint16_t max_offset = 0;

    for (int i = 0; i < MAX_FRAGMENTS; i++) {
        if (entry->fragments[i]) {
            uint16_t offset = i * IP_FRAGMENT_UNIT;
            if (offset > max_offset) {
                max_offset = offset;
            }
        } else if (i * IP_FRAGMENT_UNIT < entry->total_length) {
            // This fragment is missing and is within the expected packet length
            complete = false;
            break;
        }
    }

    if (complete && entry->total_length > 0) {
        // All fragments received, reassemble the packet

        // Allocate memory for the reassembled packet if not already done
        if (!entry->reassembled_data) {
            entry->reassembled_data = (uint8_t *)malloc(entry->total_length);
            if (!entry->reassembled_data) {
                LOG_ERROR("Failed to allocate memory for reassembled IPv6 packet");
                return ERROR_MEMORY_ALLOCATION_FAILED;
            }
        }

        // Copy the fragments to the reassembled packet
        ipv6_header_t *header = (ipv6_header_t *)entry->reassembled_data;

        // Set up basic header fields
        header->version_traffic_flow = htonl(IP_VERSION_6 << 28); // Version in the top 4 bits
        header->payload_length = htons(entry->total_length - IPV6_HEADER_LEN);
        header->next_header = entry->next_header;
        header->hop_limit = IPV6_HOP_LIMIT_DEFAULT;
        memcpy(&header->src_addr, &entry->src_addr, sizeof(ipv6_addr_t));
        memcpy(&header->dst_addr, &entry->dst_addr, sizeof(ipv6_addr_t));

        // Copy the reassembled data to the output packet
        packet_reset(reassembled);
        if (packet_append_data(reassembled, entry->reassembled_data, entry->total_length) != ERROR_SUCCESS) {
            LOG_ERROR("Failed to append reassembled IPv6 data to packet");
            return ERROR_PACKET_OPERATION_FAILED;
        }

        // Update statistics
        g_ip_stats.reassembled_packets++;

        LOG_DEBUG("Successfully reassembled IPv6 packet (ID: %u)", entry->ident);
        return ERROR_SUCCESS;
    }

    // Reassembly still in progress
    return ERROR_REASSEMBLY_IN_PROGRESS;
}

/**
 * @brief Forward an IP packet based on routing information
 *
 * Processes a packet for forwarding, updates TTL/hop-limit, performs fragmentation
 * if needed, and sends the packet to the appropriate egress port.
 *
 * @param packet The packet to forward
 * @param route The routing entry indicating where to send the packet
 * @return ERROR_SUCCESS if forwarding is successful
 *         Other error code if forwarding fails
 */
static error_t forward_ip_packet(packet_t *packet, const route_entry_t *route) {
    if (!packet || !route) {
        return ERROR_INVALID_PARAMETER;
    }

    uint16_t offset = 0;
    uint8_t version = 0;
    error_t err;

    // Read the IP version from the packet
    if (packet_peek_byte(packet, offset, &version) != ERROR_SUCCESS) {
        LOG_ERROR("Failed to read IP version");
        g_ip_stats.dropped_packets++;
        return ERROR_PACKET_OPERATION_FAILED;
    }

    version = (version >> 4) & 0x0F;

    if (version == IP_VERSION_4) {
        ipv4_header_t header;

        // Read the header
        if (packet_peek_data(packet, offset, &header, sizeof(ipv4_header_t)) != ERROR_SUCCESS) {
            LOG_ERROR("Failed to read IPv4 header");
            g_ip_stats.dropped_packets++;
            return ERROR_PACKET_OPERATION_FAILED;
        }

        // Decrement TTL
        if (header.ttl <= TTL_THRESHOLD) {
            // TTL would become zero or below, drop the packet and send ICMP time exceeded
            LOG_DEBUG("IPv4 packet TTL exceeded, dropping packet");
            g_ip_stats.ttl_exceeded++;
            g_ip_stats.dropped_packets++;

            // TODO: Generate ICMP Time Exceeded message

            return ERROR_TTL_EXCEEDED;
        }

        // Update TTL and header checksum
        header.ttl--;
        header.header_checksum = 0;
        header.header_checksum = calculate_ipv4_checksum(&header, (header.version_ihl & 0x0F) * 4);

        // Write the updated header back to the packet
        if (packet_update_data(packet, offset, &header, sizeof(ipv4_header_t)) != ERROR_SUCCESS) {
            LOG_ERROR("Failed to update IPv4 header");
            g_ip_stats.dropped_packets++;
            return ERROR_PACKET_OPERATION_FAILED;
        }

        // Check if fragmentation is needed
        uint16_t total_length = ntohs(header.total_length);
        uint16_t mtu = g_port_mtu_table[route->egress_port];

        if (total_length > mtu && !(ntohs(header.flags_fragment_offset) & IP_FLAG_DF)) {
            // Need to fragment the packet
            err = fragment_ipv4_packet(packet, mtu, route->egress_port);
            if (err != ERROR_SUCCESS) {
                LOG_ERROR("IPv4 fragmentation failed, error: %d", err);
                g_ip_stats.dropped_packets++;
                return err;
            }

            g_ip_stats.fragmented_packets++;
            LOG_DEBUG("IPv4 packet fragmented for MTU %u", mtu);

            // Fragmentation function handles forwarding
            return ERROR_SUCCESS;
        }
    } else if (version == IP_VERSION_6) {
        ipv6_header_t header;

        // Read the header
        if (packet_peek_data(packet, offset, &header, sizeof(ipv6_header_t)) != ERROR_SUCCESS) {
            LOG_ERROR("Failed to read IPv6 header");
            g_ip_stats.dropped_packets++;
            return ERROR_PACKET_OPERATION_FAILED;
        }

        // Decrement hop limit
        if (header.hop_limit <= IPV6_HOP_LIMIT_THRESHOLD) {
            // Hop limit would become zero or below, drop the packet and send ICMPv6 time exceeded
            LOG_DEBUG("IPv6 packet hop limit exceeded, dropping packet");
            g_ip_stats.ttl_exceeded++;
            g_ip_stats.dropped_packets++;

            // TODO: Generate ICMPv6 Time Exceeded message

            return ERROR_TTL_EXCEEDED;
        }

        // Update hop limit
        header.hop_limit--;

        // Write the updated header back to the packet
        if (packet_update_data(packet, offset, &header, sizeof(ipv6_header_t)) != ERROR_SUCCESS) {
            LOG_ERROR("Failed to update IPv6 header");
            g_ip_stats.dropped_packets++;
            return ERROR_PACKET_OPERATION_FAILED;
        }

        // Check if fragmentation is needed
        uint16_t payload_length = ntohs(header.payload_length);
        uint16_t total_length = payload_length + IPV6_HEADER_LEN;
        uint16_t mtu = g_port_mtu_table[route->egress_port];

        if (total_length > mtu) {
            // Need to fragment the packet
            err = fragment_ipv6_packet(packet, mtu, route->egress_port);
            if (err != ERROR_SUCCESS) {
                LOG_ERROR("IPv6 fragmentation failed, error: %d", err);
                g_ip_stats.dropped_packets++;
                return err;
            }

            g_ip_stats.fragmented_packets++;
            LOG_DEBUG("IPv6 packet fragmented for MTU %u", mtu);

            // Fragmentation function handles forwarding
            return ERROR_SUCCESS;
        }
    } else {
        LOG_ERROR("Unsupported IP version: %u", version);
        g_ip_stats.dropped_packets++;
        return ERROR_UNSUPPORTED_PROTOCOL;
    }

    // Forward the packet to the next hop
    mac_addr_t next_hop_mac;

    // Get the next hop MAC address
    if (route->gateway_addr.s_addr != 0) {
        // Use ARP to resolve the gateway MAC address
        err = arp_get_mac_for_ip(route->gateway_addr, &next_hop_mac);
    } else {
        // Direct delivery, get the destination MAC address
        ipv4_addr_t dst_ip;

        if (version == IP_VERSION_4) {
            ipv4_header_t header;
            if (packet_peek_data(packet, offset, &header, sizeof(ipv4_header_t)) != ERROR_SUCCESS) {
                LOG_ERROR("Failed to read IPv4 header for direct delivery");
                g_ip_stats.dropped_packets++;
                return ERROR_PACKET_OPERATION_FAILED;
            }
            dst_ip = header.dst_addr;
        } else {
            // For IPv6, we'd use Neighbor Discovery, but for simplicity:
            LOG_ERROR("Direct delivery for IPv6 not fully implemented");
            g_ip_stats.dropped_packets++;
            return ERROR_NOT_IMPLEMENTED;
        }

        err = arp_get_mac_for_ip(dst_ip, &next_hop_mac);
    }

    if (err != ERROR_SUCCESS) {
        if (err == ERROR_ENTRY_NOT_FOUND) {
            // ARP entry not found, trigger ARP resolution
            LOG_DEBUG("ARP entry not found, queuing packet and triggering ARP resolution");

            // Queue the packet for later transmission
            // TODO: Implement packet queuing for ARP resolution

            if (version == IP_VERSION_4) {
                ipv4_header_t header;
                if (packet_peek_data(packet, offset, &header, sizeof(ipv4_header_t)) != ERROR_SUCCESS) {
                    LOG_ERROR("Failed to read IPv4 header for ARP resolution");
                    g_ip_stats.dropped_packets++;
                    return ERROR_PACKET_OPERATION_FAILED;
                }

                if (route->gateway_addr.s_addr != 0) {
                    // Resolve gateway IP
                    arp_send_request(route->gateway_addr);
                } else {
                    // Resolve destination IP
                    arp_send_request(header.dst_addr);
                }
            }

            return ERROR_PENDING_RESOLUTION;
        } else {
            LOG_ERROR("Failed to get MAC address for next hop, error: %d", err);
            g_ip_stats.dropped_packets++;
            return err;
        }
    }

    // Update packet Ethernet header with the destination MAC
    ethernet_header_t eth_header;
    if (packet_peek_data(packet, 0, &eth_header, sizeof(ethernet_header_t)) != ERROR_SUCCESS) {
        LOG_ERROR("Failed to read Ethernet header");
        g_ip_stats.dropped_packets++;
        return ERROR_PACKET_OPERATION_FAILED;
    }

    // Set the destination MAC to the next hop
    memcpy(eth_header.dst_mac, next_hop_mac.addr, MAC_ADDR_LEN);

    // Get the egress port's MAC address for the source
    port_get_mac(route->egress_port, &eth_header.src_mac);

    // Update the Ethernet header
    if (packet_update_data(packet, 0, &eth_header, sizeof(ethernet_header_t)) != ERROR_SUCCESS) {
        LOG_ERROR("Failed to update Ethernet header");
        g_ip_stats.dropped_packets++;
        return ERROR_PACKET_OPERATION_FAILED;
    }

    // Send the packet out the egress port
    err = port_send_packet(route->egress_port, packet);
    if (err != ERROR_SUCCESS) {
        LOG_ERROR("Failed to send packet on port %u, error: %d", route->egress_port, err);
        g_ip_stats.dropped_packets++;
        return err;
    }

    // Update statistics
    g_ip_stats.forwarded_packets++;
    LOG_DEBUG("Successfully forwarded IP packet to port %u", route->egress_port);

    return ERROR_SUCCESS;
}

/**
 * @brief Fragment an IPv4 packet
 *
 * Splits an IPv4 packet into multiple fragments to fit the MTU.
 *
 * @param packet The packet to fragment
 * @param mtu Maximum Transmission Unit of the egress interface
 * @param egress_port Egress port ID
 * @return ERROR_SUCCESS if fragmentation is successful
 *         Other error code if fragmentation fails
 */
static error_t fragment_ipv4_packet(packet_t *packet, uint16_t mtu, port_id_t egress_port) {
    ipv4_header_t header;
    uint16_t offset = 0;
    error_t err;

    // Read the IPv4 header
    if (packet_peek_data(packet, offset, &header, sizeof(ipv4_header_t)) != ERROR_SUCCESS) {
        LOG_ERROR("Failed to read IPv4 header for fragmentation");
        return ERROR_PACKET_OPERATION_FAILED;
    }

    uint8_t header_len = (header.version_ihl & 0x0F) * 4;
    uint16_t total_length = ntohs(header.total_length);
    uint16_t data_len = total_length - header_len;
    uint16_t max_payload = (mtu - header_len) & ~7; // Ensure it's a multiple of 8
    uint16_t num_fragments = (data_len + max_payload - 1) / max_payload;

    LOG_DEBUG("Fragmenting IPv4 packet: total_length=%u, header_len=%u, data_len=%u, max_payload=%u, num_fragments=%u",
              total_length, header_len, data_len, max_payload, num_fragments);

    // Extract the packet data
    uint8_t *packet_data = (uint8_t *)malloc(total_length);
    if (!packet_data) {
        LOG_ERROR("Failed to allocate memory for packet data");
        return ERROR_MEMORY_ALLOCATION_FAILED;
    }

    if (packet_copy_data(packet, 0, packet_data, total_length) != ERROR_SUCCESS) {
        LOG_ERROR("Failed to copy packet data for fragmentation");
        free(packet_data);
        return ERROR_PACKET_OPERATION_FAILED;
    }

    uint16_t frag_offset = 0;
    uint16_t data_offset = header_len;
    uint16_t flags_frag_offset = ntohs(header.flags_fragment_offset);
    uint16_t orig_flags = flags_frag_offset & ~IP_FRAG_OFFSET_MASK;

    for (uint16_t i = 0; i < num_fragments; i++) {
        uint16_t payload_size = (i == num_fragments - 1) ?
                               (data_len - frag_offset) : max_payload;
        uint16_t frag_total_len = header_len + payload_size;

        // Create a new packet for the fragment
        packet_t frag_packet;
        if (packet_create(&frag_packet) != ERROR_SUCCESS) {
            LOG_ERROR("Failed to create fragment packet");
            free(packet_data);
            return ERROR_PACKET_OPERATION_FAILED;
        }

        // Update the header for this fragment
        ipv4_header_t frag_header = header;
        frag_header.total_length = htons(frag_total_len);

        // Set fragmentation flags and offset
        uint16_t new_offset = (frag_offset / 8); // Fragment offset in 8-byte units
        uint16_t new_flags = (i == num_fragments - 1) ? orig_flags : (orig_flags | IP_FLAG_MF);
        frag_header.flags_fragment_offset = htons(new_flags | new_offset);

        // Recalculate checksum
        frag_header.header_checksum = 0;
        frag_header.header_checksum = calculate_ipv4_checksum(&frag_header, header_len);

        // Append the header and payload to the fragment packet
        if (packet_append_data(&frag_packet, &frag_header, header_len) != ERROR_SUCCESS ||
            packet_append_data(&frag_packet, packet_data + data_offset + frag_offset, payload_size) != ERROR_SUCCESS) {
            LOG_ERROR("Failed to append data to fragment packet");
            packet_destroy(&frag_packet);
            free(packet_data);
            return ERROR_PACKET_OPERATION_FAILED;
        }

        // Forward the fragment
        // Note: We're bypassing the regular forwarding logic since we've already decremented TTL, etc.
        err = port_send_packet(egress_port, &frag_packet);
        packet_destroy(&frag_packet);

        if (err != ERROR_SUCCESS) {
            LOG_ERROR("Failed to send IPv4 fragment %u/%u, error: %d", i+1, num_fragments, err);
            free(packet_data);
            return err;
        }

        // Update the fragment offset for the next fragment
        frag_offset += payload_size;
    }

    free(packet_data);
    return ERROR_SUCCESS;
}

/**
 * @brief Fragment an IPv6 packet
 *
 * Splits an IPv6 packet into multiple fragments to fit the MTU.
 *
 * @param packet The packet to fragment
 * @param mtu Maximum Transmission Unit of the egress interface
 * @param egress_port Egress port ID
 * @return ERROR_SUCCESS if fragmentation is successful
 *         Other error code if fragmentation fails
 */
static error_t fragment_ipv6_packet(packet_t *packet, uint16_t mtu, port_id_t egress_port) {
    ipv6_header_t header;
    uint16_t offset = 0;
    error_t err;

    // Read the IPv6 header
    if (packet_peek_data(packet, offset, &header, sizeof(ipv6_header_t)) != ERROR_SUCCESS) {
        LOG_ERROR("Failed to read IPv6 header for fragmentation");
        return ERROR_PACKET_OPERATION_FAILED;
    }

    uint16_t payload_length = ntohs(header.payload_length);
    uint16_t total_length = payload_length + IPV6_HEADER_LEN;
    uint16_t data_len = payload_length;

    // IPv6 requires Fragment header (8 bytes)
    uint16_t fragment_header_size = 8;
    uint16_t max_payload = (mtu - IPV6_HEADER_LEN - fragment_header_size) & ~7; // Ensure it's a multiple of 8
    uint16_t num_fragments = (data_len + max_payload - 1) / max_payload;

    LOG_DEBUG("Fragmenting IPv6 packet: total_length=%u, data_len=%u, max_payload=%u, num_fragments=%u",
              total_length, data_len, max_payload, num_fragments);

    // Extract the packet data
    uint8_t *packet_data = (uint8_t *)malloc(total_length);
    if (!packet_data) {
        LOG_ERROR("Failed to allocate memory for packet data");
        return ERROR_MEMORY_ALLOCATION_FAILED;
    }

    if (packet_copy_data(packet, 0, packet_data, total_length) != ERROR_SUCCESS) {
        LOG_ERROR("Failed to copy packet data for fragmentation");
        free(packet_data);
        return ERROR_PACKET_OPERATION_FAILED;
    }

    // Generate a fragment identification
    static uint32_t frag_id = 0;
    uint32_t id = ++frag_id;

    uint16_t frag_offset = 0;
    uint16_t data_offset = IPV6_HEADER_LEN;
    uint8_t next_header = header.next_header;

    for (uint16_t i = 0; i < num_fragments; i++) {
        uint16_t payload_size = (i == num_fragments - 1) ?
                               (data_len - frag_offset) : max_payload;
        uint16_t frag_payload_len = payload_size + fragment_header_size;

        // Create a new packet for the fragment
        packet_t frag_packet;
        if (packet_create(&frag_packet) != ERROR_SUCCESS) {
            LOG_ERROR("Failed to create fragment packet");
            free(packet_data);
            return ERROR_PACKET_OPERATION_FAILED;
        }

        // Update the header for this fragment
        ipv6_header_t frag_header = header;
        frag_header.payload_length = htons(frag_payload_len);
        frag_header.next_header = IPV6_EXT_FRAGMENT;

        // Create the fragment extension header
        struct {
            uint8_t next_header;
            uint8_t reserved;
            uint16_t offset_flags;
            uint32_t identification;
        } frag_ext_header;

        frag_ext_header.next_header = next_header;
        frag_ext_header.reserved = 0;

        // Set fragmentation flags and offset
        uint16_t new_offset = (frag_offset / 8); // Fragment offset in 8-byte units
        uint16_t more_flag = (i == num_fragments - 1) ? 0 : 1;
        frag_ext_header.offset_flags = htons((new_offset << 3) | more_flag);
        frag_ext_header.identification = htonl(id);

        // Append headers and payload to the fragment packet
        if (packet_append_data(&frag_packet, &frag_header, IPV6_HEADER_LEN) != ERROR_SUCCESS ||
            packet_append_data(&frag_packet, &frag_ext_header, fragment_header_size) != ERROR_SUCCESS ||
            packet_append_data(&frag_packet, packet_data + data_offset + frag_offset, payload_size) != ERROR_SUCCESS) {
            LOG_ERROR("Failed to append data to fragment packet");
            packet_destroy(&frag_packet);
            free(packet_data);
            return ERROR_PACKET_OPERATION_FAILED;
        }

        // Forward the fragment
        err = port_send_packet(egress_port, &frag_packet);
        packet_destroy(&frag_packet);

        if (err != ERROR_SUCCESS) {
            LOG_ERROR("Failed to send IPv6 fragment %u/%u, error: %d", i+1, num_fragments, err);
            free(packet_data);
            return err;
        }

        // Update the fragment offset for the next fragment
        frag_offset += payload_size;
    }

    free(packet_data);
    return ERROR_SUCCESS;
}

/**
 * @brief Deliver a packet to the local protocol stack
 *
 * Processes a packet destined for the local system.
 *
 * @param packet The packet to deliver
 * @param protocol The protocol identifier
 * @return ERROR_SUCCESS if delivery is successful
 *         Other error code if delivery fails
 */
static error_t deliver_to_local_stack(packet_t *packet, uint8_t protocol) {
    if (!packet) {
        return ERROR_INVALID_PARAMETER;
    }
    
    // Update statistics
    g_ip_stats.local_delivered++;
    
    // In a real implementation, we would dispatch to the appropriate protocol handler
    switch (protocol) {
        case IP_PROTO_ICMP:
            LOG_DEBUG("Delivering ICMP packet to local stack");
            // TODO: Call ICMP processing function
            // icmp_process_packet(packet);
            return ERROR_SUCCESS;
            // break;
            
        case IP_PROTO_IGMP:
            LOG_DEBUG("Delivering IGMP packet to local stack");
            // TODO: Call IGMP processing function
            // igmp_process_packet(packet);
            return ERROR_SUCCESS;
            // break;

        case IP_PROTO_TCP:
            LOG_DEBUG("Delivering TCP packet to local stack");
            // TODO: Call TCP processing function
            // tcp_process_packet(packet);
            return ERROR_SUCCESS;
            // break;
            
        case IP_PROTO_UDP:
            LOG_DEBUG("Delivering UDP packet to local stack");
            // TODO: Call UDP processing function
            // udp_process_packet(packet);
            return ERROR_SUCCESS;
            // break;
            
        case IP_PROTO_OSPF:
            LOG_DEBUG("Delivering OSPF packet to local stack");
            // TODO: Call OSPF processing function
            // ospf_process_packet(packet);
            return ERROR_SUCCESS;
            // break;
            
        case IP_PROTO_ICMPV6:
            LOG_DEBUG("Delivering ICMPv6 packet to local stack");
            // TODO: Call ICMPv6 processing function
            // icmpv6_process_packet(packet);
            return ERROR_SUCCESS;
            // break;
            
        default:
            LOG_WARNING("Unsupported protocol (%u) for local delivery", protocol);
            g_ip_stats.dropped_packets++;
            return ERROR_UNSUPPORTED_PROTOCOL;
            // break;
    }
}













/**
 * @brief Calculate IPv4 header checksum
 * 
 * Calculates the checksum for an IPv4 header as per RFC 791.
 * 
 * @param data Pointer to the data to checksum
 * @param len Length of the data in bytes
 * @return The calculated checksum
 */
static uint16_t calculate_ipv4_checksum(const void *data, size_t len) {
    const uint16_t *buf = (const uint16_t *)data;
    uint32_t sum = 0;
    
    // Sum all 16-bit words
    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }
    
    // Add the padding if necessary
    if (len > 0) {
        sum += *(const uint8_t *)buf;
    }
    
    // Fold 32-bit sum to 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    // Return the one's complement
    return (uint16_t)(~sum);
}

///**
// * @brief Check if an IP address is local to this device
// * 
// * Determines if the given IP address is assigned to one of this device's interfaces.
// * 
// * @param addr Pointer to IPv4 or IPv6 address
// * @param is_ipv6 Flag indicating if the address is IPv6 (true) or IPv4 (false)
// * @return true if address is local, false otherwise
// */
//static bool is_local_address(const void *addr, bool is_ipv6) {
//    if (!addr) {
//        return false;
//    }
//    
//    // Placeholder for actual implementation
//    // In a real system, this would check all configured IP addresses
//    // on all interfaces
//    
//    if (is_ipv6) {
//        const ipv6_addr_t *ipv6_addr = (const ipv6_addr_t *)addr;
//        
//        // Check against all local IPv6 addresses
//        // For now, just check against a hardcoded address for testing
//        ipv6_addr_t local_ipv6;
//        memset(&local_ipv6, 0, sizeof(local_ipv6));
//        local_ipv6.addr[15] = 1; // ::1 (loopback)
//        
//        if (memcmp(ipv6_addr, &local_ipv6, sizeof(ipv6_addr_t)) == 0) {
//            return true;
//        }
//    } else {
//        const ipv4_addr_t *ipv4_addr = (const ipv4_addr_t *)addr;
//        
//        // Check against all local IPv4 addresses
//        // For now, just check against hardcoded addresses for testing
//        ipv4_addr_t local_ipv4 = MAKE_IPV4_ADDR(127, 0, 0, 1); // 127.0.0.1 (loopback)
//        
//        if (*ipv4_addr == local_ipv4) {
//            return true;
//        }
//        
//        // Example second address - this would be populated from configuration in a real system
//        ipv4_addr_t local_ipv4_2 = MAKE_IPV4_ADDR(192, 168, 1, 1);
//        
//        if (*ipv4_addr == local_ipv4_2) {
//            return true;
//        }
//    }
//    
//    return false;
//}
//








/**
* @brief Check if an IP address is local to this device
* 
* Determines if the given IP address is assigned to one of this device's interfaces.
* 
* @param addr Pointer to IPv4 or IPv6 address
* @param is_ipv6 Flag indicating if the address is IPv6 (true) or IPv4 (false)
* @return true if address is local, false otherwise
*/
static bool is_local_address(const void *addr, bool is_ipv6) {
   if (!addr) {
       return false;
   }
   
   // In a real implementation, this would check against a list of
   // configured addresses on all interfaces
   if (is_ipv6) {
       const ipv6_addr_t *ipv6_addr = (const ipv6_addr_t *)addr;
       
       // Check for loopback (::1)
       static const ipv6_addr_t loopback_ipv6 = {{0}};
       static const uint8_t loopback_bytes[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
       
       if (memcmp(ipv6_addr->addr, loopback_bytes, sizeof(loopback_bytes)) == 0) {
           return true;
       }
       
       // Check interface addresses (would be populated from configuration)
       // For simulation purposes, we'll use a hardcoded address
       static const uint8_t local_if_bytes[16] = {
           0x20, 0x01, 0x0d, 0xb8, // 2001:db8::/32 documentation prefix
           0x00, 0x00, 0x00, 0x00,
           0x00, 0x00, 0x00, 0x00,
           0x00, 0x00, 0x00, 0x01
       };
       
       if (memcmp(ipv6_addr->addr, local_if_bytes, sizeof(local_if_bytes)) == 0) {
           return true;
       }
   } else {
       const ipv4_addr_t *ipv4_addr = (const ipv4_addr_t *)addr;
       
       // Check for loopback (127.0.0.0/8)
       if ((*ipv4_addr & 0xFF000000) == 0x7F000000) {
           return true;
       }
       
       // Check interface addresses (would be populated from configuration)
       // For simulation purposes, we'll use hardcoded addresses
       ipv4_addr_t local_addr1 = MAKE_IPV4_ADDR(192, 168, 1, 1);
       ipv4_addr_t local_addr2 = MAKE_IPV4_ADDR(10, 0, 0, 1);
       
       if (*ipv4_addr == local_addr1 || *ipv4_addr == local_addr2) {
           return true;
       }
   }
   
   return false;
}














