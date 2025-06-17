#ifndef CORE_ENTITY_H
#define CORE_ENTITY_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

// Forward declarations
typedef struct TaskEntity TaskEntity;
typedef struct BlockEntity BlockEntity;

// Core state enumeration
typedef enum {
    CORE_STATE_IDLE = 0,
    CORE_STATE_RUNNING,
    CORE_STATE_SLEEPING,
    CORE_STATE_ERROR,
    CORE_STATE_SHUTDOWN
} CoreState;

// Core performance metrics
typedef struct {
    uint64_t operations_completed;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t compression_ratio_sum;
    uint64_t compression_operations;
    time_t last_activity;
    double cpu_utilization;
    size_t memory_usage;
} CoreMetrics;

// Core entity with full encapsulation
typedef struct CoreEntity {
    // Immutable properties
    const uint32_t id;
    const uint64_t segment_size;
    const uint32_t max_tasks;
    
    // Mutable state (protected by mutex)
    CoreState state;
    TaskEntity* current_task;
    uint32_t task_count;
    CoreMetrics metrics;
    
    // Threading
    pthread_t thread;
    pthread_mutex_t state_mutex;
    pthread_cond_t state_condition;
    
    // Resource management
    bool is_initialized;
    void* private_data; // Implementation-specific data
} CoreEntity;

// Core entity interface functions
CoreEntity* core_entity_create(uint32_t id, uint64_t segment_size, uint32_t max_tasks);
void core_entity_destroy(CoreEntity* core);

// State management (thread-safe)
CoreState core_entity_get_state(const CoreEntity* core);
bool core_entity_set_state(CoreEntity* core, CoreState new_state);
bool core_entity_is_running(const CoreEntity* core);

// Task management (thread-safe)
bool core_entity_assign_task(CoreEntity* core, TaskEntity* task);
TaskEntity* core_entity_get_current_task(const CoreEntity* core);
bool core_entity_complete_current_task(CoreEntity* core);

// Metrics management (thread-safe)
void core_entity_update_metrics(CoreEntity* core, const CoreMetrics* delta);
CoreMetrics core_entity_get_metrics(const CoreEntity* core);
void core_entity_reset_metrics(CoreEntity* core);

// Threading support
bool core_entity_start_thread(CoreEntity* core, void* (*thread_func)(void*), void* arg);
bool core_entity_stop_thread(CoreEntity* core);
bool core_entity_wait_for_completion(CoreEntity* core);

// Validation
bool core_entity_is_valid(const CoreEntity* core);
const char* core_entity_state_to_string(CoreState state);

#endif // CORE_ENTITY_H 