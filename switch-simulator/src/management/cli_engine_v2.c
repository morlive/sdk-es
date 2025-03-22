/**
 * @file cli_engine.c
 * @brief Реализация движка командной строки для switch-simulator
 */

#include "include/management/cli.h"
#include "include/common/logging.h"
#include "include/common/error_codes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Максимальное количество команд в системе */
#define MAX_CLI_COMMANDS 256

/* Максимальное количество аргументов команды */
#define MAX_CLI_ARGS 16

/* Максимальная длина строки ввода */
#define MAX_INPUT_LENGTH 1024

/* Максимальная глубина для режимов CLI */
#define MAX_CLI_MODE_DEPTH 8

/* Структура для хранения информации о команде */
typedef struct {
    char *command;                  /* Имя команды */
    char *help;                     /* Краткая справка */
    char *description;              /* Полное описание */
    cli_mode_t mode;                /* Режим CLI, в котором доступна команда */
    cli_handler_t handler;          /* Обработчик команды */
    cli_completion_handler_t comp;  /* Обработчик автодополнения */
    bool is_active;                 /* Флаг активности команды */
} cli_command_entry_t;

/* Структура для хранения текущего состояния CLI */
typedef struct {
    cli_mode_t mode_stack[MAX_CLI_MODE_DEPTH]; /* Стек режимов CLI */
    int mode_depth;                            /* Текущая глубина стека режимов */
    char prompt[64];                           /* Текущий промпт */
    bool is_running;                           /* Флаг работы CLI */
    cli_output_handler_t output_handler;       /* Обработчик вывода */
    void *output_context;                      /* Контекст для обработчика вывода */
    cli_input_handler_t input_handler;         /* Обработчик ввода */
    void *input_context;                       /* Контекст для обработчика ввода */
} cli_state_t;

/* Глобальные переменные */
static cli_command_entry_t commands[MAX_CLI_COMMANDS];
static int command_count = 0;
static cli_state_t cli_state;
static bool cli_initialized = false;

/* Предварительные объявления функций */
static void cli_update_prompt(void);
static void cli_process_command(const char *input);
static void cli_handle_help(const char *command);
static int cli_tokenize(char *input, char *tokens[], int max_tokens);

/* Стандартные обработчики вывода и ввода */
static void default_output_handler(const char *output, void *context) {
    printf("%s", output);
}

static char *default_input_handler(void *context) {
    static char input[MAX_INPUT_LENGTH];
    printf("%s", cli_state.prompt);
    if (fgets(input, sizeof(input), stdin) == NULL) {
        return NULL;
    }
    
    /* Удаление символа новой строки */
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n') {
        input[len - 1] = '\0';
    }
    
    return input;
}

/**
 * @brief Инициализация движка CLI
 * 
 * @param output_handler Обработчик вывода (NULL для стандартного)
 * @param output_context Контекст для обработчика вывода
 * @param input_handler Обработчик ввода (NULL для стандартного)
 * @param input_context Контекст для обработчика ввода
 * @return cli_status_t Статус операции
 */
cli_status_t cli_initialize(cli_output_handler_t output_handler, void *output_context,
                           cli_input_handler_t input_handler, void *input_context) {
    LOG_INFO("Initializing CLI engine");
    
    if (cli_initialized) {
        LOG_WARN("CLI engine already initialized");
        return CLI_STATUS_ALREADY_INITIALIZED;
    }
    
    /* Сброс состояния CLI */
    memset(&cli_state, 0, sizeof(cli_state));
    memset(commands, 0, sizeof(commands));
    command_count = 0;
    
    /* Установка режима по умолчанию */
    cli_state.mode_stack[0] = CLI_MODE_NORMAL;
    cli_state.mode_depth = 1;
    cli_state.is_running = false;
    
    /* Установка обработчиков */
    cli_state.output_handler = (output_handler != NULL) ? output_handler : default_output_handler;
    cli_state.output_context = output_context;
    cli_state.input_handler = (input_handler != NULL) ? input_handler : default_input_handler;
    cli_state.input_context = input_context;
    
    /* Обновление промпта */
    cli_update_prompt();
    
    /* Регистрация базовых команд */
    cli_register_command("help", "Show available commands", "Displays a list of available commands. Use 'help <command>' for detailed help on a specific command.", 
                         CLI_MODE_ANY, cli_handle_help, NULL);
    
    cli_register_command("exit", "Exit current mode or CLI", "Exits the current CLI mode. In normal mode, exits the CLI.", 
                         CLI_MODE_ANY, NULL, NULL);
    
    cli_initialized = true;
    LOG_INFO("CLI engine initialized successfully");
    
    return CLI_STATUS_SUCCESS;
}

