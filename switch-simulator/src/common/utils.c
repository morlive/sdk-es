/**
 * @file utils.c
 * @brief Utility functions for switch simulator
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../include/common/types.h"
#include "../include/common/error_codes.h"
#include "../include/common/logging.h"

/**
 * @brief Convert MAC address to string representation
 * 
 * @param mac MAC address to convert
 * @param buffer Output buffer (should be at least 18 bytes)
 * @return char* Pointer to buffer containing MAC string
 */
char* mac_to_string(const mac_addr_t *mac, char *buffer) {
    if (!mac || !buffer) {
        return NULL;
    }
    
    snprintf(buffer, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac->addr[0], mac->addr[1], mac->addr[2],
             mac->addr[3], mac->addr[4], mac->addr[5]);
    
    return buffer;
}

/**
 * @brief Convert string representation to MAC address
 * 
 * @param str String in format "xx:xx:xx:xx:xx:xx"
 * @param mac Output MAC address
 * @return status_t STATUS_SUCCESS if successful
 */
status_t string_to_mac(const char *str, mac_addr_t *mac) {
    if (!str || !mac) {
        return STATUS_INVALID_PARAMETER;
    }
    
    unsigned int values[6];
    int result = sscanf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
                       &values[0], &values[1], &values[2],
                       &values[3], &values[4], &values[5]);
    
    if (result != 6) {
        return STATUS_INVALID_PARAMETER;
    }
    
    for (int i = 0; i < 6; i++) {
        if (values[i] > 255) {
            return STATUS_INVALID_PARAMETER;
        }
        mac->addr[i] = (uint8_t)values[i];
    }
    
    return STATUS_SUCCESS;
}

/**
 * @brief Convert IPv4 address to string representation
 * 
 * @param ipv4 IPv4 address in network byte order
 * @param buffer Output buffer (should be at least 16 bytes)
 * @return char* Pointer to buffer containing IP string
 */
char* ipv4_to_string(ipv4_addr_t ipv4, char *buffer) {
    if (!buffer) {
        return NULL;
    }
    
    unsigned char bytes[4];
    bytes[0] = ipv4 & 0xFF;
    bytes[1] = (ipv4 >> 8) & 0xFF;
    bytes[2] = (ipv4 >> 16) & 0xFF;
    bytes[3] = (ipv4 >> 24) & 0xFF;
    
    snprintf(buffer, 16, "%d.%d.%d.%d", bytes[3], bytes[2], bytes[1], bytes[0]);
    
    return buffer;
}

/**
 * @brief Convert string representation to IPv4 address
 * 
 * @param str String in format "xxx.xxx.xxx.xxx"
 * @param ipv4 Output IPv4 address
 * @return status_t STATUS_SUCCESS if successful
 */
