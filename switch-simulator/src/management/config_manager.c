/**
 * @file config_manager.c
 * @brief Реализация менеджера конфигурации коммутатора
 *
 * Этот модуль отвечает за загрузку, сохранение и управление 
 * конфигурацией коммутатора, поддерживает стартовую и текущую
 * конфигурации, а также операции по их сохранению и восстановлению.
 */

#include "management/config_manager.h"
#include "common/logging.h"
#include "common/error_codes.h"
#include "common/types.h"
#include "hal/port.h"
#include "l2/vlan.h"
#include "l3/routing_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

/* Константы */
#define CONFIG_DIR              "./config"
#define STARTUP_CONFIG_FILE     CONFIG_DIR "/startup-config.json"
#define RUNNING_CONFIG_FILE     CONFIG_DIR "/running-config.json"
#define BACKUP_CONFIG_DIR       CONFIG_DIR "/backups"
#define MAX_CONFIG_SIZE         (1024 * 1024)  // 1 МБ
#define MAX_PATH_LENGTH         256
#define MAX_BACKUP_CONFIGS      10

/* Типы данных */
typedef struct {
    bool initialized;
    char *config_buffer;
    size_t config_buffer_size;
    pthread_mutex_t config_mutex;
    time_t last_save_time;
    bool config_modified;
} config_manager_t;

/* Глобальные переменные */
static config_manager_t g_config_manager = {0};

/* Локальные функции */
static sw_error_t create_config_directories(void);
static sw_error_t load_config_from_file(const char *filename, char **config_data, size_t *data_size);
static sw_error_t save_config_to_file(const char *filename, const char *config_data, size_t data_size);
static sw_error_t create_backup_config(void);
static sw_error_t parse_config_json(const char *config_data, size_t data_size);
static sw_error_t generate_config_json(char **config_data, size_t *data_size);

/**
 * @brief Инициализация менеджера конфигурации
 * 
 * @return SW_OK если успешно, иначе код ошибки
 */
sw_error_t config_manager_init(void) {
    sw_error_t err;

    if (g_config_manager.initialized) {
        LOG_WARN("Config manager already initialized");
        return SW_OK;
    }

    // Создаем директории для конфигурации
    err = create_config_directories();
    if (err != SW_OK) {
        LOG_ERROR("Failed to create config directories, error: %d", err);
        return err;
    }

    // Инициализируем мьютекс
    if (pthread_mutex_init(&g_config_manager.config_mutex, NULL) != 0) {
        LOG_ERROR("Failed to initialize config mutex");
        return SW_ERROR_INIT_FAILED;
    }

    // Выделяем буфер для конфигурации
    g_config_manager.config_buffer = (char *)malloc(MAX_CONFIG_SIZE);
    if (g_config_manager.config_buffer == NULL) {
        LOG_ERROR("Failed to allocate memory for config buffer");
        pthread_mutex_destroy(&g_config_manager.config_mutex);
        return SW_ERROR_NO_MEMORY;
    }
    g_config_manager.config_buffer_size = 0;

    // Загружаем стартовую конфигурацию
    err = config_manager_load_startup_config();
    if (err != SW_OK && err != SW_ERROR_FILE_NOT_FOUND) {
        LOG_ERROR("Failed to load startup config, error: %d", err);
        free(g_config_manager.config_buffer);
        pthread_mutex_destroy(&g_config_manager.config_mutex);
        return err;
    }

    // Если стартовая конфигурация не найдена, создаем пустую
    if (err == SW_ERROR_FILE_NOT_FOUND) {
        LOG_INFO("No startup config found, using default configuration");
        // Генерируем пустую конфигурацию
        err = generate_config_json(&g_config_manager.config_buffer, &g_config_manager.config_buffer_size);
        if (err != SW_OK) {
            LOG_ERROR("Failed to generate default config, error: %d", err);
            free(g_config_manager.config_buffer);
            pthread_mutex_destroy(&g_config_manager.config_mutex);
            return err;
        }
    }

    g_config_manager.last_save_time = time(NULL);
    g_config_manager.config_modified = false;
    g_config_manager.initialized = true;

    LOG_INFO("Config manager initialized successfully");
    return SW_OK;
}

/**
 * @brief Деинициализация менеджера конфигурации
 * 
 * @return SW_OK если успешно, иначе код ошибки
 */
