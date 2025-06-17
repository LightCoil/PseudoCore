#include "storage_engine.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zlib.h>

// Internal constants
#define DEFAULT_BUFFER_SIZE (64 * 1024) // 64KB
#define MAX_RETRY_COUNT 3
#define DEFAULT_TIMEOUT_MS 5000
#define SYNC_INTERVAL_SECONDS 30
#define MIN_BLOCK_SIZE 512
#define MAX_BLOCK_SIZE (1024ULL * 1024ULL * 1024ULL) // 1GB

// Internal error codes
typedef enum {
    STORAGE_ERROR_NONE = 0,
    STORAGE_ERROR_INVALID_PARAM,
    STORAGE_ERROR_FILE_NOT_FOUND,
    STORAGE_ERROR_PERMISSION_DENIED,
    STORAGE_ERROR_DISK_FULL,
    STORAGE_ERROR_IO_ERROR,
    STORAGE_ERROR_CORRUPTION,
    STORAGE_ERROR_TIMEOUT,
    STORAGE_ERROR_BUFFER_TOO_SMALL,
    STORAGE_ERROR_ALREADY_EXISTS,
    STORAGE_ERROR_NOT_EMPTY,
    STORAGE_ERROR_BUSY
} StorageError;

// Internal error tracking
static StorageError last_error = STORAGE_ERROR_NONE;

// Error handling
static void set_error(StorageError error) {
    last_error = error;
}

StorageError storage_engine_get_last_error(void) {
    return last_error;
}

const char* storage_engine_error_to_string(StorageError error) {
    switch (error) {
        case STORAGE_ERROR_NONE: return "No error";
        case STORAGE_ERROR_INVALID_PARAM: return "Invalid parameter";
        case STORAGE_ERROR_FILE_NOT_FOUND: return "File not found";
        case STORAGE_ERROR_PERMISSION_DENIED: return "Permission denied";
        case STORAGE_ERROR_DISK_FULL: return "Disk full";
        case STORAGE_ERROR_IO_ERROR: return "I/O error";
        case STORAGE_ERROR_CORRUPTION: return "Data corruption";
        case STORAGE_ERROR_TIMEOUT: return "Operation timeout";
        case STORAGE_ERROR_BUFFER_TOO_SMALL: return "Buffer too small";
        case STORAGE_ERROR_ALREADY_EXISTS: return "Already exists";
        case STORAGE_ERROR_NOT_EMPTY: return "Not empty";
        case STORAGE_ERROR_BUSY: return "Resource busy";
        default: return "Unknown error";
    }
}

// Validation functions
static bool validate_engine(const StorageEngine* engine) {
    return engine != NULL && engine->is_initialized;
}

static bool validate_config(const StorageConfig* config) {
    return config != NULL && 
           config->file_path != NULL &&
           config->block_size >= MIN_BLOCK_SIZE &&
           config->block_size <= MAX_BLOCK_SIZE &&
           config->buffer_size > 0 &&
           config->max_concurrent_operations > 0;
}

static bool validate_operation(const StorageEngine* engine, StorageOperation op,
                              uint64_t offset, size_t size) {
    if (!validate_engine(engine)) {
        return false;
    }
    
    switch (op) {
        case STORAGE_OP_READ:
        case STORAGE_OP_WRITE:
        case STORAGE_OP_DELETE:
            return size > 0 && size <= MAX_BLOCK_SIZE;
        case STORAGE_OP_TRUNCATE:
        case STORAGE_OP_SYNC:
        case STORAGE_OP_VERIFY:
            return true;
        default:
            return false;
    }
}

// Checksum calculation using CRC32
static uint32_t calculate_crc32(const void* data, size_t size) {
    return crc32(0, (const Bytef*)data, size);
}

// Storage engine creation
StorageEngine* storage_engine_create(const StorageConfig* config) {
    if (!validate_config(config)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    StorageEngine* engine = malloc(sizeof(StorageEngine));
    if (!engine) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    // Initialize configuration
    engine->config = *config;
    
    // Initialize file handle
    engine->file_descriptor = -1;
    engine->is_open = false;
    
    // Initialize statistics
    memset(&engine->metrics, 0, sizeof(StorageMetrics));
    engine->metrics.last_reset = time(NULL);
    
    // Initialize synchronization
    if (pthread_mutex_init(&engine->operation_mutex, NULL) != 0 ||
        pthread_mutex_init(&engine->metrics_mutex, NULL) != 0) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        free(engine);
        return NULL;
    }
    
    if (pthread_cond_init(&engine->operation_condition, NULL) != 0) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        pthread_mutex_destroy(&engine->operation_mutex);
        pthread_mutex_destroy(&engine->metrics_mutex);
        free(engine);
        return NULL;
    }
    
    // Initialize state
    engine->is_initialized = true;
    engine->file_size = 0;
    engine->current_position = 0;
    
    // Initialize error tracking
    engine->last_error = STORAGE_ERROR_NONE;
    memset(engine->last_error_message, 0, sizeof(engine->last_error_message));
    
    // Initialize background operations
    engine->sync_running = false;
    
    set_error(STORAGE_ERROR_NONE);
    return engine;
}

