/**
 * @file packet.c
 * @brief Implementation of packet manipulation functions
 */

#include "hal/packet.h"
#include "common/logging.h"
#include <stdlib.h>
#include <string.h>

#define MODULE_NAME "PACKET"

error_code_t packet_queue_init(packet_queue_t *queue) {
    if (queue == NULL) {
        LOG_ERROR("Null queue pointer");
        return ERROR_INVALID_PARAMETER;
    }
    
    memset(queue, 0, sizeof(packet_queue_t));
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    
    return ERROR_NONE;
}

error_code_t packet_queue_deinit(packet_queue_t *queue) {
    if (queue == NULL) {
        LOG_ERROR("Null queue pointer");
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Free all packets in the queue */
    packet_t *current = queue->head;
    while (current != NULL) {
        packet_t *next = current->next;
        packet_free(current);
        current = next;
    }
    
    memset(queue, 0, sizeof(packet_queue_t));
    
    return ERROR_NONE;
}

error_code_t packet_queue_enqueue(packet_queue_t *queue, packet_t *packet) {
    if (queue == NULL) {
        LOG_ERROR("Null queue pointer");
        return ERROR_INVALID_PARAMETER;
    }
    
    if (packet == NULL) {
        LOG_ERROR("Null packet pointer");
        return ERROR_INVALID_PARAMETER;
    }
    
    packet->next = NULL;
    
    if (queue->tail == NULL) {
        /* Queue is empty */
        queue->head = packet;
        queue->tail = packet;
    } else {
        /* Add to the end of the queue */
        queue->tail->next = packet;
        queue->tail = packet;
    }
    
    queue->count++;
    
    return ERROR_NONE;
}

packet_t *packet_queue_dequeue(packet_queue_t *queue) {
    if (queue == NULL || queue->head == NULL) {
        return NULL;
    }
    
    packet_t *packet = queue->head;
    queue->head = packet->next;
    
    if (queue->head == NULL) {
        /* Queue is now empty */
        queue->tail = NULL;
    }
    
    packet->next = NULL;
    queue->count--;
    
    return packet;
}

packet_t *packet_alloc(uint16_t size) {
    if (size > MAX_PACKET_SIZE) {
        LOG_ERROR("Requested packet size too large: %u (max: %u)", size, MAX_PACKET_SIZE);
        return NULL;
    }
    
    packet_t *packet = (packet_t *)malloc(sizeof(packet_t));
    if (packet == NULL) {
        LOG_ERROR("Failed to allocate memory for packet");
        return NULL;
    }
    
    memset(packet, 0, sizeof(packet_t));
    
    if (size > 0) {
        packet->data = (uint8_t *)malloc(size);
        if (packet->data == NULL) {
            LOG_ERROR("Failed to allocate memory for packet data");
            free(packet);
            return NULL;
        }
        
        memset(packet->data, 0, size);
    }
    
    packet->size = size;
    packet->next = NULL;
    
    return packet;
}

void packet_free(packet_t *packet) {
    if (packet == NULL) {
        return;
    }
    
    if (packet->data != NULL) {
        free(packet->data);
    }
    
    free(packet);
}

packet_t *packet_clone(const packet_t *packet) {
    if (packet == NULL) {
        LOG_ERROR("Null packet pointer");
        return NULL;
    }
    
    packet_t *new_packet = packet_alloc(packet->size);
    if (new_packet == NULL) {
        LOG_ERROR("Failed to allocate memory for packet clone");
        return NULL;
    }
    
    /* Copy packet metadata */
    new_packet->vlan_id = packet->vlan_id;
    new_packet->priority = packet->priority;
    
    /* Copy packet data */
    if (packet->size > 0 && packet->data != NULL) {
        memcpy(new_packet->data, packet->data, packet->size);
    }
    
    return new_packet;
}

error_code_t packet_get_mac_addresses(const packet_t *packet, mac_addr_t dst_mac, mac_addr_t src_mac) {
    if (packet == NULL || packet->data == NULL) {
        LOG_ERROR("Null packet or packet data");
        return ERROR_INVALID_PARAMETER;
    }
    
    if (packet->size < 14) { /* Ethernet header size */
        LOG_ERROR("Packet too small for Ethernet header: %u bytes", packet->size);
        return ERROR_INVALID_PARAMETER;
    }
    
    if (dst_mac != NULL) {
        memcpy(dst_mac, packet->data, 6);
    }
    
    if (src_mac != NULL) {
        memcpy(src_mac, packet->data + 6, 6);
    }
    
    return ERROR_NONE;
}

error_code_t packet_set_mac_addresses(packet_t *packet, const mac_addr_t dst_mac, const mac_addr_t src_mac) {
    if (packet == NULL || packet->data == NULL) {
        LOG_ERROR("Null packet or packet data");
        return ERROR_INVALID_PARAMETER;
    }
    
    if (packet->size < 14) { /* Ethernet header size */
        LOG_ERROR("Packet too small for Ethernet header: %u bytes", packet->size);
        return ERROR_INVALID_PARAMETER;
    }
    
    if (dst_mac != NULL) {
        memcpy(packet->data, dst_mac, 6);
    }
    
    if (src_mac != NULL) {
        memcpy(packet->data + 6, src_mac, 6);
    }
    
    return ERROR_NONE;
}

uint16_t packet_get_ethertype(const packet_t *packet) {
    if (packet == NULL || packet->data == NULL || packet->size < 14) {
        return 0;
    }
    
    /* Ethertype is at offset 12 in the Ethernet header */
    return (uint16_t)((packet->data[12] << 8) | packet->data[13]);
}

void packet_set_ethertype(packet_t *packet, uint16_t ethertype) {
    if (packet == NULL || packet->data == NULL || packet->size < 14) {
        return;
    }
    
    /* Set Ethertype at offset 12 in the Ethernet header */
    packet->data[12] = (uint8_t)(ethertype >> 8);
    packet->data[13] = (uint8_t)(ethertype & 0xFF);
}

error_code_t packet_add_vlan_tag(packet_t *packet, uint16_t vlan_id, uint8_t priority) {
    if (packet == NULL || packet->data == NULL) {
        LOG_ERROR("Null packet or packet data");
        return ERROR_INVALID_PARAMETER;
    }
    
    if (packet->size < 14) { /* Ethernet header size */
        LOG_ERROR("Packet too small for Ethernet header: %u bytes", packet->size);
        return ERROR_INVALID_PARAMETER;
    }
    
    if (vlan_id > 4095) { /* 12 bits max */
        LOG_ERROR("Invalid VLAN ID: %u (max: 4095)", vlan_id);
        return ERROR_INVALID_PARAMETER;
    }
    
    if (priority > 7) { /* 3 bits max */
        LOG_ERROR("Invalid priority: %u (max: 7)", priority);
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Check if packet already has a VLAN tag (0x8100 at offset 12) */
    if (packet->data[12] == 0x81 && packet->data[13] == 0x00) {
        /* Update existing VLAN tag */
        packet->data[14] = (uint8_t)((priority << 5) | ((vlan_id >> 8) & 0x0F));
        packet->data[15] = (uint8_t)(vlan_id & 0xFF);
    } else {
        /* Need to insert a VLAN tag */
        if (packet->size + 4 > MAX_PACKET_SIZE) {
            LOG_ERROR("Packet would exceed max size after adding VLAN tag");
            return ERROR_PACKET_TOO_LARGE;
        }
        
        /* Allocate new data buffer with extra 4 bytes */
        uint8_t *new_data = (uint8_t *)malloc(packet->size + 4);
        if (new_data == NULL) {
            LOG_ERROR("Failed to allocate memory for VLAN tagged packet");
            return ERROR_MEMORY_ALLOCATION_FAILED;
        }
        
        /* Copy destination and source MAC addresses */
        memcpy(new_data, packet->data, 12);
        
        /* Insert VLAN tag */
        new_data[12] = 0x81;    /* TPID (0x8100) high byte */
        new_data[13] = 0x00;    /* TPID (0x8100) low byte */
        new_data[14] = (uint8_t)((priority << 5) | ((vlan_id >> 8) & 0x0F));
        new_data[15] = (uint8_t)(vlan_id & 0xFF);
        
        /* Copy rest of the packet */
        memcpy(new_data + 16, packet->data + 12, packet->size - 12);
        
        /* Replace old data with new data */
        free(packet->data);
        packet->data = new_data;
        packet->size += 4;
    }
    
    /* Update packet metadata */
    packet->vlan_id = vlan_id;
    packet->priority = priority;
    
    return ERROR_NONE;
}

error_code_t packet_remove_vlan_tag(packet_t *packet) {
    if (packet == NULL || packet->data == NULL) {
        LOG_ERROR("Null packet or packet data");
        return ERROR_INVALID_PARAMETER;
    }
    
    if (packet->size < 18) { /* Ethernet header + VLAN tag */
        LOG_ERROR("Packet too small for VLAN tag: %u bytes", packet->size);
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Check if packet has a VLAN tag (0x8100 at offset 12) */
    if (packet->data[12] != 0x81 || packet->data[13] != 0x00) {
        /* No VLAN tag to remove */
        return ERROR_NONE;
    }
    
    /* Save the ethertype */
    uint16_t ethertype = (uint16_t)((packet->data[16] << 8) | packet->data[17]);
    
    /* Move the data to remove the VLAN tag */
    memmove(packet->data + 12, packet->data + 16, packet->size - 16);
    
    /* Update the size */
    packet->size -= 4;
    
    /* Restore the ethertype */
    packet->data[12] = (uint8_t)(ethertype >> 8);
    packet->data[13] = (uint8_t)(ethertype & 0xFF);
    
    /* Clear VLAN metadata */
    packet->vlan_id = 0;
    packet->priority = 0;
    
    return ERROR_NONE;
}
