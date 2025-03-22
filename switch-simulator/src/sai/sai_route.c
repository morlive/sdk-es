/**
 * @file sai_route.c
 * @brief Реализация SAI API для управления маршрутизацией
 */

#include "sai/sai_route.h"
#include "common/logging.h"
#include "common/error_codes.h"
#include "common/types.h"
#include "l3/routing_table.h"
#include "l3/ip.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

// Определения типов для модуля маршрутизации SAI
typedef struct {
    bool initialized;
    uint32_t route_count;
    sai_route_entry_t *route_entries;
    uint32_t max_routes;
} sai_route_context_t;

// Глобальный контекст маршрутизации SAI
static sai_route_context_t g_sai_route_ctx = {0};

// Максимальное количество маршрутов
#define MAX_ROUTE_COUNT 1024

/**
 * @brief Инициализирует модуль маршрутизации SAI
 * 
 * @return error_code_t Код ошибки
 */
error_code_t sai_route_module_init(void) {
    if (g_sai_route_ctx.initialized) {
        LOG_WARN("SAI Route module already initialized");
        return ERROR_ALREADY_INITIALIZED;
    }

    LOG_INFO("Initializing SAI Route module");
    
    // Выделение памяти под записи маршрутов
    g_sai_route_ctx.route_entries = calloc(MAX_ROUTE_COUNT, sizeof(sai_route_entry_t));
    if (!g_sai_route_ctx.route_entries) {
        LOG_ERROR("Failed to allocate memory for route entries");
        return ERROR_MEMORY_ALLOCATION_FAILED;
    }
    
    g_sai_route_ctx.route_count = 0;
    g_sai_route_ctx.max_routes = MAX_ROUTE_COUNT;
    g_sai_route_ctx.initialized = true;
    
    LOG_INFO("SAI Route module initialized successfully");
    return ERROR_SUCCESS;
}

/**
 * @brief Деинициализирует модуль маршрутизации SAI
 * 
 * @return error_code_t Код ошибки
 */
error_code_t sai_route_module_deinit(void) {
    if (!g_sai_route_ctx.initialized) {
        LOG_WARN("SAI Route module not initialized");
        return ERROR_NOT_INITIALIZED;
    }

    LOG_INFO("Deinitializing SAI Route module");
    
    // Освобождение ресурсов
    free(g_sai_route_ctx.route_entries);
    memset(&g_sai_route_ctx, 0, sizeof(g_sai_route_ctx));
    
    LOG_INFO("SAI Route module deinitialized successfully");
    return ERROR_SUCCESS;
}

/**
 * @brief Находит индекс свободной записи маршрута
 * 
 * @param index Указатель для сохранения индекса
 * @return error_code_t Код ошибки
 */
static error_code_t find_free_route_index(uint32_t *index) {
    if (!g_sai_route_ctx.initialized || !index) {
        return ERROR_INVALID_PARAMETER;
    }
    
    if (g_sai_route_ctx.route_count >= g_sai_route_ctx.max_routes) {
        LOG_ERROR("Route table is full");
        return ERROR_RESOURCE_EXHAUSTED;
    }
    
    // Поиск свободной записи
    for (uint32_t i = 0; i < g_sai_route_ctx.max_routes; i++) {
        if (!g_sai_route_ctx.route_entries[i].is_valid) {
            *index = i;
            return ERROR_SUCCESS;
        }
    }
    
    LOG_ERROR("Failed to find free route entry despite count check");
    return ERROR_INTERNAL;
}

/**
 * @brief Находит индекс существующего маршрута по префиксу
 * 
 * @param prefix IP-префикс
 * @param prefix_len Длина префикса
 * @param vrf_id ID VRF
 * @param index Указатель для сохранения индекса
 * @return error_code_t Код ошибки
 */
