/**
 * @file ip.h
 * @brief IP addressing and related structures and functions
 * 
 * This file contains definitions for IP addressing, headers, and
 * related functionality for the network layer (L3) of the switch simulator.
 */
#ifndef IP_H
#define IP_H

#include "../common/types.h"
#include "../common/error_codes.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IP address type enumeration
 */
typedef enum {
    IP_TYPE_V4,  /**< IPv4 address */
    IP_TYPE_V6   /**< IPv6 address */
} ip_addr_type_t;

/**
 * @brief Generic IP address structure
 */
typedef struct {
    ip_addr_type_t type;  /**< Address type (IPv4 or IPv6) */
    union {
        ipv4_addr_t v4;   /**< IPv4 address */
        ipv6_addr_t v6;   /**< IPv6 address */
    } addr;
} ip_addr_t;

/**
 * @brief IPv4 header structure
 */
typedef struct {
    uint8_t  version_ihl;      /**< Version (4 bits) + Internet header length (4 bits) */
    uint8_t  tos;              /**< Type of service */
    uint16_t total_length;     /**< Total length */
    uint16_t identification;   /**< Identification */
    uint16_t flags_fragment;   /**< Flags (3 bits) + Fragment offset (13 bits) */
    uint8_t  ttl;              /**< Time to live */
    uint8_t  protocol;         /**< Protocol */
    uint16_t header_checksum;  /**< Header checksum */
    ipv4_addr_t src_addr;      /**< Source address */
    ipv4_addr_t dst_addr;      /**< Destination address */
    /* Options and padding might follow */
} ipv4_header_t;

/**
 * @brief IPv6 header structure
 */
typedef struct {
    uint32_t version_class_flow;  /**< Version (4 bits) + Traffic class (8 bits) + Flow label (20 bits) */
    uint16_t payload_length;      /**< Payload length */
    uint8_t  next_header;         /**< Next header */
    uint8_t  hop_limit;           /**< Hop limit */
    ipv6_addr_t src_addr;         /**< Source address */
    ipv6_addr_t dst_addr;         /**< Destination address */
} ipv6_header_t;

/**
 * @brief IP protocol numbers
 */
typedef enum {
    IP_PROTO_ICMP = 1,      /**< Internet Control Message Protocol */
    IP_PROTO_IGMP = 2,      /**< Internet Group Management Protocol */
    IP_PROTO_TCP  = 6,      /**< Transmission Control Protocol */
    IP_PROTO_UDP  = 17,     /**< User Datagram Protocol */
    IP_PROTO_OSPF = 89,     /**< Open Shortest Path First */
    IP_PROTO_SCTP = 132     /**< Stream Control Transmission Protocol */
} ip_protocol_t;

/**
 * @brief IP header flags
 */
enum {
    IP_FLAG_RESERVED = 0x8000,  /**< Reserved (must be zero) */
    IP_FLAG_DF       = 0x4000,  /**< Don't fragment */
    IP_FLAG_MF       = 0x2000   /**< More fragments */
};

/**
 * @brief IP type of service priorities
 */
typedef enum {
    IP_TOS_ROUTINE           = 0x00,  /**< Routine */
    IP_TOS_PRIORITY          = 0x20,  /**< Priority */
    IP_TOS_IMMEDIATE         = 0x40,  /**< Immediate */
    IP_TOS_FLASH             = 0x60,  /**< Flash */
    IP_TOS_FLASH_OVERRIDE    = 0x80,  /**< Flash override */
    IP_TOS_CRITIC_ECP        = 0xA0,  /**< CRITIC/ECP */
    IP_TOS_INTERNETWORK_CTRL = 0xC0,  /**< Internetwork control */
    IP_TOS_NETWORK_CTRL      = 0xE0   /**< Network control */
} ip_tos_priority_t;

/**
 * @brief DSCP values for Quality of Service
 */
typedef enum {
    DSCP_CS0 = 0x00,  /**< Class Selector 0 (Best Effort) */
    DSCP_CS1 = 0x08,  /**< Class Selector 1 */
    DSCP_AF11 = 0x0A, /**< Assured Forwarding 11 */
    DSCP_AF12 = 0x0C, /**< Assured Forwarding 12 */
    DSCP_AF13 = 0x0E, /**< Assured Forwarding 13 */
    DSCP_CS2 = 0x10,  /**< Class Selector 2 */
    DSCP_AF21 = 0x12, /**< Assured Forwarding 21 */
    DSCP_AF22 = 0x14, /**< Assured Forwarding 22 */
    DSCP_AF23 = 0x16, /**< Assured Forwarding 23 */
    DSCP_CS3 = 0x18,  /**< Class Selector 3 */
    DSCP_AF31 = 0x1A, /**< Assured Forwarding 31 */
    DSCP_AF32 = 0x1C, /**< Assured Forwarding 32 */
    DSCP_AF33 = 0x1E, /**< Assured Forwarding 33 */
    DSCP_CS4 = 0x20,  /**< Class Selector 4 */
    DSCP_AF41 = 0x22, /**< Assured Forwarding 41 */
    DSCP_AF42 = 0x24, /**< Assured Forwarding 42 */
    DSCP_AF43 = 0x26, /**< Assured Forwarding 43 */
    DSCP_CS5 = 0x28,  /**< Class Selector 5 */
    DSCP_EF = 0x2E,   /**< Expedited Forwarding */
    DSCP_CS6 = 0x30,  /**< Class Selector 6 */
    DSCP_CS7 = 0x38   /**< Class Selector 7 */
} ip_dscp_t;

