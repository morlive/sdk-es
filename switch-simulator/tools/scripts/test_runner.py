#!/usr/bin/env python3
"""
Test runner script for switch-simulator project.
Автоматизирует запуск и отчётность по тестам.
"""

import os
import sys
import subprocess
import argparse
import glob
import time
import xml.etree.ElementTree as ET
from datetime import datetime

# Определение путей
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
TEST_DIR = os.path.join(PROJECT_ROOT, "tests")
BUILD_DIR = os.path.join(PROJECT_ROOT, "build")
UNIT_TEST_DIR = os.path.join(TEST_DIR, "unit")
INTEGRATION_TEST_DIR = os.path.join(TEST_DIR, "integration")
SYSTEM_TEST_DIR = os.path.join(TEST_DIR, "system")
REPORT_DIR = os.path.join(BUILD_DIR, "test-reports")

# Цвета для вывода в терминал
class Colors:
    HEADER = '\033[95m'
    BLUE = '\033[94m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

def print_color(message, color):
    """Вывод цветного сообщения в терминал"""
    print(f"{color}{message}{Colors.ENDC}")

def ensure_dir_exists(directory):
    """Проверяет, существует ли директория, и если нет, создает её"""
    if not os.path.exists(directory):
        os.makedirs(directory)
        print_color(f"Создана директория: {directory}", Colors.BLUE)

def parse_args():
    """Парсинг аргументов командной строки"""
    parser = argparse.ArgumentParser(description="Запуск тестов для проекта switch-simulator")
    parser.add_argument("--unit", action="store_true", help="Запуск только юнит-тестов")
    parser.add_argument("--integration", action="store_true", help="Запуск только интеграционных тестов")
    parser.add_argument("--system", action="store_true", help="Запуск только системных тестов")
    parser.add_argument("--all", action="store_true", help="Запуск всех тестов")
    parser.add_argument("--verbose", "-v", action="store_true", help="Подробный вывод")
    parser.add_argument("--xml", action="store_true", help="Создать XML отчёт")
    parser.add_argument("--valgrind", action="store_true", help="Запустить тесты с Valgrind")
    
    args = parser.parse_args()
    
    # Если не указаны конкретные тесты, по умолчанию запускаем все
    if not (args.unit or args.integration or args.system or args.all):
        args.all = True
        
    return args

def find_test_executables(test_dir):
    """Находит исполняемые файлы тестов в указанной директории"""
    # Предполагается, что исполняемые файлы тестов находятся в build/tests/...
    test_build_dir = os.path.join(BUILD_DIR, os.path.relpath(test_dir, PROJECT_ROOT))
    
    if not os.path.exists(test_build_dir):
        print_color(f"Директория {test_build_dir} не существует. Тесты не скомпилированы?", Colors.YELLOW)
        return []
    
    # Ищем все исполняемые файлы (без расширения в Linux/macOS)
    test_files = []
    for file in os.listdir(test_build_dir):
        file_path = os.path.join(test_build_dir, file)
        if os.path.isfile(file_path) and os.access(file_path, os.X_OK):
            if not file.endswith(".d") and not file.endswith(".o"):
                test_files.append(file_path)
    
    return test_files

def find_python_tests(test_dir):
    """Находит файлы Python-тестов в указанной директории"""
    test_files = glob.glob(os.path.join(test_dir, "test_*.py"))
    return test_files

def run_c_test(test_path, use_valgrind=False, verbose=False):
    """Запускает тест на C и возвращает результат"""
    start_time = time.time()
    
    cmd = []
    if use_valgrind:
        cmd.extend(["valgrind", "--leak-check=full", "--error-exitcode=1"])
    cmd.append(test_path)
    
    try:
        result = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        elapsed_time = time.time() - start_time
        
        test_name = os.path.basename(test_path)
        success = result.returncode == 0
        
        if success:
            print_color(f"✓ {test_name} прошел ({elapsed_time:.2f}s)", Colors.GREEN)
        else:
            print_color(f"✗ {test_name} не прошел ({elapsed_time:.2f}s)", Colors.RED)
        
        if verbose or not success:
            if result.stdout:
                print(result.stdout)
            if result.stderr:
                print_color(result.stderr, Colors.YELLOW)
        
        return {
            "name": test_name,
            "success": success,
            "time": elapsed_time,
            "stdout": result.stdout,
            "stderr": result.stderr
        }
        
    except Exception as e:
        print_color(f"Ошибка при запуске теста {test_path}: {e}", Colors.RED)
        return {
            "name": os.path.basename(test_path),
            "success": False,
            "time": time.time() - start_time,
            "stdout": "",
            "stderr": str(e)
        }