// Storage engine destruction
void storage_engine_destroy(StorageEngine* engine) {
    if (!engine) return;
    
    // Stop sync thread
    storage_engine_stop_sync_thread(engine);
    
    // Close file if open
    if (engine->is_open) {
        storage_engine_close(engine);
    }
    
    // Destroy synchronization primitives
    pthread_mutex_destroy(&engine->operation_mutex);
    pthread_mutex_destroy(&engine->metrics_mutex);
    pthread_cond_destroy(&engine->operation_condition);
    
    // Free memory
    free(engine);
}

// Core storage operations
StorageResult storage_engine_read(StorageEngine* engine, uint64_t offset, 
                                 void* buffer, size_t size) {
    if (!validate_engine(engine)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return (StorageResult){0};
    }
    
    if (!validate_operation(engine, STORAGE_OP_READ, offset, size)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return (StorageResult){0};
    }
    
    if (!engine->is_open) {
        set_error(STORAGE_ERROR_FILE_NOT_FOUND);
        return (StorageResult){0};
    }
    
    clock_t start_time = clock();
    
    pthread_mutex_lock(&engine->operation_mutex);
    
    // Seek to position
    off_t seek_result = lseek(engine->file_descriptor, offset, SEEK_SET);
    if (seek_result == (off_t)-1) {
        pthread_mutex_unlock(&engine->operation_mutex);
        set_error(STORAGE_ERROR_IO_ERROR);
        return (StorageResult){0};
    }
    
    // Read data
    ssize_t bytes_read = 0;
    size_t total_read = 0;
    size_t remaining = size;
    char* buffer_ptr = (char*)buffer;
    
    while (remaining > 0) {
        bytes_read = read(engine->file_descriptor, buffer_ptr + total_read, remaining);
        
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue; // Interrupted, retry
            }
            pthread_mutex_unlock(&engine->operation_mutex);
            set_error(STORAGE_ERROR_IO_ERROR);
            return (StorageResult){0};
        } else if (bytes_read == 0) {
            break; // End of file
        }
        
        total_read += bytes_read;
        remaining -= bytes_read;
    }
    
    pthread_mutex_unlock(&engine->operation_mutex);
    
    clock_t end_time = clock();
    double time_ms = ((double)(end_time - start_time)) / CLOCKS_PER_SEC * 1000.0;
    
    StorageResult result = {0};
    result.success = true;
    result.operation = STORAGE_OP_READ;
    result.error_code = STORAGE_ERROR_NONE;
    result.bytes_processed = total_read;
    result.operation_time_ms = time_ms;
    result.checksum = calculate_crc32(buffer, total_read);
    result.timestamp = time(NULL);
    
    // Update metrics
    pthread_mutex_lock(&engine->metrics_mutex);
    engine->metrics.total_operations++;
    engine->metrics.read_operations++;
    engine->metrics.total_bytes_read += total_read;
    engine->metrics.average_latency_ms = 
        (engine->metrics.average_latency_ms * (engine->metrics.total_operations - 1) + time_ms) /
        engine->metrics.total_operations;
    engine->metrics.average_read_speed_mbps = 
        (total_read / 1024.0 / 1024.0) / (time_ms / 1000.0);
    engine->metrics.last_operation = time(NULL);
    pthread_mutex_unlock(&engine->metrics_mutex);
    
    set_error(STORAGE_ERROR_NONE);
    return result;
}

