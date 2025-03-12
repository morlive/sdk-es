/**
 * @file types.h
 * @brief Base type definitions for switch simulator
 */

#ifndef SWITCH_SIM_TYPES_H
#define SWITCH_SIM_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


/**
 * @brief Status codes for API function returns
 */
typedef enum
{
  STATUS_SUCCESS = 0,             /**< Operation completed successfully */
  STATUS_FAILURE,                 /**< Generic failure */
  STATUS_INVALID_PARAMETER,       /**< Invalid parameter provided */
  STATUS_INSUFFICIENT_RESOURCES,  /**< Not enough resources to complete operation */
  STATUS_NOT_INITIALIZED,         /**< Component not initialized */
  STATUS_NOT_IMPLEMENTED,         /**< Feature not implemented */
  STATUS_TIMEOUT,                 /**< Operation timed out */
  STATUS_TABLE_FULL,              /**< Table is full */
  STATUS_ALREADY_EXISTS,          /**< Entry already exists */
  STATUS_NOT_FOUND,               /**< Entry not found */
} status_t;


/**
 * @brief MAC address type (6 bytes)
 */
typedef struct 
{
    uint8_t addr[6];
} mac_addr_t;


/**
 * @brief IPv4 address type
 */
typedef uint32_t ipv4_addr_t;


/**
 * @brief IPv6 address type (16 bytes)
 */
typedef struct
{
    uint8_t addr[16];
} ipv6_addr_t;

/**
 * @brief Port identifier
 */
typedef uint16_t port_id_t;

/**
 * @brief VLAN identifier
 */
typedef uint16_t vlan_id_t;

/**
 * @brief Switch identifier
 */
typedef uint32_t switch_id_t;

/**
 * @brief Maximum number of ports supported
 */
#define MAX_PORTS 64

/**
 * @brief Maximum number of VLANs supported
 */
#define MAX_VLANS 4096

/**
 * @brief Maximum size of MAC table
 */
#define MAX_MAC_TABLE_ENTRIES 8192

/**
 * @brief Maximum packet size
 */
#define MAX_PACKET_SIZE 9216  // Support for jumbo frames



#endif   /* SWITCH_SIM_TYPES_H */
