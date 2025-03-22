# Switch Simulator - Руководство пользователя

## Содержание
1. [Введение](#введение)
2. [Установка](#установка)
   - [Системные требования](#системные-требования)
   - [Процесс установки](#процесс-установки)
3. [Начало работы](#начало-работы)
   - [Запуск симулятора](#запуск-симулятора)
   - [Базовая конфигурация](#базовая-конфигурация)
4. [Использование CLI](#использование-cli)
   - [Основные команды](#основные-команды)
   - [Настройка портов](#настройка-портов)
   - [Настройка VLAN](#настройка-vlan)
   - [Настройка маршрутизации](#настройка-маршрутизации)
5. [Использование Python API](#использование-python-api)
   - [Инициализация](#инициализация)
   - [Управление портами](#управление-портами)
   - [Управление VLAN](#управление-vlan)
   - [Управление маршрутизацией](#управление-маршрутизацией)
6. [Примеры использования](#примеры-использования)
   - [Базовая настройка L2-коммутации](#базовая-настройка-l2-коммутации)
   - [Настройка L3-маршрутизации](#настройка-l3-маршрутизации)
   - [Интеграция с внешними приложениями](#интеграция-с-внешними-приложениями)
7. [Устранение неполадок](#устранение-неполадок)
   - [Известные проблемы](#известные-проблемы)
   - [Советы по отладке](#советы-по-отладке)
8. [Приложения](#приложения)
   - [Справка по командам CLI](#справка-по-командам-cli)
   - [Справка по функциям API](#справка-по-функциям-api)

## Введение

Switch Simulator — это программное обеспечение, которое эмулирует работу сетевого коммутатора, предоставляя возможности для тестирования, обучения и разработки сетевых приложений без необходимости использования физического оборудования.

Симулятор поддерживает следующие основные функции:
- Эмуляция стандартных коммутаторов второго и третьего уровней
- Поддержка VLAN, STP, MAC-таблицы
- L3-маршрутизация с поддержкой статических маршрутов и протоколов OSPF, RIP
- Интерфейс командной строки (CLI), схожий с коммерческими коммутаторами
- Python API для программного управления и интеграции

## Установка

### Системные требования

- Linux (Ubuntu 20.04+, CentOS 8+) или macOS 11+
- Python 3.8 или выше
- CMake 3.15 или выше
- GCC 9+ или Clang 10+
- 2 ГБ ОЗУ минимум (рекомендуется 4 ГБ)
- 1 ГБ свободного места на диске

### Процесс установки

1. Клонируйте репозиторий:
   ```bash
   git clone https://github.com/yourusername/switch-simulator.git
   cd switch-simulator
   ```

2. Соберите проект:
   ```bash
   mkdir build
   cd build
   cmake ..
   make
   ```

3. Установите Python-зависимости:
   ```bash
   pip install -r requirements.txt
   ```

## Начало работы

### Запуск симулятора

Запустить симулятор можно следующими способами:

1. Через CLI-интерфейс:
   ```bash
   ./bin/switch-sim-cli
   ```

2. Как службу в фоновом режиме:
   ```bash
   ./bin/switch-sim-daemon
   ```

3. Через Python API:
   ```python
   from switch_simulator.api import SwitchController
   
   # Инициализация симулятора
   switch = SwitchController()
   ```

### Базовая конфигурация

После запуска симулятора необходимо выполнить базовую конфигурацию:

1. Включение портов:
   ```
   switch> enable
   switch# configure terminal
   switch(config)# interface eth1
   switch(config-if)# no shutdown
   switch(config-if)# exit
   ```

2. Настройка имени устройства:
   ```
   switch(config)# hostname my-switch
   my-switch(config)# exit
   ```

## Использование CLI

### Основные команды

CLI симулятора основан на стандартном интерфейсе, используемом в большинстве коммерческих коммутаторов:

- `show` - отображение информации
- `configure terminal` - вход в режим конфигурации
- `interface <name>` - конфигурация интерфейса
- `vlan <id>` - конфигурация VLAN
- `exit` - выход из текущего режима
- `help` - отображение справки

### Настройка портов

```
switch# configure terminal
switch(config)# interface eth1
switch(config-if)# description "Uplink to Router"
switch(config-if)# speed 1000
switch(config-if)# duplex full
switch(config-if)# no shutdown
switch(config-if)# exit
```

### Настройка VLAN

```
switch(config)# vlan 10
switch(config-vlan)# name Engineering
switch(config-vlan)# exit

switch(config)# interface eth2
switch(config-if)# switchport mode access
switch(config-if)# switchport access vlan 10
switch(config-if)# exit

switch(config)# interface eth3
switch(config-if)# switchport mode trunk
switch(config-if)# switchport trunk allowed vlan 10,20,30
switch(config-if)# exit
```

### Настройка маршрутизации

```
switch(config)# ip routing
switch(config)# interface vlan10
switch(config-if)# ip address 192.168.10.1 255.255.255.0
switch(config-if)# exit

switch(config)# ip route 0.0.0.0 0.0.0.0 192.168.1.1
```

## Использование Python API

### Инициализация

```python
from switch_simulator.api import SwitchController

# Создание экземпляра контроллера
switch = SwitchController()

# Получение списка портов
ports = switch.get_port_list()
print(f"Available ports: {ports}")
```

### Управление портами

```python
from switch_simulator.api import SwitchController, PortState, PortSpeed

switch = SwitchController()

# Включение порта
switch.set_port_state(1, PortState.UP)

# Установка скорости порта
switch.set_port_speed(1, PortSpeed.SPEED_10G)

# Получение информации о порте
port_info = switch.get_port_info(1)
print(f"Port 1 state: {port_info['state']}, speed: {port_info['speed']} Mbps")
```

### Управление VLAN

```python
from switch_simulator.api import SwitchController

switch = SwitchController()

# Создание VLAN
switch.create_vlan(10, name="Engineering")

# Добавление порта в VLAN
switch.add_port_to_vlan(2, 10, tagged=False)
switch.add_port_to_vlan(3, 10, tagged=True)

# Получение информации о VLAN
vlan_info = switch.get_vlan_info(10)
print(f"VLAN 10 ports: {vlan_info['ports']}")
```

### Управление маршрутизацией

```python
from switch_simulator.api import SwitchController

switch = SwitchController()

# Включение IP-маршрутизации
switch.enable_ip_routing()

# Настройка IP-адреса на VLAN-интерфейсе
switch.set_interface_ip("vlan10", "192.168.10.1", "255.255.255.0")

# Добавление статического маршрута
switch.add_static_route("0.0.0.0", "0.0.0.0", "192.168.1.1")

# Получение таблицы маршрутизации
routes = switch.get_routing_table()
for route in routes:
    print(f"Destination: {route['destination']}, Next hop: {route['next_hop']}")
```

## Примеры использования

### Базовая настройка L2-коммутации

В этом примере мы настроим базовую L2-коммутацию с двумя VLAN:

```python
from switch_simulator.api import SwitchController, PortState

switch = SwitchController()

# Настройка VLAN
switch.create_vlan(10, name="Data")
switch.create_vlan(20, name="Voice")

# Настройка портов доступа
for port_id in [1, 2, 3]:
    switch.set_port_state(port_id, PortState.UP)
    switch.add_port_to_vlan(port_id, 10, tagged=False)

for port_id in [4, 5, 6]:
    switch.set_port_state(port_id, PortState.UP)
    switch.add_port_to_vlan(port_id, 20, tagged=False)

# Настройка транковых портов
for port_id in [7, 8]:
    switch.set_port_state(port_id, PortState.UP)
    switch.set_port_mode(port_id, "trunk")
    switch.add_port_to_vlan(port_id, 10, tagged=True)
    switch.add_port_to_vlan(port_id, 20, tagged=True)

print("L2 switching configuration completed!")
```

### Настройка L3-маршрутизации

В этом примере мы настроим L3-маршрутизацию между VLAN:

```python
from switch_simulator.api import SwitchController

switch = SwitchController()

# Включение IP-маршрутизации
switch.enable_ip_routing()

# Настройка VLAN
switch.create_vlan(10, name="Engineering")
switch.create_vlan(20, name="Sales")

# Настройка портов
for port_id in [1, 2]:
    switch.add_port_to_vlan(port_id, 10, tagged=False)
for port_id in [3, 4]:
    switch.add_port_to_vlan(port_id, 20, tagged=False)

# Настройка VLAN-интерфейсов
switch.set_interface_ip("vlan10", "192.168.10.1", "255.255.255.0")
switch.set_interface_ip("vlan20", "192.168.20.1", "255.255.255.0")

# Настройка маршрута по умолчанию
switch.add_static_route("0.0.0.0", "0.0.0.0", "192.168.1.1")

print("L3 routing configuration completed!")
```

## Устранение неполадок

### Известные проблемы

1. **Ошибка при загрузке библиотеки simulator**: Убедитесь, что путь к библиотеке `libswitchsim.so` правильный.
   ```
   export LD_LIBRARY_PATH=/path/to/switch-simulator/build:$LD_LIBRARY_PATH
   ```

2. **CLI не запускается**: Проверьте права доступа к бинарным файлам.
   ```
   chmod +x bin/switch-sim-cli
   ```

3. **Высокое использование CPU**: Оптимизируйте параметр `sim_rate` в конфигурационном файле.

### Советы по отладке

1. Увеличьте уровень логирования:
   ```
   switch# configure terminal
   switch(config)# logging level debug
   ```

2. Используйте команды для диагностики:
   ```
   switch# show tech-support
   switch# show logging
   switch# show interfaces status
   switch# debug packet all
   ```

3. Проверьте логи системы:
   ```bash
   tail -f logs/switch-simulator.log
   ```

## Приложения

### Справка по командам CLI

| Команда | Описание |
|---------|----------|
| `show running-config` | Показать текущую конфигурацию |
| `show interfaces status` | Показать состояние интерфейсов |
| `show vlan` | Показать информацию о VLAN |
| `show mac-address-table` | Показать MAC-таблицу |
| `show ip route` | Показать таблицу маршрутизации |
| `configure terminal` | Войти в режим конфигурации |
| `interface <name>` | Настроить интерфейс |
| `vlan <id>` | Настроить VLAN |
| `ip route <prefix> <mask> <next-hop>` | Настроить статический маршрут |

### Справка по функциям API

Полная документация по Python API доступна в директории `doc/api/`.

Основные классы и методы:
- `SwitchController` - основной класс для управления коммутатором
- `StatsViewer` - класс для просмотра и анализа статистики
- `NetworkTopology` - класс для работы с топологией сети
- `TrafficGenerator` - класс для генерации тестового трафика
