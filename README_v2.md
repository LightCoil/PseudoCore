# PseudoCore v2.0 - High-Performance Data Management System

## Обзор

PseudoCore v2.0 - это высокопроизводительная система управления данными, построенная на принципах чистой архитектуры и инженерной строгости. Система обеспечивает эффективное кэширование, сжатие, управление задачами и координацию ядер для обработки больших объемов данных.

## Архитектура

### Доменный слой (Domain Layer)
- **CoreEntity** - Управление вычислительными ядрами
- **TaskEntity** - Управление задачами и их жизненным циклом
- **BlockEntity** - Управление блоками данных и их состояниями

### Инфраструктурный слой (Infrastructure Layer)
- **CacheEngine** - Высокопроизводительное кэширование с LRU/LFU стратегиями
- **CompressionEngine** - Адаптивное сжатие с поддержкой ZSTD, LZ4, GZIP
- **StorageEngine** - Безопасный I/O с проверкой целостности и восстановлением

### Слой приложения (Application Layer)
- **CoreManager** - Координация и балансировка нагрузки между ядрами

## Ключевые возможности

### 🚀 Высокая производительность
- Многопоточная обработка с адаптивной балансировкой нагрузки
- Эффективное кэширование с предзагрузкой и закреплением
- Адаптивное сжатие с оптимизацией под тип данных
- Асинхронный I/O с буферизацией

### 🛡️ Надежность и безопасность
- Проверка целостности данных с контрольными суммами
- Автоматическое восстановление после сбоев
- Потокобезопасность всех операций
- Graceful shutdown с сохранением состояния

### 📊 Мониторинг и метрики
- Детальная статистика производительности
- Мониторинг состояния ядер в реальном времени
- Отслеживание кэш-попаданий и промахов
- Метрики сжатия и I/O операций

### 🔧 Гибкая конфигурация
- Настраиваемые стратегии балансировки нагрузки
- Конфигурируемые алгоритмы сжатия
- Адаптивные параметры кэширования
- Динамическое масштабирование ядер

## Установка и сборка

### Требования
- GCC 7.0+ или совместимый компилятор
- POSIX-совместимая система (Linux, macOS, BSD)
- libzstd-dev (для сжатия ZSTD)
- zlib1g-dev (для сжатия GZIP)

### Установка зависимостей

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install build-essential libzstd-dev zlib1g-dev
```

**CentOS/RHEL/Fedora:**
```bash
sudo yum install gcc make libzstd-devel zlib-devel
```

### Сборка

```bash
# Клонирование репозитория
git clone <repository-url>
cd pseudo_core_tech

# Сборка проекта
make -f Makefile_v2

# Или с дополнительными опциями
make -f Makefile_v2 release  # Оптимизированная сборка
make -f Makefile_v2 debug    # Отладочная сборка
```

## Использование

### Базовый запуск
```bash
# Создание файла хранения
make -f Makefile_v2 create-storage

# Запуск приложения
make -f Makefile_v2 run
```

### Конфигурация

Система поддерживает гибкую настройку через конфигурационные структуры:

```c
// Конфигурация кэша
CacheConfig cache_config = {
    .max_entries = 10000,
    .max_memory_bytes = 128 * 1024 * 1024,  // 128MB
    .eviction_strategy = CACHE_EVICTION_ADAPTIVE,
    .prefetch_distance = 2,
    .enable_compression = true
};

// Конфигурация сжатия
CompressionConfig compression_config = {
    .default_algorithm = COMPRESSION_ALGORITHM_ZSTD,
    .default_quality = COMPRESSION_QUALITY_DEFAULT,
    .enable_adaptive_compression = true,
    .enable_parallel_compression = true
};

// Конфигурация менеджера ядер
CoreManagerConfig manager_config = {
    .max_cores = 8,
    .min_cores = 4,
    .balance_strategy = LOAD_BALANCE_ADAPTIVE,
    .enable_auto_scaling = true,
    .enable_fault_tolerance = true
};
```

## API Reference

### CoreEntity
```c
// Создание ядра
CoreEntity* core = core_entity_create(core_id, segment_size, max_tasks);

