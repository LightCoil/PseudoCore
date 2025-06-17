#include "task_entity.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <errno.h>

// Internal validation constants
#define MAX_TASK_ID 0xFFFFFFFFFFFFFFFFULL
#define MIN_DATA_SIZE 1
#define MAX_DATA_SIZE (1024ULL * 1024ULL * 1024ULL) // 1GB
#define MAX_RETRY_COUNT 10
#define MAX_BUFFER_SIZE (1024ULL * 1024ULL * 1024ULL) // 1GB

// Internal error codes
typedef enum {
    TASK_ERROR_NONE = 0,
    TASK_ERROR_INVALID_PARAM,
    TASK_ERROR_MEMORY_ALLOCATION,
    TASK_ERROR_INVALID_STATE,
    TASK_ERROR_BUFFER_OVERFLOW,
    TASK_ERROR_CHECKSUM_MISMATCH,
    TASK_ERROR_DEPENDENCY_CYCLE
} TaskError;

// Internal error tracking
static TaskError last_error = TASK_ERROR_NONE;

// Error handling
static void set_error(TaskError error) {
    last_error = error;
}

TaskError task_entity_get_last_error(void) {
    return last_error;
}

const char* task_entity_error_to_string(TaskError error) {
    switch (error) {
        case TASK_ERROR_NONE: return "No error";
        case TASK_ERROR_INVALID_PARAM: return "Invalid parameter";
        case TASK_ERROR_MEMORY_ALLOCATION: return "Memory allocation failed";
        case TASK_ERROR_INVALID_STATE: return "Invalid state transition";
        case TASK_ERROR_BUFFER_OVERFLOW: return "Buffer overflow";
        case TASK_ERROR_CHECKSUM_MISMATCH: return "Checksum mismatch";
        case TASK_ERROR_DEPENDENCY_CYCLE: return "Dependency cycle detected";
        default: return "Unknown error";
    }
}

// Validation functions
static bool validate_task_id(uint64_t id) {
    return id > 0 && id <= MAX_TASK_ID;
}

static bool validate_data_size(size_t size) {
    return size >= MIN_DATA_SIZE && size <= MAX_DATA_SIZE;
}

static bool validate_task(const TaskEntity* task) {
    return task != NULL && task->is_initialized;
}

static bool validate_state_transition(TaskState current_state, TaskState new_state) {
    switch (current_state) {
        case TASK_STATE_PENDING:
            return new_state == TASK_STATE_RUNNING || new_state == TASK_STATE_CANCELLED;
        case TASK_STATE_RUNNING:
            return new_state == TASK_STATE_COMPLETED || new_state == TASK_STATE_FAILED || 
                   new_state == TASK_STATE_CANCELLED;
        case TASK_STATE_COMPLETED:
        case TASK_STATE_FAILED:
        case TASK_STATE_CANCELLED:
            return false; // Terminal states
        default:
            return false;
    }
}

// Checksum calculation using FNV-1a
static uint64_t calculate_fnv1a_checksum(const void* data, size_t size) {
    const uint64_t FNV_PRIME = 1099511628211ULL;
    const uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    
    uint64_t hash = FNV_OFFSET_BASIS;
    const uint8_t* bytes = (const uint8_t*)data;
    
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= FNV_PRIME;
    }
    
    return hash;
}

// Task entity creation with full validation
TaskEntity* task_entity_create(uint64_t id, TaskType type, TaskPriority priority, 
                              uint64_t block_offset, size_t data_size) {
    // Parameter validation
    if (!validate_task_id(id)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    if (!validate_data_size(data_size)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    // Allocate task entity
    TaskEntity* task = malloc(sizeof(TaskEntity));
    if (!task) {
        set_error(TASK_ERROR_MEMORY_ALLOCATION);
        return NULL;
    }
    
    // Initialize immutable properties
    *(uint64_t*)&task->id = id;
    *(TaskType*)&task->type = type;
    *(TaskPriority*)&task->priority = priority;
    *(uint64_t*)&task->block_offset = block_offset;
    *(size_t*)&task->data_size = data_size;
    
    // Initialize mutable state
    task->state = TASK_STATE_PENDING;
    task->retry_count = 0;
    task->max_retries = 3; // Default value
    
    // Initialize metrics
    memset(&task->metrics, 0, sizeof(TaskMetrics));
    task->metrics.start_time = time(NULL);
    
    // Initialize resource management
    task->target_block = NULL;
    task->data_buffer = NULL;
    task->buffer_size = 0;
    
    // Initialize dependencies
    task->dependent_task = NULL;
    task->next_task = NULL;
    
    // Initialize validation and security
    task->checksum = 0;
    task->is_initialized = true;
    task->created_time = time(NULL);
    task->last_modified = task->created_time;
    
    // Calculate initial checksum
    task_entity_update_checksum(task);
    
    set_error(TASK_ERROR_NONE);
    return task;
}

// Safe destruction with cleanup
void task_entity_destroy(TaskEntity* task) {
    if (!task) return;
    
    // Free data buffer if allocated
    if (task->data_buffer) {
        free(task->data_buffer);
        task->data_buffer = NULL;
    }
    
    // Clear sensitive data
    memset(task, 0, sizeof(TaskEntity));
    free(task);
}

// State management
TaskState task_entity_get_state(const TaskEntity* task) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return TASK_STATE_FAILED;
    }
    
    set_error(TASK_ERROR_NONE);
    return task->state;
}

