switch-simulator/
├── bsp/
│   ├── include/
│   │   └── bsp.h
│   └── src/
│       ├── bsp_init.c
│       ├── bsp_config.c
│       └── bsp_drivers.c
├── drivers/
│   ├── include/
│   │   ├── ethernet_driver.h
│   │   └── sim_driver.h
│   └── src/
│       ├── ethernet_driver.c
│       └── sim_driver.c
├── build/
├── doc/
│   ├── architecture/
│   │   ├── high_level_design.md
│   │   └── component_diagrams/
│   ├── api/
│   │   ├── hal_api.md
│   │   ├── sai_api.md
│   │   └── cli_api.md
│   └── user_guide.md
├── include/
│   ├── common/
│   │   ├── types.h
│   │   ├── error_codes.h
│   │   └── logging.h
│   ├── hal/
│   │   ├── port.h
│   │   ├── packet.h
│   │   └── hw_resources.h
│   ├── l2/
│   │   ├── mac_table.h
│   │   ├── vlan.h
│   │   └── stp.h
│   ├── l3/
│   │   ├── routing_table.h
│   │   ├── ip.h
│   │   └── routing_protocols.h
│   ├── sai/
│   │   ├── sai_port.h
│   │   ├── sai_vlan.h
│   │   └── sai_route.h
│   └── management/
│       ├── cli.h
│       └── stats.h
├── src/
│   ├── common/
│   │   ├── logging.c
│   │   └── utils.c
│   ├── hal/
│   │   ├── port.c
│   │   ├── packet.c
│   │   └── hw_simulation.c
│   ├── l2/
│   │   ├── mac_learning.c
│   │   ├── mac_table.c
│   │   ├── vlan.c
│   │   └── stp.c
│   ├── l3/
│   │   ├── routing_table.c
│   │   ├── ip_processing.c
│   │   ├── arp.c
│   │   └── routing_protocols/
│   │       ├── rip.c
│   │       └── ospf.c
│   ├── sai/
│   │   ├── sai_adapter.c
│   │   ├── sai_port.c
│   │   ├── sai_vlan.c
│   │   └── sai_route.c
│   ├── management/
│   │   ├── cli_engine.c
│   │   ├── stats_collector.c
│   │   └── config_manager.c
│   └── main.c
├── tools/
│   ├── simulators/
│   │   ├── traffic_generator.c
│   │   └── network_topology.py
│   └── scripts/
│       ├── build.sh
│       └── test_runner.py
├── python/
│   ├── cli/
│   │   ├── __init__.py
│   │   ├── commands/
│   │   │   ├── __init__.py
│   │   │   ├── port_commands.py
│   │   │   ├── vlan_commands.py
│   │   │   └── routing_commands.py
│   │   ├── cli_main.py
│   │   └── cli_parser.py
│   └── api/
│       ├── __init__.py
│       ├── switch_controller.py
│       └── stats_viewer.py
├── tests/
│   ├── unit/
│   │   ├── test_mac_table.c
│   │   ├── test_routing.c
│   │   └── test_vlan.c
│   ├── integration/
│   │   ├── test_l2_switching.c
│   │   └── test_l3_routing.c
│   └── system/
│       ├── test_performance.py
│       └── test_network_scenarios.py
├── CMakeLists.txt
├── Makefile
├── README.md
└── LICENSE
