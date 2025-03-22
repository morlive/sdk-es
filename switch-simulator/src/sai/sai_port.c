/**
 * @file sai_port.c
 * @brief Реализация SAI API для управления портами коммутатора
 */

#include "sai/sai_port.h"
#include "common/logging.h"
#include "common/error_codes.h"
#include "common/types.h"
#include "hal/port.h"
#include "hal/hw_resources.h"
#include <stdlib.h>
#include <string.h>

// Определения типов для модуля портов SAI
typedef struct {
    bool initialized;
    uint32_t port_count;
    sai_port_config_t *port_configs;
} sai_port_context_t;

// Глобальный контекст портов SAI
static sai_port_context_t g_sai_port_ctx = {0};

// Максимальное количество портов в системе
#define MAX_PORT_COUNT 64

/**
 * @brief Инициализирует модуль портов SAI
 * 
 * @return error_code_t Код ошибки
 */
error_code_t sai_port_module_init(void) {
    if (g_sai_port_ctx.initialized) {
        LOG_WARN("SAI Port module already initialized");
        return ERROR_ALREADY_INITIALIZED;
    }

    LOG_INFO("Initializing SAI Port module");
    
    // Получение информации о портах из HAL
    hw_context_t *hw_ctx = sai_adapter_get_hw_context();
    if (!hw_ctx) {
        LOG_ERROR("Failed to get hardware context");
        return ERROR_INTERNAL;
    }
    
    uint32_t port_count = hal_port_get_count(hw_ctx);
    if (port_count == 0 || port_count > MAX_PORT_COUNT) {
        LOG_ERROR("Invalid port count from HAL: %u", port_count);
        return ERROR_INTERNAL;
    }
    
    // Выделение памяти под конфигурации портов
    g_sai_port_ctx.port_configs = calloc(port_count, sizeof(sai_port_config_t));
    if (!g_sai_port_ctx.port_configs) {
        LOG_ERROR("Failed to allocate memory for port configurations");
        return ERROR_MEMORY_ALLOCATION_FAILED;
    }
    
    // Инициализация конфигурации портов из HAL
    for (uint32_t i = 0; i < port_count; i++) {
        hal_port_info_t port_info;
        error_code_t result = hal_port_get_info(hw_ctx, i, &port_info);
        if (result != ERROR_SUCCESS) {
            LOG_ERROR("Failed to get port info for port %u", i);
            free(g_sai_port_ctx.port_configs);
            g_sai_port_ctx.port_configs = NULL;
            return result;
        }
        
        // Заполняем конфигурацию порта из HAL
        g_sai_port_ctx.port_configs[i].port_id = i;
        g_sai_port_ctx.port_configs[i].admin_state = port_info.admin_state;
        g_sai_port_ctx.port_configs[i].speed = port_info.speed;
        g_sai_port_ctx.port_configs[i].duplex = port_info.duplex;
        g_sai_port_ctx.port_configs[i].mtu = port_info.mtu;
        g_sai_port_ctx.port_configs[i].default_vlan = port_info.default_vlan;
        strncpy(g_sai_port_ctx.port_configs[i].name, port_info.name, MAX_PORT_NAME_LEN - 1);
        g_sai_port_ctx.port_configs[i].name[MAX_PORT_NAME_LEN - 1] = '\0';
    }
    
    g_sai_port_ctx.port_count = port_count;
    g_sai_port_ctx.initialized = true;
    
    LOG_INFO("SAI Port module initialized successfully with %u ports", port_count);
    return ERROR_SUCCESS;
}

/**
 * @brief Деинициализирует модуль портов SAI
 * 
 * @return error_code_t Код ошибки
 */
error_code_t sai_port_module_deinit(void) {
    if (!g_sai_port_ctx.initialized) {
        LOG_WARN("SAI Port module not initialized");
        return ERROR_NOT_INITIALIZED;
    }

    LOG_INFO("Deinitializing SAI Port module");
    
    // Освобождение ресурсов
    free(g_sai_port_ctx.port_configs);
    memset(&g_sai_port_ctx, 0, sizeof(g_sai_port_ctx));
    
    LOG_INFO("SAI Port module deinitialized successfully");
    return ERROR_SUCCESS;
}

/**
 * @brief Создает порт в SAI
 * 
 * @param port_id ID порта
 * @param port_config Конфигурация порта
 * @return error_code_t Код ошибки
 */
