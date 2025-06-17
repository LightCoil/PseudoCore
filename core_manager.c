#include "core_manager.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <math.h>

// Internal constants
#define MAX_CORES 64
#define MAX_TASKS_PER_CORE 1000
#define LOAD_BALANCE_THRESHOLD 0.2
#define HEALTH_CHECK_INTERVAL_SECONDS 10
#define TASK_TIMEOUT_SECONDS 30
#define RECOVERY_TIMEOUT_SECONDS 60

// Internal error codes
typedef enum {
    CORE_MANAGER_ERROR_NONE = 0,
    CORE_MANAGER_ERROR_INVALID_PARAM,
    CORE_MANAGER_ERROR_MEMORY_ALLOCATION,
    CORE_MANAGER_ERROR_CORE_NOT_FOUND,
    CORE_MANAGER_ERROR_TASK_QUEUE_FULL,
    CORE_MANAGER_ERROR_BALANCE_FAILED,
    CORE_MANAGER_ERROR_THREAD_CREATION,
    CORE_MANAGER_ERROR_INVALID_STATE
} CoreManagerError;

// Internal error tracking
static CoreManagerError last_error = CORE_MANAGER_ERROR_NONE;

// Error handling
static void set_error(CoreManagerError error) {
    last_error = error;
}

CoreManagerError core_manager_get_last_error(void) {
    return last_error;
}

const char* core_manager_error_to_string(CoreManagerError error) {
    switch (error) {
        case CORE_MANAGER_ERROR_NONE: return "No error";
        case CORE_MANAGER_ERROR_INVALID_PARAM: return "Invalid parameter";
        case CORE_MANAGER_ERROR_MEMORY_ALLOCATION: return "Memory allocation failed";
        case CORE_MANAGER_ERROR_CORE_NOT_FOUND: return "Core not found";
        case CORE_MANAGER_ERROR_TASK_QUEUE_FULL: return "Task queue full";
        case CORE_MANAGER_ERROR_BALANCE_FAILED: return "Load balance failed";
        case CORE_MANAGER_ERROR_THREAD_CREATION: return "Thread creation failed";
        case CORE_MANAGER_ERROR_INVALID_STATE: return "Invalid state";
        default: return "Unknown error";
    }
}

// Validation functions
static bool validate_manager(const CoreManager* manager) {
    return manager != NULL && manager->is_initialized;
}

static bool validate_config(const CoreManagerConfig* config) {
    return config != NULL && 
           config->max_cores > 0 && config->max_cores <= MAX_CORES &&
           config->min_cores > 0 && config->min_cores <= config->max_cores &&
           config->balance_interval_ms > 0 &&
           config->task_timeout_ms > 0 &&
           config->core_health_check_interval_ms > 0;
}

static bool validate_core_id(const CoreManager* manager, uint32_t core_id) {
    return validate_manager(manager) && core_id < manager->core_count;
}

static bool validate_task(const TaskEntity* task) {
    return task != NULL && task_entity_is_valid(task);
}

// Load balancing algorithms
static uint32_t select_core_round_robin(CoreManager* manager) {
    static uint32_t next_core = 0;
    uint32_t selected = next_core % manager->core_count;
    next_core = (next_core + 1) % manager->core_count;
    return selected;
}

static uint32_t select_core_least_loaded(CoreManager* manager) {
    uint32_t least_loaded_core = 0;
    uint32_t min_tasks = UINT32_MAX;
    
    for (uint32_t i = 0; i < manager->core_count; i++) {
        CoreEntity* core = manager->cores[i];
        if (core && core_entity_is_running(core)) {
            CoreMetrics metrics = core_entity_get_metrics(core);
            if (metrics.operations_completed < min_tasks) {
                min_tasks = metrics.operations_completed;
                least_loaded_core = i;
            }
        }
    }
    
    return least_loaded_core;
}