static error_code_t find_route_index(ip_addr_t prefix, uint8_t prefix_len, uint32_t vrf_id, uint32_t *index) {
    if (!g_sai_route_ctx.initialized || !index) {
        return ERROR_INVALID_PARAMETER;
    }
    
    // Поиск записи с указанным префиксом
    for (uint32_t i = 0; i < g_sai_route_ctx.max_routes; i++) {
        if (g_sai_route_ctx.route_entries[i].is_valid &&
            g_sai_route_ctx.route_entries[i].vrf_id == vrf_id &&
            g_sai_route_ctx.route_entries[i].prefix_len == prefix_len &&
            memcmp(&g_sai_route_ctx.route_entries[i].prefix, &prefix, sizeof(ip_addr_t)) == 0) {
            *index = i;
            return ERROR_SUCCESS;
        }
    }
    
    return ERROR_NOT_FOUND;
}

/**
 * @brief Создает запись маршрутизации
 * 
 * @param route_entry Описание записи маршрута
 * @return error_code_t Код ошибки
 */
error_code_t sai_route_create(const sai_route_entry_t *route_entry) {
    if (!g_sai_route_ctx.initialized) {
        LOG_ERROR("SAI Route module not initialized");
        return ERROR_NOT_INITIALIZED;
    }

    if (!route_entry) {
        LOG_ERROR("Invalid route entry pointer");
        return ERROR_INVALID_PARAMETER;
    }

    // Проверка параметров маршрута
    if (route_entry->prefix_len > 32 || 
        (route_entry->next_hop_type != SAI_ROUTE_NEXT_HOP_IP && 
         route_entry->next_hop_type != SAI_ROUTE_NEXT_HOP_INTERFACE)) {
        LOG_ERROR("Invalid route parameters: prefix_len=%u, next_hop_type=%d", 
                 route_entry->prefix_len, route_entry->next_hop_type);
        return ERROR_INVALID_PARAMETER;
    }
    
    char prefix_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &route_entry->prefix, prefix_str, INET_ADDRSTRLEN);
    
    LOG_INFO("Creating route: %s/%u in VRF %u", prefix_str, route_entry->prefix_len, route_entry->vrf_id);
    
    // Проверка существования маршрута
    uint32_t index;
    error_code_t result = find_route_index(route_entry->prefix, route_entry->prefix_len, route_entry->vrf_id, &index);
    if (result == ERROR_SUCCESS) {
        LOG_ERROR("Route already exists at index %u", index);
        return ERROR_ALREADY_EXISTS;
    }
    
    // Поиск свободной записи
    result = find_free_route_index(&index);
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to find free route entry");
        return result;
    }
    
    // Копирование записи маршрута
    memcpy(&g_sai_route_ctx.route_entries[index], route_entry, sizeof(sai_route_entry_t));
    g_sai_route_ctx.route_entries[index].is_valid = true;
    
    // Добавление маршрута в таблицу маршрутизации L3
    route_entry_t l3_route;
    l3_route.prefix = route_entry->prefix;
    l3_route.prefix_len = route_entry->prefix_len;
    l3_route.vrf_id = route_entry->vrf_id;
    
    if (route_entry->next_hop_type == SAI_ROUTE_NEXT_HOP_IP) {
        l3_route.next_hop_type = ROUTE_NEXT_HOP_IP;
        l3_route.next_hop.ip = route_entry->next_hop.ip;
    } else {
        l3_route.next_hop_type = ROUTE_NEXT_HOP_INTERFACE;
        l3_route.next_hop.interface_id = route_entry->next_hop.interface_id;
    }
    
    l3_route.metric = route_entry->metric;
    l3_route.priority = route_entry->priority;
    
    result = routing_table_add_route(&l3_route);
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to add route to L3 routing table");
        g_sai_route_ctx.route_entries[index].is_valid = false;
        return result;
    }
    
    // Сохранение в адаптере SAI
    result = sai_adapter_store_object(SAI_OBJECT_TYPE_ROUTE, index, 
                                     &g_sai_route_ctx.route_entries[index], 
                                     sizeof(sai_route_entry_t));
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to store route object in SAI adapter");
        // Отменяем добавление маршрута в таблицу маршрутизации
        routing_table_remove_route(&l3_route);
        g_sai_route_ctx.route_entries[index].is_valid = false;
        return result;
    }
    
    g_sai_route_ctx.route_count++;
    
    LOG_INFO("Route created successfully, total routes: %u", g_sai_route_ctx.route_count);
    return ERROR_SUCCESS;
}

