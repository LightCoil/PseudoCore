#ifndef STORAGE_ENGINE_H
#define STORAGE_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <pthread.h>

// Forward declarations
typedef struct BlockEntity BlockEntity;

// Storage access modes
typedef enum {
    STORAGE_ACCESS_READ_ONLY = 0,
    STORAGE_ACCESS_READ_WRITE,
    STORAGE_ACCESS_WRITE_ONLY,
    STORAGE_ACCESS_APPEND
} StorageAccessMode;

// Storage operation types
typedef enum {
    STORAGE_OP_READ = 0,
    STORAGE_OP_WRITE,
    STORAGE_OP_DELETE,
    STORAGE_OP_TRUNCATE,
    STORAGE_OP_SYNC,
    STORAGE_OP_VERIFY
} StorageOperation;

// Storage error codes
typedef enum {
    STORAGE_ERROR_NONE = 0,
    STORAGE_ERROR_FILE_NOT_FOUND,
    STORAGE_ERROR_PERMISSION_DENIED,
    STORAGE_ERROR_DISK_FULL,
    STORAGE_ERROR_IO_ERROR,
    STORAGE_ERROR_CORRUPTION,
    STORAGE_ERROR_TIMEOUT,
    STORAGE_ERROR_INVALID_PARAMETER,
    STORAGE_ERROR_BUFFER_TOO_SMALL,
    STORAGE_ERROR_ALREADY_EXISTS,
    STORAGE_ERROR_NOT_EMPTY,
    STORAGE_ERROR_BUSY
} StorageError;

// Storage performance metrics
typedef struct {
    uint64_t total_operations;
    uint64_t read_operations;
    uint64_t write_operations;
    uint64_t delete_operations;
    uint64_t sync_operations;
    
    uint64_t total_bytes_read;
    uint64_t total_bytes_written;
    uint64_t total_bytes_deleted;
    
    double average_read_speed_mbps;
    double average_write_speed_mbps;
    double average_latency_ms;
    
    uint64_t failed_operations;
    uint64_t corruption_errors;
    uint64_t timeout_errors;
    
    time_t last_reset;
    time_t last_operation;
} StorageMetrics;

// Storage configuration
typedef struct {
    char* file_path;
    StorageAccessMode access_mode;
    size_t block_size;
    size_t buffer_size;
    uint32_t max_concurrent_operations;
    bool enable_checksum_validation;
    bool enable_async_io;
    bool enable_direct_io;
    uint32_t operation_timeout_ms;
    uint32_t retry_count;
    uint32_t retry_delay_ms;
} StorageConfig;

// Storage operation result
typedef struct {
    bool success;
    StorageOperation operation;
    StorageError error_code;
    size_t bytes_processed;
    double operation_time_ms;
    uint64_t checksum;
    time_t timestamp;
    char error_message[256];
} StorageResult;

// Storage engine interface
typedef struct StorageEngine {
    // Configuration
    StorageConfig config;
    
    // File handle
    int file_descriptor;
    bool is_open;
    
    // Statistics
    StorageMetrics metrics;
    
    // Synchronization
    pthread_mutex_t operation_mutex;
    pthread_mutex_t metrics_mutex;
    pthread_cond_t operation_condition;
    
    // State
    bool is_initialized;
    uint64_t file_size;
    uint64_t current_position;
    
    // Error tracking
    StorageError last_error;
    char last_error_message[256];
    
    // Background operations
    pthread_t sync_thread;
    bool sync_running;
} StorageEngine;

// Storage engine interface functions
StorageEngine* storage_engine_create(const StorageConfig* config);
void storage_engine_destroy(StorageEngine* engine);

// Core storage operations
StorageResult storage_engine_read(StorageEngine* engine, uint64_t offset, 
                                 void* buffer, size_t size);
StorageResult storage_engine_write(StorageEngine* engine, uint64_t offset,
                                  const void* data, size_t size);
