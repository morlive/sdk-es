/**
 * @file rip.c
 * @brief Реализация протокола маршрутизации RIP (Routing Information Protocol)
 * 
 * Данный файл содержит реализацию функций для поддержки протокола RIP
 * в симуляторе коммутатора. Включает в себя обработку RIP-сообщений,
 * поддержку таблицы маршрутизации, механизмы распространения маршрутов
 * и таймеры для поддержания актуальности маршрутной информации.
 */

#include "../../include/l3/routing_protocols.h"
#include "../../include/common/logging.h"
#include "../../include/common/error_codes.h"
#include "../../include/l3/routing_table.h"
#include "../../include/hal/packet.h"
#include "../../include/common/types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Константы протокола RIP */
#define RIP_PORT                520     // Стандартный UDP порт для RIP
#define RIP_VERSION             2       // Версия RIP (используем RIPv2)
#define RIP_UPDATE_INTERVAL     30      // Интервал обновления в секундах
#define RIP_TIMEOUT             180     // Таймаут маршрута в секундах
#define RIP_GARBAGE_TIMEOUT     120     // Время до удаления маршрута в секундах
#define RIP_INFINITY            16      // Значение метрики "бесконечность"
#define RIP_MAX_ENTRIES         25      // Максимальное количество записей в пакете

/* RIP команды */
#define RIP_CMD_REQUEST         1
#define RIP_CMD_RESPONSE        2

/* Структуры для работы с RIP */

/**
 * Структура записи маршрута RIP
 */
typedef struct {
    uint16_t family;            // Семейство адресов (обычно AF_INET = 2)
    uint16_t route_tag;         // Тэг маршрута (используется для внешних маршрутов)
    uint32_t ip_address;        // IP-адрес
    uint32_t subnet_mask;       // Маска подсети
    uint32_t next_hop;          // IP адрес следующего хопа
    uint32_t metric;            // Метрика маршрута (1-15, 16 = бесконечность)
} rip_entry_t;

/**
 * Структура заголовка пакета RIP
 */
typedef struct {
    uint8_t command;            // Команда (1 - Request, 2 - Response)
    uint8_t version;            // Версия протокола
    uint16_t zero;              // Зарезервировано, должно быть 0
} rip_header_t;

/**
 * Структура пакета RIP
 */
typedef struct {
    rip_header_t header;                // Заголовок RIP
    rip_entry_t entries[RIP_MAX_ENTRIES]; // Записи маршрутов
    uint8_t entry_count;                // Количество записей в пакете
} rip_packet_t;

/**
 * Структура записи таблицы RIP
 */
typedef struct rip_table_entry {
    uint32_t ip_address;        // Адрес сети
    uint32_t subnet_mask;       // Маска подсети
    uint32_t next_hop;          // Адрес следующего хопа
    uint32_t metric;            // Метрика маршрута
    uint32_t port_id;           // ID порта, через который доступен маршрут
    time_t last_update;         // Время последнего обновления
    uint8_t is_garbage;         // Флаг "мусорного" маршрута
    struct rip_table_entry *next; // Указатель на следующую запись (для связного списка)
} rip_table_entry_t;

/* Глобальные переменные */
static rip_table_entry_t *rip_table = NULL;        // Таблица маршрутов RIP
static time_t last_update_time = 0;                // Время последнего отправленного обновления
static bool rip_enabled = false;                   // Флаг включения RIP
static uint32_t rip_router_id = 0;                 // ID маршрутизатора (обычно IP-адрес)

/* Прототипы статических функций */
static int rip_process_request(const packet_t *packet, uint32_t src_ip, uint32_t port_id);
static int rip_process_response(const packet_t *packet, uint32_t src_ip, uint32_t port_id);
static int rip_send_update(bool triggered, uint32_t dest_ip, uint32_t port_id);
static int rip_add_route_to_table(uint32_t ip, uint32_t mask, uint32_t next_hop, uint32_t metric, uint32_t port_id);
static void rip_update_timers(void);
static void rip_sync_with_routing_table(void);
static rip_table_entry_t *find_route_in_table(uint32_t ip, uint32_t mask);
static void rip_packet_to_buffer(const rip_packet_t *packet, uint8_t *buffer, size_t *len);
static void buffer_to_rip_packet(const uint8_t *buffer, size_t len, rip_packet_t *packet);
static void rip_free_table(void);

