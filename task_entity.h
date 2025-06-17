#ifndef TASK_ENTITY_H
#define TASK_ENTITY_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Forward declarations
typedef struct BlockEntity BlockEntity;

// Task priority levels
typedef enum {
    TASK_PRIORITY_LOW = 0,
    TASK_PRIORITY_NORMAL,
    TASK_PRIORITY_HIGH,
    TASK_PRIORITY_CRITICAL
} TaskPriority;

// Task state enumeration
typedef enum {
    TASK_STATE_PENDING = 0,
    TASK_STATE_RUNNING,
    TASK_STATE_COMPLETED,
    TASK_STATE_FAILED,
    TASK_STATE_CANCELLED
} TaskState;

// Task type enumeration
typedef enum {
    TASK_TYPE_READ = 0,
    TASK_TYPE_WRITE,
    TASK_TYPE_COMPRESS,
    TASK_TYPE_DECOMPRESS,
    TASK_TYPE_CACHE_UPDATE,
    TASK_TYPE_PREFETCH
} TaskType;

// Task performance metrics
typedef struct {
    uint64_t bytes_processed;
    uint64_t operations_performed;
    time_t start_time;
    time_t end_time;
    double compression_ratio;
    uint64_t cache_hits;
    uint64_t cache_misses;
} TaskMetrics;

// Task entity with full encapsulation
typedef struct TaskEntity {
    // Immutable properties
    const uint64_t id;
    const TaskType type;
    const TaskPriority priority;
    const uint64_t block_offset;
    const size_t data_size;
    
    // Mutable state
    TaskState state;
    uint32_t retry_count;
    uint32_t max_retries;
    
    // Performance tracking
    TaskMetrics metrics;
    
    // Resource management
    BlockEntity* target_block;
    void* data_buffer;
    size_t buffer_size;
    
    // Dependencies and relationships
    TaskEntity* dependent_task;
    TaskEntity* next_task;
    
    // Validation and security
    uint64_t checksum;
    bool is_initialized;
    time_t created_time;
    time_t last_modified;
} TaskEntity;

// Task entity interface functions
TaskEntity* task_entity_create(uint64_t id, TaskType type, TaskPriority priority, 
                              uint64_t block_offset, size_t data_size);
void task_entity_destroy(TaskEntity* task);

// State management
TaskState task_entity_get_state(const TaskEntity* task);
bool task_entity_set_state(TaskEntity* task, TaskState new_state);
bool task_entity_is_completed(const TaskEntity* task);
bool task_entity_is_failed(const TaskEntity* task);

// Priority management
TaskPriority task_entity_get_priority(const TaskEntity* task);
bool task_entity_set_priority(TaskEntity* task, TaskPriority new_priority);
int task_entity_compare_priority(const TaskEntity* task1, const TaskEntity* task2);

// Data management
bool task_entity_set_data_buffer(TaskEntity* task, void* buffer, size_t size);
void* task_entity_get_data_buffer(const TaskEntity* task);
size_t task_entity_get_buffer_size(const TaskEntity* task);
bool task_entity_resize_buffer(TaskEntity* task, size_t new_size);

// Block management
bool task_entity_set_target_block(TaskEntity* task, BlockEntity* block);
BlockEntity* task_entity_get_target_block(const TaskEntity* task);

// Metrics management
void task_entity_update_metrics(TaskEntity* task, const TaskMetrics* delta);
TaskMetrics task_entity_get_metrics(const TaskEntity* task);
void task_entity_reset_metrics(TaskEntity* task);
double task_entity_get_execution_time(const TaskEntity* task);

// Retry logic
bool task_entity_can_retry(const TaskEntity* task);
bool task_entity_increment_retry(TaskEntity* task);
uint32_t task_entity_get_retry_count(const TaskEntity* task);

// Dependency management
bool task_entity_set_dependent_task(TaskEntity* task, TaskEntity* dependent);
TaskEntity* task_entity_get_dependent_task(const TaskEntity* task);
bool task_entity_set_next_task(TaskEntity* task, TaskEntity* next);
TaskEntity* task_entity_get_next_task(const TaskEntity* task);

// Validation and security
bool task_entity_is_valid(const TaskEntity* task);
bool task_entity_verify_checksum(const TaskEntity* task);
uint64_t task_entity_calculate_checksum(const TaskEntity* task);
bool task_entity_update_checksum(TaskEntity* task);

// Utility functions
const char* task_entity_type_to_string(TaskType type);
const char* task_entity_priority_to_string(TaskPriority priority);
const char* task_entity_state_to_string(TaskState state);
uint64_t task_entity_generate_id(void);

// Task comparison for sorting
int task_entity_compare(const TaskEntity* task1, const TaskEntity* task2);

#endif // TASK_ENTITY_H 