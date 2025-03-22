/**
 * @file stats_collector.c
 * @brief Реализация системы сбора и управления статистикой коммутатора
 *
 * Этот модуль отвечает за сбор, хранение и предоставление
 * доступа к различным статистическим данным о работе коммутатора,
 * включая статистику по портам, VLAN, таблицам MAC-адресов и маршрутизации.
 */

#include "management/stats_collector.h"
#include "common/logging.h"
#include "common/error_codes.h"
#include "common/types.h"
#include "hal/port.h"
#include "l2/mac_table.h"
#include "l2/vlan.h"
#include "l3/routing_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>

/* Константы */
#define MAX_PORTS               64
#define MAX_VLANS               4096
#define STATS_COLLECT_INTERVAL  5     // Интервал сбора статистики в секундах
#define STATS_HISTORY_SIZE      300   // Размер истории для каждого счетчика (для хранения значений за 5 минут при интервале 1 сек)
#define MAX_COUNTER_NAME_LEN    64
#define MAX_MAC_TABLE_SIZE      16384
#define MAX_ROUTE_TABLE_SIZE    8192

/* Типы данных */
typedef enum {
    COUNTER_TYPE_PACKETS,
    COUNTER_TYPE_BYTES,
    COUNTER_TYPE_ERRORS,
    COUNTER_TYPE_DROPS,
    COUNTER_TYPE_COLLISIONS,
    COUNTER_TYPE_MAC_ENTRIES,
    COUNTER_TYPE_ROUTE_ENTRIES,
    COUNTER_TYPE_CPU_USAGE,
    COUNTER_TYPE_MEMORY_USAGE,
    COUNTER_TYPE_CUSTOM,
    COUNTER_TYPE_MAX
} counter_type_t;

typedef struct {
    char name[MAX_COUNTER_NAME_LEN];
    counter_type_t type;
    uint64_t current_value;
    uint64_t history[STATS_HISTORY_SIZE];
    uint32_t history_index;
    bool is_rate;                  // Счетчик скорости (значения в секунду) или абсолютный
    bool is_active;                // Счетчик активен
    time_t last_update;            // Время последнего обновления
} counter_t;

typedef struct {
    counter_t rx_packets;          // Принятые пакеты
    counter_t tx_packets;          // Отправленные пакеты
    counter_t rx_bytes;            // Принятые байты
    counter_t tx_bytes;            // Отправленные байты
    counter_t rx_errors;           // Ошибки приема
    counter_t tx_errors;           // Ошибки передачи
    counter_t rx_drops;            // Отброшенные пакеты (прием)
    counter_t tx_drops;            // Отброшенные пакеты (передача)
    counter_t collisions;          // Коллизии
} port_stats_t;

typedef struct {
    counter_t rx_packets;          // Принятые пакеты
    counter_t tx_packets;          // Отправленные пакеты
    counter_t rx_bytes;            // Принятые байты
    counter_t tx_bytes;            // Отправленные байты
} vlan_stats_t;

counter_t mac_entries;             // Количество MAC-адресов
    counter_t mac_learning_rate;   // Скорость изучения MAC-адресов
    counter_t mac_aging_rate;      // Скорость устаревания MAC-адресов
} mac_table_stats_t;

typedef struct {
    counter_t route_entries;       // Количество маршрутов
    counter_t route_lookups;       // Количество поисков маршрута
    counter_t route_hits;          // Успешные поиски маршрута
    counter_t route_misses;        // Неудачные поиски маршрута
} routing_stats_t;

typedef struct {
    counter_t cpu_usage;           // Использование CPU (%)
    counter_t memory_usage;        // Использование памяти (%)
    counter_t uptime;              // Время работы (секунды)
} system_stats_t;

/* Глобальные переменные */
static struct {
    bool initialized;
    pthread_t collector_thread;
    pthread_mutex_t stats_mutex;
    bool collector_running;
    port_stats_t port_stats[MAX_PORTS];
    vlan_stats_t vlan_stats[MAX_VLANS];
    mac_table_stats_t mac_table_stats;
    routing_stats_t routing_stats;
    system_stats_t system_stats;
    time_t stats_start_time;
    time_t last_collection_time;
} g_stats_collector = {0};

