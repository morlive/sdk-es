/**
 * @file sai_vlan.c
 * @brief Реализация Switch Abstraction Interface для управления VLAN
 */

#include "include/sai/sai_vlan.h"
#include "include/l2/vlan.h"
#include "include/hal/port.h"
#include "include/common/error_codes.h"
#include "include/common/logging.h"
#include <stdlib.h>
#include <string.h>

/* Максимальное количество VLAN в системе */
#define MAX_VLAN_COUNT 4096

/* Внутренняя структура для отслеживания состояния VLAN */
typedef struct {
    bool is_active;
    char name[32];
    uint16_t tagged_ports;    // Битовая маска для тегированных портов
    uint16_t untagged_ports;  // Битовая маска для нетегированных портов
} vlan_entry_t;

/* Локальное хранилище для VLAN */
static vlan_entry_t vlan_table[MAX_VLAN_COUNT];
static bool vlan_module_initialized = false;

/**
 * @brief Инициализация модуля SAI VLAN
 * 
 * @return sai_status_t Статус операции
 */
sai_status_t sai_vlan_initialize(void) {
    LOG_INFO("Initializing SAI VLAN module");
    
    if (vlan_module_initialized) {
        LOG_WARN("SAI VLAN module already initialized");
        return SAI_STATUS_SUCCESS;
    }
    
    /* Сброс таблицы VLAN */
    memset(vlan_table, 0, sizeof(vlan_table));
    
    /* Инициализация VLAN 1 (по умолчанию) */
    vlan_table[1].is_active = true;
    strncpy(vlan_table[1].name, "default", sizeof(vlan_table[1].name) - 1);
    
    /* Присвоим все порты к VLAN 1 как нетегированные */
    uint16_t all_ports = port_get_all_ports_mask();
    vlan_table[1].untagged_ports = all_ports;
    
    vlan_module_initialized = true;
    LOG_INFO("SAI VLAN module initialized successfully");
    
    return SAI_STATUS_SUCCESS;
}

/**
 * @brief Создание VLAN
 * 
 * @param vlan_id Идентификатор VLAN
 * @param vlan_name Имя VLAN (опционально)
 * @return sai_status_t Статус операции
 */