static uint32_t select_core_weighted_round_robin(CoreManager* manager) {
    // Weighted round-robin based on core performance
    static uint32_t current_core = 0;
    static uint32_t weight_counter = 0;
    
    uint32_t total_weight = 0;
    uint32_t weights[MAX_CORES] = {0};
    
    // Calculate weights based on core performance
    for (uint32_t i = 0; i < manager->core_count; i++) {
        CoreEntity* core = manager->cores[i];
        if (core && core_entity_is_running(core)) {
            CoreMetrics metrics = core_entity_get_metrics(core);
            weights[i] = (uint32_t)(metrics.cpu_utilization * 100);
            total_weight += weights[i];
        }
    }
    
    if (total_weight == 0) {
        return select_core_round_robin(manager);
    }
    
    // Find next core based on weights
    uint32_t target_weight = weight_counter % total_weight;
    uint32_t current_weight = 0;
    
    for (uint32_t i = 0; i < manager->core_count; i++) {
        current_weight += weights[i];
        if (current_weight > target_weight) {
            weight_counter++;
            return i;
        }
    }
    
    weight_counter++;
    return 0;
}

static uint32_t select_core_adaptive(CoreManager* manager) {
    // Adaptive selection based on current load and performance history
    double system_load = core_manager_get_system_load(manager);
    
    if (system_load > 0.8) {
        // High load: use least loaded
        return select_core_least_loaded(manager);
    } else if (system_load > 0.5) {
        // Medium load: use weighted round-robin
        return select_core_weighted_round_robin(manager);
    } else {
        // Low load: use simple round-robin
        return select_core_round_robin(manager);
    }
}

static uint32_t select_core_power_aware(CoreManager* manager) {
    // Power-aware selection (simplified)
    // In a real implementation, this would consider power consumption
    return select_core_least_loaded(manager);
}

// Core manager creation
CoreManager* core_manager_create(const CoreManagerConfig* config,
                                CacheEngine* cache_engine,
                                CompressionEngine* compression_engine,
                                StorageEngine* storage_engine) {
    if (!validate_config(config)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    if (!cache_engine || !compression_engine || !storage_engine) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    CoreManager* manager = malloc(sizeof(CoreManager));
    if (!manager) {
        set_error(CORE_MANAGER_ERROR_MEMORY_ALLOCATION);
        return NULL;
    }
    
    // Initialize configuration
    manager->config = *config;
    
    // Allocate core array
    manager->cores = calloc(config->max_cores, sizeof(CoreEntity*));
    if (!manager->cores) {
        set_error(CORE_MANAGER_ERROR_MEMORY_ALLOCATION);
        free(manager);
        return NULL;
    }
    
    manager->core_count = 0;
    manager->max_core_count = config->max_cores;
    
    // Initialize infrastructure engines
    manager->cache_engine = cache_engine;
    manager->compression_engine = compression_engine;
    manager->storage_engine = storage_engine;
    
    // Allocate task queue
    manager->task_queue_capacity = config->max_cores * MAX_TASKS_PER_CORE;
    manager->task_queue = calloc(manager->task_queue_capacity, sizeof(TaskEntity*));
    if (!manager->task_queue) {
        set_error(CORE_MANAGER_ERROR_MEMORY_ALLOCATION);
        free(manager->cores);
        free(manager);
        return NULL;
    }
    
    manager->task_queue_size = 0;
    
    // Initialize statistics
    memset(&manager->metrics, 0, sizeof(CoreManagerMetrics));
    manager->metrics.last_reset = time(NULL);
    
    // Initialize synchronization
    if (pthread_mutex_init(&manager->core_mutex, NULL) != 0 ||
        pthread_mutex_init(&manager->task_mutex, NULL) != 0 ||
        pthread_mutex_init(&manager->metrics_mutex, NULL) != 0) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        free(manager->task_queue);
        free(manager->cores);
        free(manager);
        return NULL;
    }
    
    if (pthread_cond_init(&manager->task_condition, NULL) != 0) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        pthread_mutex_destroy(&manager->core_mutex);
        pthread_mutex_destroy(&manager->task_mutex);
        pthread_mutex_destroy(&manager->metrics_mutex);
        free(manager->task_queue);
        free(manager->cores);
        free(manager);
        return NULL;
    }
    
    // Initialize state
    manager->state = CORE_MANAGER_STATE_INITIALIZING;
    manager->is_initialized = true;
    manager->balance_running = false;
    manager->health_check_running = false;
    
    // Initialize error tracking
    manager->last_error_code = 0;
    memset(manager->last_error_message, 0, sizeof(manager->last_error_message));
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return manager;
}

