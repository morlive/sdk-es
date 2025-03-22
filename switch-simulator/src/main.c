/**
 * @file main.c
 * @brief Основной файл проекта switch-simulator
 * 
 * Этот файл содержит основную точку входа программы и инициализацию
 * всех компонентов симулятора сетевого коммутатора.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>

#include "common/logging.h"
#include "common/types.h"
#include "hal/hw_resources.h"
#include "l2/mac_table.h"
#include "l2/vlan.h"
#include "l3/routing_table.h"
#include "management/cli.h"
#include "management/stats.h"
#include "sai/sai_adapter.h"
#include "bsp/bsp.h"

/* Глобальные переменные */
static volatile bool g_running = true;

/**
 * Обработчик сигналов для корректного завершения работы
 */
static void signal_handler(int signum) {
    LOG_INFO("Получен сигнал %d, завершение работы...", signum);
    g_running = false;
}

/**
 * Инициализация обработчиков сигналов
 */
static error_code_t setup_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        LOG_ERROR("Не удалось установить обработчик для SIGINT");
        return ERROR_GENERAL;
    }
    
    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        LOG_ERROR("Не удалось установить обработчик для SIGTERM");
        return ERROR_GENERAL;
    }
    
    return SUCCESS;
}

/**
 * Инициализация всех компонентов симулятора
 */
static error_code_t initialize_simulator(void) {
    error_code_t err;
    
    // Инициализация платформы (BSP)
    LOG_INFO("Инициализация платформы...");
    err = bsp_init();
    if (err != SUCCESS) {
        LOG_ERROR("Ошибка инициализации платформы: %d", err);
        return err;
    }
    
    // Инициализация аппаратных ресурсов (HAL)
    LOG_INFO("Инициализация аппаратных ресурсов...");
    err = hw_resources_init();
    if (err != SUCCESS) {
        LOG_ERROR("Ошибка инициализации аппаратных ресурсов: %d", err);
        return err;
    }
    
    // Инициализация L2 компонентов
    LOG_INFO("Инициализация L2 компонентов...");
    err = mac_table_init();
    if (err != SUCCESS) {
        LOG_ERROR("Ошибка инициализации таблицы MAC-адресов: %d", err);
        return err;
    }
    
    err = vlan_init();
    if (err != SUCCESS) {
        LOG_ERROR("Ошибка инициализации VLAN: %d", err);
        return err;
    }
    
    // Инициализация L3 компонентов
    LOG_INFO("Инициализация L3 компонентов...");
    err = routing_table_init();
    if (err != SUCCESS) {
        LOG_ERROR("Ошибка инициализации таблицы маршрутизации: %d", err);
        return err;
    }
    
    // Инициализация SAI
    LOG_INFO("Инициализация SAI...");
    err = sai_adapter_init();
    if (err != SUCCESS) {
        LOG_ERROR("Ошибка инициализации SAI адаптера: %d", err);
        return err;
    }
    
    // Инициализация компонентов управления
    LOG_INFO("Инициализация компонентов управления...");
    err = stats_init();
    if (err != SUCCESS) {
        LOG_ERROR("Ошибка инициализации системы статистики: %d", err);
        return err;
    }
    
    err = cli_init();
    if (err != SUCCESS) {
        LOG_ERROR("Ошибка инициализации CLI: %d", err);
        return err;
    }
    
    LOG_INFO("Инициализация завершена успешно");
    return SUCCESS;
}

/**
 * Деинициализация всех компонентов симулятора
 */
static void deinitialize_simulator(void) {
    LOG_INFO("Деинициализация системы...");
    
    // Деинициализация в обратном порядке
    cli_deinit();
    stats_deinit();
    sai_adapter_deinit();
    routing_table_deinit();
    vlan_deinit();
    mac_table_deinit();
    hw_resources_deinit();
    bsp_deinit();
    
    LOG_INFO("Деинициализация завершена");
}

/**
 * Основной цикл симулятора
 */
static void simulator_main_loop(void) {
    LOG_INFO("Запуск основного цикла симулятора");
    
    while (g_running) {
        // Обработка пакетов и других задач
        // ...
        
        // Чтобы не загружать процессор на 100%
        usleep(1000);
    }
    
    LOG_INFO("Основной цикл симулятора завершен");
}

/**
 * Основная функция программы
 */
int main(int argc, char *argv[]) {
    error_code_t err;
    
    // Инициализация системы логирования
    log_init();
    LOG_INFO("Switch Simulator запущен");
    
    // Проверка и обработка аргументов командной строки
    // Здесь можно добавить парсинг аргументов
    
    // Настройка обработчиков сигналов
    err = setup_signal_handlers();
    if (err != SUCCESS) {
        LOG_ERROR("Ошибка при настройке обработчиков сигналов");
        log_deinit();
        return EXIT_FAILURE;
    }
    
    // Инициализация всех компонентов симулятора
    err = initialize_simulator();
    if (err != SUCCESS) {
        LOG_ERROR("Ошибка при инициализации симулятора");
        log_deinit();
        return EXIT_FAILURE;
    }
    
    // Запуск основного цикла
    simulator_main_loop();
    
    // Деинициализация
    deinitialize_simulator();
    
    LOG_INFO("Switch Simulator завершен");
    log_deinit();
    
    return EXIT_SUCCESS;
}
