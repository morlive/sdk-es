# Hardware Abstraction Layer (HAL) API Documentation

## Overview

HAL (Hardware Abstraction Layer) предоставляет абстракцию над аппаратным обеспечением коммутатора, позволяя компонентам верхнего уровня работать с оборудованием без знания его особенностей.

## Основные компоненты HAL API

### Hardware Resources API (`hw_resources.h`)

#### Функции

```c
hal_status_t hal_init_hw_resources(void);
hal_status_t hal_allocate_resource(hal_resource_type_t type, uint32_t count, hal_resource_id_t *resource_ids);
hal_status_t hal_free_resource(hal_resource_type_t type, uint32_t count, const hal_resource_id_t *resource_ids);
hal_status_t hal_get_resource_stats(hal_resource_type_t type, hal_resource_stats_t *stats);
```

#### Типы ресурсов

| Тип ресурса | Описание |
|-------------|----------|
| HAL_RESOURCE_PORT | Физические порты |
| HAL_RESOURCE_VLAN | VLAN ресурсы |
| HAL_RESOURCE_MAC_TABLE | Записи MAC-таблицы |
| HAL_RESOURCE_ROUTE_TABLE | Записи таблицы маршрутизации |
| HAL_RESOURCE_ACL | ACL ресурсы |

### Port API (`port.h`)

#### Функции

```c
hal_status_t hal_port_init(hal_port_id_t port_id, hal_port_config_t *config);
hal_status_t hal_port_deinit(hal_port_id_t port_id);
hal_status_t hal_port_set_state(hal_port_id_t port_id, hal_port_state_t state);
hal_status_t hal_port_get_state(hal_port_id_t port_id, hal_port_state_t *state);
hal_status_t hal_port_get_stats(hal_port_id_t port_id, hal_port_stats_t *stats);
```

#### Состояния порта

| Состояние | Описание |
|-----------|----------|
| HAL_PORT_STATE_DOWN | Порт выключен |
| HAL_PORT_STATE_UP | Порт включен |
| HAL_PORT_STATE_TESTING | Порт в режиме тестирования |
| HAL_PORT_STATE_UNKNOWN | Неизвестное состояние |

### Packet API (`packet.h`)

#### Функции

```c
hal_status_t hal_packet_create(hal_packet_t **packet, uint32_t size);
hal_status_t hal_packet_destroy(hal_packet_t *packet);
hal_status_t hal_packet_send(hal_port_id_t port_id, hal_packet_t *packet);
hal_status_t hal_packet_receive(hal_port_id_t port_id, hal_packet_t **packet, uint32_t timeout_ms);
hal_status_t hal_packet_register_handler(hal_packet_handler_t handler, void *context);
```

#### Структура пакета

```c
typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t offset;
    hal_packet_metadata_t metadata;
} hal_packet_t;

typedef struct {
    hal_port_id_t ingress_port;
    uint64_t timestamp;
    uint16_t vlan_id;
    uint8_t priority;
    bool is_tagged;
    bool is_control;
} hal_packet_metadata_t;
```

## Обработка ошибок

| Код ошибки | Описание |
|------------|----------|
| HAL_STATUS_SUCCESS | Операция успешна |
| HAL_STATUS_FAILURE | Общая ошибка |
| HAL_STATUS_NOT_IMPLEMENTED | Функция не реализована |
| HAL_STATUS_INVALID_PARAMETER | Неверный параметр |
| HAL_STATUS_RESOURCE_NOT_FOUND | Ресурс не найден |
| HAL_STATUS_INSUFFICIENT_RESOURCES | Недостаточно ресурсов |
| HAL_STATUS_TIMEOUT | Истекло время ожидания |

## Примеры использования

### Инициализация порта

```c
hal_port_config_t port_config;
port_config.speed = HAL_PORT_SPEED_10G;
port_config.duplex = HAL_PORT_DUPLEX_FULL;
port_config.auto_neg = true;
port_config.mtu = 9000;

hal_status_t status = hal_port_init(1, &port_config);
if (status != HAL_STATUS_SUCCESS) {
    // Handle error
}

status = hal_port_set_state(1, HAL_PORT_STATE_UP);
if (status != HAL_STATUS_SUCCESS) {
    // Handle error
}
```

### Отправка пакета

```c
hal_packet_t *packet;
hal_status_t status = hal_packet_create(&packet, 1500);
if (status != HAL_STATUS_SUCCESS) {
    // Handle error
}

// Заполнение данных пакета
memcpy(packet->data, ethernet_frame, frame_size);
packet->size = frame_size;

// Установка метаданных
packet->metadata.vlan_id = 100;
packet->metadata.priority = 3;
packet->metadata.is_tagged = true;

// Отправка пакета
status = hal_packet_send(1, packet);
if (status != HAL_STATUS_SUCCESS) {
    // Handle error
}

// Освобождение памяти
hal_packet_destroy(packet);
```

### Регистрация обработчика пакетов

```c
hal_status_t packet_handler(hal_packet_t *packet, void *context) {
    // Обработка пакета
    printf("Received packet on port %d, size %d\n", 
           packet->metadata.ingress_port, packet->size);
    return HAL_STATUS_SUCCESS;
}

hal_status_t status = hal_packet_register_handler(packet_handler, NULL);
if (status != HAL_STATUS_SUCCESS) {
    // Handle error
}
```
