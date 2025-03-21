/**
 * @file routing_protocols.h
 * @brief Интерфейсы и определения для протоколов маршрутизации
 * 
 * Этот файл содержит определения типов данных и функций для работы с различными
 * протоколами маршрутизации, такими как RIP и OSPF, в симуляторе коммутатора.
 */
#ifndef ROUTING_PROTOCOLS_H
#define ROUTING_PROTOCOLS_H

#include "../common/types.h"
#include "../common/error_codes.h"
#include "routing_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Максимальное количество соседних устройств для протоколов маршрутизации */
#define MAX_ROUTING_NEIGHBORS 64

/** Максимальное количество маршрутов в обновлении */
#define MAX_ROUTES_IN_UPDATE 100

/** Максимальное количество сетей в анонсе OSPF */
#define MAX_NETWORKS_IN_LSA 50

/** Время ожидания перед отправкой обновлений маршрутизации (в мс) */
#define ROUTING_UPDATE_INTERVAL 30000

/** Таймаут для соседних устройств (в мс) */
#define NEIGHBOR_TIMEOUT 180000

/**
 * @brief Состояния соседних устройств для протоколов маршрутизации
 */
typedef enum {
    NEIGHBOR_STATE_DOWN,        /**< Соседнее устройство недоступно */
    NEIGHBOR_STATE_INIT,        /**< Инициализация соединения */
    NEIGHBOR_STATE_EXCHANGE,    /**< Обмен информацией о маршрутизации */
    NEIGHBOR_STATE_FULL,        /**< Полная синхронизация маршрутов */
    NEIGHBOR_STATE_PASSIVE      /**< Пассивное соединение */
} neighbor_state_t;

/**
 * @brief Типы сообщений протокола RIP
 */
typedef enum {
    RIP_MESSAGE_REQUEST,        /**< Запрос таблицы маршрутизации */
    RIP_MESSAGE_RESPONSE,       /**< Ответ с таблицей маршрутизации */
    RIP_MESSAGE_TRACEON,        /**< Запрос включения трассировки (устаревший) */
    RIP_MESSAGE_TRACEOFF,       /**< Запрос выключения трассировки (устаревший) */
    RIP_MESSAGE_RESERVED         /**< Зарезервированный тип сообщения */
} rip_message_type_t;

/**
 * @brief Версии протокола RIP
 */
typedef enum {
    RIP_VERSION_1 = 1,         /**< RIP версия 1 */
    RIP_VERSION_2 = 2          /**< RIP версия 2 */
} rip_version_t;

/**
 * @brief Структура RIP записи
 */
typedef struct {
    uint16_t address_family;    /**< Семейство адресов (обычно AF_INET) */
    uint16_t route_tag;         /**< Тег маршрута (используется для маршрутов из внешних протоколов) */
    ip_addr_t destination;      /**< IP-адрес назначения */
    ip_addr_t netmask;          /**< Маска подсети */
    ip_addr_t next_hop;         /**< IP-адрес следующего перехода */
    uint32_t metric;            /**< Метрика маршрута (16 = бесконечность) */
} rip_entry_t;

/**
 * @brief Структура RIP сообщения
 */
typedef struct {
    uint8_t command;            /**< Тип команды (запрос/ответ) */
    uint8_t version;            /**< Версия протокола RIP */
    uint16_t zero;              /**< Зарезервированное поле, должно быть 0 */
    rip_entry_t entries[MAX_ROUTES_IN_UPDATE]; /**< Массив записей маршрутов */
    uint32_t entry_count;       /**< Количество записей в сообщении */
} rip_message_t;

/**
 * @brief Структура конфигурации RIP
 */
typedef struct {
    bool enabled;               /**< Включен ли протокол RIP */
    rip_version_t version;      /**< Версия протокола RIP */
    uint32_t update_interval;   /**< Интервал отправки обновлений (в секундах) */
    uint32_t timeout;           /**< Таймаут маршрута (в секундах) */
    uint32_t garbage_collect;   /**< Время до удаления маршрута (в секундах) */
    bool split_horizon;         /**< Включено ли правило расщепленного горизонта */
    bool poison_reverse;        /**< Включено ли правило отравленного обратного пути */
    uint32_t max_routes;        /**< Максимальное количество маршрутов */
    ip_addr_t networks[MAX_NETWORKS_IN_LSA]; /**< Сети, анонсируемые через RIP */
    uint32_t network_count;     /**< Количество анонсируемых сетей */
} rip_config_t;

/**
 * @brief Типы пакетов OSPF
 */
typedef enum {
    OSPF_PACKET_HELLO,          /**< Hello пакет */
    OSPF_PACKET_DATABASE_DESC,  /**< Database Description пакет */
    OSPF_PACKET_LS_REQUEST,     /**< Link State Request пакет */
    OSPF_PACKET_LS_UPDATE,      /**< Link State Update пакет */
    OSPF_PACKET_LS_ACK          /**< Link State Acknowledgment пакет */
} ospf_packet_type_t;