/* Локальные функции */
static void *stats_collector_thread_func(void *arg);
static void init_counter(counter_t *counter, const char *name, counter_type_t type, bool is_rate);
static void update_counter(counter_t *counter, uint64_t new_value);
static sw_error_t collect_port_stats(void);
static sw_error_t collect_vlan_stats(void);
static sw_error_t collect_mac_table_stats(void);
static sw_error_t collect_routing_stats(void);
static sw_error_t collect_system_stats(void);

/**
 * @brief Инициализация системы сбора статистики
 * 
 * @return SW_OK если успешно, иначе код ошибки
 */
sw_error_t stats_collector_init(void) {
    int i, j;

    if (g_stats_collector.initialized) {
        LOG_WARN("Stats collector already initialized");
        return SW_OK;
    }

    // Инициализируем мьютекс
    if (pthread_mutex_init(&g_stats_collector.stats_mutex, NULL) != 0) {
        LOG_ERROR("Failed to initialize stats mutex");
        return SW_ERROR_INIT_FAILED;
    }

    // Инициализируем счетчики для портов
    for (i = 0; i < MAX_PORTS; i++) {
        char name[MAX_COUNTER_NAME_LEN];
        
        snprintf(name, sizeof(name), "port%d_rx_packets", i);
        init_counter(&g_stats_collector.port_stats[i].rx_packets, name, COUNTER_TYPE_PACKETS, false);
        
        snprintf(name, sizeof(name), "port%d_tx_packets", i);
        init_counter(&g_stats_collector.port_stats[i].tx_packets, name, COUNTER_TYPE_PACKETS, false);
        
        snprintf(name, sizeof(name), "port%d_rx_bytes", i);
        init_counter(&g_stats_collector.port_stats[i].rx_bytes, name, COUNTER_TYPE_BYTES, false);
        
        snprintf(name, sizeof(name), "port%d_tx_bytes", i);
        init_counter(&g_stats_collector.port_stats[i].tx_bytes, name, COUNTER_TYPE_BYTES, false);
        
        snprintf(name, sizeof(name), "port%d_rx_errors", i);
        init_counter(&g_stats_collector.port_stats[i].rx_errors, name, COUNTER_TYPE_ERRORS, false);
        
        snprintf(name, sizeof(name), "port%d_tx_errors", i);
        init_counter(&g_stats_collector.port_stats[i].tx_errors, name, COUNTER_TYPE_ERRORS, false);
        
        snprintf(name, sizeof(name), "port%d_rx_drops", i);
        init_counter(&g_stats_collector.port_stats[i].rx_drops, name, COUNTER_TYPE_DROPS, false);
        
        snprintf(name, sizeof(name), "port%d_tx_drops", i);
        init_counter(&g_stats_collector.port_stats[i].tx_drops, name, COUNTER_TYPE_DROPS, false);
        
        snprintf(name, sizeof(name), "port%d_collisions", i);
        init_counter(&g_stats_collector.port_stats[i].collisions, name, COUNTER_TYPE_COLLISIONS, false);
    }

    // Инициализируем счетчики для VLAN
    for (i = 0; i < MAX_VLANS; i++) {
        char name[MAX_COUNTER_NAME_LEN];
        
        snprintf(name, sizeof(name), "vlan%d_rx_packets", i);
        init_counter(&g_stats_collector.vlan_stats[i].rx_packets, name, COUNTER_TYPE_PACKETS, false);
        
        snprintf(name, sizeof(name), "vlan%d_tx_packets", i);
        init_counter(&g_stats_collector.vlan_stats[i].tx_packets, name, COUNTER_TYPE_PACKETS, false);
        
        snprintf(name, sizeof(name), "vlan%d_rx_bytes", i);
        init_counter(&g_stats_collector.vlan_stats[i].rx_bytes, name, COUNTER_TYPE_BYTES, false);
        
        snprintf(name, sizeof(name), "vlan%d_tx_bytes", i);
        init_counter(&g_stats_collector.vlan_stats[i].tx_bytes, name, COUNTER_TYPE_BYTES, false);
    }

    // Инициализируем счетчики для таблицы MAC-адресов
    init_counter(&g_stats_collector.mac_table_stats.mac_entries, "mac_entries", COUNTER_TYPE_MAC_ENTRIES, false);
    init_counter(&g_stats_collector.mac_table_stats.mac_learning_rate, "mac_learning_rate", COUNTER_TYPE_MAC_ENTRIES, true);
    init_counter(&g_stats_collector.mac_table_stats.mac_aging_rate, "mac_aging_rate", COUNTER_TYPE_MAC_ENTRIES, true);

    // Инициализируем счетчики для таблицы маршрутизации
    init_counter(&g_stats_collector.routing_stats.route_entries, "route_entries", COUNTER_TYPE_ROUTE_ENTRIES, false);
    init_counter(&g_stats_collector.routing_stats.route_lookups, "route_lookups", COUNTER_TYPE_ROUTE_ENTRIES, false);
    init_counter(&g_stats_collector.routing_stats.route_hits, "route_hits", COUNTER_TYPE_ROUTE_ENTRIES, false);
    init_counter(&g_stats_collector.routing_stats.route_misses, "route_misses", COUNTER_TYPE_ROUTE_ENTRIES, false);

    // Инициализируем счетчики для системы
    init_counter(&g_stats_collector.system_stats.cpu_usage, "cpu_usage", COUNTER_TYPE_CPU_USAGE, false);
    init_counter(&g_stats_collector.system_stats.memory_usage, "memory_usage", COUNTER_TYPE_MEMORY_USAGE, false);
    init_counter(&g_stats_collector.system_stats.uptime, "uptime", COUNTER_TYPE_CUSTOM, false);

    g_stats_collector.stats_start_time = time(NULL);
    g_stats_collector.last_collection_time = g_stats_collector.stats_start_time;
    g_stats_collector.collector_running = true;
    g_stats_collector.initialized = true;

    // Создаем поток для сбора статистики
    if (pthread_create(&g_stats_collector.collector_thread, NULL, stats_collector_thread_func, NULL) != 0) {
        LOG_ERROR("Failed to create stats collector thread");
        pthread_mutex_destroy(&g_stats_collector.stats_mutex);
        g_stats_collector.initialized = false;
        g_stats_collector.collector_running = false;
        return SW_ERROR_THREAD_CREATE_FAILED;
    }

    LOG_INFO("Stats collector initialized successfully");
    return SW_OK;
}