status_t string_to_ipv4(const char *str, ipv4_addr_t *ipv4) {
    if (!str || !ipv4) {
        return STATUS_INVALID_PARAMETER;
    }
    
    unsigned int b[4];
    int result = sscanf(str, "%u.%u.%u.%u", &b[0], &b[1], &b[2], &b[3]);
    
    if (result != 4) {
        return STATUS_INVALID_PARAMETER;
    }
    
    for (int i = 0; i < 4; i++) {
        if (b[i] > 255) {
            return STATUS_INVALID_PARAMETER;
        }
    }
    
    *ipv4 = (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
    
    return STATUS_SUCCESS;
}

/**
 * @brief Convert IPv6 address to string representation
 * 
 * @param ipv6 IPv6 address
 * @param buffer Output buffer (should be at least 40 bytes)
 * @return char* Pointer to buffer containing IPv6 string
 */
char* ipv6_to_string(const ipv6_addr_t *ipv6, char *buffer) {
    if (!ipv6 || !buffer) {
        return NULL;
    }
    
    snprintf(buffer, 40, "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
             ipv6->addr[0], ipv6->addr[1], ipv6->addr[2], ipv6->addr[3],
             ipv6->addr[4], ipv6->addr[5], ipv6->addr[6], ipv6->addr[7],
             ipv6->addr[8], ipv6->addr[9], ipv6->addr[10], ipv6->addr[11],
             ipv6->addr[12], ipv6->addr[13], ipv6->addr[14], ipv6->addr[15]);
    
    return buffer;
}

/**
 * @brief Convert string representation to IPv6 address
 * 
 * @param str String in IPv6 format
 * @param ipv6 Output IPv6 address
 * @return status_t STATUS_SUCCESS if successful
 */
status_t string_to_ipv6(const char *str, ipv6_addr_t *ipv6) {
    if (!str || !ipv6) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // Simple implementation for basic format without compression
    unsigned int values[16];
    int fields = sscanf(str, 
                      "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
                      &values[0], &values[1], &values[2], &values[3],
                      &values[4], &values[5], &values[6], &values[7],
                      &values[8], &values[9], &values[10], &values[11],
                      &values[12], &values[13], &values[14], &values[15]);
    
    if (fields != 16) {
        return STATUS_INVALID_PARAMETER;
    }
    
    for (int i = 0; i < 16; i++) {
        if (values[i] > 255) {
            return STATUS_INVALID_PARAMETER;
        }
        ipv6->addr[i] = (uint8_t)values[i];
    }
    
    return STATUS_SUCCESS;
}

/**
 * @brief Compare two MAC addresses
 * 
 * @param mac1 First MAC address
 * @param mac2 Second MAC address
 * @return int Negative if mac1 < mac2, 0 if equal, positive if mac1 > mac2
 */
int mac_compare(const mac_addr_t *mac1, const mac_addr_t *mac2) {
    if (!mac1 || !mac2) {
        return 0;
    }
    
    return memcmp(mac1->addr, mac2->addr, 6);
}

/**
 * @brief Check if MAC address is broadcast
 * 
 * @param mac MAC address to check
 * @return bool true if MAC is broadcast (FF:FF:FF:FF:FF:FF)
 */
bool mac_is_broadcast(const mac_addr_t *mac) {
    if (!mac) {
        return false;
    }
    
    for (int i = 0; i < 6; i++) {
        if (mac->addr[i] != 0xFF) {
            return false;
        }
    }
    
    return true;
}

/**
 * @brief Check if MAC address is multicast
 * 
 * @param mac MAC address to check
 * @return bool true if MAC is multicast (least significant bit of first byte set)
 */
bool mac_is_multicast(const mac_addr_t *mac) {
    if (!mac) {
        return false;
    }
    
    // Check least significant bit of most significant byte
    return (mac->addr[0] & 0x01) == 0x01;
}

/**
 * @brief Get current timestamp in milliseconds
 * 
 * @return uint64_t Current timestamp
 */
uint64_t get_timestamp_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/**
 * @brief Calculate CRC32 checksum for data
 * 
 * @param data Input data buffer
 * @param length Length of data in bytes
 * @return uint32_t CRC32 checksum
 */
uint32_t calculate_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    
    if (!data) {
        return 0;
    }
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    
    return ~crc;
}

/**
 * @brief Memory copy function with bounds checking
 * 
 * @param dst Destination buffer
 * @param dst_size Size of destination buffer
 * @param src Source buffer
 * @param src_size Size to copy from source
 * @return status_t STATUS_SUCCESS if successful
 */
status_t safe_memcpy(void *dst, size_t dst_size, const void *src, size_t src_size) {
    if (!dst || !src) {
        return STATUS_INVALID_PARAMETER;
    }
    
    if (src_size > dst_size) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    memcpy(dst, src, src_size);
    return STATUS_SUCCESS;
}

/**
 * @brief Convert port ID to string name
 * 
 * @param port_id Port identifier
 * @param buffer Output buffer (should be at least 16 bytes)
 * @return char* Pointer to buffer containing port name
 */
char* port_id_to_name(port_id_t port_id, char *buffer) {
    if (!buffer) {
        return NULL;
    }
    
    snprintf(buffer, 16, "Port%u", port_id);
    return buffer;
}

/**
 * @brief Generate random MAC address
 * 
 * @param mac Output MAC address
 */
void generate_random_mac(mac_addr_t *mac) {
    if (!mac) {
        return;
    }
    
    // Generate random MAC but ensure it's locally administered and unicast
    for (int i = 0; i < 6; i++) {
        mac->addr[i] = (uint8_t)(rand() & 0xFF);
    }
    
    // Set locally administered bit, clear multicast bit
    mac->addr[0] &= 0xFE; // Clear multicast bit
    mac->addr[0] |= 0x02; // Set locally administered bit
}