/**
 * @brief Типы Link State Advertisement (LSA) в OSPF
 */
typedef enum {
    OSPF_LSA_ROUTER,            /**< Router LSA */
    OSPF_LSA_NETWORK,           /**< Network LSA */
    OSPF_LSA_SUMMARY_IP,        /**< Summary LSA для IP сети */
    OSPF_LSA_SUMMARY_ASBR,      /**< Summary LSA для ASBR */
    OSPF_LSA_EXTERNAL           /**< External LSA */
} ospf_lsa_type_t;

/**
 * @brief Структура OSPF соседа
 */
typedef struct {
    ip_addr_t ip_address;       /**< IP-адрес соседа */
    uint32_t router_id;         /**< ID роутера соседа */
    uint16_t interface_index;   /**< Индекс интерфейса */
    uint32_t priority;          /**< Приоритет соседа */
    neighbor_state_t state;     /**< Состояние соседства */
    uint32_t last_hello;        /**< Время последнего Hello пакета */
    uint32_t dead_interval;     /**< Интервал для определения недоступности */
    uint32_t sequence_number;   /**< Номер последовательности для обмена БД */
    bool is_dr;                 /**< Является ли этот сосед DR */
    bool is_bdr;                /**< Является ли этот сосед BDR */
} ospf_neighbor_t;

/**
 * @brief Структура конфигурации OSPF
 */
typedef struct {
    bool enabled;               /**< Включен ли протокол OSPF */
    uint32_t router_id;         /**< ID роутера */
    uint32_t area_id;           /**< ID области */
    uint32_t hello_interval;    /**< Интервал отправки Hello пакетов (в секундах) */
    uint32_t dead_interval;     /**< Интервал для определения недоступности (в секундах) */
    uint32_t lsa_interval;      /**< Интервал между отправками LSA (в секундах) */
    uint32_t spf_interval;      /**< Минимальный интервал между расчетами SPF (в секундах) */
    uint32_t max_lsa;           /**< Максимальное количество LSA */
    uint32_t reference_bandwidth; /**< Опорная пропускная способность для расчета стоимости */
    ip_addr_t networks[MAX_NETWORKS_IN_LSA]; /**< Сети, анонсируемые через OSPF */
    uint32_t network_count;     /**< Количество анонсируемых сетей */
} ospf_config_t;

/**
 * @brief Структура управления протоколами маршрутизации
 */
typedef struct {
    rip_config_t rip_config;    /**< Конфигурация RIP */
    ospf_config_t ospf_config;  /**< Конфигурация OSPF */
    routing_table_t *routing_table; /**< Указатель на таблицу маршрутизации */
    bool protocols_enabled;     /**< Включены ли протоколы маршрутизации */
} routing_protocols_t;

/**
 * @brief Инициализирует подсистему протоколов маршрутизации
 * 
 * @param protocols Указатель на структуру протоколов
 * @param routing_table Указатель на таблицу маршрутизации
 * @return error_code_t Код ошибки (OK в случае успеха)
 */
error_code_t routing_protocols_init(routing_protocols_t *protocols, routing_table_t *routing_table);

/**
 * @brief Запускает протоколы маршрутизации
 * 
 * @param protocols Указатель на структуру протоколов
 * @return error_code_t Код ошибки (OK в случае успеха)
 */
error_code_t routing_protocols_start(routing_protocols_t *protocols);

/**
 * @brief Останавливает протоколы маршрутизации
 * 
 * @param protocols Указатель на структуру протоколов
 * @return error_code_t Код ошибки (OK в случае успеха)
 */
error_code_t routing_protocols_stop(routing_protocols_t *protocols);

/**
 * @brief Настраивает протокол RIP
 * 
 * @param protocols Указатель на структуру протоколов
 * @param config Указатель на конфигурацию RIP
 * @return error_code_t Код ошибки (OK в случае успеха)
 */
error_code_t routing_protocols_configure_rip(routing_protocols_t *protocols, const rip_config_t *config);

/**
 * @brief Настраивает протокол OSPF
 * 
 * @param protocols Указатель на структуру протоколов
 * @param config Указатель на конфигурацию OSPF
 * @return error_code_t Код ошибки (OK в случае успеха)
 */
error_code_t routing_protocols_configure_ospf(routing_protocols_t *protocols, const ospf_config_t *config);

/**
 * @brief Добавляет сеть для анонсирования через RIP
 * 
 * @param protocols Указатель на структуру протоколов
 * @param network IP-адрес сети
 * @param netmask Маска подсети
 * @return error_code_t Код ошибки (OK в случае успеха)
 */
error_code_t routing_protocols_rip_add_network(routing_protocols_t *protocols, ip_addr_t network, ip_addr_t netmask);

/**
 * @brief Удаляет сеть из анонсирования через RIP
 * 
 * @param protocols Указатель на структуру протоколов
 * @param network IP-адрес сети
 * @param netmask Маска подсети
 * @return error_code_t Код ошибки (OK в случае успеха)
 */