/**
 * @brief Деинициализация системы сбора статистики
 * 
 * @return SW_OK если успешно, иначе код ошибки
 */
sw_error_t stats_collector_deinit(void) {
    if (!g_stats_collector.initialized) {
        LOG_WARN("Stats collector not initialized");
        return SW_OK;
    }

    // Останавливаем поток сбора статистики
    g_stats_collector.collector_running = false;
    pthread_join(g_stats_collector.collector_thread, NULL);

    pthread_mutex_destroy(&g_stats_collector.stats_mutex);
    g_stats_collector.initialized = false;

    LOG_INFO("Stats collector deinitialized successfully");
    return SW_OK;
}

/**
 * @brief Функция потока сбора статистики
 * 
 * @param arg Аргументы потока (не используются)
 * @return Указатель на результат работы потока (не используется)
 */
static void *stats_collector_thread_func(void *arg) {
    sw_error_t err;

    LOG_INFO("Stats collector thread started");

    while (g_stats_collector.collector_running) {
        // Собираем статистику
        err = collect_port_stats();
        if (err != SW_OK) {
            LOG_WARN("Failed to collect port stats, error: %d", err);
        }

        err = collect_vlan_stats();
        if (err != SW_OK) {
            LOG_WARN("Failed to collect VLAN stats, error: %d", err);
        }

        err = collect_mac_table_stats();
        if (err != SW_OK) {
            LOG_WARN("Failed to collect MAC table stats, error: %d", err);
        }

        err = collect_routing_stats();
        if (err != SW_OK) {
            LOG_WARN("Failed to collect routing stats, error: %d", err);
        }

        err = collect_system_stats();
        if (err != SW_OK) {
            LOG_WARN("Failed to collect system stats, error: %d", err);
        }

        g_stats_collector.last_collection_time = time(NULL);

        // Спим до следующего сбора статистики
        sleep(STATS_COLLECT_INTERVAL);
    }

    LOG_INFO("Stats collector thread stopped");
    return NULL;
}