StorageResult storage_engine_write(StorageEngine* engine, uint64_t offset,
                                  const void* data, size_t size) {
    if (!validate_engine(engine)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return (StorageResult){0};
    }
    
    if (!validate_operation(engine, STORAGE_OP_WRITE, offset, size)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return (StorageResult){0};
    }
    
    if (!engine->is_open) {
        set_error(STORAGE_ERROR_FILE_NOT_FOUND);
        return (StorageResult){0};
    }
    
    clock_t start_time = clock();
    
    pthread_mutex_lock(&engine->operation_mutex);
    
    // Seek to position
    off_t seek_result = lseek(engine->file_descriptor, offset, SEEK_SET);
    if (seek_result == (off_t)-1) {
        pthread_mutex_unlock(&engine->operation_mutex);
        set_error(STORAGE_ERROR_IO_ERROR);
        return (StorageResult){0};
    }
    
    // Write data
    ssize_t bytes_written = 0;
    size_t total_written = 0;
    size_t remaining = size;
    const char* data_ptr = (const char*)data;
    
    while (remaining > 0) {
        bytes_written = write(engine->file_descriptor, data_ptr + total_written, remaining);
        
        if (bytes_written < 0) {
            if (errno == EINTR) {
                continue; // Interrupted, retry
            }
            pthread_mutex_unlock(&engine->operation_mutex);
            set_error(STORAGE_ERROR_IO_ERROR);
            return (StorageResult){0};
        }
        
        total_written += bytes_written;
        remaining -= bytes_written;
    }
    
    // Update file size if necessary
    if (offset + total_written > engine->file_size) {
        engine->file_size = offset + total_written;
    }
    
    pthread_mutex_unlock(&engine->operation_mutex);
    
    clock_t end_time = clock();
    double time_ms = ((double)(end_time - start_time)) / CLOCKS_PER_SEC * 1000.0;
    
    StorageResult result = {0};
    result.success = true;
    result.operation = STORAGE_OP_WRITE;
    result.error_code = STORAGE_ERROR_NONE;
    result.bytes_processed = total_written;
    result.operation_time_ms = time_ms;
    result.checksum = calculate_crc32(data, total_written);
    result.timestamp = time(NULL);
    
    // Update metrics
    pthread_mutex_lock(&engine->metrics_mutex);
    engine->metrics.total_operations++;
    engine->metrics.write_operations++;
    engine->metrics.total_bytes_written += total_written;
    engine->metrics.average_latency_ms = 
        (engine->metrics.average_latency_ms * (engine->metrics.total_operations - 1) + time_ms) /
        engine->metrics.total_operations;
    engine->metrics.average_write_speed_mbps = 
        (total_written / 1024.0 / 1024.0) / (time_ms / 1000.0);
    engine->metrics.last_operation = time(NULL);
    pthread_mutex_unlock(&engine->metrics_mutex);
    
    set_error(STORAGE_ERROR_NONE);
    return result;
}

StorageResult storage_engine_delete(StorageEngine* engine, uint64_t offset, size_t size) {
    if (!validate_engine(engine)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return (StorageResult){0};
    }
    
    if (!validate_operation(engine, STORAGE_OP_DELETE, offset, size)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return (StorageResult){0};
    }
    
    if (!engine->is_open) {
        set_error(STORAGE_ERROR_FILE_NOT_FOUND);
        return (StorageResult){0};
    }
    
    clock_t start_time = clock();
    
    pthread_mutex_lock(&engine->operation_mutex);
    
    // For file-based storage, "delete" means zeroing out the range
    void* zero_buffer = calloc(1, engine->config.block_size);
    if (!zero_buffer) {
        pthread_mutex_unlock(&engine->operation_mutex);
        set_error(STORAGE_ERROR_MEMORY_ALLOCATION);
        return (StorageResult){0};
    }
    
    size_t total_zeroed = 0;
    size_t remaining = size;
    uint64_t current_offset = offset;
    
    while (remaining > 0) {
        size_t chunk_size = (remaining < engine->config.block_size) ? remaining : engine->config.block_size;
        
        // Seek to position
        off_t seek_result = lseek(engine->file_descriptor, current_offset, SEEK_SET);
        if (seek_result == (off_t)-1) {
            free(zero_buffer);
            pthread_mutex_unlock(&engine->operation_mutex);
            set_error(STORAGE_ERROR_IO_ERROR);
            return (StorageResult){0};
        }
        
        // Write zeros
        ssize_t bytes_written = write(engine->file_descriptor, zero_buffer, chunk_size);
        if (bytes_written < 0) {
            free(zero_buffer);
            pthread_mutex_unlock(&engine->operation_mutex);
            set_error(STORAGE_ERROR_IO_ERROR);
            return (StorageResult){0};
        }
        
        total_zeroed += bytes_written;
        remaining -= bytes_written;
        current_offset += bytes_written;
    }
    
    free(zero_buffer);
    pthread_mutex_unlock(&engine->operation_mutex);
    
    clock_t end_time = clock();
    double time_ms = ((double)(end_time - start_time)) / CLOCKS_PER_SEC * 1000.0;
    
    StorageResult result = {0};
    result.success = true;
    result.operation = STORAGE_OP_DELETE;
    result.error_code = STORAGE_ERROR_NONE;
    result.bytes_processed = total_zeroed;
    result.operation_time_ms = time_ms;
    result.checksum = 0; // Zero checksum for deleted data
    result.timestamp = time(NULL);
    
    // Update metrics
    pthread_mutex_lock(&engine->metrics_mutex);
    engine->metrics.total_operations++;
    engine->metrics.delete_operations++;
    engine->metrics.total_bytes_deleted += total_zeroed;
    engine->metrics.last_operation = time(NULL);
    pthread_mutex_unlock(&engine->metrics_mutex);
    
    set_error(STORAGE_ERROR_NONE);
    return result;
}

