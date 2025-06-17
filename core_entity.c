#include "core_entity.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

// Internal validation constants
#define MAX_CORE_ID 1024
#define MIN_SEGMENT_SIZE 4096
#define MAX_SEGMENT_SIZE (1024ULL * 1024ULL * 1024ULL) // 1GB
#define MAX_TASKS 1000

// Internal error codes
typedef enum {
    CORE_ERROR_NONE = 0,
    CORE_ERROR_INVALID_PARAM,
    CORE_ERROR_MEMORY_ALLOCATION,
    CORE_ERROR_THREAD_CREATION,
    CORE_ERROR_THREAD_JOIN,
    CORE_ERROR_MUTEX_INIT,
    CORE_ERROR_COND_INIT,
    CORE_ERROR_INVALID_STATE
} CoreError;

// Internal error tracking
static CoreError last_error = CORE_ERROR_NONE;

// Error handling
static void set_error(CoreError error) {
    last_error = error;
}

CoreError core_entity_get_last_error(void) {
    return last_error;
}

const char* core_entity_error_to_string(CoreError error) {
    switch (error) {
        case CORE_ERROR_NONE: return "No error";
        case CORE_ERROR_INVALID_PARAM: return "Invalid parameter";
        case CORE_ERROR_MEMORY_ALLOCATION: return "Memory allocation failed";
        case CORE_ERROR_THREAD_CREATION: return "Thread creation failed";
        case CORE_ERROR_THREAD_JOIN: return "Thread join failed";
        case CORE_ERROR_MUTEX_INIT: return "Mutex initialization failed";
        case CORE_ERROR_COND_INIT: return "Condition variable initialization failed";
        case CORE_ERROR_INVALID_STATE: return "Invalid state transition";
        default: return "Unknown error";
    }
}

// Validation functions
static bool validate_core_id(uint32_t id) {
    return id < MAX_CORE_ID;
}

static bool validate_segment_size(uint64_t size) {
    return size >= MIN_SEGMENT_SIZE && size <= MAX_SEGMENT_SIZE;
}

static bool validate_max_tasks(uint32_t max_tasks) {
    return max_tasks > 0 && max_tasks <= MAX_TASKS;
}

static bool validate_core(const CoreEntity* core) {
    return core != NULL && core->is_initialized;
}