bool task_entity_set_state(TaskEntity* task, TaskState new_state) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return false;
    }
    
    if (!validate_state_transition(task->state, new_state)) {
        set_error(TASK_ERROR_INVALID_STATE);
        return false;
    }
    
    task->state = new_state;
    task->last_modified = time(NULL);
    
    // Update metrics based on state
    if (new_state == TASK_STATE_RUNNING) {
        task->metrics.start_time = time(NULL);
    } else if (new_state == TASK_STATE_COMPLETED || new_state == TASK_STATE_FAILED) {
        task->metrics.end_time = time(NULL);
    }
    
    // Update checksum after state change
    task_entity_update_checksum(task);
    
    set_error(TASK_ERROR_NONE);
    return true;
}

bool task_entity_is_completed(const TaskEntity* task) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return false;
    }
    
    set_error(TASK_ERROR_NONE);
    return task->state == TASK_STATE_COMPLETED;
}

bool task_entity_is_failed(const TaskEntity* task) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return false;
    }
    
    set_error(TASK_ERROR_NONE);
    return task->state == TASK_STATE_FAILED;
}

// Priority management
TaskPriority task_entity_get_priority(const TaskEntity* task) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return TASK_PRIORITY_LOW;
    }
    
    set_error(TASK_ERROR_NONE);
    return task->priority;
}

bool task_entity_set_priority(TaskEntity* task, TaskPriority new_priority) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return false;
    }
    
    // Can only change priority if task is pending
    if (task->state != TASK_STATE_PENDING) {
        set_error(TASK_ERROR_INVALID_STATE);
        return false;
    }
    
    *(TaskPriority*)&task->priority = new_priority;
    task->last_modified = time(NULL);
    task_entity_update_checksum(task);
    
    set_error(TASK_ERROR_NONE);
    return true;
}

int task_entity_compare_priority(const TaskEntity* task1, const TaskEntity* task2) {
    if (!validate_task(task1) || !validate_task(task2)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return 0;
    }
    
    set_error(TASK_ERROR_NONE);
    return (int)task1->priority - (int)task2->priority;
}

// Data management
bool task_entity_set_data_buffer(TaskEntity* task, void* buffer, size_t size) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return false;
    }
    
    if (size > MAX_BUFFER_SIZE) {
        set_error(TASK_ERROR_BUFFER_OVERFLOW);
        return false;
    }
    
    // Free existing buffer if different size
    if (task->data_buffer && task->buffer_size != size) {
        free(task->data_buffer);
        task->data_buffer = NULL;
        task->buffer_size = 0;
    }
    
    // Allocate new buffer if needed
    if (!task->data_buffer) {
        task->data_buffer = malloc(size);
        if (!task->data_buffer) {
            set_error(TASK_ERROR_MEMORY_ALLOCATION);
            return false;
        }
        task->buffer_size = size;
    }
    
    // Copy data
    if (buffer) {
        memcpy(task->data_buffer, buffer, size);
    } else {
        memset(task->data_buffer, 0, size);
    }
    
    task->last_modified = time(NULL);
    task_entity_update_checksum(task);
    
    set_error(TASK_ERROR_NONE);
    return true;
}

void* task_entity_get_data_buffer(const TaskEntity* task) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    set_error(TASK_ERROR_NONE);
    return task->data_buffer;
}

size_t task_entity_get_buffer_size(const TaskEntity* task) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return 0;
    }
    
    set_error(TASK_ERROR_NONE);
    return task->buffer_size;
}