/**
 * @brief Инициализация счетчика
 * 
 * @param counter Указатель на счетчик
 * @param name Название счетчика
 * @param type Тип счетчика
 * @param is_rate Счетчик скорости или абсолютный
 */
static void init_counter(counter_t *counter, const char *name, counter_type_t type, bool is_rate) {
    if (counter == NULL || name == NULL) {
        return;
    }

    memset(counter, 0, sizeof(counter_t));
    strncpy(counter->name, name, MAX_COUNTER_NAME_LEN - 1);
    counter->name[MAX_COUNTER_NAME_LEN - 1] = '\0';
    counter->type = type;
    counter->current_value = 0;
    counter->history_index = 0;
    counter->is_rate = is_rate;
    counter->is_active = false;
    counter->last_update = time(NULL);

    memset(counter->history, 0, sizeof(counter->history));
}

/**
 * @brief Обновление значения счетчика
 * 
 * @param counter Указатель на счетчик
 * @param new_value Новое значение счетчика
 */
static void update_counter(counter_t *counter, uint64_t new_value) {
    time_t current_time;

    if (counter == NULL) {
        return;
    }

    pthread_mutex_lock(&g_stats_collector.stats_mutex);

    current_time = time(NULL);

    // Если это счетчик скорости, вычисляем скорость изменения в единицу времени
    if (counter->is_rate && counter->is_active) {
        time_t time_diff = current_time - counter->last_update;
        if (time_diff > 0) {
            if (new_value >= counter->current_value) {
                counter->current_value = (new_value - counter->current_value) / time_diff;
            } else {
                // Счетчик мог быть сброшен
                counter->current_value = new_value / time_diff;
            }
        }
    } else {
        counter->current_value = new_value;
    }

    // Обновляем историю
    counter->history[counter->history_index] = counter->current_value;
    counter->history_index = (counter->history_index + 1) % STATS_HISTORY_SIZE;
    counter->last_update = current_time;
    counter->is_active = true;

    pthread_mutex_unlock(&g_stats_collector.stats_mutex);
}

/**
 * @brief Сбор статистики по портам
 * 
 * @return SW_OK если успешно, иначе код ошибки
 */