// Core manager destruction
void core_manager_destroy(CoreManager* manager) {
    if (!manager) return;
    
    // Stop background threads
    core_manager_stop_background_threads(manager);
    
    // Stop all cores
    core_manager_stop_cores(manager);
    
    // Destroy all cores
    for (uint32_t i = 0; i < manager->core_count; i++) {
        if (manager->cores[i]) {
            core_entity_destroy(manager->cores[i]);
        }
    }
    
    // Destroy synchronization primitives
    pthread_mutex_destroy(&manager->core_mutex);
    pthread_mutex_destroy(&manager->task_mutex);
    pthread_mutex_destroy(&manager->metrics_mutex);
    pthread_cond_destroy(&manager->task_condition);
    
    // Free memory
    free(manager->task_queue);
    free(manager->cores);
    free(manager);
}

// Core lifecycle management
bool core_manager_initialize_cores(CoreManager* manager) {
    if (!validate_manager(manager)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&manager->core_mutex);
    
    // Create cores up to min_cores
    for (uint32_t i = 0; i < manager->config.min_cores; i++) {
        uint64_t segment_size = (uint64_t)512 * 1024 * 1024; // 512MB per core
        CoreEntity* core = core_entity_create(i, segment_size, MAX_TASKS_PER_CORE);
        
        if (!core) {
            pthread_mutex_unlock(&manager->core_mutex);
            set_error(CORE_MANAGER_ERROR_MEMORY_ALLOCATION);
            return false;
        }
        
        manager->cores[manager->core_count++] = core;
    }
    
    manager->state = CORE_MANAGER_STATE_RUNNING;
    pthread_mutex_unlock(&manager->core_mutex);
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return true;
}

bool core_manager_start_cores(CoreManager* manager) {
    if (!validate_manager(manager)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&manager->core_mutex);
    
    bool all_started = true;
    for (uint32_t i = 0; i < manager->core_count; i++) {
        CoreEntity* core = manager->cores[i];
        if (core) {
            if (!core_entity_set_state(core, CORE_STATE_RUNNING)) {
                all_started = false;
            }
        }
    }
    
    pthread_mutex_unlock(&manager->core_mutex);
    
    set_error(all_started ? CORE_MANAGER_ERROR_NONE : CORE_MANAGER_ERROR_INVALID_STATE);
    return all_started;
}

bool core_manager_stop_cores(CoreManager* manager) {
    if (!validate_manager(manager)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&manager->core_mutex);
    
    bool all_stopped = true;
    for (uint32_t i = 0; i < manager->core_count; i++) {
        CoreEntity* core = manager->cores[i];
        if (core) {
            if (!core_entity_set_state(core, CORE_STATE_SHUTDOWN)) {
                all_stopped = false;
            }
        }
    }
    
    manager->state = CORE_MANAGER_STATE_SHUTTING_DOWN;
    pthread_mutex_unlock(&manager->core_mutex);
    
    set_error(all_stopped ? CORE_MANAGER_ERROR_NONE : CORE_MANAGER_ERROR_INVALID_STATE);
    return all_stopped;
}

bool core_manager_pause_cores(CoreManager* manager) {
    if (!validate_manager(manager)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&manager->core_mutex);
    
    bool all_paused = true;
    for (uint32_t i = 0; i < manager->core_count; i++) {
        CoreEntity* core = manager->cores[i];
        if (core) {
            if (!core_entity_set_state(core, CORE_STATE_SLEEPING)) {
                all_paused = false;
            }
        }
    }
    
    manager->state = CORE_MANAGER_STATE_PAUSED;
    pthread_mutex_unlock(&manager->core_mutex);
    
    set_error(all_paused ? CORE_MANAGER_ERROR_NONE : CORE_MANAGER_ERROR_INVALID_STATE);
    return all_paused;
}

bool core_manager_resume_cores(CoreManager* manager) {
    if (!validate_manager(manager)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&manager->core_mutex);
    
    bool all_resumed = true;
    for (uint32_t i = 0; i < manager->core_count; i++) {
        CoreEntity* core = manager->cores[i];
        if (core) {
            if (!core_entity_set_state(core, CORE_STATE_RUNNING)) {
                all_resumed = false;
            }
        }
    }
    
    manager->state = CORE_MANAGER_STATE_RUNNING;
    pthread_mutex_unlock(&manager->core_mutex);
    
    set_error(all_resumed ? CORE_MANAGER_ERROR_NONE : CORE_MANAGER_ERROR_INVALID_STATE);
    return all_resumed;
}