sai_status_t sai_vlan_create(uint16_t vlan_id, const char *vlan_name) {
    if (!vlan_module_initialized) {
        LOG_ERROR("SAI VLAN module not initialized");
        return SAI_STATUS_UNINITIALIZED;
    }
    
    if (vlan_id == 0 || vlan_id >= MAX_VLAN_COUNT) {
        LOG_ERROR("Invalid VLAN ID: %d", vlan_id);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    
    if (vlan_table[vlan_id].is_active) {
        LOG_WARN("VLAN %d already exists", vlan_id);
        return SAI_STATUS_ITEM_ALREADY_EXISTS;
    }
    
    /* Создание VLAN в нижележащем уровне */
    vlan_status_t vlan_status = vlan_create(vlan_id);
    if (vlan_status != VLAN_STATUS_SUCCESS) {
        LOG_ERROR("Failed to create VLAN %d at L2 level, status: %d", vlan_id, vlan_status);
        return SAI_STATUS_FAILURE;
    }
    
    /* Инициализация записи VLAN */
    vlan_table[vlan_id].is_active = true;
    vlan_table[vlan_id].tagged_ports = 0;
    vlan_table[vlan_id].untagged_ports = 0;
    
    /* Установка имени VLAN, если задано */
    if (vlan_name != NULL) {
        strncpy(vlan_table[vlan_id].name, vlan_name, sizeof(vlan_table[vlan_id].name) - 1);
    } else {
        snprintf(vlan_table[vlan_id].name, sizeof(vlan_table[vlan_id].name), "VLAN%d", vlan_id);
    }
    
    LOG_INFO("Created VLAN %d with name '%s'", vlan_id, vlan_table[vlan_id].name);
    
    return SAI_STATUS_SUCCESS;
}

/**
 * @brief Удаление VLAN
 * 
 * @param vlan_id Идентификатор VLAN
 * @return sai_status_t Статус операции
 */
sai_status_t sai_vlan_remove(uint16_t vlan_id) {
    if (!vlan_module_initialized) {
        LOG_ERROR("SAI VLAN module not initialized");
        return SAI_STATUS_UNINITIALIZED;
    }
    
    if (vlan_id == 0 || vlan_id >= MAX_VLAN_COUNT) {
        LOG_ERROR("Invalid VLAN ID: %d", vlan_id);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    
    if (!vlan_table[vlan_id].is_active) {
        LOG_WARN("VLAN %d does not exist", vlan_id);
        return SAI_STATUS_ITEM_NOT_FOUND;
    }
    
    /* Специальная обработка для VLAN 1 */
    if (vlan_id == 1) {
        LOG_ERROR("Cannot remove default VLAN (ID 1)");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    
    /* Вызов нижележащего уровня для удаления VLAN */
    vlan_status_t vlan_status = vlan_remove(vlan_id);
    if (vlan_status != VLAN_STATUS_SUCCESS) {
        LOG_ERROR("Failed to remove VLAN %d at L2 level, status: %d", vlan_id, vlan_status);
        return SAI_STATUS_FAILURE;
    }
    
    /* Очистка записи VLAN */
    memset(&vlan_table[vlan_id], 0, sizeof(vlan_entry_t));
    
    LOG_INFO("Removed VLAN %d", vlan_id);
    
    return SAI_STATUS_SUCCESS;
}

/**
 * @brief Добавление порта к VLAN как тегированного
 * 
 * @param vlan_id Идентификатор VLAN
 * @param port_id Идентификатор порта
 * @return sai_status_t Статус операции
 */
sai_status_t sai_vlan_add_tagged_port(uint16_t vlan_id, uint16_t port_id) {
    if (!vlan_module_initialized) {
        LOG_ERROR("SAI VLAN module not initialized");
        return SAI_STATUS_UNINITIALIZED;
    }
    
    if (vlan_id == 0 || vlan_id >= MAX_VLAN_COUNT) {
        LOG_ERROR("Invalid VLAN ID: %d", vlan_id);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    
    if (!vlan_table[vlan_id].is_active) {
        LOG_ERROR("VLAN %d does not exist", vlan_id);
        return SAI_STATUS_ITEM_NOT_FOUND;
    }
    
    if (!port_is_valid(port_id)) {
        LOG_ERROR("Invalid port ID: %d", port_id);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    
    uint16_t port_mask = (1 << port_id);
    
    /* Проверка, является ли порт уже тегированным в этом VLAN */
    if (vlan_table[vlan_id].tagged_ports & port_mask) {
        LOG_WARN("Port %d is already a tagged member of VLAN %d", port_id, vlan_id);
        return SAI_STATUS_SUCCESS;
    }
    
    /* Если порт является нетегированным членом VLAN, удаляем его оттуда */
    if (vlan_table[vlan_id].untagged_ports & port_mask) {
        vlan_table[vlan_id].untagged_ports &= ~port_mask;
        LOG_INFO("Removing port %d from untagged ports of VLAN %d", port_id, vlan_id);
    }
    
    /* Вызов нижележащего уровня для добавления порта к VLAN */
    vlan_status_t vlan_status = vlan_add_port(vlan_id, port_id, true);
    if (vlan_status != VLAN_STATUS_SUCCESS) {
        LOG_ERROR("Failed to add tagged port %d to VLAN %d at L2 level, status: %d", port_id, vlan_id, vlan_status);
        return SAI_STATUS_FAILURE;
    }
    
    /* Обновление локальной записи */
    vlan_table[vlan_id].tagged_ports |= port_mask;
    
    LOG_INFO("Added port %d as tagged member to VLAN %d", port_id, vlan_id);
    
    return SAI_STATUS_SUCCESS;
}

/**
 * @brief Добавление порта к VLAN как нетегированного
 * 
 * @param vlan_id Идентификатор VLAN
 * @param port_id Идентификатор порта
 * @return sai_status_t Статус операции
 */
sai_status_t sai_vlan_add_untagged_port(uint16_t vlan_id, uint16_t port_id) {
    if (!vlan_module_initialized) {
        LOG_ERROR("SAI VLAN module not initialized");
        return SAI_STATUS_UNINITIALIZED;
    }
    
    if (vlan_id == 0 || vlan_id >= MAX_VLAN_COUNT) {
        LOG_ERROR("Invalid VLAN ID: %d", vlan_id);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    
    if (!vlan_table[vlan_id].is_active) {
        LOG_ERROR("VLAN %d does not exist", vlan_id);
        return SAI_STATUS_ITEM_NOT_FOUND;
    }
    
    if (!port_is_valid(port_id)) {
        LOG_ERROR("Invalid port ID: %d", port_id);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    
    uint16_t port_mask = (1 << port_id);
    
    /* Проверка, является ли порт уже нетегированным в этом VLAN */
    if (vlan_table[vlan_id].untagged_ports & port_mask) {
        LOG_WARN("Port %d is already an untagged member of VLAN %d", port_id, vlan_id);
        return SAI_STATUS_SUCCESS;
    }
    
    /* Если порт является тегированным членом VLAN, удаляем его оттуда */
    if (vlan_table[vlan_id].tagged_ports & port_mask) {
        vlan_table[vlan_id].tagged_ports &= ~port_mask;
        LOG_INFO("Removing port %d from tagged ports of VLAN %d", port_id, vlan_id);
    }
    
    /* Проверка, является ли порт нетегированным членом другого VLAN */
    for (int i = 1; i < MAX_VLAN_COUNT; i++) {
        if (i != vlan_id && vlan_table[i].is_active && (vlan_table[i].untagged_ports & port_mask)) {
            LOG_INFO("Removing port %d from untagged ports of VLAN %d", port_id, i);
            vlan_table[i].untagged_ports &= ~port_mask;
            vlan_remove_port(i, port_id, false);
        }
    }
    
    /* Вызов нижележащего уровня для добавления порта к VLAN */
    vlan_status_t vlan_status = vlan_add_port(vlan_id, port_id, false);
    if (vlan_status != VLAN_STATUS_SUCCESS) {
        LOG_ERROR("Failed to add untagged port %d to VLAN %d at L2 level, status: %d", port_id, vlan_id, vlan_status);
        return SAI_STATUS_FAILURE;
    }
    
    /* Обновление локальной записи */
    vlan_table[vlan_id].untagged_ports |= port_mask;
    
    LOG_INFO("Added port %d as untagged member to VLAN %d", port_id, vlan_id);
    
    return SAI_STATUS_SUCCESS;
}

/**
 * @brief Удаление порта из VLAN
 * 
 * @param vlan_id Идентификатор VLAN
 * @param port_id Идентификатор порта
 * @return sai_status_t Статус операции
 */
sai_status_t sai_vlan_remove_port(uint16_t vlan_id, uint16_t port_id) {
    if (!vlan_module_initialized) {
        LOG_ERROR("SAI VLAN module not initialized");
        return SAI_STATUS_UNINITIALIZED;
    }
    
    if (vlan_id == 0 || vlan_id >= MAX_VLAN_COUNT) {
        LOG_ERROR("Invalid VLAN ID: %d", vlan_id);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    
    if (!vlan_table[vlan_id].is_active) {
        LOG_ERROR("VLAN %d does not exist", vlan_id);
        return SAI_STATUS_ITEM_NOT_FOUND;
    }
    
    if (!port_is_valid(port_id)) {
        LOG_ERROR("Invalid port ID: %d", port_id);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    
    uint16_t port_mask = (1 << port_id);
    bool is_tagged = (vlan_table[vlan_id].tagged_ports & port_mask) != 0;
    bool is_untagged = (vlan_table[vlan_id].untagged_ports & port_mask) != 0;
    
    /* Проверка, является ли порт членом VLAN */
    if (!is_tagged && !is_untagged) {
        LOG_WARN("Port %d is not a member of VLAN %d", port_id, vlan_id);
        return SAI_STATUS_ITEM_NOT_FOUND;
    }
    
    /* Вызов нижележащего уровня для удаления порта из VLAN */
    vlan_status_t vlan_status = vlan_remove_port(vlan_id, port_id, is_tagged);
    if (vlan_status != VLAN_STATUS_SUCCESS) {
        LOG_ERROR("Failed to remove port %d from VLAN %d at L2 level, status: %d", port_id, vlan_id, vlan_status);
        return SAI_STATUS_FAILURE;
    }
    
    /* Обновление локальной записи */
    if (is_tagged) {
        vlan_table[vlan_id].tagged_ports &= ~port_mask;
    }
    
    if (is_untagged) {
        vlan_table[vlan_id].untagged_ports &= ~port_mask;
    }
    
    LOG_INFO("Removed port %d from VLAN %d", port_id, vlan_id);
    
    return SAI_STATUS_SUCCESS;
}

/**
 * @brief Получение списка VLANs
 * 
 * @param vlan_list Указатель на массив для хранения списка VLANs
 * @param list_size Размер массива
 * @param count Указатель для сохранения количества найденных VLANs
 * @return sai_status_t Статус операции
 */
sai_status_t sai_vlan_get_list(uint16_t *vlan_list, uint16_t list_size, uint16_t *count) {
    if (!vlan_module_initialized) {
        LOG_ERROR("SAI VLAN module not initialized");
        return SAI_STATUS_UNINITIALIZED;
    }
    
    if (vlan_list == NULL || count == NULL) {
        LOG_ERROR("Invalid pointers (vlan_list: %p, count: %p)", vlan_list, count);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    
    *count = 0;
    
    for (uint16_t i = 1; i < MAX_VLAN_COUNT && *count < list_size; i++) {
        if (vlan_table[i].is_active) {
            vlan_list[*count] = i;
            (*count)++;
        }
    }
    
    LOG_INFO("Retrieved %d VLANs from the database", *count);
    
    return SAI_STATUS_SUCCESS;
}

/**
 * @brief Получение информации о VLAN
 * 
 * @param vlan_id Идентификатор VLAN
 * @param vlan_info Указатель на структуру для сохранения информации о VLAN
 * @return sai_status_t Статус операции
 */
sai_status_t sai_vlan_get_info(uint16_t vlan_id, sai_vlan_info_t *vlan_info) {
    if (!vlan_module_initialized) {
        LOG_ERROR("SAI VLAN module not initialized");
        return SAI_STATUS_UNINITIALIZED;
    }
    
    if (vlan_id == 0 || vlan_id >= MAX_VLAN_COUNT) {
        LOG_ERROR("Invalid VLAN ID: %d", vlan_id);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    
    if (!vlan_table[vlan_id].is_active) {
        LOG_ERROR("VLAN %d does not exist", vlan_id);
        return SAI_STATUS_ITEM_NOT_FOUND;
    }
    
    if (vlan_info == NULL) {
        LOG_ERROR("Invalid vlan_info pointer");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    
    /* Заполнение структуры информации о VLAN */
    vlan_info->vlan_id = vlan_id;
    strncpy(vlan_info->name, vlan_table[vlan_id].name, sizeof(vlan_info->name) - 1);
    vlan_info->name[sizeof(vlan_info->name) - 1] = '\0';
    vlan_info->tagged_ports = vlan_table[vlan_id].tagged_ports;
    vlan_info->untagged_ports = vlan_table[vlan_id].untagged_ports;
    
    LOG_INFO("Retrieved information for VLAN %d", vlan_id);
    
    return SAI_STATUS_SUCCESS;
}

/**
 * @brief Деинициализация модуля SAI VLAN
 * 
 * @return sai_status_t Статус операции
 */
sai_status_t sai_vlan_deinitialize(void) {
    LOG_INFO("Deinitializing SAI VLAN module");
    
    if (!vlan_module_initialized) {
        LOG_WARN("SAI VLAN module not initialized");
        return SAI_STATUS_UNINITIALIZED;
    }
    
    /* Очистка всех VLANs, кроме VLAN 1 */
    for (uint16_t i = 2; i < MAX_VLAN_COUNT; i++) {
        if (vlan_table[i].is_active) {
            LOG_INFO("Removing VLAN %d during deinitialization", i);
            vlan_remove(i);
            memset(&vlan_table[i], 0, sizeof(vlan_entry_t));
        }
    }
    
    /* Сброс VLAN 1 к начальному состоянию */
    uint16_t all_ports = port_get_all_ports_mask();
    vlan_table[1].tagged_ports = 0;
    vlan_table[1].untagged_ports = all_ports;
    
    vlan_module_initialized = false;
    LOG_INFO("SAI VLAN module deinitialized successfully");
    
    return SAI_STATUS_SUCCESS;
}