StorageResult storage_engine_truncate(StorageEngine* engine, uint64_t new_size) {
    if (!validate_engine(engine)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return (StorageResult){0};
    }
    
    if (!engine->is_open) {
        set_error(STORAGE_ERROR_FILE_NOT_FOUND);
        return (StorageResult){0};
    }
    
    clock_t start_time = clock();
    
    pthread_mutex_lock(&engine->operation_mutex);
    
    int result_code = ftruncate(engine->file_descriptor, new_size);
    
    pthread_mutex_unlock(&engine->operation_mutex);
    
    if (result_code < 0) {
        set_error(STORAGE_ERROR_IO_ERROR);
        return (StorageResult){0};
    }
    
    engine->file_size = new_size;
    
    clock_t end_time = clock();
    double time_ms = ((double)(end_time - start_time)) / CLOCKS_PER_SEC * 1000.0;
    
    StorageResult result = {0};
    result.success = true;
    result.operation = STORAGE_OP_TRUNCATE;
    result.error_code = STORAGE_ERROR_NONE;
    result.bytes_processed = 0;
    result.operation_time_ms = time_ms;
    result.checksum = 0;
    result.timestamp = time(NULL);
    
    // Update metrics
    pthread_mutex_lock(&engine->metrics_mutex);
    engine->metrics.total_operations++;
    engine->metrics.last_operation = time(NULL);
    pthread_mutex_unlock(&engine->metrics_mutex);
    
    set_error(STORAGE_ERROR_NONE);
    return result;
}

// Block-based operations
StorageResult storage_engine_read_block(StorageEngine* engine, uint64_t block_offset,
                                       BlockEntity* block) {
    if (!validate_engine(engine) || !block) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return (StorageResult){0};
    }
    
    size_t block_size = block_entity_get_data_size(block);
    if (block_size == 0) {
        block_size = engine->config.block_size;
    }
    
    void* buffer = malloc(block_size);
    if (!buffer) {
        set_error(STORAGE_ERROR_MEMORY_ALLOCATION);
        return (StorageResult){0};
    }
    
    StorageResult result = storage_engine_read(engine, block_offset, buffer, block_size);
    
    if (result.success) {
        block_entity_set_data(block, buffer, result.bytes_processed);
    }
    
    free(buffer);
    return result;
}

StorageResult storage_engine_write_block(StorageEngine* engine, uint64_t block_offset,
                                        const BlockEntity* block) {
    if (!validate_engine(engine) || !block) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return (StorageResult){0};
    }
    
    const void* data = block_entity_get_data(block);
    size_t data_size = block_entity_get_data_size(block);
    
    if (!data || data_size == 0) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return (StorageResult){0};
    }
    
    return storage_engine_write(engine, block_offset, data, data_size);
}

StorageResult storage_engine_delete_block(StorageEngine* engine, uint64_t block_offset) {
    if (!validate_engine(engine)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return (StorageResult){0};
    }
    
    return storage_engine_delete(engine, block_offset, engine->config.block_size);
}

// Batch operations
bool storage_engine_read_batch(StorageEngine* engine, uint64_t* offsets, size_t* sizes,
                              void** buffers, size_t count, StorageResult* results) {
    if (!validate_engine(engine) || !offsets || !sizes || !buffers || !results) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return false;
    }
    
    bool all_success = true;
    for (size_t i = 0; i < count; i++) {
        results[i] = storage_engine_read(engine, offsets[i], buffers[i], sizes[i]);
        if (!results[i].success) {
            all_success = false;
        }
    }
    
    set_error(all_success ? STORAGE_ERROR_NONE : STORAGE_ERROR_IO_ERROR);
    return all_success;
}