sw_error_t config_manager_deinit(void) {
    if (!g_config_manager.initialized) {
        LOG_WARN("Config manager not initialized");
        return SW_OK;
    }

    pthread_mutex_lock(&g_config_manager.config_mutex);

    // Сохраняем текущую конфигурацию при выходе
    if (g_config_manager.config_modified) {
        LOG_INFO("Saving modified configuration before shutdown");
        save_config_to_file(RUNNING_CONFIG_FILE, 
                           g_config_manager.config_buffer, 
                           g_config_manager.config_buffer_size);
    }

    free(g_config_manager.config_buffer);
    g_config_manager.config_buffer = NULL;
    g_config_manager.config_buffer_size = 0;
    g_config_manager.initialized = false;

    pthread_mutex_unlock(&g_config_manager.config_mutex);
    pthread_mutex_destroy(&g_config_manager.config_mutex);

    LOG_INFO("Config manager deinitialized successfully");
    return SW_OK;
}

/**
 * @brief Загрузка стартовой конфигурации
 * 
 * @return SW_OK если успешно, иначе код ошибки
 */
sw_error_t config_manager_load_startup_config(void) {
    sw_error_t err;
    char *config_data = NULL;
    size_t data_size = 0;

    if (!g_config_manager.initialized) {
        LOG_ERROR("Config manager not initialized");
        return SW_ERROR_NOT_INITIALIZED;
    }

    pthread_mutex_lock(&g_config_manager.config_mutex);

    // Загружаем файл стартовой конфигурации
    err = load_config_from_file(STARTUP_CONFIG_FILE, &config_data, &data_size);
    if (err != SW_OK) {
        pthread_mutex_unlock(&g_config_manager.config_mutex);
        return err;
    }

    // Парсим JSON-конфигурацию и применяем настройки
    err = parse_config_json(config_data, data_size);
    if (err != SW_OK) {
        LOG_ERROR("Failed to parse startup config, error: %d", err);
        free(config_data);
        pthread_mutex_unlock(&g_config_manager.config_mutex);
        return err;
    }

    // Копируем конфигурацию в буфер менеджера
    if (data_size <= MAX_CONFIG_SIZE) {
        memcpy(g_config_manager.config_buffer, config_data, data_size);
        g_config_manager.config_buffer_size = data_size;
    } else {
        LOG_ERROR("Config file too large, max size: %d bytes", MAX_CONFIG_SIZE);
        free(config_data);
        pthread_mutex_unlock(&g_config_manager.config_mutex);
        return SW_ERROR_BUFFER_OVERFLOW;
    }

    free(config_data);
    g_config_manager.config_modified = false;
    g_config_manager.last_save_time = time(NULL);

    LOG_INFO("Startup configuration loaded successfully");
    pthread_mutex_unlock(&g_config_manager.config_mutex);
    return SW_OK;
}

/**
 * @brief Сохранение текущей конфигурации как стартовой
 * 
 * @return SW_OK если успешно, иначе код ошибки
 */
sw_error_t config_manager_save_startup_config(void) {
    sw_error_t err;

    if (!g_config_manager.initialized) {
        LOG_ERROR("Config manager not initialized");
        return SW_ERROR_NOT_INITIALIZED;
    }

    pthread_mutex_lock(&g_config_manager.config_mutex);

    // Создаем резервную копию текущей стартовой конфигурации
    err = create_backup_config();
    if (err != SW_OK) {
        LOG_WARN("Failed to create backup config, error: %d", err);
        // Продолжаем выполнение даже при ошибке резервного копирования
    }

    // Сохраняем текущую конфигурацию как стартовую
    err = save_config_to_file(STARTUP_CONFIG_FILE, 
                             g_config_manager.config_buffer, 
                             g_config_manager.config_buffer_size);
    if (err != SW_OK) {
        LOG_ERROR("Failed to save startup config, error: %d", err);
        pthread_mutex_unlock(&g_config_manager.config_mutex);
        return err;
    }

    g_config_manager.config_modified = false;
    g_config_manager.last_save_time = time(NULL);

    LOG_INFO("Current configuration saved as startup configuration");
    pthread_mutex_unlock(&g_config_manager.config_mutex);
    return SW_OK;
}

/**
 * @brief Создание директорий для конфигурации
 * 
 * @return SW_OK если успешно, иначе код ошибки
 */