bool task_entity_resize_buffer(TaskEntity* task, size_t new_size) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return false;
    }
    
    if (new_size > MAX_BUFFER_SIZE) {
        set_error(TASK_ERROR_BUFFER_OVERFLOW);
        return false;
    }
    
    if (new_size == task->buffer_size) {
        set_error(TASK_ERROR_NONE);
        return true; // No change needed
    }
    
    void* new_buffer = realloc(task->data_buffer, new_size);
    if (!new_buffer) {
        set_error(TASK_ERROR_MEMORY_ALLOCATION);
        return false;
    }
    
    task->data_buffer = new_buffer;
    task->buffer_size = new_size;
    
    // Zero out new memory if expanding
    if (new_size > task->buffer_size) {
        memset((char*)task->data_buffer + task->buffer_size, 0, 
               new_size - task->buffer_size);
    }
    
    task->last_modified = time(NULL);
    task_entity_update_checksum(task);
    
    set_error(TASK_ERROR_NONE);
    return true;
}

// Block management
bool task_entity_set_target_block(TaskEntity* task, BlockEntity* block) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return false;
    }
    
    task->target_block = block;
    task->last_modified = time(NULL);
    task_entity_update_checksum(task);
    
    set_error(TASK_ERROR_NONE);
    return true;
}

BlockEntity* task_entity_get_target_block(const TaskEntity* task) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    set_error(TASK_ERROR_NONE);
    return task->target_block;
}

// Metrics management
void task_entity_update_metrics(TaskEntity* task, const TaskMetrics* delta) {
    if (!validate_task(task) || !delta) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return;
    }
    
    task->metrics.bytes_processed += delta->bytes_processed;
    task->metrics.operations_performed += delta->operations_performed;
    task->metrics.compression_ratio = delta->compression_ratio;
    task->metrics.cache_hits += delta->cache_hits;
    task->metrics.cache_misses += delta->cache_misses;
    
    task->last_modified = time(NULL);
    task_entity_update_checksum(task);
    
    set_error(TASK_ERROR_NONE);
}

TaskMetrics task_entity_get_metrics(const TaskEntity* task) {
    TaskMetrics metrics = {0};
    
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return metrics;
    }
    
    set_error(TASK_ERROR_NONE);
    return task->metrics;
}

void task_entity_reset_metrics(TaskEntity* task) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return;
    }
    
    memset(&task->metrics, 0, sizeof(TaskMetrics));
    task->metrics.start_time = time(NULL);
    task->last_modified = time(NULL);
    task_entity_update_checksum(task);
    
    set_error(TASK_ERROR_NONE);
}

double task_entity_get_execution_time(const TaskEntity* task) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return 0.0;
    }
    
    if (task->metrics.end_time == 0) {
        set_error(TASK_ERROR_NONE);
        return 0.0; // Task not completed
    }
    
    double execution_time = difftime(task->metrics.end_time, task->metrics.start_time);
    set_error(TASK_ERROR_NONE);
    return execution_time;
}

// Retry logic
bool task_entity_can_retry(const TaskEntity* task) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return false;
    }
    
    set_error(TASK_ERROR_NONE);
    return task->retry_count < task->max_retries;
}

bool task_entity_increment_retry(TaskEntity* task) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return false;
    }
    
    if (!task_entity_can_retry(task)) {
        set_error(TASK_ERROR_INVALID_STATE);
        return false;
    }
    
    task->retry_count++;
    task->last_modified = time(NULL);
    task_entity_update_checksum(task);
    
    set_error(TASK_ERROR_NONE);
    return true;
}

uint32_t task_entity_get_retry_count(const TaskEntity* task) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return 0;
    }
    
    set_error(TASK_ERROR_NONE);
    return task->retry_count;
}

// Dependency management
bool task_entity_set_dependent_task(TaskEntity* task, TaskEntity* dependent) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return false;
    }
    
    // Check for dependency cycles
    TaskEntity* current = dependent;
    while (current) {
        if (current == task) {
            set_error(TASK_ERROR_DEPENDENCY_CYCLE);
            return false;
        }
        current = current->dependent_task;
    }
    
    task->dependent_task = dependent;
    task->last_modified = time(NULL);
    task_entity_update_checksum(task);
    
    set_error(TASK_ERROR_NONE);
    return true;
}

TaskEntity* task_entity_get_dependent_task(const TaskEntity* task) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    set_error(TASK_ERROR_NONE);
    return task->dependent_task;
}