bool storage_engine_write_batch(StorageEngine* engine, uint64_t* offsets, size_t* sizes,
                               const void** data, size_t count, StorageResult* results) {
    if (!validate_engine(engine) || !offsets || !sizes || !data || !results) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return false;
    }
    
    bool all_success = true;
    for (size_t i = 0; i < count; i++) {
        results[i] = storage_engine_write(engine, offsets[i], data[i], sizes[i]);
        if (!results[i].success) {
            all_success = false;
        }
    }
    
    set_error(all_success ? STORAGE_ERROR_NONE : STORAGE_ERROR_IO_ERROR);
    return all_success;
}

// Synchronization operations
StorageResult storage_engine_sync(StorageEngine* engine) {
    if (!validate_engine(engine)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return (StorageResult){0};
    }
    
    if (!engine->is_open) {
        set_error(STORAGE_ERROR_FILE_NOT_FOUND);
        return (StorageResult){0};
    }
    
    clock_t start_time = clock();
    
    pthread_mutex_lock(&engine->operation_mutex);
    
    int result_code = fsync(engine->file_descriptor);
    
    pthread_mutex_unlock(&engine->operation_mutex);
    
    if (result_code < 0) {
        set_error(STORAGE_ERROR_IO_ERROR);
        return (StorageResult){0};
    }
    
    clock_t end_time = clock();
    double time_ms = ((double)(end_time - start_time)) / CLOCKS_PER_SEC * 1000.0;
    
    StorageResult result = {0};
    result.success = true;
    result.operation = STORAGE_OP_SYNC;
    result.error_code = STORAGE_ERROR_NONE;
    result.bytes_processed = 0;
    result.operation_time_ms = time_ms;
    result.checksum = 0;
    result.timestamp = time(NULL);
    
    // Update metrics
    pthread_mutex_lock(&engine->metrics_mutex);
    engine->metrics.total_operations++;
    engine->metrics.sync_operations++;
    engine->metrics.last_operation = time(NULL);
    pthread_mutex_unlock(&engine->metrics_mutex);
    
    set_error(STORAGE_ERROR_NONE);
    return result;
}

StorageResult storage_engine_sync_range(StorageEngine* engine, uint64_t offset, size_t size) {
    if (!validate_engine(engine)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return (StorageResult){0};
    }
    
    if (!engine->is_open) {
        set_error(STORAGE_ERROR_FILE_NOT_FOUND);
        return (StorageResult){0};
    }
    
    clock_t start_time = clock();
    
    pthread_mutex_lock(&engine->operation_mutex);
    
    // Note: sync_file_range is Linux-specific
    // For portability, we'll use fsync
    int result_code = fsync(engine->file_descriptor);
    
    pthread_mutex_unlock(&engine->operation_mutex);
    
    if (result_code < 0) {
        set_error(STORAGE_ERROR_IO_ERROR);
        return (StorageResult){0};
    }
    
    clock_t end_time = clock();
    double time_ms = ((double)(end_time - start_time)) / CLOCKS_PER_SEC * 1000.0;
    
    StorageResult result = {0};
    result.success = true;
    result.operation = STORAGE_OP_SYNC;
    result.error_code = STORAGE_ERROR_NONE;
    result.bytes_processed = size;
    result.operation_time_ms = time_ms;
    result.checksum = 0;
    result.timestamp = time(NULL);
    
    set_error(STORAGE_ERROR_NONE);
    return result;
}

// Background sync thread
static void* sync_thread_func(void* arg) {
    StorageEngine* engine = (StorageEngine*)arg;
    
    while (engine->sync_running) {
        sleep(SYNC_INTERVAL_SECONDS);
        
        if (engine->sync_running) {
            storage_engine_sync(engine);
        }
    }
    
    return NULL;
}

bool storage_engine_start_sync_thread(StorageEngine* engine) {
    if (!validate_engine(engine)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return false;
    }
    
    if (engine->sync_running) {
        set_error(STORAGE_ERROR_NONE);
        return true; // Already running
    }
    
    engine->sync_running = true;
    
    if (pthread_create(&engine->sync_thread, NULL, sync_thread_func, engine) != 0) {
        engine->sync_running = false;
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return false;
    }
    
    set_error(STORAGE_ERROR_NONE);
    return true;
}

bool storage_engine_stop_sync_thread(StorageEngine* engine) {
    if (!validate_engine(engine)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return false;
    }
    
    if (!engine->sync_running) {
        set_error(STORAGE_ERROR_NONE);
        return true; // Already stopped
    }
    
    engine->sync_running = false;
    pthread_join(engine->sync_thread, NULL);
    
    set_error(STORAGE_ERROR_NONE);
    return true;
}

