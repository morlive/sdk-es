# Switch Abstraction Interface (SAI) API Documentation

## Overview

SAI (Switch Abstraction Interface) предоставляет стандартизированный API для управления коммутатором. Switch Simulator реализует SAI API, обеспечивая совместимость с реальными коммутаторами.

## Компоненты SAI API

### Port API (`sai_port.h`)

#### Функции

```c
sai_status_t sai_port_create(sai_port_id_t *port_id, sai_attribute_t *attr_list);
sai_status_t sai_port_remove(sai_port_id_t port_id);
sai_status_t sai_port_set_attribute(sai_port_id_t port_id, sai_attribute_t *attr);
sai_status_t sai_port_get_attribute(sai_port_id_t port_id, uint32_t attr_count, sai_attribute_t *attr_list);
sai_status_t sai_port_get_stats(sai_port_id_t port_id, uint32_t counter_count, const sai_port_stat_t *counter_ids, uint64_t *counters);
```

#### Атрибуты

| Атрибут | Тип | Описание |
|---------|-----|----------|
| SAI_PORT_ATTR_SPEED | sai_uint32_t | Скорость порта в Mbps |
| SAI_PORT_ATTR_ADMIN_STATE | bool | Административное состояние порта |
| SAI_PORT_ATTR_HW_LANE_LIST | sai_u32_list_t | Список физических линий |
| SAI_PORT_ATTR_MTU | sai_uint32_t | Максимальный размер пакета |

### VLAN API (`sai_vlan.h`)

#### Функции

```c
sai_status_t sai_create_vlan(sai_vlan_id_t vlan_id);
sai_status_t sai_remove_vlan(sai_vlan_id_t vlan_id);
sai_status_t sai_add_ports_to_vlan(sai_vlan_id_t vlan_id, uint32_t port_count, const sai_vlan_port_t *port_list);
sai_status_t sai_remove_ports_from_vlan(sai_vlan_id_t vlan_id, uint32_t port_count, const sai_vlan_port_t *port_list);
sai_status_t sai_get_vlan_stats(sai_vlan_id_t vlan_id, uint32_t counter_count, const sai_vlan_stat_t *counter_ids, uint64_t *counters);
```

### Route API (`sai_route.h`)

#### Функции

```c
sai_status_t sai_create_route(sai_route_entry_t *route_entry, uint32_t attr_count, const sai_attribute_t *attr_list);
sai_status_t sai_remove_route(const sai_route_entry_t *route_entry);
sai_status_t sai_set_route_attribute(const sai_route_entry_t *route_entry, const sai_attribute_t *attr);
sai_status_t sai_get_route_attribute(const sai_route_entry_t *route_entry, uint32_t attr_count, sai_attribute_t *attr_list);
```

#### Атрибуты

| Атрибут | Тип | Описание |
|---------|-----|----------|
| SAI_ROUTE_ATTR_NEXT_HOP_ID | sai_object_id_t | ID следующего хопа |
| SAI_ROUTE_ATTR_PACKET_ACTION | sai_packet_action_t | Действие для пакетов |
| SAI_ROUTE_ATTR_USER_TRAP_ID | sai_object_id_t | ID ловушки для передачи CPU |

## Коды ошибок

| Код ошибки | Описание |
|------------|----------|
| SAI_STATUS_SUCCESS | Операция успешна |
| SAI_STATUS_FAILURE | Общая ошибка |
| SAI_STATUS_NOT_IMPLEMENTED | Функция не реализована |
| SAI_STATUS_INVALID_PARAMETER | Неверный параметр |
| SAI_STATUS_INSUFFICIENT_RESOURCES | Недостаточно ресурсов |
| SAI_STATUS_TABLE_FULL | Таблица заполнена |

## Примеры использования

### Создание и настройка порта

```c
sai_attribute_t port_attrs[3];
sai_port_id_t port_id;

port_attrs[0].id = SAI_PORT_ATTR_SPEED;
port_attrs[0].value.u32 = 10000; // 10G

port_attrs[1].id = SAI_PORT_ATTR_ADMIN_STATE;
port_attrs[1].value.booldata = true; // Enabled

port_attrs[2].id = SAI_PORT_ATTR_MTU;
port_attrs[2].value.u32 = 9000; // Jumbo frames

sai_status_t status = sai_port_create(&port_id, port_attrs);
if (status != SAI_STATUS_SUCCESS) {
    // Handle error
}
```

### Создание VLAN и добавление портов

```c
sai_vlan_id_t vlan_id = 100;
sai_status_t status = sai_create_vlan(vlan_id);
if (status != SAI_STATUS_SUCCESS) {
    // Handle error
}

sai_vlan_port_t vlan_ports[2];
vlan_ports[0].port_id = port_id_1;
vlan_ports[0].tagging_mode = SAI_VLAN_PORT_TAGGED;

vlan_ports[1].port_id = port_id_2;
vlan_ports[1].tagging_mode = SAI_VLAN_PORT_UNTAGGED;

status = sai_add_ports_to_vlan(vlan_id, 2, vlan_ports);
if (status != SAI_STATUS_SUCCESS) {
    // Handle error
}
```

### Добавление маршрута

```c
sai_route_entry_t route_entry;
route_entry.destination.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
route_entry.destination.addr.ip4 = 0xC0A80100; // 192.168.1.0
route_entry.destination.mask.ip4 = 0xFFFFFF00; // 255.255.255.0
route_entry.vr_id = virtual_router_id;

sai_attribute_t route_attr;
route_attr.id = SAI_ROUTE_ATTR_NEXT_HOP_ID;
route_attr.value.oid = next_hop_id;

status = sai_create_route(&route_entry, 1, &route_attr);
if (status != SAI_STATUS_SUCCESS) {
    // Handle error
}
```