/**
 * @brief Обновление промпта CLI
 */
static void cli_update_prompt(void) {
    const char *mode_names[] = {
        "normal",  /* CLI_MODE_NORMAL */
        "config",  /* CLI_MODE_CONFIG */
        "vlan",    /* CLI_MODE_VLAN */
        "interface", /* CLI_MODE_INTERFACE */
        "routing", /* CLI_MODE_ROUTING */
        "debug"    /* CLI_MODE_DEBUG */
    };
    
    /* Формирование промпта на основе текущего режима */
    if (cli_state.mode_depth > 0) {
        cli_mode_t current_mode = cli_state.mode_stack[cli_state.mode_depth - 1];
        if (current_mode < sizeof(mode_names) / sizeof(mode_names[0])) {
            snprintf(cli_state.prompt, sizeof(cli_state.prompt), "switch-%s> ", mode_names[current_mode]);
        } else {
            strcpy(cli_state.prompt, "switch> ");
        }
    } else {
        strcpy(cli_state.prompt, "switch> ");
    }
}

/**
 * @brief Регистрация команды в CLI
 * 
 * @param command Имя команды
 * @param help Краткая справка
 * @param description Полное описание
 * @param mode Режим CLI, в котором доступна команда
 * @param handler Обработчик команды
 * @param comp Обработчик автодополнения
 * @return cli_status_t Статус операции
 */
cli_status_t cli_register_command(const char *command, const char *help, const char *description,
                                 cli_mode_t mode, cli_handler_t handler, cli_completion_handler_t comp) {
    if (!cli_initialized) {
        LOG_ERROR("CLI engine not initialized");
        return CLI_STATUS_NOT_INITIALIZED;
    }
    
    if (command == NULL || strlen(command) == 0) {
        LOG_ERROR("Invalid command name");
        return CLI_STATUS_INVALID_PARAMETER;
    }
    
    /* Проверка наличия команды */
    for (int i = 0; i < command_count; i++) {
        if (commands[i].is_active && strcmp(commands[i].command, command) == 0) {
            LOG_ERROR("Command '%s' already registered", command);
            return CLI_STATUS_DUPLICATE;
        }
    }
    
    /* Проверка переполнения таблицы команд */
    if (command_count >= MAX_CLI_COMMANDS) {
        LOG_ERROR("Command table overflow");
        return CLI_STATUS_OVERFLOW;
    }
    
    /* Регистрация команды */
    int index = command_count++;
    
    commands[index].command = strdup(command);
    commands[index].help = (help != NULL) ? strdup(help) : strdup("");
    commands[index].description = (description != NULL) ? strdup(description) : strdup("");
    commands[index].mode = mode;
    commands[index].handler = handler;
    commands[index].comp = comp;
    commands[index].is_active = true;
    
    LOG_INFO("Registered command '%s' in mode %d", command, mode);
    
    return CLI_STATUS_SUCCESS;
}

/**
 * @brief Отмена регистрации команды в CLI
 * 
 * @param command Имя команды
 * @return cli_status_t Статус операции
 */
cli_status_t cli_unregister_command(const char *command) {
    if (!cli_initialized) {
        LOG_ERROR("CLI engine not initialized");
        return CLI_STATUS_NOT_INITIALIZED;
    }
    
    if (command == NULL) {
        LOG_ERROR("Invalid command name");
        return CLI_STATUS_INVALID_PARAMETER;
    }
    
    /* Поиск команды */
    for (int i = 0; i < command_count; i++) {
        if (commands[i].is_active && strcmp(commands[i].command, command) == 0) {
            /* Освобождение памяти */
            free(commands[i].command);
            free(commands[i].help);
            free(commands[i].description);
            
            /* Пометка команды как неактивной */
            commands[i].is_active = false;
            
            LOG_INFO("Unregistered command '%s'", command);
            return CLI_STATUS_SUCCESS;
        }
    }
    
    LOG_WARN("Command '%s' not found", command);
    return CLI_STATUS_NOT_FOUND;
}