StorageResult storage_engine_delete(StorageEngine* engine, uint64_t offset, size_t size);
StorageResult storage_engine_truncate(StorageEngine* engine, uint64_t new_size);

// Block-based operations
StorageResult storage_engine_read_block(StorageEngine* engine, uint64_t block_offset,
                                       BlockEntity* block);
StorageResult storage_engine_write_block(StorageEngine* engine, uint64_t block_offset,
                                        const BlockEntity* block);
StorageResult storage_engine_delete_block(StorageEngine* engine, uint64_t block_offset);

// Batch operations
bool storage_engine_read_batch(StorageEngine* engine, uint64_t* offsets, size_t* sizes,
                              void** buffers, size_t count, StorageResult* results);
bool storage_engine_write_batch(StorageEngine* engine, uint64_t* offsets, size_t* sizes,
                               const void** data, size_t count, StorageResult* results);

// Synchronization operations
StorageResult storage_engine_sync(StorageEngine* engine);
StorageResult storage_engine_sync_range(StorageEngine* engine, uint64_t offset, size_t size);
bool storage_engine_start_sync_thread(StorageEngine* engine);
bool storage_engine_stop_sync_thread(StorageEngine* engine);

// Integrity and verification
StorageResult storage_engine_verify_integrity(StorageEngine* engine, uint64_t offset, size_t size);
StorageResult storage_engine_verify_checksum(StorageEngine* engine, uint64_t offset, 
                                            size_t size, uint64_t expected_checksum);
bool storage_engine_repair_corruption(StorageEngine* engine, uint64_t offset, size_t size);

// File management
bool storage_engine_open(StorageEngine* engine);
bool storage_engine_close(StorageEngine* engine);
bool storage_engine_is_open(const StorageEngine* engine);
uint64_t storage_engine_get_file_size(const StorageEngine* engine);
bool storage_engine_set_file_size(StorageEngine* engine, uint64_t size);

// Performance and monitoring
StorageMetrics storage_engine_get_metrics(const StorageEngine* engine);
void storage_engine_reset_metrics(StorageEngine* engine);
bool storage_engine_print_stats(const StorageEngine* engine, FILE* stream);

// Configuration management
bool storage_engine_update_config(StorageEngine* engine, const StorageConfig* new_config);
StorageConfig storage_engine_get_config(const StorageEngine* engine);
bool storage_engine_validate_config(const StorageConfig* config);

// Error handling
StorageError storage_engine_get_last_error(const StorageEngine* engine);
const char* storage_engine_get_last_error_message(const StorageEngine* engine);
const char* storage_engine_error_to_string(StorageError error);

// Utility functions
bool storage_engine_is_offset_valid(const StorageEngine* engine, uint64_t offset, size_t size);
uint64_t storage_engine_calculate_checksum(const void* data, size_t size);
bool storage_engine_validate_buffer(const void* buffer, size_t size);

// Advanced operations
StorageResult storage_engine_copy_range(StorageEngine* engine, uint64_t src_offset,
                                       uint64_t dst_offset, size_t size);
StorageResult storage_engine_zero_range(StorageEngine* engine, uint64_t offset, size_t size);
StorageResult storage_engine_compare_range(StorageEngine* engine, uint64_t offset1,
                                          uint64_t offset2, size_t size);

// Threading support
bool storage_engine_lock_operation(StorageEngine* engine);
bool storage_engine_unlock_operation(StorageEngine* engine);
bool storage_engine_wait_for_operation(StorageEngine* engine, uint32_t timeout_ms);

// Memory management
size_t storage_engine_get_optimal_buffer_size(const StorageEngine* engine);
bool storage_engine_allocate_buffer(StorageEngine* engine, size_t size);
void storage_engine_free_buffer(StorageEngine* engine);

// Validation
bool storage_engine_validate_engine(const StorageEngine* engine);
bool storage_engine_check_permissions(const StorageEngine* engine);
bool storage_engine_validate_operation(const StorageEngine* engine, StorageOperation op,
                                      uint64_t offset, size_t size);

#endif // STORAGE_ENGINE_H 