/**
 * Инициализация протокола RIP
 * 
 * @param router_id ID маршрутизатора (обычно IP-адрес интерфейса)
 * @return STATUS_SUCCESS при успешной инициализации, код ошибки в противном случае
 */
int rip_init(uint32_t router_id)
{
    LOG_INFO("Initializing RIP protocol with router ID %u.%u.%u.%u",
            (router_id >> 24) & 0xFF,
            (router_id >> 16) & 0xFF,
            (router_id >> 8) & 0xFF,
            router_id & 0xFF);
    
    // Освобождаем ресурсы, если RIP уже был инициализирован
    if (rip_table != NULL) {
        rip_free_table();
    }
    
    rip_table = NULL;
    rip_router_id = router_id;
    last_update_time = time(NULL);
    rip_enabled = true;
    
    // Регистрация в таблице маршрутизации как протокол
    int ret = routing_register_protocol(ROUTING_PROTOCOL_RIP, rip_process_routes);
    if (ret != STATUS_SUCCESS) {
        LOG_ERROR("Failed to register RIP with routing table, error: %d", ret);
        return ret;
    }
    
    // Отправка первоначального запроса соседям
    rip_packet_t request_packet;
    memset(&request_packet, 0, sizeof(request_packet));
    request_packet.header.command = RIP_CMD_REQUEST;
    request_packet.header.version = RIP_VERSION;
    
    // Добавляем запрос на все маршруты (адрес 0.0.0.0/0 с метрикой 16)
    request_packet.entries[0].family = 2; // AF_INET
    request_packet.entries[0].ip_address = 0;
    request_packet.entries[0].subnet_mask = 0;
    request_packet.entries[0].metric = RIP_INFINITY;
    request_packet.entry_count = 1;
    
    // Преобразуем пакет в буфер
    uint8_t buffer[1500]; // Достаточный размер для UDP пакета
    size_t buffer_len = 0;
    rip_packet_to_buffer(&request_packet, buffer, &buffer_len);
    
    // Отправляем запрос на мультикаст или широковещательный адрес
    packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.data = buffer;
    packet.len = buffer_len;
    packet.protocol = PROTOCOL_UDP;
    packet.src_port = RIP_PORT;
    packet.dst_port = RIP_PORT;
    packet.src_ip = router_id;
    packet.dst_ip = 0xFFFFFFFF; // Широковещательный адрес 255.255.255.255
    
    // Отправляем через все доступные интерфейсы
    port_list_t ports;
    if (get_active_ports(&ports) == STATUS_SUCCESS) {
        for (uint32_t i = 0; i < ports.count; i++) {
            packet_send(&packet, ports.port_ids[i]);
        }
    }
    
    LOG_INFO("RIP initialization completed successfully");
    return STATUS_SUCCESS;
}

/**
 * Завершение работы протокола RIP и освобождение ресурсов
 * 
 * @return STATUS_SUCCESS при успешном завершении, код ошибки в противном случае
 */
