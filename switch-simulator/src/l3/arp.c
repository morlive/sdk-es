/**
 * @file arp.c
 * @brief Implementation of ARP (Address Resolution Protocol) functionality
 *
 * This file contains the implementation of ARP table management and related operations,
 * including ARP cache maintenance, lookups, and packet processing.
 */

#include "l3/arp.h"
#include "l3/ip_processing.h"
#include "l2/mac_table.h"
#include "common/logging.h"
#include "common/error_codes.h"
#include "hal/packet.h"
#include "hal/port.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

/* Defines */
#define ARP_CACHE_SIZE 1024
#define ARP_CACHE_TIMEOUT_SEC 1200  /* 20 minutes by default */
#define ARP_REQUEST_RETRY_COUNT 3
#define ARP_REQUEST_RETRY_INTERVAL_MS 1000

/* ARP packet format definitions */
#define ARP_HARDWARE_TYPE_ETHERNET 1
#define ARP_PROTOCOL_TYPE_IPV4 0x0800
#define ARP_HARDWARE_SIZE_ETHERNET 6
#define ARP_PROTOCOL_SIZE_IPV4 4
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY 2

/* ARP packet structure */
typedef struct __attribute__((__packed__)) {
    uint16_t hw_type;       /* Hardware type (Ethernet = 1) */
    uint16_t protocol_type; /* Protocol type (IPv4 = 0x0800) */
    uint8_t hw_addr_len;    /* Hardware address length (6 for MAC) */
    uint8_t proto_addr_len; /* Protocol address length (4 for IPv4) */
    uint16_t operation;     /* Operation (1=request, 2=reply) */
    mac_addr_t sender_mac;  /* Sender MAC address */
    ipv4_addr_t sender_ip;  /* Sender IP address */
    mac_addr_t target_mac;  /* Target MAC address */
    ipv4_addr_t target_ip;  /* Target IP address */
} arp_packet_t;

/* ARP cache entry states */
typedef enum {
    ARP_STATE_INCOMPLETE,  /* Resolution in progress */
    ARP_STATE_REACHABLE,   /* Confirmed reachability */
    ARP_STATE_STALE,       /* Reachability requires confirmation */
    ARP_STATE_DELAY,       /* Waiting before sending probe */
    ARP_STATE_PROBE,       /* Actively probing */
    ARP_STATE_FAILED       /* ARP resolution failed */
} arp_state_t;

/* ARP cache entry structure */
typedef struct arp_entry {
    ipv4_addr_t ip;           /* IPv4 address */
    mac_addr_t mac;           /* MAC address */
    arp_state_t state;        /* Entry state */
    uint32_t created_time;    /* Creation timestamp */
    uint32_t updated_time;    /* Last update timestamp */
    uint16_t port_index;      /* Port where this MAC was learned */
    uint8_t retry_count;      /* Retry counter for ARP requests */
    struct arp_entry *next;   /* Pointer for hash collision resolution */
} arp_entry_t;

/* ARP table structure */
typedef struct {
    arp_entry_t *hash_table[ARP_CACHE_SIZE]; /* Hash table buckets */
    arp_entry_t *entry_pool;                 /* Pre-allocated entries pool */
    uint16_t entry_count;                    /* Number of entries in use */
    uint32_t timeout;                        /* ARP cache timeout in seconds */
    bool initialized;                        /* Initialization flag */
    arp_stats_t stats;                       /* ARP statistics */
} arp_table_t;

/* Function prototypes for internal use */
static uint32_t hash_ipv4(const ipv4_addr_t *ipv4);
static arp_entry_t *arp_find_entry(arp_table_t *table, const ipv4_addr_t *ipv4);
static arp_entry_t *arp_allocate_entry(arp_table_t *table);
static void arp_free_entry(arp_table_t *table, arp_entry_t *entry);
static status_t arp_send_request(arp_table_t *table, const ipv4_addr_t *target_ip, uint16_t port_index);
static status_t arp_send_reply(arp_table_t *table, const ipv4_addr_t *target_ip, const mac_addr_t *target_mac,
                               const ipv4_addr_t *sender_ip, uint16_t port_index);
static status_t arp_process_packet(arp_table_t *table, const packet_t *packet, uint16_t port_index);
static uint32_t get_current_time(void);