/**
 * @brief Поиск команды по имени
 * 
 * @param command Имя команды
 * @param mode Режим CLI
 * @return int Индекс команды или -1, если не найдена
 */
static int cli_find_command(const char *command, cli_mode_t mode) {
    for (int i = 0; i < command_count; i++) {
        if (commands[i].is_active && strcmp(commands[i].command, command) == 0 && 
            (commands[i].mode == mode || commands[i].mode == CLI_MODE_ANY)) {
            return i;
        }
    }
    
    return -1;
}

/**
 * @brief Установка режима CLI
 * 
 * @param mode Новый режим CLI
 * @return cli_status_t Статус операции
 */
cli_status_t cli_set_mode(cli_mode_t mode) {
    if (!cli_initialized) {
        LOG_ERROR("CLI engine not initialized");
        return CLI_STATUS_NOT_INITIALIZED;
    }
    
    if (mode < 0 || mode >= CLI_MODE_MAX) {
        LOG_ERROR("Invalid CLI mode: %d", mode);
        return CLI_STATUS_INVALID_PARAMETER;
    }
    
    /* Проверка глубины стека режимов */
    if (cli_state.mode_depth >= MAX_CLI_MODE_DEPTH) {
        LOG_ERROR("CLI mode stack overflow");
        return CLI_STATUS_OVERFLOW;
    }
    
    /* Добавление режима в стек */
    cli_state.mode_stack[cli_state.mode_depth++] = mode;
    cli_update_prompt();
    
    LOG_INFO("CLI mode changed to %d", mode);
    
    return CLI_STATUS_SUCCESS;
}

/**
 * @brief Возврат к предыдущему режиму CLI
 * 
 * @return cli_status_t Статус операции
 */
cli_status_t cli_exit_mode(void) {
    if (!cli_initialized) {
        LOG_ERROR("CLI engine not initialized");
        return CLI_STATUS_NOT_INITIALIZED;
    }
    
    /* Проверка наличия предыдущего режима */
    if (cli_state.mode_depth <= 1) {
        LOG_WARN("Cannot exit from base mode");
        return CLI_STATUS_INVALID_STATE;
    }
    
    /* Удаление текущего режима из стека */
    cli_state.mode_depth--;
    cli_update_prompt();
    
    LOG_INFO("CLI returned to previous mode: %d", cli_state.mode_stack[cli_state.mode_depth - 1]);
    
    return CLI_STATUS_SUCCESS;
}

/**
 * @brief Получение текущего режима CLI
 * 
 * @return cli_mode_t Текущий режим CLI
 */
cli_mode_t cli_get_current_mode(void) {
    if (!cli_initialized || cli_state.mode_depth == 0) {
        return CLI_MODE_NORMAL;
    }
    
    return cli_state.mode_stack[cli_state.mode_depth - 1];
}

/**
 * @brief Вывод текста через обработчик вывода
 * 
 * @param format Форматная строка (printf-style)
 * @param ... Аргументы форматирования
 */
void cli_printf(const char *format, ...) {
    char buffer[4096];
    va_list args;
    
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (cli_state.output_handler != NULL) {
        cli_state.output_handler(buffer, cli_state.output_context);
    }
}

/**
 * @brief Обработчик команды help
 * 
 * @param command Команда, для которой нужна справка (NULL для всех)
 */
static void cli_handle_help(const char *command) {
    cli_mode_t current_mode = cli_get_current_mode();
    
    if (command == NULL || strlen(command) == 0) {
        /* Отображение всех команд для текущего режима */
        cli_printf("Available commands:\n");
        
        for (int i = 0; i < command_count; i++) {
            if (commands[i].is_active && (commands[i].mode == current_mode || commands[i].mode == CLI_MODE_ANY)) {
                cli_printf("  %-20s - %s\n", commands[i].command, commands[i].help);
            }
        }
        
        cli_printf("\nUse 'help <command>' for more information on a specific command.\n");
    } else {
        /* Поиск команды */
        int idx = cli_find_command(command, current_mode);
        if (idx == -1) {
            idx = cli_find_command(command, CLI_MODE_ANY);
        }
        
        if (idx >= 0) {
            /* Отображение подробной справки по команде */
            cli_printf("Command: %s\n", commands[idx].command);
            cli_printf("Description: %s\n", commands[idx].description);
        } else {
            cli_printf("Unknown command: %s\n", command);
        }
    }
}