bool task_entity_set_next_task(TaskEntity* task, TaskEntity* next) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return false;
    }
    
    task->next_task = next;
    task->last_modified = time(NULL);
    task_entity_update_checksum(task);
    
    set_error(TASK_ERROR_NONE);
    return true;
}

TaskEntity* task_entity_get_next_task(const TaskEntity* task) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    set_error(TASK_ERROR_NONE);
    return task->next_task;
}

// Validation and security
bool task_entity_is_valid(const TaskEntity* task) {
    return validate_task(task);
}

bool task_entity_verify_checksum(const TaskEntity* task) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return false;
    }
    
    uint64_t calculated = task_entity_calculate_checksum(task);
    bool valid = (calculated == task->checksum);
    
    if (!valid) {
        set_error(TASK_ERROR_CHECKSUM_MISMATCH);
    } else {
        set_error(TASK_ERROR_NONE);
    }
    
    return valid;
}

uint64_t task_entity_calculate_checksum(const TaskEntity* task) {
    if (!validate_task(task)) {
        return 0;
    }
    
    // Calculate checksum over all critical fields
    uint64_t checksum = 0;
    
    // Include immutable fields
    checksum ^= task->id;
    checksum ^= (uint64_t)task->type << 32;
    checksum ^= (uint64_t)task->priority << 40;
    checksum ^= task->block_offset;
    checksum ^= (uint64_t)task->data_size << 32;
    
    // Include mutable state
    checksum ^= (uint64_t)task->state << 48;
    checksum ^= (uint64_t)task->retry_count << 56;
    checksum ^= task->created_time;
    checksum ^= task->last_modified;
    
    // Include data buffer if present
    if (task->data_buffer && task->buffer_size > 0) {
        checksum ^= calculate_fnv1a_checksum(task->data_buffer, 
                                            task->buffer_size > 64 ? 64 : task->buffer_size);
    }
    
    return checksum;
}

bool task_entity_update_checksum(TaskEntity* task) {
    if (!validate_task(task)) {
        set_error(TASK_ERROR_INVALID_PARAM);
        return false;
    }
    
    task->checksum = task_entity_calculate_checksum(task);
    set_error(TASK_ERROR_NONE);
    return true;
}

// Utility functions
const char* task_entity_type_to_string(TaskType type) {
    switch (type) {
        case TASK_TYPE_READ: return "READ";
        case TASK_TYPE_WRITE: return "WRITE";
        case TASK_TYPE_COMPRESS: return "COMPRESS";
        case TASK_TYPE_DECOMPRESS: return "DECOMPRESS";
        case TASK_TYPE_CACHE_UPDATE: return "CACHE_UPDATE";
        case TASK_TYPE_PREFETCH: return "PREFETCH";
        default: return "UNKNOWN";
    }
}

const char* task_entity_priority_to_string(TaskPriority priority) {
    switch (priority) {
        case TASK_PRIORITY_LOW: return "LOW";
        case TASK_PRIORITY_NORMAL: return "NORMAL";
        case TASK_PRIORITY_HIGH: return "HIGH";
        case TASK_PRIORITY_CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

const char* task_entity_state_to_string(TaskState state) {
    switch (state) {
        case TASK_STATE_PENDING: return "PENDING";
        case TASK_STATE_RUNNING: return "RUNNING";
        case TASK_STATE_COMPLETED: return "COMPLETED";
        case TASK_STATE_FAILED: return "FAILED";
        case TASK_STATE_CANCELLED: return "CANCELLED";
        default: return "UNKNOWN";
    }
}

uint64_t task_entity_generate_id(void) {
    static uint64_t next_id = 1;
    return __sync_fetch_and_add(&next_id, 1);
}

// Task comparison for sorting
int task_entity_compare(const TaskEntity* task1, const TaskEntity* task2) {
    if (!validate_task(task1) || !validate_task(task2)) {
        return 0;
    }
    
    // First compare by priority (higher priority first)
    int priority_diff = task_entity_compare_priority(task1, task2);
    if (priority_diff != 0) {
        return -priority_diff; // Higher priority first
    }
    
    // Then compare by creation time (older first)
    if (task1->created_time != task2->created_time) {
        return (task1->created_time < task2->created_time) ? -1 : 1;
    }
    
    // Finally compare by ID
    if (task1->id != task2->id) {
        return (task1->id < task2->id) ? -1 : 1;
    }
    
    return 0;
} 