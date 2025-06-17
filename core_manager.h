#ifndef CORE_MANAGER_H
#define CORE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

// Forward declarations
typedef struct CoreEntity CoreEntity;
typedef struct TaskEntity TaskEntity;
typedef struct CacheEngine CacheEngine;
typedef struct CompressionEngine CompressionEngine;
typedef struct StorageEngine StorageEngine;

// Core manager states
typedef enum {
    CORE_MANAGER_STATE_INITIALIZING = 0,
    CORE_MANAGER_STATE_RUNNING,
    CORE_MANAGER_STATE_PAUSED,
    CORE_MANAGER_STATE_SHUTTING_DOWN,
    CORE_MANAGER_STATE_ERROR
} CoreManagerState;

// Load balancing strategies
typedef enum {
    LOAD_BALANCE_ROUND_ROBIN = 0,
    LOAD_BALANCE_LEAST_LOADED,
    LOAD_BALANCE_WEIGHTED_ROUND_ROBIN,
    LOAD_BALANCE_ADAPTIVE,
    LOAD_BALANCE_POWER_AWARE
} LoadBalanceStrategy;

// Core manager performance metrics
typedef struct {
    uint32_t total_cores;
    uint32_t active_cores;
    uint32_t idle_cores;
    uint32_t error_cores;
    
    uint64_t total_tasks_processed;
    uint64_t total_tasks_failed;
    uint64_t total_tasks_migrated;
    
    double average_cpu_utilization;
    double average_memory_usage;
    double average_task_completion_time;
    
    uint64_t load_balance_operations;
    uint64_t core_failures;
    uint64_t recovery_operations;
    
    time_t last_reset;
    time_t last_balance_operation;
} CoreManagerMetrics;

// Core manager configuration
typedef struct {
    uint32_t max_cores;
    uint32_t min_cores;
    LoadBalanceStrategy balance_strategy;
    uint32_t balance_interval_ms;
    uint32_t task_timeout_ms;
    uint32_t core_health_check_interval_ms;
    bool enable_auto_scaling;
    bool enable_fault_tolerance;
    uint32_t max_core_failures;
    uint32_t recovery_timeout_ms;
} CoreManagerConfig;

// Core manager interface
typedef struct CoreManager {
    // Configuration
    CoreManagerConfig config;
    
    // Core management
    CoreEntity** cores;
    uint32_t core_count;
    uint32_t max_core_count;
    
    // Infrastructure engines
    CacheEngine* cache_engine;
    CompressionEngine* compression_engine;
    StorageEngine* storage_engine;
    
    // Task management
    TaskEntity** task_queue;
    uint32_t task_queue_size;
    uint32_t task_queue_capacity;
    
    // Statistics
    CoreManagerMetrics metrics;
    
    // Synchronization
    pthread_mutex_t core_mutex;
    pthread_mutex_t task_mutex;
    pthread_mutex_t metrics_mutex;
    pthread_cond_t task_condition;
    
    // State
    CoreManagerState state;
    bool is_initialized;
    
    // Background threads
    pthread_t balance_thread;
    pthread_t health_check_thread;
    bool balance_running;
    bool health_check_running;
    
    // Error tracking
    uint32_t last_error_code;
    char last_error_message[256];
} CoreManager;

// Core manager interface functions
CoreManager* core_manager_create(const CoreManagerConfig* config,
                                CacheEngine* cache_engine,
                                CompressionEngine* compression_engine,
                                StorageEngine* storage_engine);
void core_manager_destroy(CoreManager* manager);

// Core lifecycle management
bool core_manager_initialize_cores(CoreManager* manager);
bool core_manager_start_cores(CoreManager* manager);
bool core_manager_stop_cores(CoreManager* manager);
bool core_manager_pause_cores(CoreManager* manager);
bool core_manager_resume_cores(CoreManager* manager);

// Core operations
bool core_manager_add_core(CoreManager* manager, CoreEntity* core);
bool core_manager_remove_core(CoreManager* manager, uint32_t core_id);
CoreEntity* core_manager_get_core(CoreManager* manager, uint32_t core_id);
uint32_t core_manager_get_core_count(const CoreManager* manager);

// Task management
bool core_manager_submit_task(CoreManager* manager, TaskEntity* task);
bool core_manager_submit_task_batch(CoreManager* manager, TaskEntity** tasks, uint32_t count);
TaskEntity* core_manager_get_next_task(CoreManager* manager);
bool core_manager_complete_task(CoreManager* manager, TaskEntity* task);
uint32_t core_manager_get_pending_task_count(const CoreManager* manager);

// Load balancing
bool core_manager_balance_load(CoreManager* manager);
bool core_manager_migrate_task(CoreManager* manager, TaskEntity* task, uint32_t target_core_id);
uint32_t core_manager_select_optimal_core(CoreManager* manager, TaskEntity* task);
bool core_manager_set_balance_strategy(CoreManager* manager, LoadBalanceStrategy strategy);

// Health monitoring
bool core_manager_check_core_health(CoreManager* manager, uint32_t core_id);
bool core_manager_recover_core(CoreManager* manager, uint32_t core_id);
bool core_manager_start_health_monitoring(CoreManager* manager);
bool core_manager_stop_health_monitoring(CoreManager* manager);

// Performance monitoring
CoreManagerMetrics core_manager_get_metrics(const CoreManager* manager);
void core_manager_reset_metrics(CoreManager* manager);
bool core_manager_print_stats(const CoreManager* manager, FILE* stream);

// Configuration management
bool core_manager_update_config(CoreManager* manager, const CoreManagerConfig* new_config);
CoreManagerConfig core_manager_get_config(const CoreManager* manager);
bool core_manager_validate_config(const CoreManagerConfig* config);

// State management
CoreManagerState core_manager_get_state(const CoreManager* manager);
bool core_manager_set_state(CoreManager* manager, CoreManagerState new_state);
bool core_manager_is_running(const CoreManager* manager);

// Error handling
uint32_t core_manager_get_last_error_code(const CoreManager* manager);
const char* core_manager_get_last_error_message(const CoreManager* manager);
const char* core_manager_error_code_to_string(uint32_t error_code);

// Utility functions
bool core_manager_is_core_available(const CoreManager* manager, uint32_t core_id);
uint32_t core_manager_get_least_loaded_core(const CoreManager* manager);
uint32_t core_manager_get_most_loaded_core(const CoreManager* manager);
double core_manager_get_system_load(const CoreManager* manager);

// Advanced operations
bool core_manager_scale_up(CoreManager* manager, uint32_t additional_cores);
bool core_manager_scale_down(CoreManager* manager, uint32_t cores_to_remove);
bool core_manager_emergency_shutdown(CoreManager* manager);
bool core_manager_graceful_shutdown(CoreManager* manager);

// Threading support
bool core_manager_start_background_threads(CoreManager* manager);
bool core_manager_stop_background_threads(CoreManager* manager);
bool core_manager_wait_for_completion(CoreManager* manager);

// Validation
bool core_manager_validate_manager(const CoreManager* manager);
bool core_manager_validate_core_id(const CoreManager* manager, uint32_t core_id);
bool core_manager_validate_task(const TaskEntity* task);

// Memory management
size_t core_manager_get_memory_usage(const CoreManager* manager);
bool core_manager_optimize_memory(CoreManager* manager);

#endif // CORE_MANAGER_H 