static sw_error_t create_config_directories(void) {
    // Создаем основную директорию для конфигурации
    if (mkdir(CONFIG_DIR, 0755) != 0 && errno != EEXIST) {
        LOG_ERROR("Failed to create config directory, errno: %d", errno);
        return SW_ERROR_IO;
    }

    // Создаем директорию для резервных копий
    if (mkdir(BACKUP_CONFIG_DIR, 0755) != 0 && errno != EEXIST) {
        LOG_ERROR("Failed to create backup config directory, errno: %d", errno);
        return SW_ERROR_IO;
    }

    return SW_OK;
}

/**
 * @brief Загрузка конфигурации из файла
 * 
 * @param filename Имя файла конфигурации
 * @param config_data Указатель на буфер, который будет выделен для конфигурации
 * @param data_size Размер загруженной конфигурации
 * @return SW_OK если успешно, иначе код ошибки
 */
static sw_error_t load_config_from_file(const char *filename, char **config_data, size_t *data_size) {
    FILE *f;
    size_t file_size;
    char *buffer;

    if (filename == NULL || config_data == NULL || data_size == NULL) {
        return SW_ERROR_INVALID_PARAMETER;
    }

    f = fopen(filename, "rb");
    if (f == NULL) {
        if (errno == ENOENT) {
            LOG_WARN("Config file not found: %s", filename);
            return SW_ERROR_FILE_NOT_FOUND;
        } else {
            LOG_ERROR("Failed to open config file: %s, errno: %d", filename, errno);
            return SW_ERROR_IO;
        }
    }

    // Определяем размер файла
    fseek(f, 0, SEEK_END);
    file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size == 0) {
        LOG_WARN("Config file is empty: %s", filename);
        fclose(f);
        return SW_ERROR_EMPTY_FILE;
    }

    if (file_size > MAX_CONFIG_SIZE) {
        LOG_ERROR("Config file too large: %s, size: %zu bytes, max: %d bytes", 
                  filename, file_size, MAX_CONFIG_SIZE);
        fclose(f);
        return SW_ERROR_BUFFER_OVERFLOW;
    }

    // Выделяем память для конфигурации
    buffer = (char *)malloc(file_size + 1);
    if (buffer == NULL) {
        LOG_ERROR("Failed to allocate memory for config file: %s", filename);
        fclose(f);
        return SW_ERROR_NO_MEMORY;
    }

    // Читаем файл
    if (fread(buffer, 1, file_size, f) != file_size) {
        LOG_ERROR("Failed to read config file: %s", filename);
        free(buffer);
        fclose(f);
        return SW_ERROR_IO;
    }

    // Добавляем нулевой байт в конец строки
    buffer[file_size] = '\0';
    
    *config_data = buffer;
    *data_size = file_size;

    fclose(f);
    return SW_OK;
}

/**
 * @brief Сохранение конфигурации в файл
 * 
 * @param filename Имя файла конфигурации
 * @param config_data Данные конфигурации
 * @param data_size Размер данных конфигурации
 * @return SW_OK если успешно, иначе код ошибки
 */
static sw_error_t save_config_to_file(const char *filename, const char *config_data, size_t data_size) {
    FILE *f;

    if (filename == NULL || config_data == NULL || data_size == 0) {
        return SW_ERROR_INVALID_PARAMETER;
    }

    f = fopen(filename, "wb");
    if (f == NULL) {
        LOG_ERROR("Failed to open config file for writing: %s, errno: %d", filename, errno);
        return SW_ERROR_IO;
    }

    if (fwrite(config_data, 1, data_size, f) != data_size) {
        LOG_ERROR("Failed to write config file: %s", filename);
        fclose(f);
        return SW_ERROR_IO;
    }

    fclose(f);
    return SW_OK;
}

/**
 * @brief Создание резервной копии текущей стартовой конфигурации
 * 
 * @return SW_OK если успешно, иначе код ошибки
 */
