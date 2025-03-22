#!/bin/bash

# Скрипт для сборки проекта switch-simulator

# Определение директории проекта
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"

# Функция для вывода сообщений
log() {
    echo -e "\033[1;34m[BUILD]\033[0m $1"
}

error() {
    echo -e "\033[1;31m[ERROR]\033[0m $1" >&2
    exit 1
}

# Функция для вывода справки
show_help() {
    echo "Использование: $0 [ОПЦИИ]"
    echo "Опции:"
    echo "  -h, --help           Вывод этой справки"
    echo "  -c, --clean          Очистка перед сборкой"
    echo "  -t, --test           Сборка с тестами"
    echo "  -d, --debug          Сборка в режиме отладки"
    echo "  -r, --release        Сборка в режиме релиза (по умолчанию)"
    echo "  --cmake              Использовать CMake для сборки"
    echo "  --make               Использовать Make для сборки (по умолчанию)"
}

# Параметры по умолчанию
BUILD_TYPE="release"
BUILD_SYSTEM="make"
CLEAN=0
BUILD_TESTS=0

# Парсинг аргументов командной строки
while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            show_help
            exit 0
            ;;
        -c|--clean)
            CLEAN=1
            shift
            ;;
        -t|--test)
            BUILD_TESTS=1
            shift
            ;;
        -d|--debug)
            BUILD_TYPE="debug"
            shift
            ;;
        -r|--release)
            BUILD_TYPE="release"
            shift
            ;;
        --cmake)
            BUILD_SYSTEM="cmake"
            shift
            ;;
        --make)
            BUILD_SYSTEM="make"
            shift
            ;;
        *)
            error "Неизвестный параметр: $1"
            ;;
    esac
done

# Проверка наличия необходимых инструментов
check_dependencies() {
    log "Проверка зависимостей..."
    
    if [[ "${BUILD_SYSTEM}" == "cmake" ]]; then
        if ! command -v cmake &> /dev/null; then
            error "CMake не найден. Установите CMake или используйте --make."
        fi
    fi
    
    if ! command -v gcc &> /dev/null; then
        error "GCC не найден. Установите GCC для сборки проекта."
    fi
}

# Сборка с использованием Make
build_with_make() {
    log "Сборка с использованием Make..."
    
    cd "${PROJECT_DIR}" || error "Невозможно перейти в директорию проекта"
    
    if [[ ${CLEAN} -eq 1 ]]; then
        log "Очистка сборки..."
        make clean || error "Ошибка при очистке"
    fi
    
    if [[ "${BUILD_TYPE}" == "debug" ]]; then
        log "Сборка в режиме отладки..."
        make debug || error "Ошибка при сборке в режиме отладки"
    else
        log "Сборка в режиме релиза..."
        make release || error "Ошибка при сборке в режиме релиза"
    fi
    
    if [[ ${BUILD_TESTS} -eq 1 ]]; then
        log "Сборка и запуск тестов..."
        make test || error "Ошибка при сборке или запуске тестов"
    fi
}

# Сборка с использованием CMake
build_with_cmake() {
    log "Сборка с использованием CMake..."
    
    mkdir -p "${BUILD_DIR}" || error "Невозможно создать директорию для сборки"
    cd "${BUILD_DIR}" || error "Невозможно перейти в директорию для сборки"
    
    if [[ ${CLEAN} -eq 1 ]]; then
        log "Очистка сборки..."
        rm -rf *
    fi
    
    log "Конфигурирование проекта..."
    
    CMAKE_ARGS=()
    if [[ "${BUILD_TYPE}" == "debug" ]]; then
        CMAKE_ARGS+=("-DCMAKE_BUILD_TYPE=Debug")
    else
        CMAKE_ARGS+=("-DCMAKE_BUILD_TYPE=Release")
    fi
    
    if [[ ${BUILD_TESTS} -eq 1 ]]; then
        CMAKE_ARGS+=("-DBUILD_TESTS=ON")
    else
        CMAKE_ARGS+=("-DBUILD_TESTS=OFF")
    fi
    
    cmake "${CMAKE_ARGS[@]}" .. || error "Ошибка при конфигурировании CMake"
    
    log "Сборка проекта..."
    cmake --build . || error "Ошибка при сборке проекта"
    
    if [[ ${BUILD_TESTS} -eq 1 ]]; then
        log "Запуск тестов..."
        ctest --output-on-failure || error "Ошибка при запуске тестов"
    fi
}

# Основная логика
main() {
    check_dependencies
    
    if [[ "${BUILD_SYSTEM}" == "cmake" ]]; then
        build_with_cmake
    else
        build_with_make
    fi
    
    log "Сборка успешно завершена!"
    
    if [[ "${BUILD_TYPE}" == "debug" ]]; then
        log "Исполняемый файл: ${BUILD_DIR}/switch_simulator"
    else
        log "Исполняемый файл: ${BUILD_DIR}/switch_simulator"
    fi
}

# Запуск основной логики
main