// Core operations
bool core_manager_add_core(CoreManager* manager, CoreEntity* core) {
    if (!validate_manager(manager) || !core) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&manager->core_mutex);
    
    if (manager->core_count >= manager->max_core_count) {
        pthread_mutex_unlock(&manager->core_mutex);
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    manager->cores[manager->core_count++] = core;
    
    pthread_mutex_unlock(&manager->core_mutex);
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return true;
}

bool core_manager_remove_core(CoreManager* manager, uint32_t core_id) {
    if (!validate_core_id(manager, core_id)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&manager->core_mutex);
    
    // Move all cores after this one forward
    for (uint32_t i = core_id; i < manager->core_count - 1; i++) {
        manager->cores[i] = manager->cores[i + 1];
    }
    
    manager->core_count--;
    
    pthread_mutex_unlock(&manager->core_mutex);
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return true;
}

CoreEntity* core_manager_get_core(CoreManager* manager, uint32_t core_id) {
    if (!validate_core_id(manager, core_id)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return manager->cores[core_id];
}

uint32_t core_manager_get_core_count(const CoreManager* manager) {
    if (!validate_manager(manager)) {
        return 0;
    }
    
    return manager->core_count;
}

// Task management
bool core_manager_submit_task(CoreManager* manager, TaskEntity* task) {
    if (!validate_manager(manager) || !validate_task(task)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&manager->task_mutex);
    
    if (manager->task_queue_size >= manager->task_queue_capacity) {
        pthread_mutex_unlock(&manager->task_mutex);
        set_error(CORE_MANAGER_ERROR_TASK_QUEUE_FULL);
        return false;
    }
    
    manager->task_queue[manager->task_queue_size++] = task;
    
    // Signal waiting threads
    pthread_cond_signal(&manager->task_condition);
    
    pthread_mutex_unlock(&manager->task_mutex);
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return true;
}

bool core_manager_submit_task_batch(CoreManager* manager, TaskEntity** tasks, uint32_t count) {
    if (!validate_manager(manager) || !tasks || count == 0) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&manager->task_mutex);
    
    if (manager->task_queue_size + count > manager->task_queue_capacity) {
        pthread_mutex_unlock(&manager->task_mutex);
        set_error(CORE_MANAGER_ERROR_TASK_QUEUE_FULL);
        return false;
    }
    
    for (uint32_t i = 0; i < count; i++) {
        if (validate_task(tasks[i])) {
            manager->task_queue[manager->task_queue_size++] = tasks[i];
        }
    }
    
    // Signal waiting threads
    pthread_cond_broadcast(&manager->task_condition);
    
    pthread_mutex_unlock(&manager->task_mutex);
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return true;
}