/**
 * @brief Удаляет запись маршрутизации
 * 
 * @param prefix IP-префикс
 * @param prefix_len Длина префикса
 * @param vrf_id ID VRF
 * @return error_code_t Код ошибки
 */
error_code_t sai_route_remove(ip_addr_t prefix, uint8_t prefix_len, uint32_t vrf_id) {
    if (!g_sai_route_ctx.initialized) {
        LOG_ERROR("SAI Route module not initialized");
        return ERROR_NOT_INITIALIZED;
    }

    char prefix_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &prefix, prefix_str, INET_ADDRSTRLEN);
    
    LOG_INFO("Removing route: %s/%u in VRF %u", prefix_str, prefix_len, vrf_id);
    
    // Поиск маршрута
    uint32_t index;
    error_code_t result = find_route_index(prefix, prefix_len, vrf_id, &index);
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Route not found");
        return ERROR_NOT_FOUND;
    }
    
    // Удаление маршрута из таблицы маршрутизации L3
    route_entry_t l3_route;
    l3_route.prefix = prefix;
    l3_route.prefix_len = prefix_len;
    l3_route.vrf_id = vrf_id;
    
    result = routing_table_remove_route(&l3_route);
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to remove route from L3 routing table");
        return result;
    }
    
    // Удаление объекта из адаптера SAI
    result = sai_adapter_remove_object(SAI_OBJECT_TYPE_ROUTE, index);
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to remove route object from SAI adapter");
        return result;
    }
    
    // Очистка записи маршрута
    memset(&g_sai_route_ctx.route_entries[index], 0, sizeof(sai_route_entry_t));
    g_sai_route_ctx.route_count--;
    
    LOG_INFO("Route removed successfully, total routes: %u", g_sai_route_ctx.route_count);
    return ERROR_SUCCESS;
}

/**
 * @brief Получает запись маршрутизации
 * 
 * @param prefix IP-префикс
 * @param prefix_len Длина префикса
 * @param vrf_id ID VRF
 * @param route_entry Указатель для сохранения записи маршрута
 * @return error_code_t Код ошибки
 */
error_code_t sai_route_get(ip_addr_t prefix, uint8_t prefix_len, uint32_t vrf_id, sai_route_entry_t *route_entry) {
    if (!g_sai_route_ctx.initialized) {
        LOG_ERROR("SAI Route module not initialized");
        return ERROR_NOT_INITIALIZED;
    }

    if (!route_entry) {
        LOG_ERROR("Invalid route entry pointer");
        return ERROR_INVALID_PARAMETER;
    }

    char prefix_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &prefix, prefix_str, INET_ADDRSTRLEN);
    
    LOG_DEBUG("Getting route: %s/%u in VRF %u", prefix_str, prefix_len, vrf_id);
    
    // Поиск маршрута
    uint32_t index;
    error_code_t result = find_route_index(prefix, prefix_len, vrf_id, &index);
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Route not found");
        return ERROR_NOT_FOUND;
    }
    
    // Копирование записи маршрута
    memcpy(route_entry, &g_sai_route_ctx.route_entries[index], sizeof(sai_route_entry_t));
    
    return ERROR_SUCCESS;
}

/**
 * @brief Получает все записи маршрутизации
 * 
 * @param vrf_id ID VRF (если 0, то все VRF)
 * @param routes Указатель на массив для сохранения маршрутов
 * @param max_routes Максимальное количество маршрутов в массиве
 * @param count Указатель для сохранения количества найденных маршрутов
 * @return error_code_t Код ошибки
 */