error_code_t sai_port_create(uint32_t port_id, const sai_port_config_t *port_config) {
    if (!g_sai_port_ctx.initialized) {
        LOG_ERROR("SAI Port module not initialized");
        return ERROR_NOT_INITIALIZED;
    }

    if (port_id >= g_sai_port_ctx.port_count || !port_config) {
        LOG_ERROR("Invalid port ID or config");
        return ERROR_INVALID_PARAMETER;
    }

    LOG_INFO("Creating SAI port %u with name '%s'", port_id, port_config->name);
    
    // Проверка существования порта в HAL
    hw_context_t *hw_ctx = sai_adapter_get_hw_context();
    hal_port_info_t port_info;
    error_code_t result = hal_port_get_info(hw_ctx, port_id, &port_info);
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Port %u does not exist in hardware", port_id);
        return ERROR_NOT_FOUND;
    }
    
    // Копирование конфигурации
    memcpy(&g_sai_port_ctx.port_configs[port_id], port_config, sizeof(sai_port_config_t));
    g_sai_port_ctx.port_configs[port_id].port_id = port_id;  // Убедимся, что ID правильный
    
    // Применение конфигурации в HAL
    hal_port_config_t hal_config;
    hal_config.admin_state = port_config->admin_state;
    hal_config.speed = port_config->speed;
    hal_config.duplex = port_config->duplex;
    hal_config.mtu = port_config->mtu;
    hal_config.default_vlan = port_config->default_vlan;
    strncpy(hal_config.name, port_config->name, MAX_PORT_NAME_LEN - 1);
    hal_config.name[MAX_PORT_NAME_LEN - 1] = '\0';
    
    result = hal_port_configure(hw_ctx, port_id, &hal_config);
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to configure port %u in hardware", port_id);
        return result;
    }
    
    // Сохранение в адаптере SAI
    result = sai_adapter_store_object(SAI_OBJECT_TYPE_PORT, port_id, 
                                     &g_sai_port_ctx.port_configs[port_id], 
                                     sizeof(sai_port_config_t));
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to store port object in SAI adapter");
        return result;
    }
    
    LOG_INFO("SAI port %u created successfully", port_id);
    return ERROR_SUCCESS;
}

/**
 * @brief Удаляет порт из SAI
 * 
 * @param port_id ID порта
 * @return error_code_t Код ошибки
 */
error_code_t sai_port_remove(uint32_t port_id) {
    if (!g_sai_port_ctx.initialized) {
        LOG_ERROR("SAI Port module not initialized");
        return ERROR_NOT_INITIALIZED;
    }

    if (port_id >= g_sai_port_ctx.port_count) {
        LOG_ERROR("Invalid port ID");
        return ERROR_INVALID_PARAMETER;
    }

    LOG_INFO("Removing SAI port %u", port_id);
    
    // Выключаем порт в HAL
    hw_context_t *hw_ctx = sai_adapter_get_hw_context();
    hal_port_config_t hal_config;
    
    error_code_t result = hal_port_get_config(hw_ctx, port_id, &hal_config);
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to get port %u configuration from hardware", port_id);
        return result;
    }
    
    hal_config.admin_state = PORT_STATE_DOWN;
    result = hal_port_configure(hw_ctx, port_id, &hal_config);
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to disable port %u in hardware", port_id);
        return result;
    }
    
    // Сбрасываем конфигурацию порта
    memset(&g_sai_port_ctx.port_configs[port_id], 0, sizeof(sai_port_config_t));
    g_sai_port_ctx.port_configs[port_id].port_id = port_id;
    
    // Удаление объекта из адаптера SAI
    result = sai_adapter_remove_object(SAI_OBJECT_TYPE_PORT, port_id);
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to remove port object from SAI adapter");
        return result;
    }
    
    LOG_INFO("SAI port %u removed successfully", port_id);
    return ERROR_SUCCESS;
}

/**
 * @brief Получает конфигурацию порта SAI
 * 
 * @param port_id ID порта
 * @param port_config Указатель на структуру для сохранения конфигурации
 * @return error_code_t Код ошибки
 */
error_code_t sai_port_get_config(uint32_t port_id, sai_port_config_t *port_config) {
    if (!g_sai_port_ctx.initialized) {
        LOG_ERROR("SAI Port module not initialized");
        return ERROR_NOT_INITIALIZED;
    }

    if (port_id >= g_sai_port_ctx.port_count || !port_config) {
        LOG_ERROR("Invalid port ID or config pointer");
        return ERROR_INVALID_PARAMETER;
    }

    LOG_DEBUG("Getting SAI port %u configuration", port_id);
    
    // Копируем конфигурацию из внутреннего хранилища
    memcpy(port_config, &g_sai_port_ctx.port_configs[port_id], sizeof(sai_port_config_t));
    
    return ERROR_SUCCESS;
}

/**
 * @brief Устанавливает конфигурацию порта SAI
 * 
 * @param port_id ID порта
 * @param port_config Конфигурация порта
 * @return error_code_t Код ошибки
 */
