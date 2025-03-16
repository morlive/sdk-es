/**
 * @file packet.c
 * @brief Реализация интерфейса обработки пакетов для симулятора коммутатора
 * 
 * Этот модуль обеспечивает полную функциональность для управления пакетами в симуляторе сетевого
 * коммутатора, включая:
 * - Создание, клонирование и управление буферами пакетов
 * - Систему регистрации и приоритизации обработчиков пакетов
 * - Поддержку VLAN-тегов и других метаданных пакета
 * - Потокобезопасный доступ для многопоточных сред
 * - Интерфейс для инжекции и передачи пакетов
 * - Управление буферными пулами для оптимизации использования памяти
 * - Интеграцию с симулируемым аппаратным обеспечением
 */
#include "../include/hal/packet.h"
#include "../include/hal/hw_resources.h"
#include "../include/common/logging.h"
#include "../include/common/error_codes.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

/**
 * @brief Максимальное количество обработчиков пакетов, которые могут быть зарегистрированы
 */
#define MAX_PACKET_PROCESSORS 64

/**
 * @brief Размер пула буферов для повторного использования
 */
#define PACKET_BUFFER_POOL_SIZE 128

/**
 * @brief Типичный размер буфера пакета для пула
 */
#define PACKET_BUFFER_POOL_BUFFER_SIZE 2048

/**
 * @brief Максимальное количество рециркуляций пакета для предотвращения зацикливания
 */
#define MAX_RECIRCULATION_COUNT 8

/**
 * @brief Структура для зарегистрированного обработчика пакетов
 */
typedef struct {
    packet_process_cb_t callback;   /**< Функция обратного вызова обработчика */
    uint32_t priority;              /**< Приоритет обработки (меньшее значение = выше приоритет) */
    void *user_data;                /**< Пользовательские данные для обратного вызова */
    bool active;                    /**< Является ли эта запись активной */
} packet_processor_t;

/**
 * @brief Структура для управления пулом буферов пакетов
 */
typedef struct {
    packet_buffer_t *buffers[PACKET_BUFFER_POOL_SIZE];  /**< Массив доступных буферов */
    uint32_t count;                                     /**< Количество буферов в пуле */
    pthread_mutex_t mutex;                              /**< Мьютекс для управления доступом к пулу */
} packet_buffer_pool_t;

/* Статические переменные для хранения состояния модуля */
/**
 * @brief Массив зарегистрированных обработчиков пакетов
 */
static packet_processor_t g_processors[MAX_PACKET_PROCESSORS];

/**
 * @brief Количество зарегистрированных обработчиков
 */
static uint32_t g_processor_count = 0;

/**
 * @brief Мьютекс для защиты доступа к списку обработчиков
 */
static pthread_mutex_t g_processors_mutex;

/**
 * @brief Флаг, указывающий, инициализирована ли подсистема пакетов
 */
static bool g_initialized = false;

/**
 * @brief Пул буферов пакетов для повторного использования
 */
static packet_buffer_pool_t g_buffer_pool;

/**
 * @brief Статистика работы подсистемы пакетов
 */
static struct {
    uint64_t packets_processed;   /**< Общее количество обработанных пакетов */
    uint64_t packets_dropped;     /**< Количество отброшенных пакетов */
    uint64_t packets_forwarded;   /**< Количество пересланных пакетов */
    uint64_t packets_consumed;    /**< Количество потребленных пакетов */
    uint64_t packets_recirculated; /**< Количество рециркулированных пакетов */
    uint64_t buffer_alloc_count;  /**< Счетчик выделений буферов */
    uint64_t buffer_free_count;   /**< Счетчик освобождений буферов */
    pthread_mutex_t mutex;        /**< Мьютекс для защиты доступа к статистике */
} g_packet_stats;

/* Прототипы внутренних функций */
static status_t init_buffer_pool(void);
static packet_buffer_t* get_buffer_from_pool(uint32_t size);
static void return_buffer_to_pool(packet_buffer_t *packet);
static int compare_processors(const void *a, const void *b);
static void sort_processors(void);
static void update_packet_statistics(packet_result_t result);