static sw_error_t collect_port_stats(void) {
    // Здесь должен быть код для получения статистики от драйверов портов
    // В реальном проекте здесь будут вызовы API для получения статистики
    
    // Примечание: Эта функция является заглушкой и должна быть доработана
    // в соответствии с реальными интерфейсами драйверов портов

    // Для демонстрации обновляем счетчики случайными значениями
    for (int i = 0; i < MAX_PORTS; i++) {
        // Только для первых 8 портов, которые считаем активными
        if (i < 8) {
            // Имитируем рост счетчиков
            static uint64_t port_rx_packets[MAX_PORTS] = {0};
            static uint64_t port_tx_packets[MAX_PORTS] = {0};
            static uint64_t port_rx_bytes[MAX_PORTS] = {0};
            static uint64_t port_tx_bytes[MAX_PORTS] = {0};
            
            // Увеличиваем счетчики пакетов и байт
            port_rx_packets[i] += rand() % 100;
            port_tx_packets[i] += rand() % 100;
            port_rx_bytes[i] += (rand() % 100) * 1500;
            port_tx_bytes[i] += (rand() % 100) * 1500;
            
            // Обновляем счетчики
            update_counter(&g_stats_collector.port_stats[i].rx_packets, port_rx_packets[i]);
            update_counter(&g_stats_collector.port_stats[i].tx_packets, port_tx_packets[i]);
            update_counter(&g_stats_collector.port_stats[i].rx_bytes, port_rx_bytes[i]);
            update_counter(&g_stats_collector.port_stats[i].tx_bytes, port_tx_bytes[i]);
            
            // Иногда генерируем ошибки
            if (rand() % 100 < 5) {
                static uint64_t port_rx_errors[MAX_PORTS] = {0};
                port_rx_errors[i]++;
                update_counter(&g_stats_collector.port_stats[i].rx_errors, port_rx_errors[i]);
            }
            
            if (rand() % 100 < 5) {
                static uint64_t port_tx_errors[MAX_PORTS] = {0};
                port_tx_errors[i]++;
                update_counter(&g_stats_collector.port_stats[i].tx_errors, port_tx_errors[i]);
            }
            
            // Иногда генерируем отброшенные пакеты
            if (rand() % 100 < 3) {
                static uint64_t port_rx_drops[MAX_PORTS] = {0};
                port_rx_drops[i]++;
                update_counter(&g_stats_collector.port_stats[i].rx_drops, port_rx_drops[i]);
            }
            
            if (rand() % 100 < 3) {
                static uint64_t port_tx_drops[MAX_PORTS] = {0};
                port_tx_drops[i]++;
                update_counter(&g_stats_collector.port_stats[i].tx_drops, port_tx_drops[i]);
            }
            
            // Иногда генерируем коллизии
            if (rand() % 100 < 2) {
                static uint64_t port_collisions[MAX_PORTS] = {0};
                port_collisions[i]++;
                update_counter(&g_stats_collector.port_stats[i].collisions, port_collisions[i]);
            }
        }
    }

    return SW_OK;
}

/**
 * @brief Сбор статистики по VLAN
 * 
 * @return SW_OK если успешно, иначе код ошибки
 */
static sw_error_t collect_vlan_stats(void) {
    // Здесь должен быть код для получения статистики от модуля VLAN
    // В реальном проекте здесь будут вызовы API для получения статистики
    
    // Примечание: Эта функция является заглушкой и должна быть доработана
    // в соответствии с реальными интерфейсами модуля VLAN

    // Для демонстрации обновляем счетчики случайными значениями
    for (int i = 1; i < 10; i++) {  // VLAN 1-9
        // Имитируем рост счетчиков
        static uint64_t vlan_rx_packets[MAX_VLANS] = {0};
        static uint64_t vlan_tx_packets[MAX_VLANS] = {0};
        static uint64_t vlan_rx_bytes[MAX_VLANS] = {0};
        static uint64_t vlan_tx_bytes[MAX_VLANS] = {0};
        
        // Увеличиваем счетчики пакетов и байт
        vlan_rx_packets[i] += rand() % 200;
        vlan_tx_packets[i] += rand() % 200;
        vlan_rx_bytes[i] += (rand() % 200) * 1500;
        vlan_tx_bytes[i] += (rand() % 200) * 1500;
        
        // Обновляем счетчики
        update_counter(&g_stats_collector.vlan_stats[i].rx_packets, vlan_rx_packets[i]);
        update_counter(&g_stats_collector.vlan_stats[i].tx_packets, vlan_tx_packets[i]);
        update_counter(&g_stats_collector.vlan_stats[i].rx_bytes, vlan_rx_bytes[i]);
        update_counter(&g_stats_collector.vlan_stats[i].tx_bytes, vlan_tx_bytes[i]);
    }

    return SW_OK;
}

/**
 * @brief Сбор статистики по таблице MAC-адресов
 * 
 * @return SW_OK если успешно, иначе код ошибки
 */