/**
 * @brief Parse VLAN range string into VLAN IDs
 * 
 * @param range_str String in format "X" or "X-Y" where X,Y are VLAN IDs
 * @param vlan_ids Output array of VLAN IDs
 * @param max_ids Maximum number of IDs that can be stored in array
 * @param[out] count Number of VLAN IDs parsed
 * @return status_t STATUS_SUCCESS if successful
 */
status_t parse_vlan_range(const char *range_str, vlan_id_t *vlan_ids, 
                         uint32_t max_ids, uint32_t *count) {
    if (!range_str || !vlan_ids || !count) {
        return STATUS_INVALID_PARAMETER;
    }
    
    *count = 0;
    
    // Check for range format "X-Y"
    char *dash = strchr(range_str, '-');
    if (dash) {
        // Parse as range
        vlan_id_t start, end;
        if (sscanf(range_str, "%hu-%hu", &start, &end) != 2) {
            return STATUS_INVALID_PARAMETER;
        }
        
        if (start > end || start == 0 || end > 4095) {
            return STATUS_INVALID_PARAMETER;
        }
        
        uint32_t range_size = end - start + 1;
        if (range_size > max_ids) {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        
        for (vlan_id_t id = start; id <= end; id++) {
            vlan_ids[(*count)++] = id;
        }
    } else {
        // Parse as single VLAN
        vlan_id_t id;
        if (sscanf(range_str, "%hu", &id) != 1) {
            return STATUS_INVALID_PARAMETER;
        }
        
        if (id == 0 || id > 4095) {
            return STATUS_INVALID_PARAMETER;
        }
        
        if (*count >= max_ids) {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        
        vlan_ids[(*count)++] = id;
    }
    
    return STATUS_SUCCESS;
}

/**
 * @brief Safely concatenate strings with buffer size checking
 * 
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param src Source string to append
 * @return status_t STATUS_SUCCESS if successful
 */
status_t safe_strcat(char *dest, size_t dest_size, const char *src) {
    if (!dest || !src) {
        return STATUS_INVALID_PARAMETER;
    }
    
    size_t dest_len = strlen(dest);
    size_t src_len = strlen(src);
    
    if (dest_len + src_len + 1 > dest_size) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    strcat(dest, src);
    return STATUS_SUCCESS;
}

/**
 * @brief Check if a string is a valid decimal number
 * 
 * @param str String to check
 * @return bool true if string is a valid number
 */
bool is_valid_number(const char *str) {
    if (!str || *str == '\0') {
        return false;
    }
    
    // Skip initial whitespace
    while (*str == ' ' || *str == '\t') {
        str++;
    }
    
    // Handle optional sign
    if (*str == '-' || *str == '+') {
        str++;
    }
    
    // Need at least one digit
    if (*str < '0' || *str > '9') {
        return false;
    }
    
    // Check remaining characters are digits
    while (*str) {
        if (*str < '0' || *str > '9') {
            return false;
        }
        str++;
    }
    
    return true;
}

/**
 * @brief Convert byte array to hexadecimal string
 * 
 * @param data Byte array
 * @param length Length of byte array
 * @param buffer Output buffer (should be at least length*2+1 bytes)
 * @return char* Pointer to buffer containing hex string
 */
char* bytes_to_hex(const uint8_t *data, size_t length, char *buffer) {
    if (!data || !buffer) {
        return NULL;
    }
    
    for (size_t i = 0; i < length; i++) {
        sprintf(buffer + (i * 2), "%02x", data[i]);
    }
    
    buffer[length * 2] = '\0';
    return buffer;
}

/**
 * @brief Convert hexadecimal string to byte array
 * 
 * @param hex Hexadecimal string
 * @param data Output byte array
 * @param max_length Maximum length of byte array
 * @param[out] length Actual length of converted data
 * @return status_t STATUS_SUCCESS if successful
 */
status_t hex_to_bytes(const char *hex, uint8_t *data, size_t max_length, size_t *length) {
    if (!hex || !data || !length) {
        return STATUS_INVALID_PARAMETER;
    }
    
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0) {
        return STATUS_INVALID_PARAMETER;
    }
    
    size_t byte_count = hex_len / 2;
    if (byte_count > max_length) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    for (size_t i = 0; i < byte_count; i++) {
        unsigned int value;
        if (sscanf(hex + (i * 2), "%02x", &value) != 1) {
            return STATUS_INVALID_PARAMETER;
        }
        data[i] = (uint8_t)value;
    }
    
    *length = byte_count;
    return STATUS_SUCCESS;
}