TaskEntity* core_manager_get_next_task(CoreManager* manager) {
    if (!validate_manager(manager)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    pthread_mutex_lock(&manager->task_mutex);
    
    while (manager->task_queue_size == 0) {
        pthread_cond_wait(&manager->task_condition, &manager->task_mutex);
    }
    
    TaskEntity* task = manager->task_queue[0];
    
    // Remove from queue
    for (uint32_t i = 0; i < manager->task_queue_size - 1; i++) {
        manager->task_queue[i] = manager->task_queue[i + 1];
    }
    manager->task_queue_size--;
    
    pthread_mutex_unlock(&manager->task_mutex);
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return task;
}

bool core_manager_complete_task(CoreManager* manager, TaskEntity* task) {
    if (!validate_manager(manager) || !validate_task(task)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    // Update metrics
    pthread_mutex_lock(&manager->metrics_mutex);
    manager->metrics.total_tasks_processed++;
    manager->metrics.last_balance_operation = time(NULL);
    pthread_mutex_unlock(&manager->metrics_mutex);
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return true;
}

uint32_t core_manager_get_pending_task_count(const CoreManager* manager) {
    if (!validate_manager(manager)) {
        return 0;
    }
    
    return manager->task_queue_size;
}

// Load balancing
bool core_manager_balance_load(CoreManager* manager) {
    if (!validate_manager(manager)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&manager->core_mutex);
    
    // Calculate average load
    uint32_t total_tasks = 0;
    uint32_t active_cores = 0;
    
    for (uint32_t i = 0; i < manager->core_count; i++) {
        CoreEntity* core = manager->cores[i];
        if (core && core_entity_is_running(core)) {
            CoreMetrics metrics = core_entity_get_metrics(core);
            total_tasks += metrics.operations_completed;
            active_cores++;
        }
    }
    
    if (active_cores == 0) {
        pthread_mutex_unlock(&manager->core_mutex);
        set_error(CORE_MANAGER_ERROR_BALANCE_FAILED);
        return false;
    }
    
    uint32_t average_load = total_tasks / active_cores;
    
    // Check for imbalance and migrate tasks
    bool balanced = true;
    for (uint32_t i = 0; i < manager->core_count; i++) {
        CoreEntity* core = manager->cores[i];
        if (core && core_entity_is_running(core)) {
            CoreMetrics metrics = core_entity_get_metrics(core);
            if (metrics.operations_completed > average_load * (1 + LOAD_BALANCE_THRESHOLD)) {
                // This core is overloaded, try to migrate tasks
                balanced = false;
                break;
            }
        }
    }
    
    pthread_mutex_unlock(&manager->core_mutex);
    
    if (!balanced) {
        // Perform task migration
        // This would involve moving tasks between cores
        manager->metrics.load_balance_operations++;
    }
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return true;
}

bool core_manager_migrate_task(CoreManager* manager, TaskEntity* task, uint32_t target_core_id) {
    if (!validate_manager(manager) || !validate_task(task) || !validate_core_id(manager, target_core_id)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    CoreEntity* target_core = manager->cores[target_core_id];
    if (!target_core || !core_entity_is_running(target_core)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    bool success = core_entity_assign_task(target_core, task);
    
    if (success) {
        manager->metrics.total_tasks_migrated++;
    }
    
    set_error(success ? CORE_MANAGER_ERROR_NONE : CORE_MANAGER_ERROR_BALANCE_FAILED);
    return success;
}

uint32_t core_manager_select_optimal_core(CoreManager* manager, TaskEntity* task) {
    if (!validate_manager(manager) || !validate_task(task)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return 0;
    }
    
    switch (manager->config.balance_strategy) {
        case LOAD_BALANCE_ROUND_ROBIN:
            return select_core_round_robin(manager);
        case LOAD_BALANCE_LEAST_LOADED:
            return select_core_least_loaded(manager);
        case LOAD_BALANCE_WEIGHTED_ROUND_ROBIN:
            return select_core_weighted_round_robin(manager);
        case LOAD_BALANCE_ADAPTIVE:
            return select_core_adaptive(manager);
        case LOAD_BALANCE_POWER_AWARE:
            return select_core_power_aware(manager);
        default:
            return select_core_round_robin(manager);
    }
}

bool core_manager_set_balance_strategy(CoreManager* manager, LoadBalanceStrategy strategy) {
    if (!validate_manager(manager)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    manager->config.balance_strategy = strategy;
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return true;
}

// Health monitoring
bool core_manager_check_core_health(CoreManager* manager, uint32_t core_id) {
    if (!validate_core_id(manager, core_id)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    CoreEntity* core = manager->cores[core_id];
    if (!core) {
        set_error(CORE_MANAGER_ERROR_CORE_NOT_FOUND);
        return false;
    }
    
    CoreState state = core_entity_get_state(core);
    bool healthy = (state == CORE_STATE_RUNNING || state == CORE_STATE_IDLE);
    
    if (!healthy) {
        manager->metrics.core_failures++;
    }
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return healthy;
}

bool core_manager_recover_core(CoreManager* manager, uint32_t core_id) {
    if (!validate_core_id(manager, core_id)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    CoreEntity* core = manager->cores[core_id];
    if (!core) {
        set_error(CORE_MANAGER_ERROR_CORE_NOT_FOUND);
        return false;
    }
    
    // Try to restart the core
    bool success = core_entity_set_state(core, CORE_STATE_RUNNING);
    
    if (success) {
        manager->metrics.recovery_operations++;
    }
    
    set_error(success ? CORE_MANAGER_ERROR_NONE : CORE_MANAGER_ERROR_INVALID_STATE);
    return success;
}

// Background health check thread
static void* health_check_thread_func(void* arg) {
    CoreManager* manager = (CoreManager*)arg;
    
    while (manager->health_check_running) {
        sleep(manager->config.core_health_check_interval_ms / 1000);
        
        if (manager->health_check_running) {
            // Check all cores
            for (uint32_t i = 0; i < manager->core_count; i++) {
                if (!core_manager_check_core_health(manager, i)) {
                    // Try to recover
                    core_manager_recover_core(manager, i);
                }
            }
        }
    }
    
    return NULL;
}

bool core_manager_start_health_monitoring(CoreManager* manager) {
    if (!validate_manager(manager)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    if (manager->health_check_running) {
        set_error(CORE_MANAGER_ERROR_NONE);
        return true; // Already running
    }
    
    manager->health_check_running = true;
    
    if (pthread_create(&manager->health_check_thread, NULL, health_check_thread_func, manager) != 0) {
        manager->health_check_running = false;
        set_error(CORE_MANAGER_ERROR_THREAD_CREATION);
        return false;
    }
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return true;
}

bool core_manager_stop_health_monitoring(CoreManager* manager) {
    if (!validate_manager(manager)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    if (!manager->health_check_running) {
        set_error(CORE_MANAGER_ERROR_NONE);
        return true; // Already stopped
    }
    
    manager->health_check_running = false;
    pthread_join(manager->health_check_thread, NULL);
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return true;
}

// Performance monitoring
CoreManagerMetrics core_manager_get_metrics(const CoreManager* manager) {
    CoreManagerMetrics metrics = {0};
    
    if (!validate_manager(manager)) {
        return metrics;
    }
    
    pthread_mutex_lock(&manager->metrics_mutex);
    metrics = manager->metrics;
    pthread_mutex_unlock(&manager->metrics_mutex);
    
    // Update dynamic metrics
    pthread_mutex_lock(&manager->core_mutex);
    metrics.total_cores = manager->core_count;
    metrics.active_cores = 0;
    metrics.idle_cores = 0;
    metrics.error_cores = 0;
    
    for (uint32_t i = 0; i < manager->core_count; i++) {
        CoreEntity* core = manager->cores[i];
        if (core) {
            CoreState state = core_entity_get_state(core);
            switch (state) {
                case CORE_STATE_RUNNING:
                    metrics.active_cores++;
                    break;
                case CORE_STATE_IDLE:
                    metrics.idle_cores++;
                    break;
                case CORE_STATE_ERROR:
                    metrics.error_cores++;
                    break;
                default:
                    break;
            }
        }
    }
    pthread_mutex_unlock(&manager->core_mutex);
    
    return metrics;
}

void core_manager_reset_metrics(CoreManager* manager) {
    if (!validate_manager(manager)) {
        return;
    }
    
    pthread_mutex_lock(&manager->metrics_mutex);
    memset(&manager->metrics, 0, sizeof(CoreManagerMetrics));
    manager->metrics.last_reset = time(NULL);
    pthread_mutex_unlock(&manager->metrics_mutex);
}

bool core_manager_print_stats(const CoreManager* manager, FILE* stream) {
    if (!validate_manager(manager) || !stream) {
        return false;
    }
    
    CoreManagerMetrics metrics = core_manager_get_metrics(manager);
    
    fprintf(stream, "Core Manager Statistics:\n");
    fprintf(stream, "  Total Cores: %u\n", metrics.total_cores);
    fprintf(stream, "  Active Cores: %u\n", metrics.active_cores);
    fprintf(stream, "  Idle Cores: %u\n", metrics.idle_cores);
    fprintf(stream, "  Error Cores: %u\n", metrics.error_cores);
    fprintf(stream, "  Total Tasks Processed: %lu\n", metrics.total_tasks_processed);
    fprintf(stream, "  Total Tasks Failed: %lu\n", metrics.total_tasks_failed);
    fprintf(stream, "  Total Tasks Migrated: %lu\n", metrics.total_tasks_migrated);
    fprintf(stream, "  Average CPU Utilization: %.2f%%\n", metrics.average_cpu_utilization * 100.0);
    fprintf(stream, "  Average Memory Usage: %.2f MB\n", metrics.average_memory_usage / 1024.0 / 1024.0);
    fprintf(stream, "  Average Task Completion Time: %.2f ms\n", metrics.average_task_completion_time);
    fprintf(stream, "  Load Balance Operations: %lu\n", metrics.load_balance_operations);
    fprintf(stream, "  Core Failures: %lu\n", metrics.core_failures);
    fprintf(stream, "  Recovery Operations: %lu\n", metrics.recovery_operations);
    
    return true;
}

// Configuration management
bool core_manager_update_config(CoreManager* manager, const CoreManagerConfig* new_config) {
    if (!validate_manager(manager) || !validate_config(new_config)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    manager->config = *new_config;
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return true;
}

CoreManagerConfig core_manager_get_config(const CoreManager* manager) {
    CoreManagerConfig config = {0};
    
    if (!validate_manager(manager)) {
        return config;
    }
    
    return manager->config;
}

bool core_manager_validate_config(const CoreManagerConfig* config) {
    return validate_config(config);
}

// State management
CoreManagerState core_manager_get_state(const CoreManager* manager) {
    if (!validate_manager(manager)) {
        return CORE_MANAGER_STATE_ERROR;
    }
    
    return manager->state;
}

bool core_manager_set_state(CoreManager* manager, CoreManagerState new_state) {
    if (!validate_manager(manager)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    manager->state = new_state;
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return true;
}

bool core_manager_is_running(const CoreManager* manager) {
    if (!validate_manager(manager)) {
        return false;
    }
    
    return manager->state == CORE_MANAGER_STATE_RUNNING;
}

// Error handling
uint32_t core_manager_get_last_error_code(const CoreManager* manager) {
    if (!validate_manager(manager)) {
        return 0;
    }
    
    return manager->last_error_code;
}

const char* core_manager_get_last_error_message(const CoreManager* manager) {
    if (!validate_manager(manager)) {
        return "Invalid manager";
    }
    
    return manager->last_error_message;
}

const char* core_manager_error_code_to_string(uint32_t error_code) {
    return core_manager_error_to_string((CoreManagerError)error_code);
}

// Utility functions
bool core_manager_is_core_available(const CoreManager* manager, uint32_t core_id) {
    if (!validate_core_id(manager, core_id)) {
        return false;
    }
    
    CoreEntity* core = manager->cores[core_id];
    return core && core_entity_is_running(core);
}

uint32_t core_manager_get_least_loaded_core(const CoreManager* manager) {
    return select_core_least_loaded(manager);
}

uint32_t core_manager_get_most_loaded_core(const CoreManager* manager) {
    if (!validate_manager(manager)) {
        return 0;
    }
    
    uint32_t most_loaded_core = 0;
    uint32_t max_tasks = 0;
    
    for (uint32_t i = 0; i < manager->core_count; i++) {
        CoreEntity* core = manager->cores[i];
        if (core && core_entity_is_running(core)) {
            CoreMetrics metrics = core_entity_get_metrics(core);
            if (metrics.operations_completed > max_tasks) {
                max_tasks = metrics.operations_completed;
                most_loaded_core = i;
            }
        }
    }
    
    return most_loaded_core;
}

double core_manager_get_system_load(const CoreManager* manager) {
    if (!validate_manager(manager)) {
        return 0.0;
    }
    
    uint32_t total_operations = 0;
    uint32_t active_cores = 0;
    
    for (uint32_t i = 0; i < manager->core_count; i++) {
        CoreEntity* core = manager->cores[i];
        if (core && core_entity_is_running(core)) {
            CoreMetrics metrics = core_entity_get_metrics(core);
            total_operations += metrics.operations_completed;
            active_cores++;
        }
    }
    
    if (active_cores == 0) {
        return 0.0;
    }
    
    // Normalize to 0-1 range
    return (double)total_operations / (active_cores * 1000.0); // Assuming 1000 ops per core is 100% load
}

// Advanced operations
bool core_manager_scale_up(CoreManager* manager, uint32_t additional_cores) {
    if (!validate_manager(manager)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    if (manager->core_count + additional_cores > manager->max_core_count) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&manager->core_mutex);
    
    bool all_added = true;
    for (uint32_t i = 0; i < additional_cores; i++) {
        uint32_t new_core_id = manager->core_count;
        uint64_t segment_size = (uint64_t)512 * 1024 * 1024; // 512MB per core
        CoreEntity* core = core_entity_create(new_core_id, segment_size, MAX_TASKS_PER_CORE);
        
        if (core) {
            manager->cores[manager->core_count++] = core;
            core_entity_set_state(core, CORE_STATE_RUNNING);
        } else {
            all_added = false;
        }
    }
    
    pthread_mutex_unlock(&manager->core_mutex);
    
    set_error(all_added ? CORE_MANAGER_ERROR_NONE : CORE_MANAGER_ERROR_MEMORY_ALLOCATION);
    return all_added;
}

bool core_manager_scale_down(CoreManager* manager, uint32_t cores_to_remove) {
    if (!validate_manager(manager)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    if (manager->core_count - cores_to_remove < manager->config.min_cores) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&manager->core_mutex);
    
    // Remove cores from the end (simplified approach)
    for (uint32_t i = 0; i < cores_to_remove; i++) {
        if (manager->core_count > 0) {
            uint32_t core_to_remove = manager->core_count - 1;
            CoreEntity* core = manager->cores[core_to_remove];
            
            if (core) {
                core_entity_set_state(core, CORE_STATE_SHUTDOWN);
                core_entity_destroy(core);
            }
            
            manager->cores[core_to_remove] = NULL;
            manager->core_count--;
        }
    }
    
    pthread_mutex_unlock(&manager->core_mutex);
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return true;
}

bool core_manager_emergency_shutdown(CoreManager* manager) {
    if (!validate_manager(manager)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    // Immediate shutdown without cleanup
    manager->state = CORE_MANAGER_STATE_ERROR;
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return true;
}

bool core_manager_graceful_shutdown(CoreManager* manager) {
    if (!validate_manager(manager)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    // Stop background threads
    core_manager_stop_background_threads(manager);
    
    // Stop all cores
    core_manager_stop_cores(manager);
    
    // Wait for completion
    core_manager_wait_for_completion(manager);
    
    manager->state = CORE_MANAGER_STATE_SHUTTING_DOWN;
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return true;
}

// Threading support
bool core_manager_start_background_threads(CoreManager* manager) {
    if (!validate_manager(manager)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    bool all_started = true;
    
    // Start health monitoring
    if (!core_manager_start_health_monitoring(manager)) {
        all_started = false;
    }
    
    set_error(all_started ? CORE_MANAGER_ERROR_NONE : CORE_MANAGER_ERROR_THREAD_CREATION);
    return all_started;
}

bool core_manager_stop_background_threads(CoreManager* manager) {
    if (!validate_manager(manager)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    bool all_stopped = true;
    
    // Stop health monitoring
    if (!core_manager_stop_health_monitoring(manager)) {
        all_stopped = false;
    }
    
    set_error(all_stopped ? CORE_MANAGER_ERROR_NONE : CORE_MANAGER_ERROR_INVALID_STATE);
    return all_stopped;
}

bool core_manager_wait_for_completion(CoreManager* manager) {
    if (!validate_manager(manager)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    // Wait for all cores to complete
    pthread_mutex_lock(&manager->core_mutex);
    
    for (uint32_t i = 0; i < manager->core_count; i++) {
        CoreEntity* core = manager->cores[i];
        if (core) {
            core_entity_wait_for_completion(core);
        }
    }
    
    pthread_mutex_unlock(&manager->core_mutex);
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return true;
}

// Validation
bool core_manager_validate_manager(const CoreManager* manager) {
    return validate_manager(manager);
}

bool core_manager_validate_core_id(const CoreManager* manager, uint32_t core_id) {
    return validate_core_id(manager, core_id);
}

bool core_manager_validate_task(const TaskEntity* task) {
    return validate_task(task);
}

// Memory management
size_t core_manager_get_memory_usage(const CoreManager* manager) {
    if (!validate_manager(manager)) {
        return 0;
    }
    
    size_t usage = sizeof(CoreManager);
    
    // Add core memory usage
    for (uint32_t i = 0; i < manager->core_count; i++) {
        CoreEntity* core = manager->cores[i];
        if (core) {
            // Estimate core memory usage
            usage += sizeof(CoreEntity);
        }
    }
    
    // Add task queue memory usage
    usage += manager->task_queue_size * sizeof(TaskEntity*);
    
    return usage;
}

bool core_manager_optimize_memory(CoreManager* manager) {
    if (!validate_manager(manager)) {
        set_error(CORE_MANAGER_ERROR_INVALID_PARAM);
        return false;
    }
    
    // Compact task queue
    pthread_mutex_lock(&manager->task_mutex);
    
    uint32_t new_size = 0;
    for (uint32_t i = 0; i < manager->task_queue_size; i++) {
        if (manager->task_queue[i]) {
            if (new_size != i) {
                manager->task_queue[new_size] = manager->task_queue[i];
            }
            new_size++;
        }
    }
    
    manager->task_queue_size = new_size;
    
    pthread_mutex_unlock(&manager->task_mutex);
    
    set_error(CORE_MANAGER_ERROR_NONE);
    return true;
} 