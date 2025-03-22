# Command Line Interface (CLI) API Documentation

## Overview

CLI API предоставляет интерфейс для разработки и интеграции команд командной строки в симулятор коммутатора.

## Компоненты CLI API

### CLI Engine API

#### Функции

```c
cli_status_t cli_init(cli_config_t *config);
cli_status_t cli_register_command(const char *command, const char *help, cli_handler_t handler);
cli_status_t cli_register_command_group(const char *group, const char *help);
cli_status_t cli_register_subcommand(const char *group, const char *command, const char *help, cli_handler_t handler);
cli_status_t cli_start(void);
cli_status_t cli_stop(void);
cli_status_t cli_execute_command(const char *command_line, cli_output_t *output);
```

#### Типы данных

```c
typedef struct {
    bool telnet_enabled;
    uint16_t telnet_port;
    bool local_console_enabled;
    uint32_t history_size;
    const char *prompt;
} cli_config_t;

typedef struct {
    char *buffer;
    uint32_t buffer_size;
    uint32_t length;
} cli_output_t;

typedef cli_status_t (*cli_handler_t)(int argc, char **argv, cli_output_t *output);
```

### CLI Integration API

#### Функции для интеграции CLI с Python

```c
cli_status_t cli_register_python_command(const char *command, const char *help, PyObject *callable);
cli_status_t cli_register_python_command_group(const char *group, const char *help);
cli_status_t cli_register_python_subcommand(const char *group, const char *command, const char *help, PyObject *callable);
```

## Стандартные группы команд

| Группа команд | Описание |
|---------------|----------|
| show | Команды для отображения информации |
| configure | Команды для настройки коммутатора |
| clear | Команды для очистки статистики и таблиц |
| debug | Команды для отладки |
| system | Системные команды |

## Обработка ошибок

| Код ошибки | Описание |
|------------|----------|
| CLI_STATUS_SUCCESS | Операция успешна |
| CLI_STATUS_FAILURE | Общая ошибка |
| CLI_STATUS_INVALID_PARAMETER | Неверный параметр |
| CLI_STATUS_COMMAND_NOT_FOUND | Команда не найдена |
| CLI_STATUS_INSUFFICIENT_RESOURCES | Недостаточно ресурсов |
| CLI_STATUS_PERMISSION_DENIED | Нет прав доступа |

## Примеры использования

### Инициализация CLI

```c
cli_config_t config;
config.telnet_enabled = true;
config.telnet_port = 2323;
config.local_console_enabled = true;
config.history_size = 100;
config.prompt = "switch> ";

cli_status_t status = cli_init(&config);
if (status != CLI_STATUS_SUCCESS) {
    // Handle error
}

status = cli_start();
if (status != CLI_STATUS_SUCCESS) {
    // Handle error
}
```

### Регистрация команды

```c
cli_status_t show_vlan_handler(int argc, char **argv, cli_output_t *output) {
    // Реализация команды
    snprintf(output->buffer, output->buffer_size, "VLAN Information:\n");
    output->length = strlen(output->buffer);
    return CLI_STATUS_SUCCESS;
}

cli_status_t status = cli_register_command_group("show", "Display information");
if (status != CLI_STATUS_SUCCESS) {
    // Handle error
}

status = cli_register_subcommand("show", "vlan", "Display VLAN information", show_vlan_handler);
if (status != CLI_STATUS_SUCCESS) {
    // Handle error
}
```

### Выполнение команды

```c
cli_output_t output;
char buffer[1024];
output.buffer = buffer;
output.buffer_size = sizeof(buffer);
output.length = 0;

cli_status_t status = cli_execute_command("show vlan", &output);
if (status != CLI_STATUS_SUCCESS) {
    // Handle error
}

printf("Command output: %s\n", output.buffer);
```

### Интеграция Python-команды

```c
PyObject *show_interfaces_func = PyObject_GetAttrString(module, "show_interfaces");
cli_status_t status = cli_register_python_subcommand("show", "interfaces", 
    "Display interface information", show_interfaces_func);
if (status != CLI_STATUS_SUCCESS) {
    // Handle error
}
Py_DECREF(show_interfaces_func);
```