int rip_shutdown(void)
{
    LOG_INFO("Shutting down RIP protocol");
    
    if (!rip_enabled) {
        LOG_WARN("RIP is not running, nothing to shutdown");
        return STATUS_SUCCESS;
    }
    
    // Отправляем уведомление соседям о недоступности наших маршрутов
    rip_packet_t poison_packet;
    memset(&poison_packet, 0, sizeof(poison_packet));
    poison_packet.header.command = RIP_CMD_RESPONSE;
    poison_packet.header.version = RIP_VERSION;
    
    uint8_t entry_index = 0;
    rip_table_entry_t *entry = rip_table;
    while (entry != NULL && entry_index < RIP_MAX_ENTRIES) {
        poison_packet.entries[entry_index].family = 2; // AF_INET
        poison_packet.entries[entry_index].ip_address = entry->ip_address;
        poison_packet.entries[entry_index].subnet_mask = entry->subnet_mask;
        poison_packet.entries[entry_index].next_hop = 0;
        poison_packet.entries[entry_index].metric = RIP_INFINITY; // Устанавливаем метрику в бесконечность
        
        entry = entry->next;
        entry_index++;
    }
    
    if (entry_index > 0) {
        poison_packet.entry_count = entry_index;
        
        // Преобразуем пакет в буфер
        uint8_t buffer[1500];
        size_t buffer_len = 0;
        rip_packet_to_buffer(&poison_packet, buffer, &buffer_len);
        
        // Отправляем отравленные маршруты на мультикаст адрес
        packet_t packet;
        memset(&packet, 0, sizeof(packet));
        packet.data = buffer;
        packet.len = buffer_len;
        packet.protocol = PROTOCOL_UDP;
        packet.src_port = RIP_PORT;
        packet.dst_port = RIP_PORT;
        packet.src_ip = rip_router_id;
        packet.dst_ip = 0xFFFFFFFF; // Широковещательный адрес 255.255.255.255
        
        // Отправляем через все доступные интерфейсы
        port_list_t ports;
        if (get_active_ports(&ports) == STATUS_SUCCESS) {
            for (uint32_t i = 0; i < ports.count; i++) {
                packet_send(&packet, ports.port_ids[i]);
            }
        }
    }
    
    // Освобождаем ресурсы
    rip_free_table();
    rip_enabled = false;
    
    LOG_INFO("RIP shutdown completed successfully");
    return STATUS_SUCCESS;
}

/**
 * Обработчик входящих RIP-пакетов
 * 
 * @param packet Указатель на структуру пакета
 * @param port_id ID входящего порта
 * @return STATUS_SUCCESS при успешной обработке, код ошибки в противном случае
 */
int rip_process_packet(const packet_t *packet, uint32_t port_id)
{
    if (!rip_enabled) {
        LOG_WARN("RIP is not enabled, ignoring packet");
        return STATUS_FEATURE_DISABLED;
    }
    
    if (packet == NULL) {
        LOG_ERROR("NULL packet pointer provided to rip_process_packet");
        return STATUS_INVALID_PARAM;
    }
    
    // Проверяем, что это UDP пакет на порту RIP
    if (packet->protocol != PROTOCOL_UDP || packet->dst_port != RIP_PORT) {
        LOG_DEBUG("Not a RIP packet (protocol: %d, port: %d)", packet->protocol, packet->dst_port);
        return STATUS_INVALID_PACKET;
    }
    
    // Минимальная длина пакета RIP - заголовок + минимум 1 запись
    if (packet->len < sizeof(rip_header_t) + sizeof(rip_entry_t)) {
        LOG_WARN("RIP packet too short: %zu bytes", packet->len);
        return STATUS_INVALID_PACKET;
    }
    
    // Преобразуем буфер в структуру пакета RIP
    rip_packet_t rip_packet;
    buffer_to_rip_packet(packet->data, packet->len, &rip_packet);
    
    // Проверяем версию RIP
    if (rip_packet.header.version != RIP_VERSION) {
        LOG_WARN("Unsupported RIP version: %d", rip_packet.header.version);
        return STATUS_UNSUPPORTED;
    }
    
    // В зависимости от типа команды обрабатываем соответствующим образом
    if (rip_packet.header.command == RIP_CMD_REQUEST) {
        return rip_process_request(packet, packet->src_ip, port_id);
    } else if (rip_packet.header.command == RIP_CMD_RESPONSE) {
        return rip_process_response(packet, packet->src_ip, port_id);
    } else {
        LOG_WARN("Unknown RIP command: %d", rip_packet.header.command);
        return STATUS_INVALID_PACKET;
    }
}

/**
 * Основной цикл обслуживания протокола RIP
 * Вызывается периодически для обновления таймеров и отправки обновлений
 * 
 * @return STATUS_SUCCESS при успешном выполнении, код ошибки в противном случае
 */
