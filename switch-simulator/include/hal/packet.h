/**
 * @file packet.h
 * @brief Packet processing interface for switch simulator
 */

#ifndef SWITCH_SIM_PACKET_H
#define SWITCH_SIM_PACKET_H

#include "../common/types.h"
#include "../common/error_codes.h"

/**
 * @brief Packet direction
 */
typedef enum {
    PACKET_DIR_RX = 0,   /**< Received packet */
    PACKET_DIR_TX,       /**< Transmitted packet */
    PACKET_DIR_INTERNAL  /**< Internally generated packet */
} packet_direction_t;

/**
 * @brief Packet metadata structure
 */
typedef struct {
    port_id_t port;              /**< Source/destination port */
    packet_direction_t direction; /**< Packet direction */
    vlan_id_t vlan;              /**< VLAN ID */
    uint8_t priority;            /**< Priority/CoS value */
    mac_addr_t src_mac;          /**< Source MAC address */
    mac_addr_t dst_mac;          /**< Destination MAC address */
    uint16_t ethertype;          /**< Ethertype */
    bool is_tagged;              /**< VLAN tagged flag */
    bool is_dropped;             /**< Packet drop flag */
    uint32_t timestamp;          /**< Packet timestamp */
} packet_metadata_t;

/**
 * @brief Packet buffer structure
 */
typedef struct {
    uint8_t *data;               /**< Pointer to packet data */
    uint32_t size;               /**< Current size of packet data */
    uint32_t capacity;           /**< Total capacity of buffer */
    packet_metadata_t metadata;  /**< Packet metadata */
    void *user_data;             /**< User data pointer */
} packet_buffer_t;

/**
 * @brief Packet processing result codes
 */
typedef enum {
    PACKET_RESULT_FORWARD = 0,   /**< Forward packet normally */
    PACKET_RESULT_DROP,          /**< Drop packet */
    PACKET_RESULT_CONSUME,       /**< Packet consumed (e.g., by control plane) */
    PACKET_RESULT_RECIRCULATE    /**< Recirculate packet for additional processing */
} packet_result_t;

/**
 * @brief Packet processing callback function type
 * 
 * @param packet Packet buffer to process
 * @param user_data User data supplied during callback registration
 * @return packet_result_t Processing result
 */
typedef packet_result_t (*packet_process_cb_t)(packet_buffer_t *packet, void *user_data);

/**
 * @brief Initialize packet processing subsystem
 * 
 * @return status_t STATUS_SUCCESS if successful
 */
status_t packet_init(void);

/**
 * @brief Shutdown packet processing subsystem
 * 
 * @return status_t STATUS_SUCCESS if successful
 */
status_t packet_shutdown(void);

/**
 * @brief Allocate a new packet buffer
 * 
 * @param size Initial size of packet data
 * @return packet_buffer_t* Newly allocated packet buffer or NULL if failed
 */
packet_buffer_t* packet_buffer_alloc(uint32_t size);

/**
 * @brief Free a previously allocated packet buffer
 * 
 * @param packet Packet buffer to free
 */
void packet_buffer_free(packet_buffer_t *packet);

/**
 * @brief Clone a packet buffer
 * 
 * @param packet Source packet buffer
 * @return packet_buffer_t* New copy of packet buffer or NULL if failed
 */
packet_buffer_t* packet_buffer_clone(const packet_buffer_t *packet);

/**
 * @brief Resize packet buffer data section
 * 
 * @param packet Packet buffer to resize
 * @param new_size New size for packet data
 * @return status_t STATUS_SUCCESS if successful
 */
status_t packet_buffer_resize(packet_buffer_t *packet, uint32_t new_size);

/**
 * @brief Register a packet processing callback
 * 
 * @param callback Processing callback function
 * @param priority Processing priority (lower numbers = higher priority)
 * @param user_data User data to pass to callback
 * @param[out] handle_out Handle for registered callback
 * @return status_t STATUS_SUCCESS if successful
 */
status_t packet_register_processor(packet_process_cb_t callback, 
                                  uint32_t priority, 
                                  void *user_data, 
                                  uint32_t *handle_out);

/**
 * @brief Unregister a packet processing callback
 * 
 * @param handle Handle of registered callback
 * @return status_t STATUS_SUCCESS if successful
 */
status_t packet_unregister_processor(uint32_t handle);

/**
 * @brief Process a packet through registered processors
 * 
 * @param packet Packet buffer to process
 * @return packet_result_t Final processing result
 */
packet_result_t packet_process(packet_buffer_t *packet);

/**
 * @brief Inject a packet into the switch processing pipeline
 * 
 * @param packet Packet buffer to inject
 * @return status_t STATUS_SUCCESS if successful
 */
status_t packet_inject(packet_buffer_t *packet);

/**
 * @brief Transmit a packet out of a specific port
 * 
 * @param packet Packet buffer to transmit
 * @param port_id Destination port ID
 * @return status_t STATUS_SUCCESS if successful
 */
status_t packet_transmit(packet_buffer_t *packet, port_id_t port_id);

#endif /* SWITCH_SIM_PACKET_H */