// Integrity and verification
StorageResult storage_engine_verify_integrity(StorageEngine* engine, uint64_t offset, size_t size) {
    if (!validate_engine(engine)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return (StorageResult){0};
    }
    
    if (!engine->is_open) {
        set_error(STORAGE_ERROR_FILE_NOT_FOUND);
        return (StorageResult){0};
    }
    
    clock_t start_time = clock();
    
    // Read the data and calculate checksum
    void* buffer = malloc(size);
    if (!buffer) {
        set_error(STORAGE_ERROR_MEMORY_ALLOCATION);
        return (StorageResult){0};
    }
    
    StorageResult read_result = storage_engine_read(engine, offset, buffer, size);
    
    if (!read_result.success) {
        free(buffer);
        return read_result;
    }
    
    uint32_t calculated_checksum = calculate_crc32(buffer, read_result.bytes_processed);
    free(buffer);
    
    clock_t end_time = clock();
    double time_ms = ((double)(end_time - start_time)) / CLOCKS_PER_SEC * 1000.0;
    
    StorageResult result = {0};
    result.success = true;
    result.operation = STORAGE_OP_VERIFY;
    result.error_code = STORAGE_ERROR_NONE;
    result.bytes_processed = read_result.bytes_processed;
    result.operation_time_ms = time_ms;
    result.checksum = calculated_checksum;
    result.timestamp = time(NULL);
    
    set_error(STORAGE_ERROR_NONE);
    return result;
}

StorageResult storage_engine_verify_checksum(StorageEngine* engine, uint64_t offset, 
                                            size_t size, uint64_t expected_checksum) {
    StorageResult result = storage_engine_verify_integrity(engine, offset, size);
    
    if (result.success && result.checksum != expected_checksum) {
        result.success = false;
        result.error_code = STORAGE_ERROR_CORRUPTION;
        set_error(STORAGE_ERROR_CORRUPTION);
    }
    
    return result;
}

bool storage_engine_repair_corruption(StorageEngine* engine, uint64_t offset, size_t size) {
    if (!validate_engine(engine)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return false;
    }
    
    // For now, just zero out corrupted data
    // In a real implementation, you might have redundancy or backup data
    StorageResult result = storage_engine_delete(engine, offset, size);
    
    set_error(result.success ? STORAGE_ERROR_NONE : STORAGE_ERROR_IO_ERROR);
    return result.success;
}

// File management
bool storage_engine_open(StorageEngine* engine) {
    if (!validate_engine(engine)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return false;
    }
    
    if (engine->is_open) {
        set_error(STORAGE_ERROR_NONE);
        return true; // Already open
    }
    
    int flags = O_RDWR;
    if (engine->config.access_mode == STORAGE_ACCESS_READ_ONLY) {
        flags = O_RDONLY;
    } else if (engine->config.access_mode == STORAGE_ACCESS_WRITE_ONLY) {
        flags = O_WRONLY;
    }
    
    if (engine->config.enable_direct_io) {
        flags |= O_DIRECT;
    }
    
    engine->file_descriptor = open(engine->config.file_path, flags);
    
    if (engine->file_descriptor < 0) {
        switch (errno) {
            case ENOENT:
                set_error(STORAGE_ERROR_FILE_NOT_FOUND);
                break;
            case EACCES:
                set_error(STORAGE_ERROR_PERMISSION_DENIED);
                break;
            default:
                set_error(STORAGE_ERROR_IO_ERROR);
                break;
        }
        return false;
    }
    
    // Get file size
    struct stat st;
    if (fstat(engine->file_descriptor, &st) == 0) {
        engine->file_size = st.st_size;
    }
    
    engine->is_open = true;
    
    set_error(STORAGE_ERROR_NONE);
    return true;
}

bool storage_engine_close(StorageEngine* engine) {
    if (!validate_engine(engine)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return false;
    }
    
    if (!engine->is_open) {
        set_error(STORAGE_ERROR_NONE);
        return true; // Already closed
    }
    
    // Sync before closing
    storage_engine_sync(engine);
    
    int result = close(engine->file_descriptor);
    
    if (result < 0) {
        set_error(STORAGE_ERROR_IO_ERROR);
        return false;
    }
    
    engine->file_descriptor = -1;
    engine->is_open = false;
    
    set_error(STORAGE_ERROR_NONE);
    return true;
}

bool storage_engine_is_open(const StorageEngine* engine) {
    if (!validate_engine(engine)) {
        return false;
    }
    
    return engine->is_open;
}

uint64_t storage_engine_get_file_size(const StorageEngine* engine) {
    if (!validate_engine(engine)) {
        return 0;
    }
    
    return engine->file_size;
}

