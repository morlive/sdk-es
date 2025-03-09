switch-simulator/
├── bsp/
│   ├── include/
│   │   └── bsp.h             # Заголовочные файлы BSP
│   └── src/
│       ├── bsp_init.c        # Инициализация аппаратной платформы
│       ├── bsp_config.c      # Конфигурация аппаратных параметров
│       └── bsp_drivers.c     # Драйверы для базовых функций оборудования
├── drivers/
│   ├── include/
│   │   ├── ethernet_driver.h # Интерфейс драйвера для Ethernet
│   │   └── sim_driver.h      # Интерфейс драйвера для аппаратной симуляции
│   └── src/
│       ├── ethernet_driver.c # Реализация драйвера Ethernet
│       └── sim_driver.c      # Реализация драйвера симуляции  
├── build/                      # Директория для скомпилированных файлов
├── doc/                        # Документация
│   ├── architecture/           # Архитектурные документы
│   │   ├── high_level_design.md
│   │   └── component_diagrams/
│   ├── api/                    # Документация по API
│   │   ├── hal_api.md
│   │   ├── sai_api.md
│   │   └── cli_api.md
│   └── user_guide.md
├── include/                    # Публичные заголовочные файлы
│   ├── common/                 # Общие определения
│   │   ├── types.h             # Базовые типы данных
│   │   ├── error_codes.h       # Коды ошибок
│   │   └── logging.h           # Интерфейс логирования
│   ├── hal/                    # Интерфейс аппаратной абстракции
│   │   ├── port.h              # Абстракция портов
│   │   ├── packet.h            # Обработка пакетов
│   │   └── hw_resources.h      # Виртуальные ресурсы оборудования
│   ├── l2/                     # L2 функциональность
│   │   ├── mac_table.h         # Таблица MAC-адресов
│   │   ├── vlan.h              # VLAN функциональность
│   │   └── stp.h               # Spanning Tree Protocol
│   ├── l3/                     # L3 функциональность
│   │   ├── routing_table.h     # Таблица маршрутизации
│   │   ├── ip.h                # Обработка IP-пакетов
│   │   └── routing_protocols.h # Интерфейс протоколов маршрутизации
│   ├── sai/                    # Switch Abstraction Interface
│   │   ├── sai_port.h
│   │   ├── sai_vlan.h
│   │   └── sai_route.h
│   └── management/             # Управление устройством
│       ├── cli.h               # Интерфейс для CLI
│       └── stats.h             # Интерфейс статистики
├── src/                        # Исходный код
│   ├── common/                 # Общая функциональность
│   │   ├── logging.c
│   │   └── utils.c
│   ├── hal/                    # Реализация HAL
│   │   ├── port.c
│   │   ├── packet.c
│   │   └── hw_simulation.c     # Симуляция аппаратных компонентов
│   ├── l2/                     # Реализация L2
│   │   ├── mac_learning.c
│   │   ├── mac_table.c
│   │   ├── vlan.c
│   │   └── stp.c
│   ├── l3/                     # Реализация L3
│   │   ├── routing_table.c
│   │   ├── ip_processing.c
│   │   ├── arp.c
│   │   └── routing_protocols/
│   │       ├── rip.c
│   │       └── ospf.c
│   ├── sai/                    # Реализация SAI
│   │   ├── sai_adapter.c       # Адаптер SAI к HAL
│   │   ├── sai_port.c
│   │   ├── sai_vlan.c
│   │   └── sai_route.c
│   ├── management/             # Управление и мониторинг
│   │   ├── cli_engine.c        # Движок CLI
│   │   ├── stats_collector.c   # Сбор статистики
│   │   └── config_manager.c    # Управление конфигурацией
│   └── main.c                  # Точка входа
├── tools/                      # Инструменты
│   ├── simulators/             # Симуляторы
│   │   ├── traffic_generator.c # Генератор трафика
│   │   └── network_topology.py # Симулятор топологии
│   └── scripts/                # Скрипты
│       ├── build.sh            # Скрипт сборки
│       └── test_runner.py      # Запуск тестов
├── python/                     # Python интерфейс
│   ├── cli/                    # CLI реализация
│   │   ├── __init__.py
│   │   ├── commands/           # Команды CLI
│   │   │   ├── __init__.py
│   │   │   ├── port_commands.py
│   │   │   ├── vlan_commands.py
│   │   │   └── routing_commands.py
│   │   ├── cli_main.py         # Главный модуль CLI
│   │   └── cli_parser.py       # Парсер команд
│   └── api/                    # Python API
│       ├── __init__.py
│       ├── switch_controller.py # Контроллер коммутатора
│       └── stats_viewer.py     # Просмотр статистики
├── tests/                      # Тесты
│   ├── unit/                   # Модульные тесты
│   │   ├── test_mac_table.c
│   │   ├── test_routing.c
│   │   └── test_vlan.c
│   ├── integration/            # Интеграционные тесты
│   │   ├── test_l2_switching.c
│   │   └── test_l3_routing.c
│   └── system/                 # Системные тесты
│       ├── test_performance.py
│       └── test_network_scenarios.py
├── CMakeLists.txt              # Файл сборки CMake
├── Makefile                    # Makefile
├── README.md                   # Описание проекта
└── LICENSE                     # Лицензия