def run_python_test(test_path, verbose=False):
    """Запускает тест на Python и возвращает результат"""
    start_time = time.time()
    
    try:
        result = subprocess.run(
            [sys.executable, "-m", "unittest", test_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        elapsed_time = time.time() - start_time
        
        test_name = os.path.basename(test_path)
        success = result.returncode == 0
        
        if success:
            print_color(f"✓ {test_name} прошел ({elapsed_time:.2f}s)", Colors.GREEN)
        else:
            print_color(f"✗ {test_name} не прошел ({elapsed_time:.2f}s)", Colors.RED)
        
        if verbose or not success:
            if result.stdout:
                print(result.stdout)
            if result.stderr:
                print_color(result.stderr, Colors.YELLOW)
        
        return {
            "name": test_name,
            "success": success,
            "time": elapsed_time,
            "stdout": result.stdout,
            "stderr": result.stderr
        }
        
    except Exception as e:
        print_color(f"Ошибка при запуске теста {test_path}: {e}", Colors.RED)
        return {
            "name": os.path.basename(test_path),
            "success": False,
            "time": time.time() - start_time,
            "stdout": "",
            "stderr": str(e)
        }

def create_xml_report(results, report_dir):
    """Создает XML отчет о результатах тестирования"""
    ensure_dir_exists(report_dir)
    
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    report_file = os.path.join(report_dir, f"test-report-{timestamp}.xml")
    
    root = ET.Element("testsuites")
    
    for test_type, test_results in results.items():
        testsuite = ET.SubElement(root, "testsuite")
        testsuite.set("name", test_type)
        testsuite.set("tests", str(len(test_results)))
        
        # Подсчет количества успешных/неуспешных тестов
        failures = sum(1 for r in test_results if not r["success"])
        testsuite.set("failures", str(failures))
        
        for test_result in test_results:
            testcase = ET.SubElement(testsuite, "testcase")
            testcase.set("name", test_result["name"])
            testcase.set("time", str(test_result["time"]))
            
            if not test_result["success"]:
                failure = ET.SubElement(testcase, "failure")
                failure.set("message", "Test failed")
                failure.text = test_result["stderr"]
            
            system_out = ET.SubElement(testcase, "system-out")
            system_out.text = test_result["stdout"]
            
            system_err = ET.SubElement(testcase, "system-err")
            system_err.text = test_result["stderr"]
    
    tree = ET.ElementTree(root)
    tree.write(report_file, encoding="utf-8", xml_declaration=True)
    
    print_color(f"XML отчет сохранен в {report_file}", Colors.BLUE)

def print_summary(results):
    """Выводит итоговую статистику по тестам"""
    total_tests = sum(len(test_results) for test_results in results.values())
    total_passed = sum(sum(1 for r in test_results if r["success"]) for test_results in results.values())
    total_failed = total_tests - total_passed
    
    print_color("\n=== Итоги тестирования ===", Colors.BOLD + Colors.HEADER)
    for test_type, test_results in results.items():
        passed = sum(1 for r in test_results if r["success"])
        failed = len(test_results) - passed
        if len(test_results) > 0:
            print_color(f"{test_type}: {passed}/{len(test_results)} успешно ({passed/len(test_results)*100:.1f}%)", 
                      Colors.GREEN if failed == 0 else Colors.YELLOW)
    
    print_color(f"\nВсего тестов: {total_tests}", Colors.BOLD)
    print_color(f"Успешно: {total_passed}", Colors.GREEN)
    if total_failed > 0:
        print_color(