static sw_error_t collect_mac_table_stats(void) {
    // Здесь должен быть код для получения статистики от модуля таблицы MAC-адресов
    // В реальном проекте здесь будут вызовы API для получения статистики
    
    // Примечание: Эта функция является заглушкой и должна быть доработана
    // в соответствии с реальными интерфейсами модуля таблицы MAC-адресов

    // Для демонстрации обновляем счетчики случайными значениями
    static uint64_t mac_entries = 100;     // Начинаем со 100 записей
    static uint64_t learned_macs = 0;
    static uint64_t aged_macs = 0;
    
    // Имитируем изучение новых MAC-адресов
    uint64_t new_learned = rand() % 10;
    
    // Имитируем устаревание MAC-адресов
    uint64_t new_aged = rand() % 5;
    
    // Обновляем количество MAC-адресов
    mac_entries = mac_entries + new_learned - new_aged;
    
    // Обновляем счетчики изученных и устаревших MAC-адресов
    learned_macs += new_learned;
    aged_macs += new_aged;
    
    // Обновляем счетчики
    update_counter(&g_stats_collector.mac_table_stats.mac_entries, mac_entries);
    update_counter(&g_stats_collector.mac_table_stats.mac_learning_rate, new_learned);
    update_counter(&g_stats_collector.mac_table_stats.mac_aging_rate, new_aged);

    return SW_OK;
}

/**
 * @brief Сбор статистики по таблице маршрутизации
 * 
 * @return SW_OK если успешно, иначе код ошибки
 */
static sw_error_t collect_routing_stats(void) {
    // Здесь должен быть код для получения статистики от модуля маршрутизации
    // В реальном проекте здесь будут вызовы API для получения статистики
    
    // Примечание: Эта функция является заглушкой и должна быть доработана
    // в соответствии с реальными интерфейсами модуля маршрутизации

    // Для демонстрации обновляем счетчики случайными значениями
    static uint64_t route_entries = 50;     // Начинаем с 50 маршрутов
    static uint64_t route_lookups_total = 0;
    static uint64_t route_hits_total = 0;
    static uint64_t route_misses_total = 0;
    
    // Имитируем поиск маршрутов
    uint64_t new_lookups = rand() % 100;
    
    // Имитируем успешные и неудачные поиски
    uint64_t new_hits = new_lookups * 0.95;  // 95% успешных поисков
    uint64_t new_misses = new_lookups - new_hits;
    
    // Обновляем счетчики
    route_lookups_total += new_lookups;
    route_hits_total += new_hits;
    route_misses_total += new_misses;
    
    // Обновляем счетчики
    update_counter(&g_stats_collector.routing_stats.route_entries, route_entries);
    update_counter(&g_stats_collector.routing_stats.route_lookups, route_lookups_total);
    update_counter(&g_stats_collector.routing_stats.route_hits, route_hits_total);
    update_counter(&g_stats_collector.routing_stats.route_misses, route_misses_total);

    return SW_OK;
}

/**
 * @brief Сбор системной статистики
 * 
 * @return SW_OK если успешно, иначе код ошибки
 */
static sw_error_t collect_system_stats(void) {
    // Здесь должен быть код для получения системной статистики
    // В реальном проекте здесь будут вызовы API для получения статистики
    
    // Примечание: Эта функция является заглушкой и должна быть доработана
    // в соответствии с реальными интерфейсами системы

    // Для демонстрации обновляем счетчики случайными значениями
    uint64_t cpu_usage = 10 + (rand() % 40);   // 10-50% CPU
    uint64_t memory_usage = 20 + (rand() % 30); // 20-50% памяти
    
    // Вычисляем время работы
    time_t current_time = time(NULL);
    uint64_t uptime = current_time - g_stats_collector.stats_start_time;
    
    // Обновляем счетчики
    update_counter(&g_stats_collector.system_stats.cpu_usage, cpu_usage);
    update_counter(&g_stats_collector.system_stats.memory_usage, memory_usage);
    update_counter(&g_stats_collector.system_stats.uptime, uptime);

    return SW_OK;
}

/**
 * @brief Получение статистики порта
 * 
 * @param port_id Идентификатор порта
 * @param stats Указатель на структуру для статистики порта
 * @return SW_OK если успешно, иначе код ошибки
 */