error_code_t sai_route_get_all(uint32_t vrf_id, sai_route_entry_t *routes, uint32_t max_routes, uint32_t *count) {
    if (!g_sai_route_ctx.initialized) {
        LOG_ERROR("SAI Route module not initialized");
        return ERROR_NOT_INITIALIZED;
    }

    if (!routes || !count || max_routes == 0) {
        LOG_ERROR("Invalid parameters");
        return ERROR_INVALID_PARAMETER;
    }

    LOG_DEBUG("Getting all routes for VRF %u", vrf_id);
    
    uint32_t found_count = 0;
    
    // Поиск маршрутов
    for (uint32_t i = 0; i < g_sai_route_ctx.max_routes && found_count < max_routes; i++) {
        if (g_sai_route_ctx.route_entries[i].is_valid && 
            (vrf_id == 0 || g_sai_route_ctx.route_entries[i].vrf_id == vrf_id)) {
            memcpy(&routes[found_count], &g_sai_route_ctx.route_entries[i], sizeof(sai_route_entry_t));
            found_count++;
        }
    }
    
    *count = found_count;
    
    LOG_DEBUG("Found %u routes for VRF %u", found_count, vrf_id);
    return ERROR_SUCCESS;
}

/**
 * @brief Обновляет запись маршрутизации
 * 
 * @param route_entry Обновленная запись маршрута
 * @return error_code_t Код ошибки
 */
error_code_t sai_route_update(const sai_route_entry_t *route_entry) {
    if (!g_sai_route_ctx.initialized) {
        LOG_ERROR("SAI Route module not initialized");
        return ERROR_NOT_INITIALIZED;
    }

    if (!route_entry) {
        LOG_ERROR("Invalid route entry pointer");
        return ERROR_INVALID_PARAMETER;
    }

    char prefix_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &route_entry->prefix, prefix_str, INET_ADDRSTRLEN);
    
    LOG_INFO("Updating route: %s/%u in VRF %u", prefix_str, route_entry->prefix_len, route_entry->vrf_id);
    
    // Поиск маршрута
    uint32_t index;
    error_code_t result = find_route_index(route_entry->prefix, route_entry->prefix_len, route_entry->vrf_id, &index);
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Route not found");
        return ERROR_NOT_FOUND;
    }
    
    // Обновление маршрута в таблице маршрутизации L3
    route_entry_t l3_route;
    l3_route.prefix = route_entry->prefix;
    l3_route.prefix_len = route_entry->prefix_len;
    l3_route.vrf_id = route_entry->vrf_id;
    
    if (route_entry->next_hop_type == SAI_ROUTE_NEXT_HOP_IP) {
        l3_route.next_hop_type = ROUTE_NEXT_HOP_IP;
        l3_route.next_hop.ip = route_entry->next_hop.ip;
    } else {
        l3_route.next_hop_type = ROUTE_NEXT_HOP_INTERFACE;
        l3_route.next_hop.interface_id = route_entry->next_hop.interface_id;
    }
    
    l3_route.metric = route_entry->metric;
    l3_route.priority = route_entry->priority;
    
    result = routing_table_update_route(&l3_route);
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to update route in L3 routing table");
        return result;
    }
    
    // Обновление записи маршрута
    memcpy(&g_sai_route_ctx.route_entries[index], route_entry, sizeof(sai_route_entry_t));
    g_sai_route_ctx.route_entries[index].is_valid = true;
    
    // Обновление в адаптере SAI
    result = sai_adapter_store_object(SAI_OBJECT_TYPE_ROUTE, index, 
                                     &g_sai_route_ctx.route_entries[index], 
                                     sizeof(sai_route_entry_t));
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to update route object in SAI adapter");
        return result;
    }
    
    LOG_INFO("Route updated successfully");
    return ERROR_SUCCESS;
}

/**
 * @brief Получает количество маршрутов
 * 
 * @param count Указатель для сохранения количества маршрутов
 * @return error_code_t Код ошибки
 */
error_code_t sai_route_get_count(uint32_t *count) {
    if (!g_sai_route_ctx.initialized) {
        LOG_ERROR("SAI Route module not initialized");
        return ERROR_NOT_INITIALIZED;
    }

    if (!count) {
        LOG_ERROR("Invalid count pointer");
        return ERROR_INVALID_PARAMETER;
    }

    *count = g_sai_route_ctx.route_count;
    
    LOG_DEBUG("Route count: %u", *count);
    return ERROR_SUCCESS;
}