bool storage_engine_set_file_size(StorageEngine* engine, uint64_t size) {
    if (!validate_engine(engine)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return false;
    }
    
    StorageResult result = storage_engine_truncate(engine, size);
    
    set_error(result.success ? STORAGE_ERROR_NONE : STORAGE_ERROR_IO_ERROR);
    return result.success;
}

// Performance and monitoring
StorageMetrics storage_engine_get_metrics(const StorageEngine* engine) {
    StorageMetrics metrics = {0};
    
    if (!validate_engine(engine)) {
        return metrics;
    }
    
    pthread_mutex_lock(&engine->metrics_mutex);
    metrics = engine->metrics;
    pthread_mutex_unlock(&engine->metrics_mutex);
    
    return metrics;
}

void storage_engine_reset_metrics(StorageEngine* engine) {
    if (!validate_engine(engine)) {
        return;
    }
    
    pthread_mutex_lock(&engine->metrics_mutex);
    memset(&engine->metrics, 0, sizeof(StorageMetrics));
    engine->metrics.last_reset = time(NULL);
    pthread_mutex_unlock(&engine->metrics_mutex);
}

bool storage_engine_print_stats(const StorageEngine* engine, FILE* stream) {
    if (!validate_engine(engine) || !stream) {
        return false;
    }
    
    StorageMetrics metrics = storage_engine_get_metrics(engine);
    
    fprintf(stream, "Storage Statistics:\n");
    fprintf(stream, "  Total Operations: %lu\n", metrics.total_operations);
    fprintf(stream, "  Read Operations: %lu\n", metrics.read_operations);
    fprintf(stream, "  Write Operations: %lu\n", metrics.write_operations);
    fprintf(stream, "  Delete Operations: %lu\n", metrics.delete_operations);
    fprintf(stream, "  Sync Operations: %lu\n", metrics.sync_operations);
    fprintf(stream, "  Total Bytes Read: %lu\n", metrics.total_bytes_read);
    fprintf(stream, "  Total Bytes Written: %lu\n", metrics.total_bytes_written);
    fprintf(stream, "  Total Bytes Deleted: %lu\n", metrics.total_bytes_deleted);
    fprintf(stream, "  Average Read Speed: %.2f MB/s\n", metrics.average_read_speed_mbps);
    fprintf(stream, "  Average Write Speed: %.2f MB/s\n", metrics.average_write_speed_mbps);
    fprintf(stream, "  Average Latency: %.2f ms\n", metrics.average_latency_ms);
    fprintf(stream, "  Failed Operations: %lu\n", metrics.failed_operations);
    fprintf(stream, "  Corruption Errors: %lu\n", metrics.corruption_errors);
    fprintf(stream, "  Timeout Errors: %lu\n", metrics.timeout_errors);
    
    return true;
}

// Configuration management
bool storage_engine_update_config(StorageEngine* engine, const StorageConfig* new_config) {
    if (!validate_engine(engine) || !validate_config(new_config)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return false;
    }
    
    engine->config = *new_config;
    
    set_error(STORAGE_ERROR_NONE);
    return true;
}

StorageConfig storage_engine_get_config(const StorageEngine* engine) {
    StorageConfig config = {0};
    
    if (!validate_engine(engine)) {
        return config;
    }
    
    return engine->config;
}

bool storage_engine_validate_config(const StorageConfig* config) {
    return validate_config(config);
}

// Error handling
StorageError storage_engine_get_last_error(const StorageEngine* engine) {
    if (!validate_engine(engine)) {
        return STORAGE_ERROR_INVALID_PARAM;
    }
    
    return engine->last_error;
}

const char* storage_engine_get_last_error_message(const StorageEngine* engine) {
    if (!validate_engine(engine)) {
        return "Invalid engine";
    }
    
    return engine->last_error_message;
}

const char* storage_engine_error_to_string(StorageError error) {
    return storage_engine_error_to_string(error);
}

// Utility functions
bool storage_engine_is_offset_valid(const StorageEngine* engine, uint64_t offset, size_t size) {
    if (!validate_engine(engine)) {
        return false;
    }
    
    return offset + size <= engine->file_size;
}

uint64_t storage_engine_calculate_checksum(const void* data, size_t size) {
    return calculate_crc32(data, size);
}

bool storage_engine_validate_buffer(const void* buffer, size_t size) {
    return buffer != NULL && size > 0 && size <= MAX_BLOCK_SIZE;
}