/* Внешние интерфейсы для симуляции оборудования */
/**
 * @brief Внешний интерфейс для отправки пакета через слой симуляции оборудования
 *
 * @param packet Указатель на буфер пакета
 * @param port_id Идентификатор порта для передачи
 * @return status_t Результат операции
 */
extern status_t hw_sim_transmit_packet(packet_buffer_t *packet, port_id_t port_id);

extern status_t hw_sim_receive_packet(packet_buffer_t *packet, port_id_t *port_id);

/**
 * @brief Функция сравнения для сортировки обработчиков по приоритету
 *
 * @param a Указатель на первый элемент для сравнения
 * @param b Указатель на второй элемент для сравнения
 * @return Результат сравнения (отрицательный, если a < b, 0 если равны, положительный если a > b)
 */
static int compare_processors(const void *a, const void *b) {
    const packet_processor_t *p1 = (const packet_processor_t *)a;
    const packet_processor_t *p2 = (const packet_processor_t *)b;
    
    // Сортировка по приоритету (меньшие числа идут первыми)
    // Для активных обработчиков
    if (p1->active && p2->active) {
        return (int)p1->priority - (int)p2->priority;
    }
    
    // Активные обработчики всегда идут перед неактивными
    if (p1->active && !p2->active) {
        return -1;
    }
    
    if (!p1->active && p2->active) {
        return 1;
    }
    
    // Если оба неактивны, порядок не имеет значения
    return 0;
}

/**
 * @brief Сортировка обработчиков по приоритету
 */
static void sort_processors(void) {
    pthread_mutex_lock(&g_processors_mutex);
    qsort(g_processors, g_processor_count, sizeof(packet_processor_t), compare_processors);
    pthread_mutex_unlock(&g_processors_mutex);
}

/**
 * @brief Обновление статистики пакетов на основе результата обработки
 *
 * @param result Результат обработки пакета
 */
static void update_packet_statistics(packet_result_t result) {
    pthread_mutex_lock(&g_packet_stats.mutex);
    
    g_packet_stats.packets_processed++;
    
    switch (result) {
        case PACKET_DROPPED:
            g_packet_stats.packets_dropped++;
            break;
        case PACKET_FORWARDED:
            g_packet_stats.packets_forwarded++;
            break;
        case PACKET_CONSUMED:
            g_packet_stats.packets_consumed++;
            break;
        case PACKET_RECIRCULATE:
            g_packet_stats.packets_recirculated++;
            break;
    }
    
    pthread_mutex_unlock(&g_packet_stats.mutex);
}

/**
 * @brief Получить буфер пакета из пула или выделить новый, если пул пуст
 * 
 * @param size Требуемый размер буфера данных
 * @return packet_buffer_t* Указатель на буфер пакета или NULL в случае ошибки
 */
static packet_buffer_t* get_buffer_from_pool(uint32_t size) {
    packet_buffer_t *packet = NULL;
    
    // Попытка получить буфер из пула
    pthread_mutex_lock(&g_buffer_pool.mutex);
    
    if (g_buffer_pool.count > 0 &&
        g_buffer_pool.buffers[g_buffer_pool.count - 1]->capacity >= size) {
        
        // Найден подходящий буфер в пуле
        packet = g_buffer_pool.buffers[--g_buffer_pool.count];
        packet->size = 0; // Сбросить размер
        memset(&packet->metadata, 0, sizeof(packet_metadata_t)); // Очистить метаданные
        packet->user_data = NULL;
        
        pthread_mutex_unlock(&g_buffer_pool.mutex);
        LOG_DEBUG(LOG_CATEGORY_HAL, "Получен буфер пакета из пула, размер %u", packet->capacity);
    } else {
        pthread_mutex_unlock(&g_buffer_pool.mutex);
        
        // Пул пуст или нет буфера подходящего размера, выделить новый
        packet = (packet_buffer_t *)malloc(sizeof(packet_buffer_t));
        if (packet) {
            memset(packet, 0, sizeof(packet_buffer_t));
            
            packet->data = (uint8_t *)malloc(size);
            if (!packet->data) {
                LOG_ERROR(LOG_CATEGORY_HAL, "Не удалось выделить буфер данных пакета размером %u", size);
                free(packet);
                return NULL;
            }
            
            packet->capacity = size;
            packet->size = 0;
            LOG_DEBUG(LOG_CATEGORY_HAL, "Выделен новый буфер пакета размером %u", size);
            
            // Обновить статистику
            pthread_mutex_lock(&g_packet_stats.mutex);
            g_packet_stats.buffer_alloc_count++;
            pthread_mutex_unlock(&g_packet_stats.mutex);
        }
    }
    
    return packet;
}