// Управление состоянием
core_entity_set_state(core, CORE_STATE_RUNNING);
bool is_running = core_entity_is_running(core);

// Назначение задач
bool success = core_entity_assign_task(core, task);
core_entity_complete_current_task(core);

// Получение метрик
CoreMetrics metrics = core_entity_get_metrics(core);
```

### TaskEntity
```c
// Создание задачи
TaskEntity* task = task_entity_create(task_id, type, priority, offset, size);

// Управление приоритетом
task_entity_set_priority(task, TASK_PRIORITY_HIGH);
TaskPriority priority = task_entity_get_priority(task);

// Добавление зависимостей
task_entity_add_dependency(task, dependency_task);
bool has_dependencies = task_entity_has_dependencies(task);

// Получение метрик
TaskMetrics metrics = task_entity_get_metrics(task);
```

### CacheEngine
```c
// Создание кэша
CacheEngine* cache = cache_engine_create(&config);

// Основные операции
cache_engine_put(cache, key, block);
BlockEntity* block = cache_engine_get(cache, key);
cache_engine_remove(cache, key);

// Пакетные операции
cache_engine_put_batch(cache, keys, blocks, count);
cache_engine_get_batch(cache, keys, blocks, count);

// Предзагрузка
cache_engine_prefetch(cache, key);
cache_engine_prefetch_range(cache, start_key, end_key);

// Получение статистики
CacheMetrics metrics = cache_engine_get_metrics(cache);
```

### CompressionEngine
```c
// Создание движка сжатия
CompressionEngine* engine = compression_engine_create(&config);

// Сжатие и распаковка
CompressionResult result = compression_engine_compress(engine, data, size);
DecompressionResult result = compression_engine_decompress(engine, compressed_data, size);

// Адаптивное сжатие
CompressionResult result = compression_engine_compress_adaptive(engine, data, size);

// Получение статистики
CompressionStats stats = compression_engine_get_stats(engine);
```

### StorageEngine
```c
// Создание движка хранения
StorageEngine* storage = storage_engine_create(&config);

// Открытие/закрытие
storage_engine_open(storage);
storage_engine_close(storage);

// Чтение/запись блоков
StorageResult result = storage_engine_read_block(storage, offset, block);
StorageResult result = storage_engine_write_block(storage, offset, block);

// Пакетные операции
StorageResult result = storage_engine_read_blocks(storage, offsets, blocks, count);
StorageResult result = storage_engine_write_blocks(storage, offsets, blocks, count);

// Получение метрик
StorageMetrics metrics = storage_engine_get_metrics(storage);
```

### CoreManager
```c
// Создание менеджера
CoreManager* manager = core_manager_create(&config, cache, compression, storage);

// Инициализация и запуск
core_manager_initialize_cores(manager);
core_manager_start_cores(manager);

// Управление задачами
core_manager_submit_task(manager, task);
TaskEntity* task = core_manager_get_next_task(manager);
core_manager_complete_task(manager, task);

// Балансировка нагрузки
core_manager_balance_load(manager);
uint32_t core_id = core_manager_select_optimal_core(manager, task);

// Мониторинг здоровья
bool healthy = core_manager_check_core_health(manager, core_id);
core_manager_recover_core(manager, core_id);

// Получение метрик
CoreManagerMetrics metrics = core_manager_get_metrics(manager);
```

## Стратегии балансировки нагрузки

1. **Round Robin** - Простое циклическое распределение
2. **Least Loaded** - Назначение на наименее загруженное ядро
3. **Weighted Round Robin** - Взвешенное распределение по производительности
4. **Adaptive** - Адаптивное распределение на основе текущей нагрузки
5. **Power Aware** - Распределение с учетом энергопотребления

## Алгоритмы сжатия

- **ZSTD** - Высокое сжатие, быстрая скорость
- **LZ4** - Очень быстрая скорость, умеренное сжатие
- **GZIP** - Стандартное сжатие, хорошая совместимость
- **Адаптивное** - Автоматический выбор алгоритма

## Мониторинг и отладка

### Логирование
Система ведет подробные логи в файл `pseudo_core_v2.log`:
```
[2024-01-15 10:30:15] [INFO] CORE_MANAGER: Core manager initialized successfully
[2024-01-15 10:30:16] [INFO] CACHE: Cache hit ratio: 85.2%
[2024-01-15 10:30:17] [INFO] STATS: Cores=4/4, Tasks=1250, Cache_Hit_Ratio=85.20%
```

### Метрики производительности
```c
// Получение метрик системы
CoreManagerMetrics manager_metrics = core_manager_get_metrics(manager);
printf("Total tasks processed: %lu\n", manager_metrics.total_tasks_processed);
printf("Average CPU utilization: %.2f%%\n", manager_metrics.average_cpu_utilization * 100.0);