/**
 * @brief Разбиение строки на токены
 * 
 * @param input Входная строка
 * @param tokens Массив для хранения токенов
 * @param max_tokens Максимальное количество токенов
 * @return int Количество токенов
 */
static int cli_tokenize(char *input, char *tokens[], int max_tokens) {
    int count = 0;
    char *token = strtok(input, " \t\n");
    
    while (token != NULL && count < max_tokens) {
        tokens[count++] = token;
        token = strtok(NULL, " \t\n");
    }
    
    return count;
}

/**
 * @brief Обработка введенной команды
 * 
 * @param input Строка ввода
 */
static void cli_process_command(const char *input) {
    char input_copy[MAX_INPUT_LENGTH];
    char *tokens[MAX_CLI_ARGS];
    int token_count;
    
    /* Проверка пустой строки */
    if (input == NULL || strlen(input) == 0) {
        return;
    }
    
    /* Создание копии строки для разбора (strtok модифицирует строку) */
    strncpy(input_copy, input, sizeof(input_copy) - 1);
    input_copy[sizeof(input_copy) - 1] = '\0';
    
    /* Разбор строки на токены */
    token_count = cli_tokenize(input_copy, tokens, MAX_CLI_ARGS);
    
    if (token_count == 0) {
        return;
    }
    
    /* Получение текущего режима */
    cli_mode_t current_mode = cli_get_current_mode();
    
    /* Специальная обработка команды exit */
    if (strcmp(tokens[0], "exit") == 0) {
        if (current_mode == CLI_MODE_NORMAL) {
            /* Выход из CLI */
            cli_state.is_running = false;
            return;
        } else {
            /* Возврат к предыдущему режиму */
            cli_exit_mode();
            return;
        }
    }
    
    /* Специальная обработка команды help */
    if (strcmp(tokens[0], "help") == 0) {
        cli_handle_help(token_count > 1 ? tokens[1] : NULL);
        return;
    }
    
    /* Поиск команды в текущем режиме */
    int cmd_idx = cli_find_command(tokens[0], current_mode);
    
    if (cmd_idx == -1) {
        /* Попытка найти команду в режиме ANY */
        cmd_idx = cli_find_command(tokens[0], CLI_MODE_ANY);
    }
    
    if (cmd_idx != -1) {
        /* Вызов обработчика команды */
        if (commands[cmd_idx].handler != NULL) {
            commands[cmd_idx].handler(token_count > 1 ? tokens[1] : NULL);
        } else {
            cli_printf("Command '%s' is not implemented yet\n", tokens[0]);
        }
    } else {
        cli_printf("Unknown command: %s\n", tokens[0]);
    }
}

/**
 * @brief Запуск основного цикла CLI
 * 
 * @return cli_status_t Статус операции
 */
cli_status_t cli_run(void) {
    if (!cli_initialized) {
        LOG_ERROR("CLI engine not initialized");
        return CLI_STATUS_NOT_INITIALIZED;
    }
    
    LOG_INFO("Starting CLI main loop");
    
    cli_state.is_running = true;
    
    /* Вывод приветствия */
    cli_printf("Welcome to Switch Simulator CLI\n");
    cli_printf("Type 'help' for a list of available commands\n");
    
    /* Основной цикл CLI */
    while (cli_state.is_running) {
        char *input = cli_state.input_handler(cli_state.input_context);
        
        if (input == NULL) {
            LOG_WARN("Input handler returned NULL, exiting CLI");
            break;
        }
        
        cli_process_command(input);
    }
    
    LOG_INFO("CLI main loop exited");
    
    return CLI_STATUS_SUCCESS;
}

/**
 * @brief Деинициализация движка CLI
 * 
 * @return cli_status_t Статус операции
 */
cli_status_t cli_deinitialize(void) {
    if (!cli_initialized) {
        LOG_WARN("CLI engine not initialized");
        return CLI_STATUS_NOT_INITIALIZED;
    }
    
    LOG_INFO("Deinitializing CLI engine");
    
    /* Освобождение памяти для команд */
    for (int i = 0; i < command_count; i++) {
        if (commands[i].is_active) {
            free(commands[i].command);
            free(commands[i].help);
            free(commands[i].description);
            commands[i].is_active = false;
        }
    }
    
    command_count = 0;
    cli_initialized = false;
    
    LOG_INFO("CLI engine deinitialized successfully");
    
    return CLI_STATUS_SUCCESS;
}