error_code_t routing_protocols_rip_remove_network(routing_protocols_t *protocols, ip_addr_t network, ip_addr_t netmask);

/**
 * @brief Добавляет сеть для анонсирования через OSPF
 * 
 * @param protocols Указатель на структуру протоколов
 * @param network IP-адрес сети
 * @param netmask Маска подсети
 * @param area_id ID области OSPF
 * @return error_code_t Код ошибки (OK в случае успеха)
 */
error_code_t routing_protocols_ospf_add_network(routing_protocols_t *protocols, ip_addr_t network, ip_addr_t netmask, uint32_t area_id);

/**
 * @brief Удаляет сеть из анонсирования через OSPF
 * 
 * @param protocols Указатель на структуру протоколов
 * @param network IP-адрес сети
 * @param netmask Маска подсети
 * @return error_code_t Код ошибки (OK в случае успеха)
 */
error_code_t routing_protocols_ospf_remove_network(routing_protocols_t *protocols, ip_addr_t network, ip_addr_t netmask);

/**
 * @brief Обрабатывает полученный RIP пакет
 * 
 * @param protocols Указатель на структуру протоколов
 * @param message Указатель на RIP сообщение
 * @param source_ip IP-адрес источника сообщения
 * @param interface_index Индекс интерфейса, на котором получено сообщение
 * @return error_code_t Код ошибки (OK в случае успеха)
 */
error_code_t routing_protocols_process_rip_message(routing_protocols_t *protocols, const rip_message_t *message, 
                                                ip_addr_t source_ip, uint16_t interface_index);

/**
 * @brief Получает статистику по протоколу RIP
 * 
 * @param protocols Указатель на структуру протоколов
 * @param received_updates Указатель для сохранения количества полученных обновлений
 * @param sent_updates Указатель для сохранения количества отправленных обновлений
 * @param routes_learned Указатель для сохранения количества изученных маршрутов
 * @return error_code_t Код ошибки (OK в случае успеха)
 */
error_code_t routing_protocols_get_rip_stats(const routing_protocols_t *protocols, 
                                          uint32_t *received_updates, 
                                          uint32_t *sent_updates, 
                                          uint32_t *routes_learned);

/**
 * @brief Получает статистику по протоколу OSPF
 * 
 * @param protocols Указатель на структуру протоколов
 * @param hello_sent Указатель для сохранения количества отправленных Hello пакетов
 * @param lsa_sent Указатель для сохранения количества отправленных LSA
 * @param spf_runs Указатель для сохранения количества выполненных расчетов SPF
 * @return error_code_t Код ошибки (OK в случае успеха)
 */
error_code_t routing_protocols_get_ospf_stats(const routing_protocols_t *protocols, 
                                           uint32_t *hello_sent, 
                                           uint32_t *lsa_sent, 
                                           uint32_t *spf_runs);

/**
 * @brief Получает список соседей OSPF
 * 
 * @param protocols Указатель на структуру протоколов
 * @param neighbors Массив для сохранения информации о соседях
 * @param max_neighbors Максимальное количество соседей для возврата
 * @param actual_neighbors Указатель для сохранения фактического количества соседей
 * @return error_code_t Код ошибки (OK в случае успеха)
 */
error_code_t routing_protocols_get_ospf_neighbors(const routing_protocols_t *protocols, 
                                               ospf_neighbor_t *neighbors, 
                                               uint32_t max_neighbors, 
                                               uint32_t *actual_neighbors);

/**
 * @brief Сбрасывает информацию о соседях OSPF
 * 
 * @param protocols Указатель на структуру протоколов
 * @return error_code_t Код ошибки (OK в случае успеха)
 */
error_code_t routing_protocols_clear_ospf_neighbors(routing_protocols_t *protocols);

/**
 * @brief Устанавливает router ID для OSPF
 * 
 * @param protocols Указатель на структуру протоколов
 * @param router_id Новый router ID
 * @return error_code_t Код ошибки (OK в случае успеха)
 */
error_code_t routing_protocols_set_ospf_router_id(routing_protocols_t *protocols, uint32_t router_id);

/**
 * @brief Выполняет принудительный расчет SPF для OSPF
 * 
 * @param protocols Указатель на структуру протоколов
 * @return error_code_t Код ошибки (OK в случае успеха)
 */
error_code_t routing_protocols_ospf_calculate_spf(routing_protocols_t *protocols);

/**
 * @brief Перераспределяет маршруты между протоколами маршрутизации
 * 
 * @param protocols Указатель на структуру протоколов
 * @param from_type Тип протокола, из которого перераспределяются маршруты
 * @param to_type Тип протокола, в который перераспределяются маршруты
 * @param metric Метрика для перераспределяемых маршрутов
 * @return error_code_t Код ошибки (OK в случае успеха)
 */
error_code_t routing_protocols_redistribute(routing_protocols_t *protocols, 
                                         route_type_t from_type, 
                                         route_type_t to_type, 
                                         uint16_t metric);

#ifdef __cplusplus
}
#endif

#endif /* ROUTING_PROTOCOLS_H */