error_code_t sai_port_set_config(uint32_t port_id, const sai_port_config_t *port_config) {
    if (!g_sai_port_ctx.initialized) {
        LOG_ERROR("SAI Port module not initialized");
        return ERROR_NOT_INITIALIZED;
    }

    if (port_id >= g_sai_port_ctx.port_count || !port_config) {
        LOG_ERROR("Invalid port ID or config");
        return ERROR_INVALID_PARAMETER;
    }

    LOG_INFO("Setting SAI port %u configuration, admin_state=%s, speed=%u", 
             port_id, 
             port_config->admin_state == PORT_STATE_UP ? "UP" : "DOWN", 
             port_config->speed);
    
    // Применение конфигурации в HAL
    hw_context_t *hw_ctx = sai_adapter_get_hw_context();
    hal_port_config_t hal_config;
    
    hal_config.admin_state = port_config->admin_state;
    hal_config.speed = port_config->speed;
    hal_config.duplex = port_config->duplex;
    hal_config.mtu = port_config->mtu;
    hal_config.default_vlan = port_config->default_vlan;
    strncpy(hal_config.name, port_config->name, MAX_PORT_NAME_LEN - 1);
    hal_config.name[MAX_PORT_NAME_LEN - 1] = '\0';
    
    error_code_t result = hal_port_configure(hw_ctx, port_id, &hal_config);
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to configure port %u in hardware", port_id);
        return result;
    }
    
    // Обновление внутренней конфигурации
    memcpy(&g_sai_port_ctx.port_configs[port_id], port_config, sizeof(sai_port_config_t));
    g_sai_port_ctx.port_configs[port_id].port_id = port_id;  // Убедимся, что ID правильный
    
    // Обновление в адаптере SAI
    result = sai_adapter_store_object(SAI_OBJECT_TYPE_PORT, port_id, 
                                     &g_sai_port_ctx.port_configs[port_id], 
                                     sizeof(sai_port_config_t));
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to update port object in SAI adapter");
        return result;
    }
    
    LOG_INFO("SAI port %u configuration updated successfully", port_id);
    return ERROR_SUCCESS;
}

/**
 * @brief Получает статистику порта
 * 
 * @param port_id ID порта
 * @param stats Указатель на структуру для сохранения статистики
 * @return error_code_t Код ошибки
 */
error_code_t sai_port_get_stats(uint32_t port_id, sai_port_stats_t *stats) {
    if (!g_sai_port_ctx.initialized) {
        LOG_ERROR("SAI Port module not initialized");
        return ERROR_NOT_INITIALIZED;
    }

    if (port_id >= g_sai_port_ctx.port_count || !stats) {
        LOG_ERROR("Invalid port ID or stats pointer");
        return ERROR_INVALID_PARAMETER;
    }

    LOG_DEBUG("Getting SAI port %u statistics", port_id);
    
    // Получение статистики из HAL
    hw_context_t *hw_ctx = sai_adapter_get_hw_context();
    hal_port_stats_t hal_stats;
    
    error_code_t result = hal_port_get_stats(hw_ctx, port_id, &hal_stats);
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to get port %u statistics from hardware", port_id);
        return result;
    }
    
    // Копирование статистики в структуру SAI
    stats->rx_packets = hal_stats.rx_packets;
    stats->tx_packets = hal_stats.tx_packets;
    stats->rx_bytes = hal_stats.rx_bytes;
    stats->tx_bytes = hal_stats.tx_bytes;
    stats->rx_errors = hal_stats.rx_errors;
    stats->tx_errors = hal_stats.tx_errors;
    stats->rx_drops = hal_stats.rx_drops;
    stats->tx_drops = hal_stats.tx_drops;
    stats->collisions = hal_stats.collisions;
    
    return ERROR_SUCCESS;
}

/**
 * @brief Очищает статистику порта
 * 
 * @param port_id ID порта
 * @return error_code_t Код ошибки
 */
error_code_t sai_port_clear_stats(uint32_t port_id) {
    if (!g_sai_port_ctx.initialized) {
        LOG_ERROR("SAI Port module not initialized");
        return ERROR_NOT_INITIALIZED;
    }

    if (port_id >= g_sai_port_ctx.port_count) {
        LOG_ERROR("Invalid port ID");
        return ERROR_INVALID_PARAMETER;
    }

    LOG_INFO("Clearing SAI port %u statistics", port_id);
    
    // Очистка статистики в HAL
    hw_context_t *hw_ctx = sai_adapter_get_hw_context();
    error_code_t result = hal_port_clear_stats(hw_ctx, port_id);
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to clear port %u statistics in hardware", port_id);
        return result;
    }
    
    LOG_INFO("SAI port %u statistics cleared successfully", port_id);
    return ERROR_SUCCESS;
}

/**
 * @brief Получает количество портов в системе
 * 
 * @param count Указатель для сохранения количества портов
 * @return error_code_t Код ошибки
 */
error_code_t sai_port_get_count(uint32_t *count) {
    if (!g_sai_port_ctx.initialized) {
        LOG_ERROR("SAI Port module not initialized");
        return ERROR_NOT_INITIALIZED;
    }

    if (!count) {
        LOG_ERROR("Invalid count pointer");
        return ERROR_INVALID_PARAMETER;
    }

    *count = g_sai_port_ctx.port_count;
    
    LOG_DEBUG("SAI port count: %u", *count);
    return ERROR_SUCCESS;
}