static sw_error_t create_backup_config(void) {
    char backup_filename[MAX_PATH_LENGTH];
    time_t current_time;
    struct tm *time_info;
    FILE *src, *dst;
    char buffer[4096];
    size_t bytes_read;
    struct stat st;

    // Проверяем существование стартовой конфигурации
    if (stat(STARTUP_CONFIG_FILE, &st) != 0) {
        if (errno == ENOENT) {
            // Файл не существует, резервное копирование не требуется
            return SW_OK;
        } else {
            LOG_ERROR("Failed to check startup config file, errno: %d", errno);
            return SW_ERROR_IO;
        }
    }

    // Создаем имя файла резервной копии с текущей датой и временем
    current_time = time(NULL);
    time_info = localtime(&current_time);
    snprintf(backup_filename, MAX_PATH_LENGTH, 
             "%s/startup-config-%04d%02d%02d-%02d%02d%02d.json",
             BACKUP_CONFIG_DIR,
             time_info->tm_year + 1900, time_info->tm_mon + 1, time_info->tm_mday,
             time_info->tm_hour, time_info->tm_min, time_info->tm_sec);

    // Открываем исходный файл
    src = fopen(STARTUP_CONFIG_FILE, "rb");
    if (src == NULL) {
        LOG_ERROR("Failed to open startup config for backup, errno: %d", errno);
        return SW_ERROR_IO;
    }

    // Открываем файл резервной копии
    dst = fopen(backup_filename, "wb");
    if (dst == NULL) {
        LOG_ERROR("Failed to create backup config file: %s, errno: %d", 
                  backup_filename, errno);
        fclose(src);
        return SW_ERROR_IO;
    }

    // Копируем данные
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (fwrite(buffer, 1, bytes_read, dst) != bytes_read) {
            LOG_ERROR("Failed to write to backup file: %s", backup_filename);
            fclose(src);
            fclose(dst);
            return SW_ERROR_IO;
        }
    }

    fclose(src);
    fclose(dst);

    LOG_INFO("Created backup of startup config: %s", backup_filename);
    return SW_OK;
}

/**
 * @brief Парсинг JSON-конфигурации и применение настроек
 * 
 * @param config_data Данные конфигурации в формате JSON
 * @param data_size Размер данных конфигурации
 * @return SW_OK если успешно, иначе код ошибки
 */
static sw_error_t parse_config_json(const char *config_data, size_t data_size) {
    // Здесь должен быть код для парсинга JSON и применения конфигурации
    // В реальном проекте здесь будет использована библиотека для работы с JSON
    // и вызовы API различных модулей для настройки коммутатора
    
    // Примечание: Эта функция является заглушкой и должна быть доработана
    // в соответствии с требованиями к формату конфигурации

    LOG_INFO("Parsed and applied configuration (placeholder)");
    return SW_OK;
}

/**
 * @brief Генерация JSON-конфигурации на основе текущих настроек
 * 
 * @param config_data Указатель на буфер для сохранения конфигурации
 * @param data_size Размер сгенерированной конфигурации
 * @return SW_OK если успешно, иначе код ошибки
 */
static sw_error_t generate_config_json(char **config_data, size_t *data_size) {
    // Здесь должен быть код для генерации JSON из текущей конфигурации
    // В реальном проекте здесь будут вызовы API различных модулей
    // для получения их текущих настроек и формирования JSON

    // Примечание: Эта функция является заглушкой и должна быть доработана
    // в соответствии с требованиями к формату конфигурации
    
    const char *default_config = "{\n"
                                "  \"switch\": {\n"
                                "    \"name\": \"SwitchSimulator\",\n"
                                "    \"ports\": {\n"
                                "      \"enabled\": [1, 2, 3, 4]\n"
                                "    },\n"
                                "    \"vlans\": {\n"
                                "      \"1\": {\n"
                                "        \"name\": \"default\",\n"
                                "        \"ports\": [1, 2, 3, 4]\n"
                                "      }\n"
                                "    }\n"
                                "  }\n"
                                "}";

    // Используем предопределенную конфигурацию по умолчанию
    if (*config_data != NULL) {
        // Предполагаем, что буфер уже выделен и достаточного размера
        strncpy(*config_data, default_config, MAX_CONFIG_SIZE - 1);
        (*config_data)[MAX_CONFIG_SIZE - 1] = '\0';
        *data_size = strlen(*config_data);
    } else {
        size_t len = strlen(default_config);
        *config_data = (char *)malloc(len + 1);
        if (*config_data == NULL) {
            return SW_ERROR_NO_MEMORY;
        }
        strcpy(*config_data, default_config);
        *data_size = len;
    }

    LOG_INFO("Generated default configuration (placeholder)");
    return SW_OK;
}

