/**
 * @file vlan.c
 * @brief Implementation of VLAN functionality
 *
 * This file implements the VLAN functionality for the switch simulator,
 * including VLAN creation, port membership, tagging operations, and related functions.
 */
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "common/types.h"
#include "common/error_codes.h"
#include "common/logging.h"
#include "hal/port.h"
#include "hal/packet.h"
#include "l2/vlan.h"

/**
 * @brief Default VLAN ID
 */
#define VLAN_DEFAULT_ID 1

/**
 * @brief Maximum number of VLANs supported
 */
#define VLAN_MAX_COUNT 4096

/**
 * @brief Invalid VLAN ID
 */
#define VLAN_INVALID_ID 0xFFFF

/**
 * @brief Port VLAN mode
 */
typedef enum {
    PORT_VLAN_MODE_ACCESS,    // Access port (untagged)
    PORT_VLAN_MODE_TRUNK,     // Trunk port (tagged)
    PORT_VLAN_MODE_HYBRID     // Hybrid port (both tagged and untagged)
} port_vlan_mode_t;

/**
 * @brief VLAN entry structure
 */
typedef struct {
    vlan_id_t vlan_id;            // VLAN ID
    bool active;                  // Whether this VLAN is active
    char name[VLAN_NAME_MAX_LEN]; // VLAN name
    uint8_t *port_membership;     // Bitmap of member ports
    uint8_t *untagged_ports;      // Bitmap of untagged ports
} vlan_entry_t;

/**
 * @brief Port VLAN configuration structure
 */
typedef struct {
    port_vlan_mode_t mode;        // Port VLAN mode
    vlan_id_t access_vlan;        // Access VLAN ID for this port
    vlan_id_t native_vlan;        // Native VLAN ID for trunk/hybrid mode
    uint8_t *allowed_vlans;       // Bitmap of allowed VLANs for trunk/hybrid
} port_vlan_config_t;

/**
 * @brief VLAN global state structure
 */
typedef struct {
    bool initialized;                // Whether VLAN module is initialized
    vlan_entry_t *vlans;             // Array of VLAN entries
    port_vlan_config_t *port_configs; // Array of port VLAN configs
    uint32_t num_ports;              // Number of ports in the system
    vlan_id_t max_vlan_id;           // Maximum VLAN ID supported
} vlan_state_t;

/**
 * @brief The global VLAN state instance
 */
static vlan_state_t g_vlan_state;

/**
 * @brief Lock to protect VLAN operations
 * In a real implementation, this would be a hardware mutex or spinlock
 */
static volatile int g_vlan_lock = 0;

/**
 * @brief Acquire lock for VLAN operations
 */
static inline void vlan_acquire_lock(void) {
    // Simulate lock acquisition with atomic operation
    while (__sync_lock_test_and_set(&g_vlan_lock, 1)) {
        // Spin until acquired
    }
}

/**
 * @brief Release lock for VLAN operations
 */
static inline void vlan_release_lock(void) {
    // Simulate lock release with atomic operation
    __sync_lock_release(&g_vlan_lock);
}

/**
 * @brief Check if a VLAN ID is valid
 *
 * @param vlan_id VLAN ID to check
 * @return bool True if valid, false otherwise
 */
static bool is_vlan_id_valid(vlan_id_t vlan_id) {
    return (vlan_id > 0 && vlan_id < VLAN_MAX_COUNT);
}

/**
 * @brief Set bit in a bitmap
 *
 * @param bitmap Bitmap to modify
 * @param bit Bit position to set
 */
static void set_bitmap_bit(uint8_t *bitmap, uint32_t bit) {
    bitmap[bit / 8] |= (1 << (bit % 8));
}

/**
 * @brief Clear bit in a bitmap
 *
 * @param bitmap Bitmap to modify
 * @param bit Bit position to clear
 */
static void clear_bitmap_bit(uint8_t *bitmap, uint32_t bit) {
    bitmap[bit / 8] &= ~(1 << (bit % 8));
}

/**
 * @brief Test bit in a bitmap
 *
 * @param bitmap Bitmap to test
 * @param bit Bit position to test
 * @return bool True if bit is set, false otherwise
 */
static bool test_bitmap_bit(const uint8_t *bitmap, uint32_t bit) {
    return (bitmap[bit / 8] & (1 << (bit % 8))) != 0;
}

/**
 * @brief Initialize the VLAN module
 *
 * @param num_ports Number of ports in the system
 * @return error_code_t Error code
 */