// Advanced operations
StorageResult storage_engine_copy_range(StorageEngine* engine, uint64_t src_offset,
                                       uint64_t dst_offset, size_t size) {
    if (!validate_engine(engine)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return (StorageResult){0};
    }
    
    void* buffer = malloc(size);
    if (!buffer) {
        set_error(STORAGE_ERROR_MEMORY_ALLOCATION);
        return (StorageResult){0};
    }
    
    // Read from source
    StorageResult read_result = storage_engine_read(engine, src_offset, buffer, size);
    if (!read_result.success) {
        free(buffer);
        return read_result;
    }
    
    // Write to destination
    StorageResult write_result = storage_engine_write(engine, dst_offset, buffer, read_result.bytes_processed);
    
    free(buffer);
    
    if (!write_result.success) {
        return write_result;
    }
    
    // Return combined result
    StorageResult result = {0};
    result.success = true;
    result.operation = STORAGE_OP_WRITE;
    result.error_code = STORAGE_ERROR_NONE;
    result.bytes_processed = write_result.bytes_processed;
    result.operation_time_ms = read_result.operation_time_ms + write_result.operation_time_ms;
    result.checksum = write_result.checksum;
    result.timestamp = time(NULL);
    
    set_error(STORAGE_ERROR_NONE);
    return result;
}

StorageResult storage_engine_zero_range(StorageEngine* engine, uint64_t offset, size_t size) {
    return storage_engine_delete(engine, offset, size);
}

StorageResult storage_engine_compare_range(StorageEngine* engine, uint64_t offset1,
                                          uint64_t offset2, size_t size) {
    if (!validate_engine(engine)) {
        set_error(STORAGE_ERROR_INVALID_PARAM);
        return (StorageResult){0};
    }
    
    void* buffer1 = malloc(size);
    void* buffer2 = malloc(size);
    
    if (!buffer1 || !buffer2) {
        free(buffer1);
        free(buffer2);
        set_error(STORAGE_ERROR_MEMORY_ALLOCATION);
        return (StorageResult){0};
    }
    
    // Read both ranges
    StorageResult read1 = storage_engine_read(engine, offset1, buffer1, size);
    StorageResult read2 = storage_engine_read(engine, offset2, buffer2, size);
    
    if (!read1.success || !read2.success) {
        free(buffer1);
        free(buffer2);
        return read1.success ? read2 : read1;
    }
    
    // Compare
    int compare_result = memcmp(buffer1, buffer2, size);
    
    free(buffer1);
    free(buffer2);
    
    StorageResult result = {0};
    result.success = true;
    result.operation = STORAGE_OP_VERIFY;
    result.error_code = STORAGE_ERROR_NONE;
    result.bytes_processed = size;
    result.operation_time_ms = read1.operation_time_ms + read2.operation_time_ms;
    result.checksum = compare_result == 0 ? 0 : 1; // Use checksum field to indicate match
    result.timestamp = time(NULL);
    
    set_error(STORAGE_ERROR_NONE);
    return result;
}

// Threading support
bool storage_engine_lock_operation(StorageEngine* engine) {
    if (!validate_engine(engine)) {
        return false;
    }
    
    return pthread_mutex_lock(&engine->operation_mutex) == 0;
}

bool storage_engine_unlock_operation(StorageEngine* engine) {
    if (!validate_engine(engine)) {
        return false;
    }
    
    return pthread_mutex_unlock(&engine->operation_mutex) == 0;
}

bool storage_engine_wait_for_operation(StorageEngine* engine, uint32_t timeout_ms) {
    if (!validate_engine(engine)) {
        return false;
    }
    
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
    timeout.tv_sec += timeout_ms / 1000;
    
    return pthread_cond_timedwait(&engine->operation_condition, &engine->operation_mutex, &timeout) == 0;
}

// Memory management
size_t storage_engine_get_optimal_buffer_size(const StorageEngine* engine) {
    if (!validate_engine(engine)) {
        return DEFAULT_BUFFER_SIZE;
    }
    
    return engine->config.buffer_size;
}

bool storage_engine_allocate_buffer(StorageEngine* engine, size_t size) {
    // This would allocate a buffer for the engine
    // For now, just return success
    return true;
}

void storage_engine_free_buffer(StorageEngine* engine) {
    // This would free the engine's buffer
}

// Validation
bool storage_engine_validate_engine(const StorageEngine* engine) {
    return validate_engine(engine);
}

bool storage_engine_check_permissions(const StorageEngine* engine) {
    if (!validate_engine(engine)) {
        return false;
    }
    
    // Check if we can access the file
    return access(engine->config.file_path, F_OK) == 0;
}

bool storage_engine_validate_operation(const StorageEngine* engine, StorageOperation op,
                                      uint64_t offset, size_t size) {
    return validate_operation(engine, op, offset, size);
} 