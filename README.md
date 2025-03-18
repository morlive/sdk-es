# sdk-es

# Enterprise Switch Simulator SDK (sdk-es)

## Overview

Enterprise Switch Simulator SDK is a comprehensive solution for simulating, developing, and testing software for enterprise-level network switches. The project provides a full L2/L3 stack with Switch Abstraction Interface (SAI) support and is designed to accelerate development before physical hardware becomes available.

## Key Features

- **Full Switch Virtualization**: Simulation of switch hardware components for testing software solutions without physical equipment
- **L2/L3 Functionality Support**: Implementation of VLAN, STP, MAC address tables, IP routing, and routing protocols
- **SAI (Switch Abstraction Interface)**: Standardized interface for interacting with the switch
- **Comprehensive BSP (Board Support Package)**: Abstraction for working with various hardware platforms
- **CI/CD Integration**: Automated testing and code verification
- **Python API**: Programmatic control of the simulator through Python interface

## Architecture

The project is built using a modular architecture, ensuring flexibility and extensibility. Main components:

- **HAL (Hardware Abstraction Layer)**: Hardware resource abstraction
- **BSP (Board Support Package)**: Support for various hardware platforms
- **L2/L3 Stack**: Complete implementation of network protocols
- **SAI (Switch Abstraction Interface)**: Standardized switch interface
- **CLI and REST API**: Interfaces for management and monitoring
- **Simulation Environment**: Virtual platform for testing

Detailed project structure is available in [STRUCTURE.md](STRUCTURE.md).

<hr />

## Getting Started

> ### Requirements

<hr />

> Linux-compatible OS
>>
>>- GCC 8.0+ or Clang 9.0+
>>- CMake 3.10+
>>- Python 3.8+
>>- Ninja (optional)
>>
>>  ---
>> 
>>  compiler gcc
>>  ansi | c89 | posix 

<hr />

> ### Installation
>
>- ``bash
>- Clone the repository
>- git clone https://github.com/morrisel/sdk-es.git
>- cd sdk-es
>- 
>- Build the project
>- mkdir -p build && cd build
>- mmake ..
>- make
