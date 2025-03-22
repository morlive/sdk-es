/**
 * @file sai_adapter.c
 * @brief Адаптер для SAI API, связывающий вызовы SAI API с внутренней функциональностью симулятора
 */

#include "sai/sai_port.h"
#include "sai/sai_route.h"
#include "sai/sai_vlan.h"
#include "common/logging.h"
#include "common/error_codes.h"
#include "hal/hw_resources.h"
#include <stdlib.h>
#include <string.h>

// Глобальная структура адаптера SAI
typedef struct {
    bool initialized;
    hw_context_t *hw_context;
    void *internal_db;     // Внутренняя база данных для SAI объектов
} sai_adapter_context_t;

static sai_adapter_context_t g_sai_adapter = {0};

/**
 * @brief Инициализирует адаптер SAI
 * 
 * @param hw_context Контекст аппаратного обеспечения
 * @return error_code_t Код ошибки
 */
error_code_t sai_adapter_init(hw_context_t *hw_context) {
    if (g_sai_adapter.initialized) {
        LOG_WARN("SAI adapter already initialized");
        return ERROR_ALREADY_INITIALIZED;
    }

    if (!hw_context) {
        LOG_ERROR("Invalid hardware context");
        return ERROR_INVALID_PARAMETER;
    }

    LOG_INFO("Initializing SAI adapter");

    g_sai_adapter.hw_context = hw_context;
    g_sai_adapter.internal_db = calloc(1, sizeof(void*) * 1024);  // Простая аллокация для демонстрации
    
    if (!g_sai_adapter.internal_db) {
        LOG_ERROR("Failed to allocate memory for SAI adapter internal database");
        return ERROR_MEMORY_ALLOCATION_FAILED;
    }

    // Инициализация подсистем SAI
    error_code_t result;
    
    result = sai_port_module_init();
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to initialize SAI Port module, error: %d", result);
        free(g_sai_adapter.internal_db);
        return result;
    }

    result = sai_route_module_init();
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to initialize SAI Route module, error: %d", result);
        sai_port_module_deinit();
        free(g_sai_adapter.internal_db);
        return result;
    }

    result = sai_vlan_module_init();
    if (result != ERROR_SUCCESS) {
        LOG_ERROR("Failed to initialize SAI VLAN module, error: %d", result);
        sai_route_module_deinit();
        sai_port_module_deinit();
        free(g_sai_adapter.internal_db);
        return result;
    }

    g_sai_adapter.initialized = true;
    LOG_INFO("SAI adapter initialized successfully");
    return ERROR_SUCCESS;
}

/**
 * @brief Освобождает ресурсы адаптера SAI
 * 
 * @return error_code_t Код ошибки
 */
error_code_t sai_adapter_deinit(void) {
    if (!g_sai_adapter.initialized) {
        LOG_WARN("SAI adapter not initialized");
        return ERROR_NOT_INITIALIZED;
    }

    LOG_INFO("Deinitializing SAI adapter");

    // Деинициализация подсистем SAI в обратном порядке
    sai_vlan_module_deinit();
    sai_route_module_deinit();
    sai_port_module_deinit();

    // Освобождение ресурсов
    free(g_sai_adapter.internal_db);
    memset(&g_sai_adapter, 0, sizeof(g_sai_adapter));

    LOG_INFO("SAI adapter deinitialized successfully");
    return ERROR_SUCCESS;
}

/**
 * @brief Получает контекст аппаратного обеспечения
 * 
 * @return hw_context_t* Указатель на контекст аппаратного обеспечения
 */
hw_context_t* sai_adapter_get_hw_context(void) {
    if (!g_sai_adapter.initialized) {
        LOG_ERROR("SAI adapter not initialized");
        return NULL;
    }
    
    return g_sai_adapter.hw_context;
}

/**
 * @brief Сохраняет объект SAI во внутренней базе данных
 * 
 * @param obj_type Тип объекта
 * @param obj_id ID объекта
 * @param obj_data Данные объекта
 * @return error_code_t Код ошибки
 */
error_code_t sai_adapter_store_object(uint32_t obj_type, uint32_t obj_id, void *obj_data, size_t data_size) {
    if (!g_sai_adapter.initialized) {
        LOG_ERROR("SAI adapter not initialized");
        return ERROR_NOT_INITIALIZED;
    }

    if (!obj_data || data_size == 0 || obj_id >= 1024) {  // Простая проверка для демонстрации
        LOG_ERROR("Invalid object parameters");
        return ERROR_INVALID_PARAMETER;
    }

    void **obj_db = (void**)g_sai_adapter.internal_db;
    
    // Простое хранилище для демонстрации
    // В реальной реализации требуется более сложная структура хранения
    if (obj_db[obj_id] != NULL) {
        free(obj_db[obj_id]);
    }
    
    obj_db[obj_id] = malloc(data_size);
    if (!obj_db[obj_id]) {
        LOG_ERROR("Failed to allocate memory for SAI object");
        return ERROR_MEMORY_ALLOCATION_FAILED;
    }
    
    memcpy(obj_db[obj_id], obj_data, data_size);
    
    LOG_DEBUG("Stored SAI object: type=%u, id=%u", obj_type, obj_id);
    return ERROR_SUCCESS;
}

/**
 * @brief Получает объект SAI из внутренней базы данных
 * 
 * @param obj_type Тип объекта
 * @param obj_id ID объекта
 * @param obj_data Буфер для данных объекта
 * @return error_code_t Код ошибки
 */
error_code_t sai_adapter_get_object(uint32_t obj_type, uint32_t obj_id, void *obj_data, size_t data_size) {
    if (!g_sai_adapter.initialized) {
        LOG_ERROR("SAI adapter not initialized");
        return ERROR_NOT_INITIALIZED;
    }

    if (!obj_data || data_size == 0 || obj_id >= 1024) {  // Простая проверка для демонстрации
        LOG_ERROR("Invalid object parameters");
        return ERROR_INVALID_PARAMETER;
    }

    void **obj_db = (void**)g_sai_adapter.internal_db;
    
    if (obj_db[obj_id] == NULL) {
        LOG_ERROR("SAI object not found: type=%u, id=%u", obj_type, obj_id);
        return ERROR_NOT_FOUND;
    }
    
    memcpy(obj_data, obj_db[obj_id], data_size);
    
    LOG_DEBUG("Retrieved SAI object: type=%u, id=%u", obj_type, obj_id);
    return ERROR_SUCCESS;
}

/**
 * @brief Удаляет объект SAI из внутренней базы данных
 * 
 * @param obj_type Тип объекта
 * @param obj_id ID объекта
 * @return error_code_t Код ошибки
 */
error_code_t sai_adapter_remove_object(uint32_t obj_type, uint32_t obj_id) {
    if (!g_sai_adapter.initialized) {
        LOG_ERROR("SAI adapter not initialized");
        return ERROR_NOT_INITIALIZED;
    }

    if (obj_id >= 1024) {  // Простая проверка для демонстрации
        LOG_ERROR("Invalid object ID");
        return ERROR_INVALID_PARAMETER;
    }

    void **obj_db = (void**)g_sai_adapter.internal_db;
    
    if (obj_db[obj_id] == NULL) {
        LOG_ERROR("SAI object not found: type=%u, id=%u", obj_type, obj_id);
        return ERROR_NOT_FOUND;
    }
    
    free(obj_db[obj_id]);
    obj_db[obj_id] = NULL;
    
    LOG_DEBUG("Removed SAI object: type=%u, id=%u", obj_type, obj_id);
    return ERROR_SUCCESS;
}