error_code_t vlan_init(uint32_t num_ports) {
    uint32_t bitmap_size;
    uint32_t i;
    
    vlan_acquire_lock();
    
    if (g_vlan_state.initialized) {
        LOG_WARN("VLAN: Module already initialized");
        vlan_release_lock();
        return ERROR_ALREADY_INITIALIZED;
    }
    
    if (num_ports == 0) {
        LOG_ERROR("VLAN: Invalid number of ports");
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    // Allocate VLAN entries
    g_vlan_state.vlans = (vlan_entry_t *)calloc(VLAN_MAX_COUNT, sizeof(vlan_entry_t));
    if (!g_vlan_state.vlans) {
        LOG_ERROR("VLAN: Failed to allocate VLAN entries");
        vlan_release_lock();
        return ERROR_MEMORY_ALLOCATION;
    }
    
    // Calculate bitmap size in bytes (ceiling of num_ports / 8)
    bitmap_size = (num_ports + 7) / 8;
    
    // Initialize each VLAN entry
    for (i = 0; i < VLAN_MAX_COUNT; i++) {
        g_vlan_state.vlans[i].vlan_id = i;
        g_vlan_state.vlans[i].active = false;
        strncpy(g_vlan_state.vlans[i].name, "", VLAN_NAME_MAX_LEN);
        
        g_vlan_state.vlans[i].port_membership = (uint8_t *)calloc(bitmap_size, sizeof(uint8_t));
        g_vlan_state.vlans[i].untagged_ports = (uint8_t *)calloc(bitmap_size, sizeof(uint8_t));
        
        if (!g_vlan_state.vlans[i].port_membership || !g_vlan_state.vlans[i].untagged_ports) {
            LOG_ERROR("VLAN: Failed to allocate bitmaps for VLAN %d", i);
            vlan_release_lock();
            return ERROR_MEMORY_ALLOCATION;
        }
    }
    
    // Allocate port VLAN configs
    g_vlan_state.port_configs = (port_vlan_config_t *)calloc(num_ports, sizeof(port_vlan_config_t));
    if (!g_vlan_state.port_configs) {
        LOG_ERROR("VLAN: Failed to allocate port configs");
        vlan_release_lock();
        return ERROR_MEMORY_ALLOCATION;
    }
    
    // Initialize each port VLAN config
    for (i = 0; i < num_ports; i++) {
        g_vlan_state.port_configs[i].mode = PORT_VLAN_MODE_ACCESS;
        g_vlan_state.port_configs[i].access_vlan = VLAN_DEFAULT_ID;
        g_vlan_state.port_configs[i].native_vlan = VLAN_DEFAULT_ID;
        
        g_vlan_state.port_configs[i].allowed_vlans = (uint8_t *)calloc(bitmap_size, sizeof(uint8_t));
        if (!g_vlan_state.port_configs[i].allowed_vlans) {
            LOG_ERROR("VLAN: Failed to allocate allowed VLANs bitmap for port %d", i);
            vlan_release_lock();
            return ERROR_MEMORY_ALLOCATION;
        }
        
        // By default, allow all VLANs on trunk ports
        memset(g_vlan_state.port_configs[i].allowed_vlans, 0xFF, bitmap_size);
    }
    
    // Create default VLAN 1
    g_vlan_state.vlans[VLAN_DEFAULT_ID].active = true;
    strncpy(g_vlan_state.vlans[VLAN_DEFAULT_ID].name, "default", VLAN_NAME_MAX_LEN);
    
    // Add all ports to default VLAN as untagged
    for (i = 0; i < num_ports; i++) {
        set_bitmap_bit(g_vlan_state.vlans[VLAN_DEFAULT_ID].port_membership, i);
        set_bitmap_bit(g_vlan_state.vlans[VLAN_DEFAULT_ID].untagged_ports, i);
    }
    
    g_vlan_state.num_ports = num_ports;
    g_vlan_state.max_vlan_id = VLAN_MAX_COUNT - 1;
    g_vlan_state.initialized = true;
    
    LOG_INFO("VLAN: Module initialized with %d ports", num_ports);
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Clean up the VLAN module
 *
 * @return error_code_t Error code
 */
error_code_t vlan_cleanup(void) {
    uint32_t i;
    
    vlan_acquire_lock();
    
    if (!g_vlan_state.initialized) {
        LOG_WARN("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }
    
    // Free VLAN bitmaps
    for (i = 0; i < VLAN_MAX_COUNT; i++) {
        if (g_vlan_state.vlans[i].port_membership) {
            free(g_vlan_state.vlans[i].port_membership);
        }
        if (g_vlan_state.vlans[i].untagged_ports) {
            free(g_vlan_state.vlans[i].untagged_ports);
        }
    }
    
    // Free port VLAN configs
    for (i = 0; i < g_vlan_state.num_ports; i++) {
        if (g_vlan_state.port_configs[i].allowed_vlans) {
            free(g_vlan_state.port_configs[i].allowed_vlans);
        }
    }
    
    // Free main structures
    free(g_vlan_state.vlans);
    free(g_vlan_state.port_configs);
    
    // Reset global state
    memset(&g_vlan_state, 0, sizeof(vlan_state_t));
    
    LOG_INFO("VLAN: Module cleaned up");
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Create a new VLAN
 *
 * @param vlan_id VLAN ID to create
 * @param vlan_name Name of the VLAN (optional)
 * @return error_code_t Error code
 */
error_code_t vlan_create(vlan_id_t vlan_id, const char *vlan_name) {
    vlan_acquire_lock();
    
    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!is_vlan_id_valid(vlan_id)) {
        LOG_ERROR("VLAN: Invalid VLAN ID %d", vlan_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    if (g_vlan_state.vlans[vlan_id].active) {
        LOG_WARN("VLAN: VLAN %d already exists", vlan_id);
        vlan_release_lock();
        return ERROR_ALREADY_EXISTS;
    }
    
    // Set up the VLAN
    g_vlan_state.vlans[vlan_id].active = true;
    
    // Clear any existing membership
    memset(g_vlan_state.vlans[vlan_id].port_membership, 0, (g_vlan_state.num_ports + 7) / 8);
    memset(g_vlan_state.vlans[vlan_id].untagged_ports, 0, (g_vlan_state.num_ports + 7) / 8);
    
    // Set VLAN name if provided
    if (vlan_name && strlen(vlan_name) > 0) {
        strncpy(g_vlan_state.vlans[vlan_id].name, vlan_name, VLAN_NAME_MAX_LEN);
    } else {
        // Default name is "VLAN<id>"
        snprintf(g_vlan_state.vlans[vlan_id].name, VLAN_NAME_MAX_LEN, "VLAN%d", vlan_id);
    }
    
    LOG_INFO("VLAN: Created VLAN %d '%s'", vlan_id, g_vlan_state.vlans[vlan_id].name);
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Delete a VLAN
 *
 * @param vlan_id VLAN ID to delete
 * @return error_code_t Error code
 */
error_code_t vlan_delete(vlan_id_t vlan_id) {
    uint32_t i;
    
    vlan_acquire_lock();
    
    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!is_vlan_id_valid(vlan_id)) {
        LOG_ERROR("VLAN: Invalid VLAN ID %d", vlan_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    // Cannot delete default VLAN
    if (vlan_id == VLAN_DEFAULT_ID) {
        LOG_ERROR("VLAN: Cannot delete default VLAN %d", VLAN_DEFAULT_ID);
        vlan_release_lock();
        return ERROR_FORBIDDEN;
    }
    
    if (!g_vlan_state.vlans[vlan_id].active) {
        LOG_WARN("VLAN: VLAN %d does not exist", vlan_id);
        vlan_release_lock();
        return ERROR_NOT_FOUND;
    }
    
    // Update port configurations that use this VLAN
    for (i = 0; i < g_vlan_state.num_ports; i++) {
        // If port is using this VLAN as access VLAN, move it to default VLAN
        if (g_vlan_state.port_configs[i].access_vlan == vlan_id) {
            LOG_INFO("VLAN: Port %d moved from deleted VLAN %d to default VLAN", i, vlan_id);
            g_vlan_state.port_configs[i].access_vlan = VLAN_DEFAULT_ID;
            
            // Add port to default VLAN
            set_bitmap_bit(g_vlan_state.vlans[VLAN_DEFAULT_ID].port_membership, i);
            set_bitmap_bit(g_vlan_state.vlans[VLAN_DEFAULT_ID].untagged_ports, i);
        }
        
        // If port is using this VLAN as native VLAN, move it to default VLAN
        if (g_vlan_state.port_configs[i].native_vlan == vlan_id) {
            LOG_INFO("VLAN: Port %d native VLAN changed from %d to default VLAN", i, vlan_id);
            g_vlan_state.port_configs[i].native_vlan = VLAN_DEFAULT_ID;
        }
        
        // Remove this VLAN from allowed VLANs on trunk ports
        clear_bitmap_bit(g_vlan_state.port_configs[i].allowed_vlans, vlan_id);
    }
    
    // Mark VLAN as inactive
    g_vlan_state.vlans[vlan_id].active = false;
    strncpy(g_vlan_state.vlans[vlan_id].name, "", VLAN_NAME_MAX_LEN);
    
    // Clear all port memberships
    memset(g_vlan_state.vlans[vlan_id].port_membership, 0, (g_vlan_state.num_ports + 7) / 8);
    memset(g_vlan_state.vlans[vlan_id].untagged_ports, 0, (g_vlan_state.num_ports + 7) / 8);
    
    LOG_INFO("VLAN: Deleted VLAN %d", vlan_id);
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Add a port to a VLAN
 *
 * @param vlan_id VLAN ID
 * @param port_id Port ID
 * @param tagged Whether the port should be tagged in this VLAN
 * @return error_code_t Error code
 */
error_code_t vlan_add_port(vlan_id_t vlan_id, port_id_t port_id, bool tagged) {
    vlan_acquire_lock();
    
    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!is_vlan_id_valid(vlan_id)) {
        LOG_ERROR("VLAN: Invalid VLAN ID %d", vlan_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    if (!g_vlan_state.vlans[vlan_id].active) {
        LOG_ERROR("VLAN: VLAN %d does not exist", vlan_id);
        vlan_release_lock();
        return ERROR_NOT_FOUND;
    }
    
    if (!is_port_valid(port_id) || port_id >= g_vlan_state.num_ports) {
        LOG_ERROR("VLAN: Invalid port ID %d", port_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    // Add port to VLAN membership
    set_bitmap_bit(g_vlan_state.vlans[vlan_id].port_membership, port_id);
    
    // Set tagging mode for port
    if (tagged) {
        clear_bitmap_bit(g_vlan_state.vlans[vlan_id].untagged_ports, port_id);
        LOG_INFO("VLAN: Added port %d to VLAN %d as tagged", port_id, vlan_id);
    } else {
        set_bitmap_bit(g_vlan_state.vlans[vlan_id].untagged_ports, port_id);
        LOG_INFO("VLAN: Added port %d to VLAN %d as untagged", port_id, vlan_id);
    }
    
    // Add VLAN to allowed VLANs for this port
    set_bitmap_bit(g_vlan_state.port_configs[port_id].allowed_vlans, vlan_id);
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Remove a port from a VLAN
 *
 * @param vlan_id VLAN ID
 * @param port_id Port ID
 * @return error_code_t Error code
 */
error_code_t vlan_remove_port(vlan_id_t vlan_id, port_id_t port_id) {
    vlan_acquire_lock();
    
    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!is_vlan_id_valid(vlan_id)) {
        LOG_ERROR("VLAN: Invalid VLAN ID %d", vlan_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    if (!g_vlan_state.vlans[vlan_id].active) {
        LOG_ERROR("VLAN: VLAN %d does not exist", vlan_id);
        vlan_release_lock();
        return ERROR_NOT_FOUND;
    }
    
    if (!is_port_valid(port_id) || port_id >= g_vlan_state.num_ports) {
        LOG_ERROR("VLAN: Invalid port ID %d", port_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    // Default VLAN special case
    if (vlan_id == VLAN_DEFAULT_ID) {
        // Cannot remove a port from default VLAN if it's in access mode and using default VLAN
        if (g_vlan_state.port_configs[port_id].mode == PORT_VLAN_MODE_ACCESS && 
            g_vlan_state.port_configs[port_id].access_vlan == VLAN_DEFAULT_ID) {
            LOG_ERROR("VLAN: Cannot remove port %d from default VLAN while in access mode", port_id);
            vlan_release_lock();
            return ERROR_FORBIDDEN;
        }
    }
    
    // Remove port from VLAN
    clear_bitmap_bit(g_vlan_state.vlans[vlan_id].port_membership, port_id);
    clear_bitmap_bit(g_vlan_state.vlans[vlan_id].untagged_ports, port_id);
    
    // Remove VLAN from allowed VLANs for trunk ports
    clear_bitmap_bit(g_vlan_state.port_configs[port_id].allowed_vlans, vlan_id);
    
    LOG_INFO("VLAN: Removed port %d from VLAN %d", port_id, vlan_id);
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Configure a port as an access port
 *
 * @param port_id Port ID
 * @param vlan_id VLAN ID for this access port
 * @return error_code_t Error code
 */
error_code_t vlan_set_port_access_mode(port_id_t port_id, vlan_id_t vlan_id) {
    vlan_acquire_lock();
    
    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!is_vlan_id_valid(vlan_id)) {
        LOG_ERROR("VLAN: Invalid VLAN ID %d", vlan_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    if (!g_vlan_state.vlans[vlan_id].active) {
        LOG_ERROR("VLAN: VLAN %d does not exist", vlan_id);
        vlan_release_lock();
        return ERROR_NOT_FOUND;
    }
    
    if (!is_port_valid(port_id) || port_id >= g_vlan_state.num_ports) {
        LOG_ERROR("VLAN: Invalid port ID %d", port_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    // Get previous access VLAN
    vlan_id_t old_vlan = g_vlan_state.port_configs[port_id].access_vlan;
    
    // Remove port from previous access VLAN if it's different
    if (old_vlan != vlan_id && old_vlan != VLAN_INVALID_ID) {
        clear_bitmap_bit(g_vlan_state.vlans[old_vlan].port_membership, port_id);
        clear_bitmap_bit(g_vlan_state.vlans[old_vlan].untagged_ports, port_id);
    }
    
    // Update port configuration
    g_vlan_state.port_configs[port_id].mode = PORT_VLAN_MODE_ACCESS;
    g_vlan_state.port_configs[port_id].access_vlan = vlan_id;
    
    // Add port to new access VLAN as untagged
    set_bitmap_bit(g_vlan_state.vlans[vlan_id].port_membership, port_id);
    set_bitmap_bit(g_vlan_state.vlans[vlan_id].untagged_ports, port_id);
    
    LOG_INFO("VLAN: Port %d set to access mode with VLAN %d", port_id, vlan_id);
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Configure a port as a trunk port
 *
 * @param port_id Port ID
 * @param native_vlan Native VLAN ID (for untagged frames)
 * @return error_code_t Error code
 */
error_code_t vlan_set_port_trunk_mode(port_id_t port_id, vlan_id_t native_vlan) {
    vlan_acquire_lock();
    
    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!is_port_valid(port_id) || port_id >= g_vlan_state.num_ports) {
        LOG_ERROR("VLAN: Invalid port ID %d", port_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    // If native VLAN is specified, check it
    if (native_vlan != VLAN_INVALID_ID) {
        if (!is_vlan_id_valid(native_vlan)) {
            LOG_ERROR("VLAN: Invalid native VLAN ID %d", native_vlan);
            vlan_release_lock();
            return ERROR_INVALID_PARAMETER;
        }
        
        if (!g_vlan_state.vlans[native_vlan].active) {
            LOG_ERROR("VLAN: Native VLAN %d does not exist", native_vlan);
            vlan_release_lock();
            return ERROR_NOT_FOUND;
        }
    } else {
        // Default to VLAN 1 if not specified
        native_vlan = VLAN_DEFAULT_ID;
    }
    
    // Get previous access VLAN if port was in access mode
    if (g_vlan_state.port_configs[port_id].mode == PORT_VLAN_MODE_ACCESS) {
        vlan_id_t old_vlan = g_vlan_state.port_configs[port_id].access_vlan;
        
        // Remove port from previous access VLAN membership
        clear_bitmap_bit(g_vlan_state.vlans[old_vlan].port_membership, port_id);
        clear_bitmap_bit(g_vlan_state.vlans[old_vlan].untagged_ports, port_id);
    }
    
    // Update port configuration
    g_vlan_state.port_configs[port_id].mode = PORT_VLAN_MODE_TRUNK;
    g_vlan_state.port_configs[port_id].native_vlan = native_vlan;
    
    // Add port to native VLAN as untagged
    set_bitmap_bit(g_vlan_state.vlans[native_vlan].port_membership, port_id);
    set_bitmap_bit(g_vlan_state.vlans[native_vlan].untagged_ports, port_id);
    
    LOG_INFO("VLAN: Port %d set to trunk mode with native VLAN %d", port_id, native_vlan);
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Configure allowed VLANs on a trunk port
 *
 * @param port_id Port ID
 * @param vlan_id VLAN ID to allow or disallow
 * @param allowed Whether the VLAN is allowed
 * @return error_code_t Error code
 */
error_code_t vlan_set_trunk_allowed_vlan(port_id_t port_id, vlan_id_t vlan_id, bool allowed) {
    vlan_acquire_lock();
    
    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!is_vlan_id_valid(vlan_id)) {
        LOG_ERROR("VLAN: Invalid VLAN ID %d", vlan_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    if (!is_port_valid(port_id) || port_id >= g_vlan_state.num_ports) {
        LOG_ERROR("VLAN: Invalid port ID %d", port_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    // Check if port is in trunk mode
    if (g_vlan_state.port_configs[port_id].mode != PORT_VLAN_MODE_TRUNK &&
        g_vlan_state.port_configs[port_id].mode != PORT_VLAN_MODE_HYBRID) {
        LOG_ERROR("VLAN: Port %d is not in trunk or hybrid mode", port_id);
        vlan_release_lock();
        return ERROR_INVALID_STATE;
    }

    // Cannot disallow native VLAN
    if (!allowed && vlan_id == g_vlan_state.port_configs[port_id].native_vlan) {
        LOG_ERROR("VLAN: Cannot disallow native VLAN %d on port %d", vlan_id, port_id);
        vlan_release_lock();
        return ERROR_FORBIDDEN;
    }

    if (allowed) {
        // Add VLAN to allowed list
        set_bitmap_bit(g_vlan_state.port_configs[port_id].allowed_vlans, vlan_id);
        LOG_INFO("VLAN: Allowed VLAN %d on trunk port %d", vlan_id, port_id);
    } else {
        // Remove VLAN from allowed list
        clear_bitmap_bit(g_vlan_state.port_configs[port_id].allowed_vlans, vlan_id);
        LOG_INFO("VLAN: Disallowed VLAN %d on trunk port %d", vlan_id, port_id);
    }

    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Configure a port as a hybrid port
 *
 * @param port_id Port ID
 * @param native_vlan Native VLAN ID (for untagged frames)
 * @return error_code_t Error code
 */
error_code_t vlan_set_port_hybrid_mode(port_id_t port_id, vlan_id_t native_vlan) {
    vlan_acquire_lock();

    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }

    if (!is_port_valid(port_id) || port_id >= g_vlan_state.num_ports) {
        LOG_ERROR("VLAN: Invalid port ID %d", port_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    // If native VLAN is specified, check it
    if (native_vlan != VLAN_INVALID_ID) {
        if (!is_vlan_id_valid(native_vlan)) {
            LOG_ERROR("VLAN: Invalid native VLAN ID %d", native_vlan);
            vlan_release_lock();
            return ERROR_INVALID_PARAMETER;
        }

        if (!g_vlan_state.vlans[native_vlan].active) {
            LOG_ERROR("VLAN: Native VLAN %d does not exist", native_vlan);
            vlan_release_lock();
            return ERROR_NOT_FOUND;
        }
    } else {
        // Default to VLAN 1 if not specified
        native_vlan = VLAN_DEFAULT_ID;
    }

    // Get previous access VLAN if port was in access mode
    if (g_vlan_state.port_configs[port_id].mode == PORT_VLAN_MODE_ACCESS) {
        vlan_id_t old_vlan = g_vlan_state.port_configs[port_id].access_vlan;

        // Remove port from previous access VLAN membership
        clear_bitmap_bit(g_vlan_state.vlans[old_vlan].port_membership, port_id);
        clear_bitmap_bit(g_vlan_state.vlans[old_vlan].untagged_ports, port_id);
    }

    // Update port configuration
    g_vlan_state.port_configs[port_id].mode = PORT_VLAN_MODE_HYBRID;
    g_vlan_state.port_configs[port_id].native_vlan = native_vlan;

    // Add port to native VLAN as untagged
    set_bitmap_bit(g_vlan_state.vlans[native_vlan].port_membership, port_id);
    set_bitmap_bit(g_vlan_state.vlans[native_vlan].untagged_ports, port_id);

    LOG_INFO("VLAN: Port %d set to hybrid mode with native VLAN %d", port_id, native_vlan);

    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Set the tagging mode for a port in a specific VLAN
 *
 * @param port_id Port ID
 * @param vlan_id VLAN ID
 * @param tagged Whether the port should be tagged in this VLAN
 * @return error_code_t Error code
 */
error_code_t vlan_set_port_tagging(port_id_t port_id, vlan_id_t vlan_id, bool tagged) {
    vlan_acquire_lock();

    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }

    if (!is_vlan_id_valid(vlan_id)) {
        LOG_ERROR("VLAN: Invalid VLAN ID %d", vlan_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    if (!g_vlan_state.vlans[vlan_id].active) {
        LOG_ERROR("VLAN: VLAN %d does not exist", vlan_id);
        vlan_release_lock();
        return ERROR_NOT_FOUND;
    }

    if (!is_port_valid(port_id) || port_id >= g_vlan_state.num_ports) {
        LOG_ERROR("VLAN: Invalid port ID %d", port_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    // Check if port is a member of the VLAN
    if (!test_bitmap_bit(g_vlan_state.vlans[vlan_id].port_membership, port_id)) {
        LOG_ERROR("VLAN: Port %d is not a member of VLAN %d", port_id, vlan_id);
        vlan_release_lock();
        return ERROR_NOT_FOUND;
    }

    // Special case: In access mode, ports are always untagged in their access VLAN
    if (g_vlan_state.port_configs[port_id].mode == PORT_VLAN_MODE_ACCESS) {
        if (vlan_id == g_vlan_state.port_configs[port_id].access_vlan && tagged) {
            LOG_ERROR("VLAN: Cannot set port %d as tagged in its access VLAN %d", port_id, vlan_id);
            vlan_release_lock();
            return ERROR_FORBIDDEN;
        }
    }

    // Special case: In trunk/hybrid mode, ports are always untagged in their native VLAN
    if ((g_vlan_state.port_configs[port_id].mode == PORT_VLAN_MODE_TRUNK ||
         g_vlan_state.port_configs[port_id].mode == PORT_VLAN_MODE_HYBRID) &&
        vlan_id == g_vlan_state.port_configs[port_id].native_vlan && tagged) {
        LOG_ERROR("VLAN: Cannot set port %d as tagged in its native VLAN %d", port_id, vlan_id);
        vlan_release_lock();
        return ERROR_FORBIDDEN;
    }

    // Set tagging mode
    if (tagged) {
        clear_bitmap_bit(g_vlan_state.vlans[vlan_id].untagged_ports, port_id);
        LOG_INFO("VLAN: Port %d set to tagged in VLAN %d", port_id, vlan_id);
    } else {
        set_bitmap_bit(g_vlan_state.vlans[vlan_id].untagged_ports, port_id);
        LOG_INFO("VLAN: Port %d set to untagged in VLAN %d", port_id, vlan_id);
    }

    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Check if a port is a member of a VLAN
 *
 * @param port_id Port ID
 * @param vlan_id VLAN ID
 * @param is_member Output parameter to store membership status
 * @return error_code_t Error code
 */
error_code_t vlan_is_port_member(port_id_t port_id, vlan_id_t vlan_id, bool *is_member) {
    vlan_acquire_lock();

    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }

    if (!is_vlan_id_valid(vlan_id)) {
        LOG_ERROR("VLAN: Invalid VLAN ID %d", vlan_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    if (!g_vlan_state.vlans[vlan_id].active) {
        LOG_ERROR("VLAN: VLAN %d does not exist", vlan_id);
        vlan_release_lock();
        return ERROR_NOT_FOUND;
    }

    if (!is_port_valid(port_id) || port_id >= g_vlan_state.num_ports) {
        LOG_ERROR("VLAN: Invalid port ID %d", port_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    if (!is_member) {
        LOG_ERROR("VLAN: Output parameter is NULL");
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    *is_member = test_bitmap_bit(g_vlan_state.vlans[vlan_id].port_membership, port_id);

    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Check if a port is tagged in a VLAN
 *
 * @param port_id Port ID
 * @param vlan_id VLAN ID
 * @param is_tagged Output parameter to store tagging status
 * @return error_code_t Error code
 */
error_code_t vlan_is_port_tagged(port_id_t port_id, vlan_id_t vlan_id, bool *is_tagged) {
    vlan_acquire_lock();

    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }

    if (!is_vlan_id_valid(vlan_id)) {
        LOG_ERROR("VLAN: Invalid VLAN ID %d", vlan_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    if (!g_vlan_state.vlans[vlan_id].active) {
        LOG_ERROR("VLAN: VLAN %d does not exist", vlan_id);
        vlan_release_lock();
        return ERROR_NOT_FOUND;
    }

    if (!is_port_valid(port_id) || port_id >= g_vlan_state.num_ports) {
        LOG_ERROR("VLAN: Invalid port ID %d", port_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    if (!is_tagged) {
        LOG_ERROR("VLAN: Output parameter is NULL");
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    // Check if port is a member of the VLAN
    if (!test_bitmap_bit(g_vlan_state.vlans[vlan_id].port_membership, port_id)) {
        LOG_ERROR("VLAN: Port %d is not a member of VLAN %d", port_id, vlan_id);
        vlan_release_lock();
        return ERROR_NOT_FOUND;
    }

    // Port is tagged if it's a member and not in the untagged ports bitmap
    *is_tagged = !test_bitmap_bit(g_vlan_state.vlans[vlan_id].untagged_ports, port_id);

    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Get the VLAN mode of a port
 *
 * @param port_id Port ID
 * @param mode Output parameter to store the port VLAN mode
 * @return error_code_t Error code
 */
error_code_t vlan_get_port_mode(port_id_t port_id, port_vlan_mode_t *mode) {
    vlan_acquire_lock();

    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }

    if (!is_port_valid(port_id) || port_id >= g_vlan_state.num_ports) {
        LOG_ERROR("VLAN: Invalid port ID %d", port_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    if (!mode) {
        LOG_ERROR("VLAN: Output parameter is NULL");
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    *mode = g_vlan_state.port_configs[port_id].mode;

    vlan_release_lock();
    return ERROR_SUCCESS;
}


// =======================================================================================
/**
 * @brief Get the access VLAN of a port
 *
 * @param port_id Port ID
 * @param vlan_id Output parameter to store the access VLAN ID
 * @return error_code_t Error code
 */
error_code_t vlan_get_port_access_vlan(port_id_t port_id, vlan_id_t *vlan_id) {
    vlan_acquire_lock();

    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }

    if (!is_port_valid(port_id) || port_id >= g_vlan_state.num_ports) {
        LOG_ERROR("VLAN: Invalid port ID %d", port_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    if (!vlan_id) {
      LOG_ERROR("VLAN: Output parameter is NULL");
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
   
    if (g_vlan_state.port_configs[port_id].mode != PORT_VLAN_MODE_ACCESS) {
        LOG_WARN("VLAN: Port %d is not in access mode", port_id);
        *vlan_id = VLAN_INVALID_ID;
    } else {
        *vlan_id = g_vlan_state.port_configs[port_id].access_vlan;
    }
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}
// =======================================================================================

/**
 * @brief Get the native VLAN of a trunk/hybrid port
 *
 * @param port_id Port ID
 * @param vlan_id Output parameter to store the native VLAN ID
 * @return error_code_t Error code
 */
error_code_t vlan_get_port_native_vlan(port_id_t port_id, vlan_id_t *vlan_id) {
    vlan_acquire_lock();
    
    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!is_port_valid(port_id) || port_id >= g_vlan_state.num_ports) {
        LOG_ERROR("VLAN: Invalid port ID %d", port_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    if (!vlan_id) {
        LOG_ERROR("VLAN: Output parameter is NULL");
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    if (g_vlan_state.port_configs[port_id].mode != PORT_VLAN_MODE_TRUNK && 
        g_vlan_state.port_configs[port_id].mode != PORT_VLAN_MODE_HYBRID) {
        LOG_WARN("VLAN: Port %d is not in trunk or hybrid mode", port_id);
        *vlan_id = VLAN_INVALID_ID;
    } else {
        *vlan_id = g_vlan_state.port_configs[port_id].native_vlan;
    }
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Check if a VLAN is allowed on a trunk port
 *
 * @param port_id Port ID
 * @param vlan_id VLAN ID
 * @param is_allowed Output parameter to store whether the VLAN is allowed
 * @return error_code_t Error code
 */
error_code_t vlan_is_trunk_vlan_allowed(port_id_t port_id, vlan_id_t vlan_id, bool *is_allowed) {
    vlan_acquire_lock();
    
    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!is_vlan_id_valid(vlan_id)) {
        LOG_ERROR("VLAN: Invalid VLAN ID %d", vlan_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    if (!is_port_valid(port_id) || port_id >= g_vlan_state.num_ports) {
        LOG_ERROR("VLAN: Invalid port ID %d", port_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    if (!is_allowed) {
        LOG_ERROR("VLAN: Output parameter is NULL");
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    if (g_vlan_state.port_configs[port_id].mode != PORT_VLAN_MODE_TRUNK && 
        g_vlan_state.port_configs[port_id].mode != PORT_VLAN_MODE_HYBRID) {
        LOG_WARN("VLAN: Port %d is not in trunk or hybrid mode", port_id);
        *is_allowed = false;
    } else {
        *is_allowed = test_bitmap_bit(g_vlan_state.port_configs[port_id].allowed_vlans, vlan_id);
    }
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Get the VLAN for an incoming packet
 *
 * @param port_id Port on which the packet was received
 * @param packet Packet to process
 * @param vlan_id Output parameter to store the assigned VLAN ID
 * @return error_code_t Error code
 */
error_code_t vlan_get_packet_vlan(port_id_t port_id, const packet_t *packet, vlan_id_t *vlan_id) {
    vlan_id_t vid;
    bool has_vlan_tag;
    
    vlan_acquire_lock();
    
    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!is_port_valid(port_id) || port_id >= g_vlan_state.num_ports) {
        LOG_ERROR("VLAN: Invalid port ID %d", port_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    if (!packet || !vlan_id) {
        LOG_ERROR("VLAN: Invalid packet or output parameter");
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    // Check if the packet has a VLAN tag
    has_vlan_tag = packet_has_vlan_tag(packet, &vid);
    
    if (has_vlan_tag) {
        // Check if the VLAN is valid
        if (!is_vlan_id_valid(vid)) {
            LOG_ERROR("VLAN: Packet has invalid VLAN ID %d", vid);
            vlan_release_lock();
            return ERROR_INVALID_PACKET;
        }
        
        // Check if the VLAN exists
        if (!g_vlan_state.vlans[vid].active) {
            LOG_ERROR("VLAN: Packet has nonexistent VLAN ID %d", vid);
            vlan_release_lock();
            return ERROR_INVALID_PACKET;
        }
        
        // Check if the port is a member of this VLAN
        if (!test_bitmap_bit(g_vlan_state.vlans[vid].port_membership, port_id)) {
            LOG_ERROR("VLAN: Port %d is not a member of VLAN %d", port_id, vid);
            vlan_release_lock();
            return ERROR_INVALID_PACKET;
        }
        
        // For access ports, tagged packets are not allowed
        if (g_vlan_state.port_configs[port_id].mode == PORT_VLAN_MODE_ACCESS) {
            LOG_ERROR("VLAN: Access port %d received a tagged packet", port_id);
            vlan_release_lock();
            return ERROR_INVALID_PACKET;
        }
        
        // For trunk ports, check if the VLAN is allowed
        if (g_vlan_state.port_configs[port_id].mode == PORT_VLAN_MODE_TRUNK &&
            !test_bitmap_bit(g_vlan_state.port_configs[port_id].allowed_vlans, vid)) {
            LOG_ERROR("VLAN: VLAN %d not allowed on trunk port %d", vid, port_id);
            vlan_release_lock();
            return ERROR_INVALID_PACKET;
        }
        
        // Assign the VLAN ID from the packet
        *vlan_id = vid;
    } else {
        // No VLAN tag, use the default VLAN based on port mode
        switch (g_vlan_state.port_configs[port_id].mode) {
            case PORT_VLAN_MODE_ACCESS:
                *vlan_id = g_vlan_state.port_configs[port_id].access_vlan;
                break;
            case PORT_VLAN_MODE_TRUNK:
            case PORT_VLAN_MODE_HYBRID:
                *vlan_id = g_vlan_state.port_configs[port_id].native_vlan;
                break;
            default:
                LOG_ERROR("VLAN: Unknown port mode for port %d", port_id);
                vlan_release_lock();
                return ERROR_INTERNAL;
        }
    }
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Determine if a packet should be tagged when sent out a port
 *
 * @param port_id Port ID
 * @param vlan_id VLAN ID
 * @param should_tag Output parameter to store whether the packet should be tagged
 * @return error_code_t Error code
 */
error_code_t vlan_should_tag_packet(port_id_t port_id, vlan_id_t vlan_id, bool *should_tag) {
    vlan_acquire_lock();
    
    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!is_vlan_id_valid(vlan_id)) {
        LOG_ERROR("VLAN: Invalid VLAN ID %d", vlan_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    if (!g_vlan_state.vlans[vlan_id].active) {
        LOG_ERROR("VLAN: VLAN %d does not exist", vlan_id);
        vlan_release_lock();
        return ERROR_NOT_FOUND;
    }
    
    if (!is_port_valid(port_id) || port_id >= g_vlan_state.num_ports) {
        LOG_ERROR("VLAN: Invalid port ID %d", port_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    if (!should_tag) {
        LOG_ERROR("VLAN: Output parameter is NULL");
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    // Check if port is a member of the VLAN
    if (!test_bitmap_bit(g_vlan_state.vlans[vlan_id].port_membership, port_id)) {
        // Port is not a member of this VLAN, packet should be dropped
        *should_tag = false;
        vlan_release_lock();
        return ERROR_INVALID_PORT;
    }
    
    // Check if the port is configured as untagged for this VLAN
    if (test_bitmap_bit(g_vlan_state.vlans[vlan_id].untagged_ports, port_id)) {
        *should_tag = false;
    } else {
        *should_tag = true;
    }
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Get the name of a VLAN
 *
 * @param vlan_id VLAN ID
 * @param vlan_name Output buffer to store the VLAN name
 * @param name_len Length of the output buffer
 * @return error_code_t Error code
 */
error_code_t vlan_get_name(vlan_id_t vlan_id, char *vlan_name, size_t name_len) {
    vlan_acquire_lock();
    
    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!is_vlan_id_valid(vlan_id)) {
        LOG_ERROR("VLAN: Invalid VLAN ID %d", vlan_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    if (!g_vlan_state.vlans[vlan_id].active) {
        LOG_ERROR("VLAN: VLAN %d does not exist", vlan_id);
        vlan_release_lock();
        return ERROR_NOT_FOUND;
    }
    
    if (!vlan_name || name_len == 0) {
        LOG_ERROR("VLAN: Invalid output buffer");
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    strncpy(vlan_name, g_vlan_state.vlans[vlan_id].name, name_len);
    vlan_name[name_len - 1] = '\0'; // Ensure null termination
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Set the name of a VLAN
 *
 * @param vlan_id VLAN ID
 * @param vlan_name New VLAN name
 * @return error_code_t Error code
 */
error_code_t vlan_set_name(vlan_id_t vlan_id, const char *vlan_name) {
    vlan_acquire_lock();
    
    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!is_vlan_id_valid(vlan_id)) {
        LOG_ERROR("VLAN: Invalid VLAN ID %d", vlan_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    if (!g_vlan_state.vlans[vlan_id].active) {
        LOG_ERROR("VLAN: VLAN %d does not exist", vlan_id);
        vlan_release_lock();
        return ERROR_NOT_FOUND;
    }
    
    if (!vlan_name) {
        LOG_ERROR("VLAN: Invalid VLAN name");
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    strncpy(g_vlan_state.vlans[vlan_id].name, vlan_name, VLAN_NAME_MAX_LEN);
    g_vlan_state.vlans[vlan_id].name[VLAN_NAME_MAX_LEN - 1] = '\0'; // Ensure null termination
    
    LOG_INFO("VLAN: Renamed VLAN %d to '%s'", vlan_id, g_vlan_state.vlans[vlan_id].name);
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Check if a VLAN exists
 *
 * @param vlan_id VLAN ID
 * @param exists Output parameter to store whether the VLAN exists
 * @return error_code_t Error code
 */
error_code_t vlan_exists(vlan_id_t vlan_id, bool *exists) {
    vlan_acquire_lock();
    
    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!is_vlan_id_valid(vlan_id)) {
        LOG_ERROR("VLAN: Invalid VLAN ID %d", vlan_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    if (!exists) {
        LOG_ERROR("VLAN: Output parameter is NULL");
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    *exists = g_vlan_state.vlans[vlan_id].active;

    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Get a list of active VLANs
 *
 * @param vlan_list Output array to store VLAN IDs
 * @param max_vlans Maximum number of VLANs to store
 * @param num_vlans Output parameter to store the actual number of VLANs
 * @return error_code_t Error code
 */
error_code_t vlan_get_active_vlans(vlan_id_t *vlan_list, uint32_t max_vlans, uint32_t *num_vlans) {
    uint32_t count = 0;
    uint32_t i;

    vlan_acquire_lock();

    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }

    if (!vlan_list || !num_vlans || max_vlans == 0) {
        LOG_ERROR("VLAN: Invalid parameters");
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    // Count active VLANs
    for (i = 0; i < VLAN_MAX_COUNT && count < max_vlans; i++) {
        if (g_vlan_state.vlans[i].active) {
            vlan_list[count++] = i;
        }
    }

    *num_vlans = count;

    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Get the member ports of a VLAN
 *
 * @param vlan_id VLAN ID
 * @param port_list Output array to store port IDs
 * @param max_ports Maximum number of ports to store
 * @param num_ports Output parameter to store the actual number of ports
 * @return error_code_t Error code
 */
error_code_t vlan_get_member_ports(vlan_id_t vlan_id, port_id_t *port_list, uint32_t max_ports, uint32_t *num_ports) {
    uint32_t count = 0;
    uint32_t i;

    vlan_acquire_lock();

    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }

    if (!is_vlan_id_valid(vlan_id)) {
        LOG_ERROR("VLAN: Invalid VLAN ID %d", vlan_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    if (!g_vlan_state.vlans[vlan_id].active) {
        LOG_ERROR("VLAN: VLAN %d does not exist", vlan_id);
        vlan_release_lock();
        return ERROR_NOT_FOUND;
    }

    if (!port_list || !num_ports || max_ports == 0) {
        LOG_ERROR("VLAN: Invalid parameters");
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    // Find all member ports
    for (i = 0; i < g_vlan_state.num_ports && count < max_ports; i++) {
        if (test_bitmap_bit(g_vlan_state.vlans[vlan_id].port_membership, i)) {
            port_list[count++] = i;
        }
    }

    *num_ports = count;

    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Get the untagged ports of a VLAN
 *
 * @param vlan_id VLAN ID
 * @param port_list Output array to store port IDs
 * @param max_ports Maximum number of ports to store
 * @param num_ports Output parameter to store the actual number of ports
 * @return error_code_t Error code
 */
error_code_t vlan_get_untagged_ports(vlan_id_t vlan_id, port_id_t *port_list, uint32_t max_ports, uint32_t *num_ports) {
    uint32_t count = 0;
    uint32_t i;

    vlan_acquire_lock();

    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }

    if (!is_vlan_id_valid(vlan_id)) {
        LOG_ERROR("VLAN: Invalid VLAN ID %d", vlan_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    if (!g_vlan_state.vlans[vlan_id].active) {
        LOG_ERROR("VLAN: VLAN %d does not exist", vlan_id);
        vlan_release_lock();
        return ERROR_NOT_FOUND;
    }

    if (!port_list || !num_ports || max_ports == 0) {
        LOG_ERROR("VLAN: Invalid parameters");
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    // Find all untagged ports
    for (i = 0; i < g_vlan_state.num_ports && count < max_ports; i++) {
        if (test_bitmap_bit(g_vlan_state.vlans[vlan_id].port_membership, i) &&
            test_bitmap_bit(g_vlan_state.vlans[vlan_id].untagged_ports, i)) {
            port_list[count++] = i;
        }
    }

    *num_ports = count;

    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Get the tagged ports of a VLAN
 *
 * @param vlan_id VLAN ID
 * @param port_list Output array to store port IDs
 * @param max_ports Maximum number of ports to store
 * @param num_ports Output parameter to store the actual number of ports
 * @return error_code_t Error code
 */
error_code_t vlan_get_tagged_ports(vlan_id_t vlan_id, port_id_t *port_list, uint32_t max_ports, uint32_t *num_ports) {
    uint32_t count = 0;
    uint32_t i;

    vlan_acquire_lock();

    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }

    if (!is_vlan_id_valid(vlan_id)) {
        LOG_ERROR("VLAN: Invalid VLAN ID %d", vlan_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    if (!g_vlan_state.vlans[vlan_id].active) {
        LOG_ERROR("VLAN: VLAN %d does not exist", vlan_id);
        vlan_release_lock();
        return ERROR_NOT_FOUND;
    }

    if (!port_list || !num_ports || max_ports == 0) {
        LOG_ERROR("VLAN: Invalid parameters");
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    // Find all tagged ports (ports that are members but not untagged)
    for (i = 0; i < g_vlan_state.num_ports && count < max_ports; i++) {
        if (test_bitmap_bit(g_vlan_state.vlans[vlan_id].port_membership, i) &&
            !test_bitmap_bit(g_vlan_state.vlans[vlan_id].untagged_ports, i)) {
            port_list[count++] = i;
        }
    }

    *num_ports = count;

    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Get the VLANs of which a port is a member
 *
 * @param port_id Port ID
 * @param vlan_list Output array to store VLAN IDs
 * @param max_vlans Maximum number of VLANs to store
 * @param num_vlans Output parameter to store the actual number of VLANs
 * @return error_code_t Error code
 */
error_code_t vlan_get_port_vlans(port_id_t port_id, vlan_id_t *vlan_list, uint32_t max_vlans, uint32_t *num_vlans) {
    uint32_t count = 0;
    uint32_t i;

    vlan_acquire_lock();

    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }

    if (!is_port_valid(port_id) || port_id >= g_vlan_state.num_ports) {
        LOG_ERROR("VLAN: Invalid port ID %d", port_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    if (!vlan_list || !num_vlans || max_vlans == 0) {
        LOG_ERROR("VLAN: Invalid parameters");
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    // Find all VLANs this port is a member of
    for (i = 0; i < VLAN_MAX_COUNT && count < max_vlans; i++) {
        if (g_vlan_state.vlans[i].active &&
            test_bitmap_bit(g_vlan_state.vlans[i].port_membership, port_id)) {
            vlan_list[count++] = i;
        }
    }

    *num_vlans = count;

    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Process a packet for VLAN tagging/untagging on egress
 *
 * @param packet Packet to process
 * @param vlan_id VLAN ID associated with the packet
 * @param out_port Port on which the packet will be sent
 * @param out_packet Output packet after VLAN processing
 * @return error_code_t Error code
 */
error_code_t vlan_process_egress(const packet_t *packet, vlan_id_t vlan_id, port_id_t out_port, packet_t *out_packet) {
    bool tag_required;
    error_code_t ret;

    vlan_acquire_lock();

    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }

    if (!packet || !out_packet) {
        LOG_ERROR("VLAN: Invalid packet parameters");
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    if (!is_vlan_id_valid(vlan_id)) {
        LOG_ERROR("VLAN: Invalid VLAN ID %d", vlan_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    if (!g_vlan_state.vlans[vlan_id].active) {
        LOG_ERROR("VLAN: VLAN %d does not exist", vlan_id);
        vlan_release_lock();
        return ERROR_NOT_FOUND;
    }

    if (!is_port_valid(out_port) || out_port >= g_vlan_state.num_ports) {
        LOG_ERROR("VLAN: Invalid port ID %d", out_port);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }

    // Check if port is a member of the VLAN
    if (!test_bitmap_bit(g_vlan_state.vlans[vlan_id].port_membership, out_port)) {
        LOG_ERROR("VLAN: Port %d is not a member of VLAN %d", out_port, vlan_id);
        vlan_release_lock();
        return ERROR_INVALID_PORT;
    }

    // Determine if tagging is required
    tag_required = !test_bitmap_bit(g_vlan_state.vlans[vlan_id].untagged_ports, out_port);

    if (tag_required) {
        // Check if packet already has a VLAN tag
        vlan_id_t existing_vid;
        bool has_tag = packet_has_vlan_tag(packet, &existing_vid);

        if (has_tag && existing_vid == vlan_id) {
            // Packet already has the correct VLAN tag, just copy it
            if (packet_copy(packet, out_packet) != ERROR_SUCCESS) {
                LOG_ERROR("VLAN: Failed to copy packet");
                vlan_release_lock();
                return ERROR_INTERNAL;
            }
        } else if (has_tag) {
            // Packet has a different VLAN tag, modify it
            if (packet_set_vlan_tag(packet, vlan_id, out_packet) != ERROR_SUCCESS) {
                LOG_ERROR("VLAN: Failed to modify VLAN tag");
                vlan_release_lock();
                return ERROR_INTERNAL;
            }
        } else {
            // Packet has no VLAN tag, add one
            if (packet_add_vlan_tag(packet, vlan_id, out_packet) != ERROR_SUCCESS) {
                LOG_ERROR("VLAN: Failed to add VLAN tag");
                vlan_release_lock();
                return ERROR_INTERNAL;
            }
        }
    } else {
        // Check if packet has a VLAN tag that needs to be removed
        vlan_id_t existing_vid;
        bool has_tag = packet_has_vlan_tag(packet, &existing_vid);

        if (has_tag) {
            // Remove the VLAN tag
            if (packet_remove_vlan_tag(packet, out_packet) != ERROR_SUCCESS) {
                LOG_ERROR("VLAN: Failed to remove VLAN tag");
                vlan_release_lock();
                return ERROR_INTERNAL;
            }
        } else {
            // No VLAN tag, just copy the packet
            if (packet_copy(packet, out_packet) != ERROR_SUCCESS) {
                LOG_ERROR("VLAN: Failed to copy packet");
                vlan_release_lock();
                return ERROR_INTERNAL;
            }
        }
    }

    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Reset all VLAN configurations to default
 *
 * @return error_code_t Error code
 */
error_code_t vlan_reset_config(void) {
    uint32_t i;
    uint32_t bitmap_size;

    vlan_acquire_lock();

    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }

    // Calculate bitmap size based on number of ports
    bitmap_size = (g_vlan_state.num_ports + 7) / 8;

    // Reset all VLANs except the default one
    for (i = 1; i < g_vlan_state.max_vlan_id; i++) {
        if (i == VLAN_DEFAULT_ID) {
            // For default VLAN, just reset port memberships
            memset(g_vlan_state.vlans[i].port_membership, 0, bitmap_size);
            memset(g_vlan_state.vlans[i].untagged_ports, 0, bitmap_size);
            
            // Add all ports to default VLAN as untagged
            for (uint32_t port = 0; port < g_vlan_state.num_ports; port++) {
                set_bitmap_bit(g_vlan_state.vlans[i].port_membership, port);
                set_bitmap_bit(g_vlan_state.vlans[i].untagged_ports, port);
            }
            
            // Make sure default VLAN is active and named properly
            g_vlan_state.vlans[i].active = true;
            strncpy(g_vlan_state.vlans[i].name, "default", VLAN_NAME_MAX_LEN - 1);
            g_vlan_state.vlans[i].name[VLAN_NAME_MAX_LEN - 1] = '\0';
        } else if (g_vlan_state.vlans[i].active) {
            // Deactivate non-default VLANs
            g_vlan_state.vlans[i].active = false;
            memset(g_vlan_state.vlans[i].port_membership, 0, bitmap_size);
            memset(g_vlan_state.vlans[i].untagged_ports, 0, bitmap_size);
            g_vlan_state.vlans[i].name[0] = '\0';
        }
    }

    // Reset all port configurations to access mode with default VLAN
    for (i = 0; i < g_vlan_state.num_ports; i++) {
        g_vlan_state.port_configs[i].mode = PORT_VLAN_MODE_ACCESS;
        g_vlan_state.port_configs[i].access_vlan = VLAN_DEFAULT_ID;
        g_vlan_state.port_configs[i].native_vlan = VLAN_DEFAULT_ID;
        
        // Reset allowed VLANs bitmap - only default VLAN is allowed
        memset(g_vlan_state.port_configs[i].allowed_vlans, 0, bitmap_size);
        set_bitmap_bit(g_vlan_state.port_configs[i].allowed_vlans, VLAN_DEFAULT_ID);
    }

    LOG_INFO("VLAN: Reset all configurations to default");
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Get a string representation of a port VLAN mode
 *
 * @param mode Port VLAN mode
 * @return const char* String representation
 */
const char* vlan_mode_to_string(port_vlan_mode_t mode) {
    switch (mode) {
        case PORT_VLAN_MODE_ACCESS:
            return "access";
        case PORT_VLAN_MODE_TRUNK:
            return "trunk";
        case PORT_VLAN_MODE_HYBRID:
            return "hybrid";
        default:
            return "unknown";
    }
}

/**
 * @brief Dump VLAN configuration for debugging
 *
 * @return error_code_t Error code
 */
error_code_t vlan_dump_config(void) {
    uint32_t i, j;
    uint32_t active_count = 0;
    
    vlan_acquire_lock();
    
    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }
    
    LOG_DEBUG("VLAN: Configuration dump start");
    LOG_DEBUG("VLAN: Number of ports: %d", g_vlan_state.num_ports);
    
    // Count active VLANs
    for (i = 1; i < g_vlan_state.max_vlan_id; i++) {
        if (g_vlan_state.vlans[i].active) {
            active_count++;
        }
    }
    
    LOG_DEBUG("VLAN: Number of active VLANs: %d", active_count);
    
    // Dump active VLANs
    for (i = 1; i < g_vlan_state.max_vlan_id; i++) {
        if (g_vlan_state.vlans[i].active) {
            LOG_DEBUG("VLAN: ID %d, Name '%s'", i, g_vlan_state.vlans[i].name);
            
            // Show member ports for this VLAN
            LOG_DEBUG("  Member ports: ");
            for (j = 0; j < g_vlan_state.num_ports; j++) {
                if (test_bitmap_bit(g_vlan_state.vlans[i].port_membership, j)) {
                    bool is_tagged = !test_bitmap_bit(g_vlan_state.vlans[i].untagged_ports, j);
                    LOG_DEBUG("    Port %d: %s", j, is_tagged ? "tagged" : "untagged");
                }
            }
        }
    }
    
    // Dump port configurations
    LOG_DEBUG("VLAN: Port configurations:");
    for (i = 0; i < g_vlan_state.num_ports; i++) {
        port_vlan_config_t *config = &g_vlan_state.port_configs[i];
        LOG_DEBUG("  Port %d: Mode %s", i, vlan_mode_to_string(config->mode));
        
        if (config->mode == PORT_VLAN_MODE_ACCESS) {
            LOG_DEBUG("    Access VLAN: %d", config->access_vlan);
        } else {
            LOG_DEBUG("    Native VLAN: %d", config->native_vlan);
            LOG_DEBUG("    Allowed VLANs: ");
            for (j = 1; j < g_vlan_state.max_vlan_id; j++) {
                if (test_bitmap_bit(config->allowed_vlans, j)) {
                    LOG_DEBUG("      %d", j);
                }
            }
        }
    }
    
    LOG_DEBUG("VLAN: Configuration dump end");
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Get statistics for a VLAN
 *
 * @param vlan_id VLAN ID
 * @param member_count Output parameter to store number of member ports
 * @param tagged_count Output parameter to store number of tagged ports
 * @param untagged_count Output parameter to store number of untagged ports
 * @return error_code_t Error code
 */
error_code_t vlan_get_stats(vlan_id_t vlan_id, uint32_t *member_count, uint32_t *tagged_count, uint32_t *untagged_count) {
    uint32_t i;
    uint32_t members = 0;
    uint32_t tagged = 0;
    uint32_t untagged = 0;
    
    vlan_acquire_lock();
    
    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!is_vlan_id_valid(vlan_id)) {
        LOG_ERROR("VLAN: Invalid VLAN ID %d", vlan_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    if (!g_vlan_state.vlans[vlan_id].active) {
        LOG_ERROR("VLAN: VLAN %d does not exist", vlan_id);
        vlan_release_lock();
        return ERROR_NOT_FOUND;
    }
    
    if (!member_count && !tagged_count && !untagged_count) {
        LOG_ERROR("VLAN: All output parameters are NULL");
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    // Count member ports
    for (i = 0; i < g_vlan_state.num_ports; i++) {
        if (test_bitmap_bit(g_vlan_state.vlans[vlan_id].port_membership, i)) {
            members++;
            
            if (test_bitmap_bit(g_vlan_state.vlans[vlan_id].untagged_ports, i)) {
                untagged++;
            } else {
                tagged++;
            }
        }
    }
    
    if (member_count) {
        *member_count = members;
    }
    
    if (tagged_count) {
        *tagged_count = tagged;
    }
    
    if (untagged_count) {
        *untagged_count = untagged;
    }
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Set whether a port should accept packets with no VLAN tag
 *
 * @param port_id Port ID
 * @param accept Whether to accept untagged packets
 * @return error_code_t Error code
 */
error_code_t vlan_set_accept_untagged(port_id_t port_id, bool accept) {
    vlan_acquire_lock();
    
    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!is_port_valid(port_id) || port_id >= g_vlan_state.num_ports) {
        LOG_ERROR("VLAN: Invalid port ID %d", port_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    // In a real implementation, this would set a flag in the port configuration
    // For now, we'll just log the setting
    LOG_INFO("VLAN: Port %d set to %s untagged packets", port_id, accept ? "accept" : "reject");
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Set whether a port should accept packets with VLAN tags
 *
 * @param port_id Port ID
 * @param accept Whether to accept tagged packets
 * @return error_code_t Error code
 */
error_code_t vlan_set_accept_tagged(port_id_t port_id, bool accept) {
    vlan_acquire_lock();
    
    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!is_port_valid(port_id) || port_id >= g_vlan_state.num_ports) {
        LOG_ERROR("VLAN: Invalid port ID %d", port_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    // In a real implementation, this would set a flag in the port configuration
    // For now, we'll just log the setting
    LOG_INFO("VLAN: Port %d set to %s tagged packets", port_id, accept ? "accept" : "reject");
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}

/**
 * @brief Enable or disable VLAN filtering on a port
 *
 * @param port_id Port ID
 * @param enable Whether to enable VLAN filtering
 * @return error_code_t Error code
 */
error_code_t vlan_set_filtering(port_id_t port_id, bool enable) {
    vlan_acquire_lock();
    
    if (!g_vlan_state.initialized) {
        LOG_ERROR("VLAN: Module not initialized");
        vlan_release_lock();
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!is_port_valid(port_id) || port_id >= g_vlan_state.num_ports) {
        LOG_ERROR("VLAN: Invalid port ID %d", port_id);
        vlan_release_lock();
        return ERROR_INVALID_PARAMETER;
    }
    
    // In a real implementation, this would set a flag in the port configuration
    // For now, we'll just log the setting
    LOG_INFO("VLAN: Port %d VLAN filtering %s", port_id, enable ? "enabled" : "disabled");
    
    vlan_release_lock();
    return ERROR_SUCCESS;
}