/**
 * @brief Maximum transmission unit sizes
 */
typedef enum {
    IP_MTU_ETHERNET   = 1500,   /**< Standard Ethernet MTU */
    IP_MTU_PPP        = 1500,   /**< PPP MTU */
    IP_MTU_JUMBO      = 9000,   /**< Jumbo frames MTU */
    IP_MTU_LOOPBACK   = 65536   /**< Loopback MTU */
} ip_mtu_t;

/**
 * @brief IP fragmentation parameters
 */
#define IP_FRAGMENT_MAX_SIZE 1480   /**< Maximum IP fragment size */
#define IP_FRAGMENT_MIN_SIZE 68     /**< Minimum IP fragment size */

/**
 * @brief Network address translation modes
 */
typedef enum {
    NAT_NONE,         /**< No translation */
    NAT_STATIC,       /**< Static NAT */
    NAT_DYNAMIC,      /**< Dynamic NAT */
    NAT_PAT           /**< Port Address Translation */
} nat_mode_t;

/**
 * @brief Create an IPv4 address from octets
 * 
 * @param a First octet
 * @param b Second octet
 * @param c Third octet
 * @param d Fourth octet
 * @return ipv4_addr_t The created IPv4 address
 */
ipv4_addr_t ip_create_ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d);

/**
 * @brief Create an IPv6 address from 16-bit segments
 * 
 * @param segments Array of 8 16-bit segments
 * @return ipv6_addr_t The created IPv6 address
 */
ipv6_addr_t ip_create_ipv6(uint16_t segments[8]);

/**
 * @brief Create a generic IP address from an IPv4 address
 * 
 * @param ipv4 IPv4 address
 * @return ip_addr_t Generic IP address containing the IPv4
 */
ip_addr_t ip_create_addr_from_ipv4(ipv4_addr_t ipv4);

/**
 * @brief Create a generic IP address from an IPv6 address
 * 
 * @param ipv6 IPv6 address
 * @return ip_addr_t Generic IP address containing the IPv6
 */
ip_addr_t ip_create_addr_from_ipv6(ipv6_addr_t ipv6);

/**
 * @brief Convert an IPv4 address to string notation
 * 
 * @param addr IPv4 address to convert
 * @param buf Buffer to store the string (should be at least 16 bytes)
 * @param buf_size Size of the buffer
 * @return error_code_t Error code (OK if successful)
 */
error_code_t ip_ipv4_to_str(ipv4_addr_t addr, char *buf, size_t buf_size);

/**
 * @brief Convert an IPv6 address to string notation
 * 
 * @param addr IPv6 address to convert
 * @param buf Buffer to store the string (should be at least 40 bytes)
 * @param buf_size Size of the buffer
 * @return error_code_t Error code (OK if successful)
 */
error_code_t ip_ipv6_to_str(ipv6_addr_t addr, char *buf, size_t buf_size);

/**
 * @brief Convert a generic IP address to string notation
 * 
 * @param addr Generic IP address to convert
 * @param buf Buffer to store the string (should be at least 40 bytes)
 * @param buf_size Size of the buffer
 * @return error_code_t Error code (OK if successful)
 */
error_code_t ip_addr_to_str(ip_addr_t addr, char *buf, size_t buf_size);

/**
 * @brief Convert a string to an IPv4 address
 * 
 * @param str String containing IPv4 address (e.g., "192.168.1.1")
 * @param addr Pointer to store the resulting IPv4 address
 * @return error_code_t Error code (OK if successful)
 */
error_code_t ip_str_to_ipv4(const char *str, ipv4_addr_t *addr);

/**
 * @brief Convert a string to an IPv6 address
 * 
 * @param str String containing IPv6 address (e.g., "2001:0db8:85a3:0000:0000:8a2e:0370:7334")
 * @param addr Pointer to store the resulting IPv6 address
 * @return error_code_t Error code (OK if successful)
 */
error_code_t ip_str_to_ipv6(const char *str, ipv6_addr_t *addr);

/**
 * @brief Check if an IPv4 address is multicast
 * 
 * @param addr IPv4 address to check
 * @return bool True if address is multicast, false otherwise
 */
bool ip_ipv4_is_multicast(ipv4_addr_t addr);