// Core entity creation with full validation
CoreEntity* core_entity_create(uint32_t id, uint64_t segment_size, uint32_t max_tasks) {
    // Parameter validation
    if (!validate_core_id(id)) {
        set_error(CORE_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    if (!validate_segment_size(segment_size)) {
        set_error(CORE_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    if (!validate_max_tasks(max_tasks)) {
        set_error(CORE_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    // Allocate core entity
    CoreEntity* core = malloc(sizeof(CoreEntity));
    if (!core) {
        set_error(CORE_ERROR_MEMORY_ALLOCATION);
        return NULL;
    }
    
    // Initialize immutable properties
    *(uint32_t*)&core->id = id;
    *(uint64_t*)&core->segment_size = segment_size;
    *(uint32_t*)&core->max_tasks = max_tasks;
    
    // Initialize mutable state
    core->state = CORE_STATE_IDLE;
    core->current_task = NULL;
    core->task_count = 0;
    
    // Initialize metrics
    memset(&core->metrics, 0, sizeof(CoreMetrics));
    core->metrics.last_activity = time(NULL);
    
    // Initialize threading primitives
    if (pthread_mutex_init(&core->state_mutex, NULL) != 0) {
        set_error(CORE_ERROR_MUTEX_INIT);
        free(core);
        return NULL;
    }
    
    if (pthread_cond_init(&core->state_condition, NULL) != 0) {
        set_error(CORE_ERROR_COND_INIT);
        pthread_mutex_destroy(&core->state_mutex);
        free(core);
        return NULL;
    }
    
    // Initialize thread
    core->thread = 0;
    
    // Initialize resource management
    core->is_initialized = true;
    core->private_data = NULL;
    
    set_error(CORE_ERROR_NONE);
    return core;
}

// Safe destruction with cleanup
void core_entity_destroy(CoreEntity* core) {
    if (!core) return;
    
    // Stop thread if running
    if (core->is_initialized) {
        core_entity_stop_thread(core);
        core_entity_wait_for_completion(core);
        
        // Cleanup threading primitives
        pthread_mutex_destroy(&core->state_mutex);
        pthread_cond_destroy(&core->state_condition);
    }
    
    // Free private data if allocated
    if (core->private_data) {
        free(core->private_data);
    }
    
    // Clear sensitive data
    memset(core, 0, sizeof(CoreEntity));
    free(core);
}

// Thread-safe state management
CoreState core_entity_get_state(const CoreEntity* core) {
    if (!validate_core(core)) {
        set_error(CORE_ERROR_INVALID_PARAM);
        return CORE_STATE_ERROR;
    }
    
    pthread_mutex_lock((pthread_mutex_t*)&core->state_mutex);
    CoreState state = core->state;
    pthread_mutex_unlock((pthread_mutex_t*)&core->state_mutex);
    
    set_error(CORE_ERROR_NONE);
    return state;
}

bool core_entity_set_state(CoreEntity* core, CoreState new_state) {
    if (!validate_core(core)) {
        set_error(CORE_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&core->state_mutex);
    
    // Validate state transition
    bool valid_transition = false;
    switch (core->state) {
        case CORE_STATE_IDLE:
            valid_transition = (new_state == CORE_STATE_RUNNING || 
                              new_state == CORE_STATE_SHUTDOWN);
            break;
        case CORE_STATE_RUNNING:
            valid_transition = (new_state == CORE_STATE_IDLE || 
                              new_state == CORE_STATE_SLEEPING || 
                              new_state == CORE_STATE_ERROR || 
                              new_state == CORE_STATE_SHUTDOWN);
            break;
        case CORE_STATE_SLEEPING:
            valid_transition = (new_state == CORE_STATE_RUNNING || 
                              new_state == CORE_STATE_SHUTDOWN);
            break;
        case CORE_STATE_ERROR:
            valid_transition = (new_state == CORE_STATE_IDLE || 
                              new_state == CORE_STATE_SHUTDOWN);
            break;
        case CORE_STATE_SHUTDOWN:
            valid_transition = false; // No transitions from shutdown
            break;
    }
    
    if (!valid_transition) {
        pthread_mutex_unlock(&core->state_mutex);
        set_error(CORE_ERROR_INVALID_STATE);
        return false;
    }
    
    core->state = new_state;
    core->metrics.last_activity = time(NULL);
    
    // Signal waiting threads
    pthread_cond_broadcast(&core->state_condition);
    pthread_mutex_unlock(&core->state_mutex);
    
    set_error(CORE_ERROR_NONE);
    return true;
}

bool core_entity_is_running(const CoreEntity* core) {
    CoreState state = core_entity_get_state(core);
    return state == CORE_STATE_RUNNING;
}

// Thread-safe task management
bool core_entity_assign_task(CoreEntity* core, TaskEntity* task) {
    if (!validate_core(core) || !task) {
        set_error(CORE_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&core->state_mutex);
    
    // Check if core can accept tasks
    if (core->state != CORE_STATE_IDLE && core->state != CORE_STATE_RUNNING) {
        pthread_mutex_unlock(&core->state_mutex);
        set_error(CORE_ERROR_INVALID_STATE);
        return false;
    }
    
    // Check task limit
    if (core->task_count >= core->max_tasks) {
        pthread_mutex_unlock(&core->state_mutex);
        set_error(CORE_ERROR_INVALID_PARAM);
        return false;
    }
    
    // Assign task
    core->current_task = task;
    core->task_count++;
    core->state = CORE_STATE_RUNNING;
    core->metrics.last_activity = time(NULL);
    
    pthread_mutex_unlock(&core->state_mutex);
    set_error(CORE_ERROR_NONE);
    return true;
}

TaskEntity* core_entity_get_current_task(const CoreEntity* core) {
    if (!validate_core(core)) {
        set_error(CORE_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    pthread_mutex_lock((pthread_mutex_t*)&core->state_mutex);
    TaskEntity* task = core->current_task;
    pthread_mutex_unlock((pthread_mutex_t*)&core->state_mutex);
    
    set_error(CORE_ERROR_NONE);
    return task;
}

bool core_entity_complete_current_task(CoreEntity* core) {
    if (!validate_core(core)) {
        set_error(CORE_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&core->state_mutex);
    
    if (!core->current_task) {
        pthread_mutex_unlock(&core->state_mutex);
        set_error(CORE_ERROR_INVALID_STATE);
        return false;
    }
    
    // Update metrics
    core->metrics.operations_completed++;
    core->current_task = NULL;
    core->task_count--;
    
    // Set state to idle if no more tasks
    if (core->task_count == 0) {
        core->state = CORE_STATE_IDLE;
    }
    
    core->metrics.last_activity = time(NULL);
    
    pthread_mutex_unlock(&core->state_mutex);
    set_error(CORE_ERROR_NONE);
    return true;
}

// Thread-safe metrics management
void core_entity_update_metrics(CoreEntity* core, const CoreMetrics* delta) {
    if (!validate_core(core) || !delta) {
        set_error(CORE_ERROR_INVALID_PARAM);
        return;
    }
    
    pthread_mutex_lock(&core->state_mutex);
    
    core->metrics.operations_completed += delta->operations_completed;
    core->metrics.cache_hits += delta->cache_hits;
    core->metrics.cache_misses += delta->cache_misses;
    core->metrics.compression_ratio_sum += delta->compression_ratio_sum;
    core->metrics.compression_operations += delta->compression_operations;
    core->metrics.last_activity = time(NULL);
    
    // Update calculated metrics
    if (delta->cpu_utilization > 0) {
        core->metrics.cpu_utilization = delta->cpu_utilization;
    }
    if (delta->memory_usage > 0) {
        core->metrics.memory_usage = delta->memory_usage;
    }
    
    pthread_mutex_unlock(&core->state_mutex);
    set_error(CORE_ERROR_NONE);
}

CoreMetrics core_entity_get_metrics(const CoreEntity* core) {
    CoreMetrics metrics = {0};
    
    if (!validate_core(core)) {
        set_error(CORE_ERROR_INVALID_PARAM);
        return metrics;
    }
    
    pthread_mutex_lock((pthread_mutex_t*)&core->state_mutex);
    metrics = core->metrics;
    pthread_mutex_unlock((pthread_mutex_t*)&core->state_mutex);
    
    set_error(CORE_ERROR_NONE);
    return metrics;
}

void core_entity_reset_metrics(CoreEntity* core) {
    if (!validate_core(core)) {
        set_error(CORE_ERROR_INVALID_PARAM);
        return;
    }
    
    pthread_mutex_lock(&core->state_mutex);
    memset(&core->metrics, 0, sizeof(CoreMetrics));
    core->metrics.last_activity = time(NULL);
    pthread_mutex_unlock(&core->state_mutex);
    
    set_error(CORE_ERROR_NONE);
}

// Threading support
bool core_entity_start_thread(CoreEntity* core, void* (*thread_func)(void*), void* arg) {
    if (!validate_core(core) || !thread_func) {
        set_error(CORE_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&core->state_mutex);
    
    if (core->thread != 0) {
        pthread_mutex_unlock(&core->state_mutex);
        set_error(CORE_ERROR_INVALID_STATE);
        return false;
    }
    
    int result = pthread_create(&core->thread, NULL, thread_func, arg);
    if (result != 0) {
        pthread_mutex_unlock(&core->state_mutex);
        set_error(CORE_ERROR_THREAD_CREATION);
        return false;
    }
    
    pthread_mutex_unlock(&core->state_mutex);
    set_error(CORE_ERROR_NONE);
    return true;
}

bool core_entity_stop_thread(CoreEntity* core) {
    if (!validate_core(core)) {
        set_error(CORE_ERROR_INVALID_PARAM);
        return false;
    }
    
    return core_entity_set_state(core, CORE_STATE_SHUTDOWN);
}

bool core_entity_wait_for_completion(CoreEntity* core) {
    if (!validate_core(core)) {
        set_error(CORE_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&core->state_mutex);
    pthread_t thread = core->thread;
    pthread_mutex_unlock(&core->state_mutex);
    
    if (thread == 0) {
        set_error(CORE_ERROR_NONE);
        return true;
    }
    
    int result = pthread_join(thread, NULL);
    if (result != 0) {
        set_error(CORE_ERROR_THREAD_JOIN);
        return false;
    }
    
    pthread_mutex_lock(&core->state_mutex);
    core->thread = 0;
    pthread_mutex_unlock(&core->state_mutex);
    
    set_error(CORE_ERROR_NONE);
    return true;
}

// Validation
bool core_entity_is_valid(const CoreEntity* core) {
    return validate_core(core);
}

const char* core_entity_state_to_string(CoreState state) {
    switch (state) {
        case CORE_STATE_IDLE: return "IDLE";
        case CORE_STATE_RUNNING: return "RUNNING";
        case CORE_STATE_SLEEPING: return "SLEEPING";
        case CORE_STATE_ERROR: return "ERROR";
        case CORE_STATE_SHUTDOWN: return "SHUTDOWN";
        default: return "UNKNOWN";
    }
} 