sw_error_t stats_collector_get_port_stats(uint32_t port_id, sw_port_stats_t *stats) {
    if (!g_stats_collector.initialized) {
        LOG_ERROR("Stats collector not initialized");
        return SW_ERROR_NOT_INITIALIZED;
    }

    if (port_id >= MAX_PORTS || stats == NULL) {
        return SW_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&g_stats_collector.stats_mutex);

    stats->rx_packets = g_stats_collector.port_stats[port_id].rx_packets.current_value;
    stats->tx_packets = g_stats_collector.port_stats[port_id].tx_packets.current_value;
    stats->rx_bytes = g_stats_collector.port_stats[port_id].rx_bytes.current_value;
    stats->tx_bytes = g_stats_collector.port_stats[port_id].tx_bytes.current_value;
    stats->rx_errors = g_stats_collector.port_stats[port_id].rx_errors.current_value;
    stats->tx_errors = g_stats_collector.port_stats[port_id].tx_errors.current_value;
    stats->rx_drops = g_stats_collector.port_stats[port_id].rx_drops.current_value;
    stats->tx_drops = g_stats_collector.port_stats[port_id].tx_drops.current_value;
    stats->collisions = g_stats_collector.port_stats[port_id].collisions.current_value;

    pthread_mutex_unlock(&g_stats_collector.stats_mutex);
    return SW_OK;
}

/**
 * @brief Получение статистики VLAN
 * 
 * @param vlan_id Идентификатор VLAN
 * @param stats Указатель на структуру для статистики VLAN
 * @return SW_OK если успешно, иначе код ошибки
 */
sw_error_t stats_collector_get_vlan_stats(uint16_t vlan_id, sw_vlan_stats_t *stats) {
    if (!g_stats_collector.initialized) {
        LOG_ERROR("Stats collector not initialized");
        return SW_ERROR_NOT_INITIALIZED;
    }

    if (vlan_id >= MAX_VLANS || vlan_id == 0 || stats == NULL) {
        return SW_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&g_stats_collector.stats_mutex);

    stats->rx_packets = g_stats_collector.vlan_stats[vlan_id].rx_packets.current_value;
    stats->tx_packets = g_stats_collector.vlan_stats[vlan_id].tx_packets.current_value;
    stats->rx_bytes = g_stats_collector.vlan_stats[vlan_id].rx_bytes.current_value;
    stats->tx_bytes = g_stats_collector.vlan_stats[vlan_id].tx_bytes.current_value;

    pthread_mutex_unlock(&g_stats_collector.stats_mutex);
    return SW_OK;
}

/**
 * @brief Получение статистики таблицы MAC-адресов
 * 
 * @param stats Указатель на структуру для статистики таблицы MAC-адресов
 * @return SW_OK если успешно, иначе код ошибки
 */
sw_error_t stats_collector_get_mac_table_stats(sw_mac_table_stats_t *stats) {
    if (!g_stats_collector.initialized) {
        LOG_ERROR("Stats collector not initialized");
        return SW_ERROR_NOT_INITIALIZED;
    }

    if (stats == NULL) {
        return SW_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&g_stats_collector.stats_mutex);

    stats->mac_entries = g_stats_collector.mac_table_stats.mac_entries.current_value;
    stats->mac_learning_rate = g_stats_collector.mac_table_stats.mac_learning_rate.current_value;
    stats->mac_aging_rate = g_stats_collector.mac_table_stats.mac_aging_rate.current_value;

    pthread_mutex_unlock(&g_stats_collector.stats_mutex);
    return SW_OK;
}

/**
 * @brief Получение статистики таблицы маршрутизации
 * 
 * @param stats Указатель на структуру для статистики таблицы маршрутизации
 * @return SW_OK если успешно, иначе код ошибки
 */
sw_error_t stats_collector_get_routing_stats(sw_routing_stats_t *stats) {
    if (!g_stats_collector.initialized) {
        LOG_ERROR("Stats collector not initialized");
        return SW_ERROR_NOT_INITIALIZED;
    }

    if (stats == NULL) {
        return SW_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&g_stats_collector.stats_mutex);

    stats->route_entries = g_stats_collector.routing_stats.route_entries.current_value;
    stats->route_lookups = g_stats_collector.routing_stats.route_lookups.current_value;
    stats->route_hits = g_stats_collector.routing_stats.route_hits.current_value;
    stats->route_misses = g