/**
 * @brief Check if an IPv6 address is multicast
 * 
 * @param addr IPv6 address to check
 * @return bool True if address is multicast, false otherwise
 */
bool ip_ipv6_is_multicast(ipv6_addr_t addr);

/**
 * @brief Check if an IPv4 address is private
 * 
 * @param addr IPv4 address to check
 * @return bool True if address is private, false otherwise
 */
bool ip_ipv4_is_private(ipv4_addr_t addr);

/**
 * @brief Check if an IPv4 address is loopback
 * 
 * @param addr IPv4 address to check
 * @return bool True if address is loopback, false otherwise
 */
bool ip_ipv4_is_loopback(ipv4_addr_t addr);

/**
 * @brief Check if an IPv6 address is loopback
 * 
 * @param addr IPv6 address to check
 * @return bool True if address is loopback, false otherwise
 */
bool ip_ipv6_is_loopback(ipv6_addr_t addr);

/**
 * @brief Apply a subnet mask to an IPv4 address
 * 
 * @param addr IPv4 address
 * @param mask Subnet mask
 * @return ipv4_addr_t Resulting network address
 */
ipv4_addr_t ip_ipv4_apply_mask(ipv4_addr_t addr, ipv4_addr_t mask);

/**
 * @brief Check if IPv4 address is in a subnet
 * 
 * @param addr IPv4 address to check
 * @param network Network address
 * @param mask Subnet mask
 * @return bool True if address is in subnet, false otherwise
 */
bool ip_ipv4_is_in_subnet(ipv4_addr_t addr, ipv4_addr_t network, ipv4_addr_t mask);

/**
 * @brief Create a subnet mask from prefix length
 * 
 * @param prefix_length Number of bits in prefix (e.g., 24 for a /24 network)
 * @return ipv4_addr_t The subnet mask
 */
ipv4_addr_t ip_ipv4_mask_from_prefix(uint8_t prefix_length);

/**
 * @brief Get prefix length from a subnet mask
 * 
 * @param mask Subnet mask
 * @return uint8_t Prefix length
 */
uint8_t ip_ipv4_prefix_from_mask(ipv4_addr_t mask);

/**
 * @brief Calculate IPv4 header checksum
 * 
 * @param header Pointer to IPv4 header
 * @return uint16_t Calculated checksum
 */
uint16_t ip_ipv4_calc_checksum(const ipv4_header_t *header);

/**
 * @brief Verify IPv4 header checksum
 * 
 * @param header Pointer to IPv4 header
 * @return bool True if checksum is valid, false otherwise
 */
bool ip_ipv4_verify_checksum(const ipv4_header_t *header);

/**
 * @brief Perform IPv4 fragmentation
 * 
 * @param packet Original packet
 * @param packet_size Original packet size
 * @param mtu Maximum transmission unit
 * @param fragments Array to store fragments
 * @param max_fragments Maximum number of fragments
 * @param fragment_count Pointer to store actual number of fragments
 * @return error_code_t Error code (OK if successful)
 */
error_code_t ip_ipv4_fragment(const uint8_t *packet, size_t packet_size, uint16_t mtu,
                           uint8_t **fragments, uint32_t max_fragments, uint32_t *fragment_count);

/**
 * @brief Reassemble IPv4 fragments
 * 
 * @param fragments Array of fragments
 * @param fragment_sizes Array of fragment sizes
 * @param fragment_count Number of fragments
 * @param reassembled_packet Buffer to store reassembled packet
 * @param max_packet_size Maximum size of reassembled packet
 * @param packet_size Pointer to store actual size of reassembled packet
 * @return error_code_t Error code (OK if successful)
 */
error_code_t ip_ipv4_reassemble(uint8_t **fragments, size_t *fragment_sizes, uint32_t fragment_count,
                             uint8_t *reassembled_packet, size_t max_packet_size, size_t *packet_size);

/**
 * @brief Compare two IP addresses for equality
 * 
 * @param addr1 First IP address
 * @param addr2 Second IP address
 * @return bool True if addresses are equal, false otherwise
 */
bool ip_addr_equals(ip_addr_t addr1, ip_addr_t addr2);

/**
 * @brief Get the version of an IP header
 * 
 * @param packet Pointer to IP packet
 * @return uint8_t IP version (4 or 6)
 */
uint8_t ip_get_version(const uint8_t *packet);

/**
 * @brief Well-known IPv4 addresses
 */
extern const ipv4_addr_t IP_ADDR_ANY;           /**< 0.0.0.0 */
extern const ipv4_addr_t IP_ADDR_BROADCAST;     /**< 255.255.255.255 */
extern const ipv4_addr_t IP_ADDR_LOOPBACK;      /**< 127.0.0.1 */
extern const ipv4_addr_t IP_ADDR_MULTICAST_ALL; /**< 224.0.0.1 */

#ifdef __cplusplus
}
#endif

#endif /* IP_H */