int rip_run(void)
{
    if (!rip_enabled) {
        return STATUS_FEATURE_DISABLED;
    }
    
    time_t current_time = time(NULL);
    
    // Обновляем таймеры маршрутов, удаляем устаревшие
    rip_update_timers();
    
    // Проверяем, нужно ли отправить регулярное обновление
    if (current_time - last_update_time >= RIP_UPDATE_INTERVAL) {
        LOG_DEBUG("Sending periodic RIP update");
        rip_send_update(false, 0xFFFFFFFF, 0); // Отправляем всем соседям
        last_update_time = current_time;
    }
    
    // Синхронизируем нашу таблицу RIP с основной таблицей маршрутизации
    rip_sync_with_routing_table();
    
    return STATUS_SUCCESS;
}

/**
 * Добавление маршрута в RIP
 * 
 * @param ip IP-адрес сети
 * @param mask Маска подсети
 * @param next_hop IP-адрес следующего хопа
 * @param metric Метрика маршрута
 * @param port_id ID порта, через который доступен маршрут
 * @return STATUS_SUCCESS при успешном добавлении, код ошибки в противном случае
 */
int rip_add_route(uint32_t ip, uint32_t mask, uint32_t next_hop, uint32_t metric, uint32_t port_id)
{
    if (!rip_enabled) {
        LOG_WARN("RIP is not enabled, cannot add route");
        return STATUS_FEATURE_DISABLED;
    }
    
    if (metric >= RIP_INFINITY) {
        LOG_WARN("Route metric too high: %u", metric);
        return STATUS_INVALID_PARAM;
    }
    
    // Добавляем в таблицу RIP
    int ret = rip_add_route_to_table(ip, mask, next_hop, metric, port_id);
    if (ret != STATUS_SUCCESS) {
        LOG_ERROR("Failed to add route to RIP table: %d", ret);
        return ret;
    }
    
    // Добавляем в основную таблицу маршрутизации
    route_entry_t route;
    route.ip_addr = ip;
    route.mask = mask;
    route.next_hop = next_hop;
    route.port_id = port_id;
    route.metric = metric;
    route.protocol = ROUTING_PROTOCOL_RIP;
    
    ret = routing_add_route(&route);
    if (ret != STATUS_SUCCESS) {
        LOG_WARN("Failed to add route to main routing table: %d", ret);
        // Продолжаем выполнение, так как запись в RIP таблице уже есть
    }
    
    // Отправляем обновление соседям (триггерное обновление)
    rip_send_update(true, 0xFFFFFFFF, 0);
    
    LOG_INFO("Added RIP route: %u.%u.%u.%u/%u via %u.%u.%u.%u metric %u port %u",
             (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF,
             count_mask_bits(mask),
             (next_hop >> 24) & 0xFF, (next_hop >> 16) & 0xFF, 
             (next_hop >> 8) & 0xFF, next_hop & 0xFF,
             metric, port_id);
    
    return STATUS_SUCCESS;
}

/**
 * Удаление маршрута из RIP
 * 
 * @param ip IP-адрес сети
 * @param mask Маска подсети
 * @return STATUS_SUCCESS при успешном удалении, код ошибки в противном случае
 */
int rip_remove_route(uint32_t ip, uint32_t mask)
{
    if (!rip_enabled) {
        LOG_WARN("RIP is not enabled, cannot remove route");
        return STATUS_FEATURE_DISABLED;
    }
    
    // Ищем маршрут в таблице RIP
    rip_table_entry_t *prev = NULL;
    rip_table_entry_t *current = rip_table;
    
    while (current != NULL) {
        if (current->ip_address == ip && current->subnet_mask == mask) {
            // Удаляем из таблицы RIP
            if (prev == NULL) {
                rip_table = current->next;
            } else {
                prev->next = current->next;
            }
            
            LOG_INFO("Removed RIP route: %u.%u.%u.%u/%u",
                     (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF,
                     count_mask_bits(mask));
            
            free(current);
            
            // Удаляем из основной таблицы маршрутизации
            routing_remove_route(ip, mask, ROUTING_PROTOCOL_RIP);
            
            // Отправляем обновление соседям с метрикой бесконечность для этого маршрута
            rip_packet_t poison_packet;
            memset(&poison_packet, 0, sizeof(poison_packet));
            poison_packet.header.command = RIP_CMD_RESPONSE;
            poison_packet.header.version = RIP_VERSION;
            
            poison_packet.entries[0].family = 2; // AF_INET
            poison_packet.entries[0].ip_address = ip;
            poison_packet.entries[0].subnet_mask = mask;
            poison_packet.entries[0].next_hop = 0;
            poison_packet.entries[0].metric = RIP_INFINITY; // Устанавливаем метрику в бесконечность
            poison_packet.entry_count = 1;
            
            // Преобразуем пакет в буфер
            uint8_t buffer[1500];
            size_t buffer_len = 0;
            rip_packet_to_buffer(&poison_packet, buffer, &buffer_len);
            
            // Отправляем отравленные маршруты на мультикаст адрес
            packet_t packet;
            memset(&packet, 0, sizeof(packet));
            packet.data = buffer;
            packet.len = buffer_len;
            packet.protocol = PROTOCOL_UDP;
            packet.src_port = RIP_PORT;
            packet.dst_port = RIP_PORT;
            packet.src_ip = rip_router_id;
            packet.dst_ip = 0xFFFFFFFF; // Широковещательный адрес 255.255.255.255
            
            // Отправляем через все доступные интерфейсы
            port_list_t ports;
            if (get_active_ports(&ports) == STATUS_SUCCESS) {
                for (uint32_t i = 0; i < ports.count; i++) {
                    packet_send(&packet, ports.port_ids[i]);
                }
            }
            
            return STATUS_SUCCESS;
        }
        
        prev = current;
        current = current->next;
    }
    
    LOG_WARN("Route not found in RIP table: %u.%u.%u.%u/%u",
             (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF,
             count_mask_bits(mask));
    
    return STATUS_NOT_FOUND;
}

/**
 * Обработка маршрутов из основной таблицы маршрутизации
 * Вызывается при изменении маршрутов другими протоколами
 * 
 * @param routes Указатель на массив маршрутов
 * @param count Количество маршрутов
 * @return STATUS_SUCCESS при успешной обработке, код ошибки в противном случае
 */
int rip_process_routes(const route_entry_t *routes, uint32_t count)
{
    if (!rip_enabled) {
        return STATUS_FEATURE_DISABLED;
    }
    
    if (routes == NULL && count > 0) {
        LOG_ERROR("NULL routes pointer provided to rip_process_routes");
        return STATUS_INVALID_PARAM;
    }
    
    bool changes = false;
    
    // Обрабатываем каждый маршрут
    for (uint32_t i = 0; i < count; i++) {
        const route_entry_t *route = &routes[i];
        
        // Пропускаем маршруты RIP (мы их уже знаем)
        if (route->protocol == ROUTING_PROTOCOL_RIP) {
            continue;
        }
        
        // Ищем маршрут в нашей таблице
        rip_table_entry_t *entry = find_route_in_table(route->ip_addr, route->mask);
        
        // Определяем метрику для редистрибуции
        uint32_t redis_metric = 1; // По умолчанию метрика 1
        
        // В реальных условиях метрика зависит от протокола
        if (route->protocol == ROUTING_PROTOCOL_OSPF) {
            redis_metric = 2;
        } else if (route->protocol == ROUTING_PROTOCOL_STATIC) {
            redis_metric = 1;
        } else {
            redis_metric = 3;
        }
        
        if (entry == NULL) {
            // Добавляем новый маршрут
            if (rip_add_route_to_table(route->ip_addr, route->mask, 
                                       route->next_hop, redis_metric, 
                                       route->port_id) == STATUS_SUCCESS) {
                changes = true;
            }
        } else {
            // Обновляем существующий маршрут, если есть изменения
            if (entry->next_hop != route->next_hop || 
                entry->metric != redis_metric || 
                entry->port_id != route->port_id) {
                
                entry->next_hop = route->next_hop;
                entry->metric = redis_metric;
                entry->port_id = route->port_id;
                entry->last_update = time(NULL);
                entry->is_garbage = false;
                
                changes = true;
            }
        }
    }
    
    // Если были изменения, отправляем обновление
    if (changes) {
        rip_send_update(true, 0xFFFFFFFF, 0);
    }
    
    return STATUS_SUCCESS;
}

/**
 * Получение текущего состояния таблицы RIP
 * 
 * @param entries Указатель на буфер для маршрутов
 * @param max_entries Размер буфера (максимальное количество маршрутов)
 * @param count Указатель для записи фактического количества маршрутов
 * @return STATUS_SUCCESS при успешном получении, код ошибки в противном случае
 */
int rip_get_routes(route_entry_t *entries, uint32_t max_entries, uint32_t *count)
{
    if (!rip_enabled) {
        LOG_WARN("RIP is not enabled, no routes available");
        if (count) *count = 0;
        return STATUS_FEATURE_DISABLED;
    }
    
    if (entries == NULL || count == NULL) {
        LOG_ERROR("NULL pointer provided to rip_get_routes");
        return STATUS_INVALID_PARAM;
    }
    
    *count = 0;
    rip_table_entry_t *entry = rip_table;
    
    while (entry != NULL && *count < max_entries) {
        // Пропускаем маршруты, помеченные как мусор
        if (entry->is_garbage || entry->metric >= RIP_INFINITY) {
            entry = entry->next;
            continue;
        }
        
        entries[*count].ip_addr = entry->ip_address;
        entries[*count].mask = entry->subnet_mask;
        entries[*count].next_hop = entry->next_hop;
        entries[*count].port_id = entry->port_id;
        entries[*count].metric = entry->metric;
        entries[*count].protocol = ROUTING_PROTOCOL_RIP;
        
        (*count)++;
        entry = entry->next;
    }
    
    return STATUS_SUCCESS;
}

/* Реализация вспомогательных функций */

/**
 * Обработка запроса RIP
 * 
 * @param packet Указатель на входящий пакет
 * @param src_ip IP-адрес отправителя
 * @param port_id ID входящего порта
 * @return STATUS_SUCCESS при успешной обработке, код ошибки в противном случае
 */
static int rip_process_request(const packet_t *packet, uint32_t src_ip, uint32_t port_id)
{
    LOG_DEBUG("Processing RIP request from %u.%u.%u.%u on port %u",
             (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF, 
             (src_ip >> 8) & 0xFF, src_ip & 0xFF, port_id);
    
    // Преобразуем буфер в пакет RIP
    rip_packet_t rip_request;
    buffer_to_rip_packet(packet->data, packet->len, &rip_request);
    
    // Проверяем, запрашиваются ли все маршруты
    bool request_all = false;
    if (rip_request.entry_count == 1 && 
        rip_request.entries[0].ip_address == 0 &&
        rip_request.entries[0].subnet_mask == 0 &&
        rip_request.entries[0].metric == RIP_INFINITY) {
        request_all = true;
    }
    
    if (request_all) {
        // Отправляем все наши маршруты
        return rip_send_update(false, src_ip, port_id);
    } else {
        // Формируем ответ на конкретные запросы
        rip_packet_t response;
        memset(&response, 0, sizeof(response));
        response.header.command = RIP_CMD_RESPONSE;
        response.header.version = RIP_VERSION;
        response.entry_count = 0;
        
        // Обрабатываем каждую запись в запросе
        for (uint8_t i = 0; i < rip_request.entry_count && i < RIP_MAX_ENTRIES; i++) {
            uint32_t req_ip = rip_request.entries[i].ip_address;
            uint32_t req_mask = rip_request.entries[i].subnet_mask;
            
            // Ищем маршрут в нашей таблице
            rip_table_entry_t *entry = find_route_in_table(req_ip, req_mask);
            if (entry != NULL) {
                // Добавляем маршрут в ответ
                response.entries[response.