/**
 * @brief Вернуть буфер пакета в пул для повторного использования
 * 
 * @param packet Указатель на буфер пакета для возврата
 */
static void return_buffer_to_pool(packet_buffer_t *packet) {
    if (!packet) {
        return;
    }
    
    pthread_mutex_lock(&g_buffer_pool.mutex);
    
    if (g_buffer_pool.count < PACKET_BUFFER_POOL_SIZE) {
        // Вернуть буфер в пул
        g_buffer_pool.buffers[g_buffer_pool.count++] = packet;
        pthread_mutex_unlock(&g_buffer_pool.mutex);
        LOG_DEBUG(LOG_CATEGORY_HAL, "Буфер пакета возвращен в пул, текущий размер пула: %u", g_buffer_pool.count);
    } else {
        // Пул заполнен, освободить буфер
        pthread_mutex_unlock(&g_buffer_pool.mutex);
        if (packet->data) {
            free(packet->data);
        }
        free(packet);
        LOG_DEBUG(LOG_CATEGORY_HAL, "Пул буферов полон, буфер пакета освобожден");
        
        // Обновить статистику
        pthread_mutex_lock(&g_packet_stats.mutex);
        g_packet_stats.buffer_free_count++;
        pthread_mutex_unlock(&g_packet_stats.mutex);
    }
}

/**
 * @brief Инициализировать пул буферов пакетов
 * 
 * @return status_t Результат операции
 */
static status_t init_buffer_pool(void) {
    memset(&g_buffer_pool, 0, sizeof(packet_buffer_pool_t));
    
    if (pthread_mutex_init(&g_buffer_pool.mutex, NULL) != 0) {
        LOG_ERROR(LOG_CATEGORY_HAL, "Не удалось инициализировать мьютекс пула буферов");
        return STATUS_FAILURE;
    }
    
    // Предварительно выделить некоторые буферы для пула
    for (int i = 0; i < PACKET_BUFFER_POOL_SIZE / 4; i++) {
        packet_buffer_t *packet = (packet_buffer_t *)malloc(sizeof(packet_buffer_t));
        if (!packet) {
            LOG_WARNING(LOG_CATEGORY_HAL, "Не удалось предварительно выделить буфер пакета для пула");
            continue;
        }
        
        memset(packet, 0, sizeof(packet_buffer_t));
        packet->data = (uint8_t *)malloc(PACKET_BUFFER_POOL_BUFFER_SIZE);
        if (!packet->data) {
            LOG_WARNING(LOG_CATEGORY_HAL, "Не удалось выделить буфер данных для пакета пула");
            free(packet);
            continue;
        }
        
        packet->capacity = PACKET_BUFFER_POOL_BUFFER_SIZE;
        g_buffer_pool.buffers[g_buffer_pool.count++] = packet;
        
        // Обновить статистику
        pthread_mutex_lock(&g_packet_stats.mutex);
        g_packet_stats.buffer_alloc_count++;
        pthread_mutex_unlock(&g_packet_stats.mutex);
    }
    
    LOG_INFO(LOG_CATEGORY_HAL, "Пул буферов пакетов инициализирован, предварительно выделено %u буферов", 
             g_buffer_pool.count);
    
    return STATUS_SUCCESS;
}

/**
 * @brief Инициализировать подсистему обработки пакетов
 * 
 * Инициализирует все необходимые структуры данных для обработки пакетов,
 * включая список обработчиков пакетов и пул буферов.
 * 
 * @return status_t STATUS_SUCCESS при успехе, код ошибки в противном случае
 */
status_t packet_init(void) {
    LOG_INFO(LOG_CATEGORY_HAL, "Инициализация подсистемы обработки пакетов");
    
    if (g_initialized) {
        LOG_WARNING(LOG_CATEGORY_HAL, "Подсистема обработки пакетов уже инициализ