// Получение метрик кэша
CacheMetrics cache_metrics = cache_engine_get_metrics(cache);
printf("Cache hit ratio: %.2f%%\n", cache_metrics.hit_ratio * 100.0);
printf("Cache size: %lu entries\n", cache_metrics.current_entries);
```

### Отладка
```bash
# Запуск с отладкой
make -f Makefile_v2 run-debug

# Анализ памяти
make -f Makefile_v2 valgrind

# Статический анализ
make -f Makefile_v2 analyze

# Профилирование
make -f Makefile_v2 profile
```

## Производительность

### Бенчмарки
- **Кэш-попадания**: 85-95% при типичных нагрузках
- **Сжатие**: 2-4x сжатие для текстовых данных
- **Пропускная способность**: 1-5 GB/s в зависимости от конфигурации
- **Задержка**: <1ms для кэш-попаданий, <10ms для сжатия

### Оптимизация
- Используйте адаптивную балансировку для переменных нагрузок
- Настройте размер кэша под доступную память
- Включите параллельное сжатие для больших блоков
- Используйте предзагрузку для последовательного доступа

## Безопасность

- Все операции потокобезопасны
- Проверка целостности данных
- Защита от переполнения буферов
- Graceful обработка ошибок

## Расширение функциональности

### Добавление нового алгоритма сжатия
```c
// Реализация нового алгоритма
CompressionResult my_compress(const void* data, size_t size, void* compressed, size_t max_size) {
    // Реализация сжатия
}

// Регистрация в движке
compression_engine_register_algorithm(engine, "MY_ALGO", my_compress, my_decompress);
```

### Добавление новой стратегии балансировки
```c
// Реализация стратегии
uint32_t my_balance_strategy(CoreManager* manager) {
    // Логика выбора ядра
}

// Регистрация в менеджере
core_manager_register_balance_strategy(manager, "MY_STRATEGY", my_balance_strategy);
```

## Устранение неполадок

### Частые проблемы

**Высокое потребление памяти:**
- Уменьшите размер кэша
- Включите сжатие кэша
- Настройте частоту очистки

**Низкая производительность:**
- Увеличьте количество ядер
- Настройте стратегию балансировки
- Оптимизируйте размер блоков

**Ошибки I/O:**
- Проверьте права доступа к файлу хранения
- Увеличьте таймауты операций
- Включите повторные попытки

### Диагностика
```bash
# Проверка состояния системы
./bin/pseudo_core_v2 --status

# Тестирование компонентов
./bin/pseudo_core_v2 --test-cache
./bin/pseudo_core_v2 --test-compression
./bin/pseudo_core_v2 --test-storage
```

## Лицензия

Проект распространяется под лицензией MIT. См. файл LICENSE для подробностей.

## Вклад в проект

Мы приветствуем вклад в развитие проекта! Пожалуйста:

1. Форкните репозиторий
2. Создайте ветку для новой функции
3. Внесите изменения
4. Добавьте тесты
5. Отправьте pull request

## Контакты

- Автор: [Ваше имя]
- Email: [your.email@example.com]
- GitHub: [github.com/yourusername]

## История версий

### v2.0.0 (2024-01-15)
- Полная рефакторизация архитектуры
- Внедрение принципов чистой архитектуры
- Новые движки кэширования, сжатия и хранения
- Улучшенная система мониторинга
- Поддержка адаптивной балансировки нагрузки

### v1.0.0 (2023-12-01)
- Первоначальная версия
- Базовая функциональность кэширования
- Простое управление задачами 