/**
 * @brief Установка параметра конфигурации
 * 
 * @param key Путь к параметру конфигурации (в формате "раздел.параметр")
 * @param value Значение параметра
 * @return SW_OK если успешно, иначе код ошибки
 */
sw_error_t config_manager_set_param(const char *key, const char *value) {
    if (!g_config_manager.initialized) {
        LOG_ERROR("Config manager not initialized");
        return SW_ERROR_NOT_INITIALIZED;
    }

    if (key == NULL || value == NULL) {
        return SW_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&g_config_manager.config_mutex);

    // Здесь должен быть код для установки параметра в конфигурации
    // В реальном проекте здесь будет поиск нужного параметра в JSON
    // и его модификация

    // Примечание: Эта функция является заглушкой и должна быть доработана
    // в соответствии с требованиями к формату конфигурации

    LOG_INFO("Set config parameter: %s = %s (placeholder)", key, value);
    g_config_manager.config_modified = true;

    pthread_mutex_unlock(&g_config_manager.config_mutex);
    return SW_OK;
}

/**
 * @brief Получение параметра конфигурации
 * 
 * @param key Путь к параметру конфигурации (в формате "раздел.параметр")
 * @param value Буфер для значения параметра
 * @param value_size Размер буфера
 * @return SW_OK если успешно, иначе код ошибки
 */
sw_error_t config_manager_get_param(const char *key, char *value, size_t value_size) {
    if (!g_config_manager.initialized) {
        LOG_ERROR("Config manager not initialized");
        return SW_ERROR_NOT_INITIALIZED;
    }

    if (key == NULL || value == NULL || value_size == 0) {
        return SW_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&g_config_manager.config_mutex);

    // Здесь должен быть код для получения параметра из конфигурации
    // В реальном проекте здесь будет поиск нужного параметра в JSON

    // Примечание: Эта функция является заглушкой и должна быть доработана
    // в соответствии с требованиями к формату конфигурации

    // Для демонстрации возвращаем фиктивное значение
    strncpy(value, "placeholder_value", value_size - 1);
    value[value_size - 1] = '\0';

    LOG_INFO("Get config parameter: %s (placeholder)", key);

    pthread_mutex_unlock(&g_config_manager.config_mutex);
    return SW_OK;
}

/**
 * @brief Восстановление конфигурации по умолчанию
 * 
 * @return SW_OK если успешно, иначе код ошибки
 */
sw_error_t config_manager_reset_to_defaults(void) {
    sw_error_t err;

    if (!g_config_manager.initialized) {
        LOG_ERROR("Config manager not initialized");
        return SW_ERROR_NOT_INITIALIZED;
    }

    pthread_mutex_lock(&g_config_manager.config_mutex);

    // Создаем резервную копию текущей конфигурации
    err = create_backup_config();
    if (err != SW_OK) {
        LOG_WARN("Failed to create backup config, error: %d", err);
        // Продолжаем выполнение даже при ошибке резервного копирования
    }

    // Генерируем конфигурацию по умолчанию
    err = generate_config_json(&g_config_manager.config_buffer, &g_config_manager.config_buffer_size);
    if (err != SW_OK) {
        LOG_ERROR("Failed to generate default config, error: %d", err);
        pthread_mutex_unlock(&g_config_manager.config_mutex);
        return err;
    }

    // Применяем конфигурацию
    err = parse_config_json(g_config_manager.config_buffer, g_config_manager.config_buffer_size);
    if (err != SW_OK) {
        LOG_ERROR("Failed to apply default config, error: %d", err);
        pthread_mutex_unlock(&g_config_manager.config_mutex);
        return err;
    }

    g_config_manager.config_modified = true;
    g_config_manager.last_save_time = time(NULL);

    LOG_INFO("Reset to default configuration");
    pthread_mutex_unlock(&g_config_manager.config_mutex);
    return SW_OK;
}

/**
 * @brief Проверка, была ли изменена конфигурация
 * 
 * @return true если конфигурация была изменена, иначе false
 */
bool config_manager_is_modified(void) {
    if (!g_config_manager.initialized) {
        return false;
    }

    bool modified;
    pthread_mutex_lock(&g_config_manager.config_mutex);
    modified = g_config_manager.config_modified;
    pthread_mutex_unlock(&g_config_manager.config_mutex);

    return modified;
}