/**
 * @brief Initialize the ARP module
 *
 * @param table Pointer to ARP table structure
 * @return status_t Status code indicating success or failure
 */
status_t arp_init(arp_table_t *table) {
    if (!table) {
        LOG_ERROR("Invalid parameter: NULL ARP table pointer");
        return STATUS_INVALID_PARAMETER;
    }

    LOG_INFO("Initializing ARP module");

    /* Clear the ARP table structure */
    memset(table, 0, sizeof(arp_table_t));

    /* Pre-allocate ARP entries */
    table->entry_pool = (arp_entry_t *)calloc(ARP_CACHE_SIZE, sizeof(arp_entry_t));
    if (!table->entry_pool) {
        LOG_ERROR("Failed to allocate memory for ARP cache entries");
        return STATUS_NO_MEMORY;
    }

    /* Initialize ARP cache timeout */
    table->timeout = ARP_CACHE_TIMEOUT_SEC;
    table->initialized = true;

    LOG_INFO("ARP module initialized successfully, cache size: %d entries", ARP_CACHE_SIZE);
    return STATUS_SUCCESS;
}

/**
 * @brief Clean up the ARP module resources
 *
 * @param table Pointer to ARP table structure
 * @return status_t Status code indicating success or failure
 */
status_t arp_deinit(arp_table_t *table) {
    if (!table) {
        LOG_ERROR("Invalid parameter: NULL ARP table pointer");
        return STATUS_INVALID_PARAMETER;
    }

    if (!table->initialized) {
        LOG_WARN("ARP module not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    LOG_INFO("Cleaning up ARP module resources");

    /* Free the entry pool */
    if (table->entry_pool) {
        free(table->entry_pool);
        table->entry_pool = NULL;
    }

    /* Reset table structure */
    memset(table, 0, sizeof(arp_table_t));

    LOG_INFO("ARP module resources cleaned up successfully");
    return STATUS_SUCCESS;
}

/**
 * @brief Add or update an entry in the ARP cache
 *
 * @param table Pointer to ARP table structure
 * @param ipv4 IPv4 address
 * @param mac MAC address
 * @param port_index Port index where the MAC was learned
 * @return status_t Status code indicating success or failure
 */
status_t arp_add_entry(arp_table_t *table, const ipv4_addr_t *ipv4, const mac_addr_t *mac, uint16_t port_index) {
    if (!table || !ipv4 || !mac) {
        LOG_ERROR("Invalid parameter(s) in arp_add_entry");
        return STATUS_INVALID_PARAMETER;
    }

    if (!table->initialized) {
        LOG_ERROR("ARP module not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    LOG_DEBUG("Adding/updating ARP entry for IP: %d.%d.%d.%d", 
              ipv4->bytes[0], ipv4->bytes[1], ipv4->bytes[2], ipv4->bytes[3]);

    /* Look for existing entry */
    arp_entry_t *entry = arp_find_entry(table, ipv4);
    
    if (entry) {
        /* Update existing entry */
        memcpy(&entry->mac, mac, sizeof(mac_addr_t));
        entry->port_index = port_index;
        entry->updated_time = get_current_time();
        entry->state = ARP_STATE_REACHABLE;
        
        LOG_DEBUG("Updated existing ARP entry");
    } else {
        /* Allocate new entry */
        entry = arp_allocate_entry(table);
        if (!entry) {
            LOG_ERROR("Failed to allocate new ARP entry");
            return STATUS_RESOURCE_EXHAUSTED;
        }
        
        /* Initialize new entry */
        memcpy(&entry->ip, ipv4, sizeof(ipv4_addr_t));
        memcpy(&entry->mac, mac, sizeof(mac_addr_t));
        entry->port_index = port_index;
        entry->created_time = get_current_time();
        entry->updated_time = entry->created_time;
        entry->state = ARP_STATE_REACHABLE;
        entry->retry_count = 0;
        
        /* Add to hash table */
        uint32_t hash = hash_ipv4(ipv4) % ARP_CACHE_SIZE;
        entry->next = table->hash_table[hash];
        table->hash_table[hash] = entry;
        table->entry_count++;
        
        LOG_DEBUG("Added new ARP entry, current count: %d", table->entry_count);
    }
    
    /* Update MAC table as well to ensure L2 forwarding works properly */
    /* This is assuming we have a function to update the MAC table */
    mac_table_add_entry(mac, port_index);
    
    table->stats.entries_added++;
    return STATUS_SUCCESS;
}

/**
 * @brief Look up an entry in the ARP cache
 *
 * @param table Pointer to ARP table structure
 * @param ipv4 IPv4 address to look up
 * @param mac_result Pointer to store the resulting MAC address
 * @param port_index_result Pointer to store the resulting port index
 * @return status_t Status code indicating success or failure
 */
status_t arp_lookup(arp_table_t *table, const ipv4_addr_t *ipv4, mac_addr_t *mac_result, uint16_t *port_index_result) {
    if (!table || !ipv4 || !mac_result) {
        LOG_ERROR("Invalid parameter(s) in arp_lookup");
        return STATUS_INVALID_PARAMETER;
    }

    if (!table->initialized) {
        LOG_ERROR("ARP module not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    LOG_DEBUG("Looking up ARP entry for IP: %d.%d.%d.%d", 
              ipv4->bytes[0], ipv4->bytes[1], ipv4->bytes[2], ipv4->bytes[3]);

    /* Find entry in the cache */
    arp_entry_t *entry = arp_find_entry(table, ipv4);
    
    if (!entry) {
        LOG_DEBUG("ARP entry not found, initiating resolution");
        
        /* If the entry doesn't exist, start the resolution process */
        /* Determine the outgoing interface using the routing table */
        uint16_t out_port = 0; /* This should be determined using the routing table */
        
        /* Create incomplete entry */
        arp_entry_t *new_entry = arp_allocate_entry(table);
        if (!new_entry) {
            LOG_ERROR("Failed to allocate new ARP entry for resolution");
            return STATUS_RESOURCE_EXHAUSTED;
        }
        
        /* Initialize incomplete entry */
        memcpy(&new_entry->ip, ipv4, sizeof(ipv4_addr_t));
        memset(&new_entry->mac, 0, sizeof(mac_addr_t));
        new_entry->port_index = out_port;
        new_entry->created_time = get_current_time();
        new_entry->updated_time = new_entry->created_time;
        new_entry->state = ARP_STATE_INCOMPLETE;
        new_entry->retry_count = 0;
        
        /* Add to hash table */
        uint32_t hash = hash_ipv4(ipv4) % ARP_CACHE_SIZE;
        new_entry->next = table->hash_table[hash];
        table->hash_table[hash] = new_entry;
        table->entry_count++;
        
        /* Send ARP request */
        arp_send_request(table, ipv4, out_port);
        table->stats.requests_sent++;
        
        return STATUS_PENDING;
    }
    
    /* Entry exists, check if it's complete */
    if (entry->state != ARP_STATE_REACHABLE) {
        if (entry->state == ARP_STATE_INCOMPLETE) {
            LOG_DEBUG("ARP resolution in progress");
            return STATUS_PENDING;
        } else if (entry->state == ARP_STATE_FAILED) {
            LOG_DEBUG("ARP resolution previously failed");
            return STATUS_NOT_FOUND;
        }
    }
    
    /* Return valid entry */
    memcpy(mac_result, &entry->mac, sizeof(mac_addr_t));
    if (port_index_result) {
        *port_index_result = entry->port_index;
    }
    
    /* Update statistics */
    table->stats.cache_hits++;
    
    LOG_DEBUG("ARP entry found");
    return STATUS_SUCCESS;
}

/**
 * @brief Remove an entry from the ARP cache
 *
 * @param table Pointer to ARP table structure
 * @param ipv4 IPv4 address of the entry to remove
 * @return status_t Status code indicating success or failure
 */
status_t arp_remove_entry(arp_table_t *table, const ipv4_addr_t *ipv4) {
    if (!table || !ipv4) {
        LOG_ERROR("Invalid parameter(s) in arp_remove_entry");
        return STATUS_INVALID_PARAMETER;
    }

    if (!table->initialized) {
        LOG_ERROR("ARP module not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    LOG_DEBUG("Removing ARP entry for IP: %d.%d.%d.%d", 
              ipv4->bytes[0], ipv4->bytes[1], ipv4->bytes[2], ipv4->bytes[3]);

    uint32_t hash = hash_ipv4(ipv4) % ARP_CACHE_SIZE;
    arp_entry_t *entry = table->hash_table[hash];
    arp_entry_t *prev = NULL;
    
    /* Search for the entry in the hash chain */
    while (entry) {
        if (memcmp(&entry->ip, ipv4, sizeof(ipv4_addr_t)) == 0) {
            /* Found the entry, remove it from the hash chain */
            if (prev) {
                prev->next = entry->next;
            } else {
                table->hash_table[hash] = entry->next;
            }
            
            /* Free the entry */
            arp_free_entry(table, entry);
            table->entry_count--;
            table->stats.entries_removed++;
            
            LOG_DEBUG("ARP entry removed, current count: %d", table->entry_count);
            return STATUS_SUCCESS;
        }
        
        prev = entry;
        entry = entry->next;
    }
    
    LOG_DEBUG("ARP entry not found for removal");
    return STATUS_NOT_FOUND;
}

/**
 * @brief Process an incoming ARP packet
 *
 * @param table Pointer to ARP table structure
 * @param packet Pointer to the packet structure
 * @param port_index Port index where the packet was received
 * @return status_t Status code indicating success or failure
 */
status_t arp_process_packet(arp_table_t *table, const packet_t *packet, uint16_t port_index) {
    if (!table || !packet) {
        LOG_ERROR("Invalid parameter(s) in arp_process_packet");
        return STATUS_INVALID_PARAMETER;
    }

    if (!table->initialized) {
        LOG_ERROR("ARP module not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    LOG_DEBUG("Processing ARP packet received on port %d", port_index);

    /* Verify packet size */
    if (packet->length < sizeof(arp_packet_t)) {
        LOG_WARN("Received ARP packet is too small: %d bytes", packet->length);
        table->stats.invalid_packets++;
        return STATUS_INVALID_PACKET;
    }
    
    /* Parse ARP packet */
    const arp_packet_t *arp_packet = (const arp_packet_t *)packet->data;
    
    /* Verify packet fields */
    if (ntohs(arp_packet->hw_type) != ARP_HARDWARE_TYPE_ETHERNET ||
        ntohs(arp_packet->protocol_type) != ARP_PROTOCOL_TYPE_IPV4 ||
        arp_packet->hw_addr_len != ARP_HARDWARE_SIZE_ETHERNET ||
        arp_packet->proto_addr_len != ARP_PROTOCOL_SIZE_IPV4) {
        
        LOG_WARN("Invalid ARP packet format");
        table->stats.invalid_packets++;
        return STATUS_INVALID_PACKET;
    }
    
    uint16_t operation = ntohs(arp_packet->operation);
    
    /* Learn sender's IP-to-MAC mapping regardless of packet type */
    ipv4_addr_t sender_ip;
    mac_addr_t sender_mac;
    memcpy(&sender_ip, &arp_packet->sender_ip, sizeof(ipv4_addr_t));
    memcpy(&sender_mac, &arp_packet->sender_mac, sizeof(mac_addr_t));
    
    /* Update ARP cache with sender's info */
    arp_add_entry(table, &sender_ip, &sender_mac, port_index);
    
    /* Process based on ARP operation */
    switch (operation) {
        case ARP_OP_REQUEST: {
            LOG_DEBUG("Received ARP request");
            table->stats.requests_received++;
            
            /* Extract target IP */
            ipv4_addr_t target_ip;
            memcpy(&target_ip, &arp_packet->target_ip, sizeof(ipv4_addr_t));
            
            /* Check if we can respond (if we own this IP) */
            bool is_our_ip = false; /* This should check against our interfaces */
            
            if (is_our_ip) {
                /* Get our MAC address for this interface */
                mac_addr_t our_mac;
                /* This should be a lookup to get the MAC of the target interface */
                
                /* Send ARP reply */
                arp_send_reply(table, &sender_ip, &sender_mac, &target_ip, port_index);
                table->stats.replies_sent++;
            }
            break;
        }
        
        case ARP_OP_REPLY: {
            LOG_DEBUG("Received ARP reply");
            table->stats.replies_received++;
            
            /* ARP entry already updated from sender info above */
            break;
        }
        
        default:
            LOG_WARN("Unknown ARP operation: %d", operation);
            table->stats.invalid_packets++;
            return STATUS_INVALID_PACKET;
    }
    
    return STATUS_SUCCESS;
}

/**
 * @brief Flush all entries from the ARP cache
 *
 * @param table Pointer to ARP table structure
 * @return status_t Status code indicating success or failure
 */
status_t arp_flush(arp_table_t *table) {
    if (!table) {
        LOG_ERROR("Invalid parameter: NULL ARP table pointer");
        return STATUS_INVALID_PARAMETER;
    }

    if (!table->initialized) {
        LOG_ERROR("ARP module not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    LOG_INFO("Flushing ARP cache");

    /* Free all entries in hash buckets */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_entry_t *entry = table->hash_table[i];
        
        while (entry) {
            arp_entry_t *next = entry->next;
            arp_free_entry(table, entry);
            entry = next;
        }
        
        table->hash_table[i] = NULL;
    }
    
    table->entry_count = 0;
    table->stats.cache_flushes++;
    
    LOG_INFO("ARP cache flushed successfully");
    return STATUS_SUCCESS;
}

/**
 * @brief Age out old entries from the ARP cache
 *
 * @param table Pointer to ARP table structure
 * @return status_t Status code indicating success or failure
 */
status_t arp_age_entries(arp_table_t *table) {
    if (!table) {
        LOG_ERROR("Invalid parameter: NULL ARP table pointer");
        return STATUS_INVALID_PARAMETER;
    }

    if (!table->initialized) {
        LOG_ERROR("ARP module not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    LOG_DEBUG("Aging ARP cache entries");

    uint32_t current_time = get_current_time();
    uint32_t aged_count = 0;
    
    /* Check all entries in hash buckets */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_entry_t *entry = table->hash_table[i];
        arp_entry_t *prev = NULL;
        
        while (entry) {
            arp_entry_t *next = entry->next;
            
            /* Check if entry has timed out */
            if (entry->state == ARP_STATE_REACHABLE &&
                (current_time - entry->updated_time) > table->timeout) {
                
                /* Remove from hash chain */
                if (prev) {
                    prev->next = next;
                } else {
                    table->hash_table[i] = next;
                }
                
                /* Free the entry */
                arp_free_entry(table, entry);
                table->entry_count--;
                aged_count++;
                
                /* Don't update prev since we removed the entry */
            } else {
                /* Handle incomplete entries that have timed out */
                if (entry->state == ARP_STATE_INCOMPLETE) {
                    if ((current_time - entry->updated_time) > ARP_REQUEST_RETRY_INTERVAL_MS / 1000) {
                        if (entry->retry_count < ARP_REQUEST_RETRY_COUNT) {
                            /* Retry ARP request */
                            arp_send_request(table, &entry->ip, entry->port_index);
                            entry->retry_count++;
                            entry->updated_time = current_time;
                            table->stats.requests_sent++;
                        } else {
                            /* Max retries reached, mark as failed */
                            entry->state = ARP_STATE_FAILED;
                        }
                    }
                }
                
                prev = entry;
            }
            
            entry = next;
        }
    }
    
    if (aged_count > 0) {
        LOG_DEBUG("Aged out %d ARP entries", aged_count);
        table->stats.entries_aged += aged_count;
    }
    
    return STATUS_SUCCESS;
}

/**
 * @brief Get ARP statistics
 *
 * @param table Pointer to ARP table structure
 * @param stats Pointer to store statistics
 * @return status_t Status code indicating success or failure
 */
status_t arp_get_stats(arp_table_t *table, arp_stats_t *stats) {
    if (!table || !stats) {
        LOG_ERROR("Invalid parameter(s) in arp_get_stats");
        return STATUS_INVALID_PARAMETER;
    }

    if (!table->initialized) {
        LOG_ERROR("ARP module not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    /* Update current entry count in stats */
    table->stats.current_entries = table->entry_count;
    
    /* Copy statistics */
    memcpy(stats, &table->stats, sizeof(arp_stats_t));
    
    return STATUS_SUCCESS;
}

/**
 * @brief Set the ARP cache timeout value
 *
 * @param table Pointer to ARP table structure
 * @param timeout_seconds Timeout value in seconds
 * @return status_t Status code indicating success or failure
 */
status_t arp_set_timeout(arp_table_t *table, uint32_t timeout_seconds) {
    if (!table) {
        LOG_ERROR("Invalid parameter: NULL ARP table pointer");
        return STATUS_INVALID_PARAMETER;
    }

    if (!table->initialized) {
        LOG_ERROR("ARP module not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    LOG_INFO("Setting ARP cache timeout to %d seconds", timeout_seconds);
    
    table->timeout = timeout_seconds;
    
    return STATUS_SUCCESS;
}

/**
 * @brief Get all entries in the ARP cache
 *
 * @param table Pointer to ARP table structure
 * @param entries Array to store entries
 * @param max_entries Maximum number of entries to retrieve
 * @param num_entries Pointer to store the actual number of entries retrieved
 * @return status_t Status code indicating success or failure
 */
status_t arp_get_all_entries(arp_table_t *table, arp_entry_info_t *entries, 
                            uint16_t max_entries, uint16_t *num_entries) {
    if (!table || !entries || !num_entries) {
        LOG_ERROR("Invalid parameter(s) in arp_get_all_entries");
        return STATUS_INVALID_PARAMETER;
    }

    if (!table->initialized) {
        LOG_ERROR("ARP module not initialized");
        return STATUS_NOT_INITIALIZED;
    }

    uint16_t count = 0;
    
    /* Iterate through all hash buckets */
    for (int i = 0; i < ARP_CACHE_SIZE && count < max_entries; i++) {
        arp_entry_t *entry = table->hash_table[i];
        
        while (entry && count < max_entries) {
            /* Copy entry information */
            memcpy(&entries[count].ip, &entry->ip, sizeof(ipv4_addr_t));
            memcpy(&entries[count].mac, &entry->mac, sizeof(mac_addr_t));
            entries[count].port_index = entry->port_index;
            entries[count].state = entry->state;
            entries[count].age = get_current_time() - entry->updated_time;
            
            count++;
            entry = entry->next;
        }
    }
    
    *num_entries = count;
    
    LOG_DEBUG("Retrieved %d ARP entries", count);
    return STATUS_SUCCESS;
}

/**
 * @brief Calculate hash for an IPv4 address
 *
 * @param ipv4 Pointer to IPv4 address
 * @return uint32_t Hash value
 */
static uint32_t hash_ipv4(const ipv4_addr_t *ipv4) {
    uint32_t ip_value;
    memcpy(&ip_value, ipv4, sizeof(ipv4_addr_t));
    
    /* A simple hash function based on multiplication and XOR */
    uint32_t hash = ip_value;
    hash = ((hash >> 16) ^ hash) * 0x45d9f3b;
    hash = ((hash >> 16) ^ hash) * 0x45d9f3b;
    hash = (hash >> 16) ^ hash;
    
    return hash;
}

/**
 * @brief Find an entry in the ARP cache
 *
 * @param table Pointer to ARP table structure
 * @param ipv4 IPv4 address to find
 * @return arp_entry_t* Pointer to the entry if found, NULL otherwise
 */
static arp_entry_t *arp_find_entry(arp_table_t *table, const ipv4_addr_t *ipv4) {
    uint32_t hash = hash_ipv4(ipv4) % ARP_CACHE_SIZE;
    arp_entry_t *entry = table->hash_table[hash];
    
    while (entry) {
        if (memcmp(&entry->ip, ipv4, sizeof(ipv4_addr_t)) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    
    return NULL;
}

/**
 * @brief Allocate a new ARP entry from the pool
 *
 * @param table Pointer to ARP table structure
 * @return arp_entry_t* Pointer to the allocated entry, NULL if pool is exhausted
 */
static arp_entry_t *arp_allocate_entry(arp_table_t *table) {
    /* If we've reached the maximum number of entries, recycle the oldest one */
    if (table->entry_count >= ARP_CACHE_SIZE) {
        uint32_t oldest_time = UINT32_MAX;
        arp_entry_t *oldest_entry = NULL;
        int oldest_bucket = -1;
        arp_entry_t *oldest_prev = NULL;
        
        /* Find the oldest entry */
        for (int i = 0; i < ARP_CACHE_SIZE; i++) {
            arp_entry_t *entry = table->hash_table[i];
            arp_entry_t *prev = NULL;
            
            while (entry) {
                if (entry->updated_time < oldest_time) {
                    oldest_time = entry->updated_time;
                    oldest_entry = entry;
                    oldest_bucket = i;
                    oldest_prev = prev;
                }
                
                prev = entry;
                entry = entry->next;
            }
        }
        
        /* Remove the oldest entry from its hash chain */
        if (oldest_entry) {
            if (oldest_prev) {
                oldest_prev->next = oldest_entry->next;
            } else {
                table->hash_table[oldest_bucket] = oldest_entry->next;
            }
            
            /* Clear the entry for reuse */
            memset(oldest_entry, 0, sizeof(arp_entry_t));
            table->entry_count--; /* Will be incremented again when entry is added */
            
            return oldest_entry;
        }
        
        /* If we couldn't find an entry to recycle, fail */
        LOG_ERROR("Failed to allocate ARP entry: cache full and no entry available for recycling");
        return NULL;
    }
    
    /* Allocate a new entry from the pool */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_entry_t *entry = &table->entry_pool[i];
        
        /* Find a free entry (not linked in any hash bucket) */
        bool is_used = false;
        for (int j = 0; j < ARP_CACHE_SIZE; j++) {
            arp_entry_t *bucket_entry = table->hash_table[j];
            
            while (bucket_entry) {
                if (bucket_entry == entry) {
                    is_used = true;
                    break;
                }
                bucket_entry = bucket_entry->next;
            }
            
            if (is_used) {
                break;
            }
        }
        
        if (!is_used) {
            /* Found a free entry */
            memset(entry, 0, sizeof(arp_entry_t));
            return entry;
        }
    }
    
    LOG_ERROR("Failed to allocate ARP entry: all entries in use");
    return NULL;
}

/**
 * @brief Free an ARP entry back to the pool
 *
 * @param table Pointer to ARP table structure
 * @param entry Pointer to the entry to free
 */
static void arp_free_entry(arp_table_t *table, arp_entry_t *entry) {
    /* Clear the entry */
    memset(entry, 0, sizeof(arp_entry_t));
    /* Note: We don't need to do anything else since the entry is already
     * removed from its hash chain by the caller */
}

/**
 * @brief Send an ARP request packet
 *
 * @param table Pointer to ARP table structure
 * @param target_ip Target IPv4 address to resolve
 * @param port_index Port index to send the request on
 * @return status_t Status code indicating success or failure
 */
static status_t arp_send_request(arp_table_t *table, const ipv4_addr_t *target_ip, uint16_t port_index) {
    if (!table || !target_ip) {
        LOG_ERROR("Invalid parameter(s) in arp_send_request");
        return STATUS_INVALID_PARAMETER;
    }
    
    LOG_DEBUG("Sending ARP request for IP: %d.%d.%d.%d on port %d", 
              target_ip->bytes[0], target_ip->bytes[1], target_ip->bytes[2], target_ip->bytes[3], port_index);
    
    /* Get our IP and MAC addresses for the outgoing interface */
    ipv4_addr_t our_ip;
    mac_addr_t our_mac;
    
    /* This should be a lookup to get our interface info */
    /* For now, using placeholder data */
    
    /* Create ARP request packet */
    arp_packet_t arp_packet;
    
    /* Fill in ARP packet fields */
    arp_packet.hw_type = htons(ARP_HARDWARE_TYPE_ETHERNET);
    arp_packet.protocol_type = htons(ARP_PROTOCOL_TYPE_IPV4);
    arp_packet.hw_addr_len = ARP_HARDWARE_SIZE_ETHERNET;
    arp_packet.proto_addr_len = ARP_PROTOCOL_SIZE_IPV4;
    arp_packet.operation = htons(ARP_OP_REQUEST);
    
    /* Sender details (our info) */
    memcpy(&arp_packet.sender_mac, &our_mac, sizeof(mac_addr_t));
    memcpy(&arp_packet.sender_ip, &our_ip, sizeof(ipv4_addr_t));
    
    /* Target details */
    memset(&arp_packet.target_mac, 0, sizeof(mac_addr_t)); /* Unknown, that's what we're asking for */
    memcpy(&arp_packet.target_ip, target_ip, sizeof(ipv4_addr_t));
    
    /* Create Ethernet frame to carry ARP packet */
    packet_t packet;
    
    /* Prepare packet */
    /* Set Ethernet broadcast destination */
    mac_addr_t broadcast_mac = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
    
    /* This assumes we have functions to create and send packets */
    status_t status = packet_create(&packet, sizeof(arp_packet_t));
    if (status != STATUS_SUCCESS) {
        LOG_ERROR("Failed to create packet for ARP request");
        return status;
    }
    
    /* Copy ARP packet into packet data */
    memcpy(packet.data, &arp_packet, sizeof(arp_packet_t));
    packet.length = sizeof(arp_packet_t);
    
    /* Send packet */
    status = port_send_packet(port_index, &packet, &our_mac, &broadcast_mac, ETHERTYPE_ARP);
    if (status != STATUS_SUCCESS) {
        LOG_ERROR("Failed to send ARP request packet");
    }
    
    /* Free packet resources */
    packet_destroy(&packet);
    
    return status;
}

/**
 * @brief Send an ARP reply packet
 *
 * @param table Pointer to ARP table structure
 * @param target_ip Target IPv4 address
 * @param target_mac Target MAC address
 * @param sender_ip Sender IPv4 address
 * @param port_index Port index to send the reply on
 * @return status_t Status code indicating success or failure
 */
static status_t arp_send_reply(arp_table_t *table, const ipv4_addr_t *target_ip, const mac_addr_t *target_mac,
                              const ipv4_addr_t *sender_ip, uint16_t port_index) {
    if (!table || !target_ip || !target_mac || !sender_ip) {
        LOG_ERROR("Invalid parameter(s) in arp_send_reply");
        return STATUS_INVALID_PARAMETER;
    }
    
    LOG_DEBUG("Sending ARP reply to IP: %d.%d.%d.%d on port %d", 
              target_ip->bytes[0], target_ip->bytes[1], target_ip->bytes[2], target_ip->bytes[3], port_index);
    
    /* Get our MAC address for the interface */
    mac_addr_t our_mac;
    
    /* This should be a lookup to get our interface MAC */
    /* For now, using placeholder data */
    
    /* Create ARP reply packet */
    arp_packet_t arp_packet;
    
    /* Fill in ARP packet fields */
    arp_packet.hw_type = htons(ARP_HARDWARE_TYPE_ETHERNET);
    arp_packet.protocol_type = htons(ARP_PROTOCOL_TYPE_IPV4);
    arp_packet.hw_addr_len = ARP_HARDWARE_SIZE_ETHERNET;
    arp_packet.proto_addr_len = ARP_PROTOCOL_SIZE_IPV4;
    arp_packet.operation = htons(ARP_OP_REPLY);
    
    /* Sender details (our info) */
    memcpy(&arp_packet.sender_mac, &our_mac, sizeof(mac_addr_t));
    memcpy(&arp_packet.sender_ip, sender_ip, sizeof(ipv4_addr_t));
    
    /* Target details */
    memcpy(&arp_packet.target_mac, target_mac, sizeof(mac_addr_t));
    memcpy(&arp_packet.target_ip, target_ip, sizeof(ipv4_addr_t));
    
    /* Create packet to carry ARP reply */
    packet_t packet;
    
    /* This assumes we have functions to create and send packets */
    status_t status = packet_create(&packet, sizeof(arp_packet_t));
    if (status != STATUS_SUCCESS) {
        LOG_ERROR("Failed to create packet for ARP reply");
        return status;
    }
    
    /* Copy ARP packet into packet data */
    memcpy(packet.data, &arp_packet, sizeof(arp_packet_t));
    packet.length = sizeof(arp_packet_t);
    
    /* Send packet directly to the target MAC */
    status = port_send_packet(port_index, &packet, &our_mac, target_mac, ETHERTYPE_ARP);
    if (status != STATUS_SUCCESS) {
        LOG_ERROR("Failed to send ARP reply packet");
    }
    
    /* Free packet resources */
    packet_destroy(&packet);
    
    return status;
}

/**
 * @brief Get current system time in seconds
 *
 * @return uint32_t Current time in seconds
 */
static uint32_t get_current_time(void) {
    /* This should use a system-specific function to get the current time */
    /* For now, returning a simple counter that increments with each call */
    static uint32_t time_counter = 0;
    return time_counter++;
}
