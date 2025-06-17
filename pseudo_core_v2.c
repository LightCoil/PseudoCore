#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>
#include <string.h>

// Include all our new components
#include "core_entity.h"
#include "task_entity.h"
#include "block_entity.h"
#include "cache_engine.h"
#include "compression_engine.h"
#include "storage_engine.h"
#include "core_manager.h"

// Configuration constants
#define DEFAULT_CORES 4
#define DEFAULT_CACHE_SIZE_MB 128
#define DEFAULT_SEGMENT_SIZE_MB 512
#define DEFAULT_BLOCK_SIZE 4096
#define DEFAULT_STORAGE_PATH "./storage_swap_v2.img"
#define DEFAULT_LOG_FILE "./pseudo_core_v2.log"

// Global state
static volatile bool global_running = true;
static CoreManager* core_manager = NULL;
static CacheEngine* cache_engine = NULL;
static CompressionEngine* compression_engine = NULL;
static StorageEngine* storage_engine = NULL;
static FILE* log_file = NULL;

// Signal handler for graceful shutdown
void signal_handler(int sig) {
    (void)sig;
    global_running = false;
    fprintf(stdout, "\n[MAIN] Received termination signal. Shutting down gracefully...\n");
    
    if (log_file) {
        fprintf(log_file, "[%s] [INFO] Received termination signal. Shutting down gracefully...\n", 
                get_timestamp());
        fflush(log_file);
    }
}

// Utility function to get timestamp
char* get_timestamp(void) {
    static char timestamp[26];
    time_t now = time(NULL);
    ctime_r(&now, timestamp);
    timestamp[24] = '\0'; // Remove newline
    return timestamp;
}

// Logging function
void log_message(const char* level, const char* component, const char* message) {
    char* ts = get_timestamp();
    
    // Log to file
    if (log_file) {
        fprintf(log_file, "[%s] [%s] %s: %s\n", ts, level, component, message);
        fflush(log_file);
    }
    
    // Log to stderr
    fprintf(stderr, "[%s] [%s] %s: %s\n", ts, level, component, message);
}

// Initialize storage
bool initialize_storage(void) {
    log_message("INFO", "STORAGE", "Initializing storage engine...");
    
    // Create storage configuration
    StorageConfig storage_config = {0};
    storage_config.file_path = DEFAULT_STORAGE_PATH;
    storage_config.access_mode = STORAGE_ACCESS_READ_WRITE;
    storage_config.block_size = DEFAULT_BLOCK_SIZE;
    storage_config.buffer_size = DEFAULT_BLOCK_SIZE * 4;
    storage_config.max_concurrent_operations = 16;
    storage_config.enable_checksum_validation = true;
    storage_config.enable_async_io = false;
    storage_config.enable_direct_io = false;
    storage_config.operation_timeout_ms = 5000;
    storage_config.retry_count = 3;
    storage_config.retry_delay_ms = 100;
    
    // Create storage engine
    storage_engine = storage_engine_create(&storage_config);
    if (!storage_engine) {
        log_message("ERROR", "STORAGE", "Failed to create storage engine");
        return false;
    }
    
    // Open storage file
    if (!storage_engine_open(storage_engine)) {
        log_message("ERROR", "STORAGE", "Failed to open storage file");
        return false;
    }
    
    log_message("INFO", "STORAGE", "Storage engine initialized successfully");
    return true;
}

// Initialize compression
bool initialize_compression(void) {
    log_message("INFO", "COMPRESSION", "Initializing compression engine...");
    
    // Create compression configuration
    CompressionConfig compression_config = {0};
    compression_config.default_algorithm = COMPRESSION_ALGORITHM_ZSTD;
    compression_config.default_quality = COMPRESSION_QUALITY_DEFAULT;
    compression_config.enable_adaptive_compression = true;
    compression_config.enable_parallel_compression = true;
    compression_config.max_compression_threads = 4;
    compression_config.min_size_for_compression = 1024;
    compression_config.max_size_for_compression = 1024 * 1024;
    compression_config.target_compression_ratio = 0.7;
    compression_config.compression_timeout_ms = 1000;
    compression_config.enable_checksum_validation = true;
    
    // Create compression engine
    compression_engine = compression_engine_create(&compression_config);
    if (!compression_engine) {
        log_message("ERROR", "COMPRESSION", "Failed to create compression engine");
        return false;
    }
    
    log_message("INFO", "COMPRESSION", "Compression engine initialized successfully");
    return true;
}

// Initialize cache
bool initialize_cache(void) {
    log_message("INFO", "CACHE", "Initializing cache engine...");
    
    // Create cache configuration
    CacheConfig cache_config = {0};
    cache_config.max_entries = DEFAULT_CACHE_SIZE_MB * 1024 * 1024 / DEFAULT_BLOCK_SIZE;
    cache_config.max_memory_bytes = DEFAULT_CACHE_SIZE_MB * 1024 * 1024;
    cache_config.eviction_strategy = CACHE_EVICTION_ADAPTIVE;
    cache_config.prefetch_distance = 2;
    cache_config.enable_compression = true;
    cache_config.compression_level = 3;
    cache_config.write_back_threshold = 100;
    cache_config.cleanup_interval_seconds = 30;
    
    // Create cache engine
    cache_engine = cache_engine_create(&cache_config);
    if (!cache_engine) {
        log_message("ERROR", "CACHE", "Failed to create cache engine");
        return false;
    }
    
    log_message("INFO", "CACHE", "Cache engine initialized successfully");
    return true;
}

// Initialize core manager
bool initialize_core_manager(void) {
    log_message("INFO", "CORE_MANAGER", "Initializing core manager...");
    
    // Create core manager configuration
    CoreManagerConfig manager_config = {0};
    manager_config.max_cores = DEFAULT_CORES;
    manager_config.min_cores = DEFAULT_CORES;
    manager_config.balance_strategy = LOAD_BALANCE_ADAPTIVE;
    manager_config.balance_interval_ms = 1000;
    manager_config.task_timeout_ms = 30000;
    manager_config.core_health_check_interval_ms = 10000;
    manager_config.enable_auto_scaling = false;
    manager_config.enable_fault_tolerance = true;
    manager_config.max_core_failures = 2;
    manager_config.recovery_timeout_ms = 60000;
    
    // Create core manager
    core_manager = core_manager_create(&manager_config, cache_engine, compression_engine, storage_engine);
    if (!core_manager) {
        log_message("ERROR", "CORE_MANAGER", "Failed to create core manager");
        return false;
    }
    
    // Initialize cores
    if (!core_manager_initialize_cores(core_manager)) {
        log_message("ERROR", "CORE_MANAGER", "Failed to initialize cores");
        return false;
    }
    
    log_message("INFO", "CORE_MANAGER", "Core manager initialized successfully");
    return true;
}

// Core execution function
void* core_execution_thread(void* arg) {
    CoreEntity* core = (CoreEntity*)arg;
    uint32_t core_id = core->id;
    
    log_message("INFO", "CORE", "Core execution thread started");
    
    while (global_running && core_entity_is_running(core)) {
        // Get next task from manager
        TaskEntity* task = core_manager_get_next_task(core_manager);
        if (!task) {
            usleep(1000); // Sleep 1ms if no tasks
            continue;
        }
        
        // Assign task to core
        if (!core_entity_assign_task(core, task)) {
            log_message("WARNING", "CORE", "Failed to assign task to core");
            continue;
        }
        
        // Execute task
        bool task_success = execute_task(core, task);
        
        // Complete task
        core_entity_complete_current_task(core);
        core_manager_complete_task(core_manager, task);
        
        // Update metrics
        CoreMetrics delta = {0};
        delta.operations_completed = 1;
        if (task_success) {
            delta.cache_hits = 1;
        } else {
            delta.cache_misses = 1;
        }
        core_entity_update_metrics(core, &delta);
        
        // Small delay to prevent overwhelming
        usleep(100);
    }
    
    log_message("INFO", "CORE", "Core execution thread finished");
    return NULL;
}

// Task execution function
bool execute_task(CoreEntity* core, TaskEntity* task) {
    uint32_t core_id = core->id;
    uint64_t block_offset = task->block_offset;
    size_t data_size = task->data_size;
    
    // Create block entity
    BlockEntity* block = block_entity_create(block_offset, data_size, core_id);
    if (!block) {
        log_message("ERROR", "TASK", "Failed to create block entity");
        return false;
    }
    
    // Try to get from cache first
    BlockEntity* cached_block = cache_engine_get(cache_engine, block_offset);
    if (cached_block) {
        // Cache hit
        block_entity_destroy(block);
        block = cached_block;
        
        // Update cache info
        block_entity_update_cache_info(block, true);
        
        // Simulate processing
        simulate_data_processing(core, block);
        
        return true;
    }
    
    // Cache miss - read from storage
    StorageResult read_result = storage_engine_read_block(storage_engine, block_offset, block);
    if (!read_result.success) {
        log_message("ERROR", "TASK", "Failed to read block from storage");
        block_entity_destroy(block);
        return false;
    }
    
    // Add to cache
    cache_engine_put(cache_engine, block_offset, block);
    
    // Update cache info
    block_entity_update_cache_info(block, false);
    
    // Simulate processing
    simulate_data_processing(core, block);
    
    // Write back if dirty
    if (block_entity_is_dirty(block)) {
        StorageResult write_result = storage_engine_write_block(storage_engine, block_offset, block);
        if (!write_result.success) {
            log_message("ERROR", "TASK", "Failed to write block to storage");
        }
    }
    
    block_entity_destroy(block);
    return true;
}

// Simulate data processing
void simulate_data_processing(CoreEntity* core, BlockEntity* block) {
    uint32_t core_id = core->id;
    
    // Get data buffer
    void* data = block_entity_get_data(block);
    size_t data_size = block_entity_get_data_size(block);
    
    if (!data || data_size == 0) {
        return;
    }
    
    // Simulate processing with XOR operations
    char* buffer = (char*)data;
    for (size_t i = 0; i < data_size; i++) {
        buffer[i] ^= (core_id & 0xFF);
    }
    
    // Mark as dirty
    block_entity_set_state(block, BLOCK_STATE_DIRTY);
    
    // Update task metrics
    TaskMetrics task_metrics = {0};
    task_metrics.bytes_processed = data_size;
    task_metrics.operations_performed = 1;
    task_entity_update_metrics(core->current_task, &task_metrics);
}

// Generate synthetic tasks
void generate_synthetic_tasks(void) {
    static uint64_t task_id = 1;
    static uint64_t block_offset = 0;
    
    // Generate a batch of tasks
    for (int i = 0; i < 10; i++) {
        TaskEntity* task = task_entity_create(
            task_id++,
            TASK_TYPE_READ,
            TASK_PRIORITY_NORMAL,
            block_offset,
            DEFAULT_BLOCK_SIZE
        );
        
        if (task) {
            core_manager_submit_task(core_manager, task);
            block_offset += DEFAULT_BLOCK_SIZE;
            
            // Wrap around after 1GB
            if (block_offset >= 1024 * 1024 * 1024) {
                block_offset = 0;
            }
        }
    }
}

// Statistics reporting thread
void* statistics_thread(void* arg) {
    (void)arg;
    
    while (global_running) {
        sleep(10); // Report every 10 seconds
        
        if (!global_running) break;
        
        // Get and display statistics
        CoreManagerMetrics manager_metrics = core_manager_get_metrics(core_manager);
        CacheMetrics cache_metrics = cache_engine_get_metrics(cache_engine);
        CompressionStats compression_stats = compression_engine_get_stats(compression_engine);
        StorageMetrics storage_metrics = storage_engine_get_metrics(storage_engine);
        
        // Log statistics
        char stats_msg[512];
        snprintf(stats_msg, sizeof(stats_msg),
                "STATS: Cores=%u/%u, Tasks=%lu, Cache_Hit_Ratio=%.2f%%, "
                "Compression_Ratio=%.2f, Storage_Ops=%lu",
                manager_metrics.active_cores, manager_metrics.total_cores,
                manager_metrics.total_tasks_processed,
                cache_metrics.hit_ratio * 100.0,
                compression_stats.average_compression_ratio,
                storage_metrics.total_operations);
        
        log_message("INFO", "STATS", stats_msg);
        
        // Generate more tasks
        generate_synthetic_tasks();
    }
    
    return NULL;
}

// Cleanup function
void cleanup(void) {
    log_message("INFO", "MAIN", "Starting cleanup...");
    
    // Stop core manager
    if (core_manager) {
        core_manager_graceful_shutdown(core_manager);
        core_manager_destroy(core_manager);
        core_manager = NULL;
    }
    
    // Destroy engines
    if (cache_engine) {
        cache_engine_destroy(cache_engine);
        cache_engine = NULL;
    }
    
    if (compression_engine) {
        compression_engine_destroy(compression_engine);
        compression_engine = NULL;
    }
    
    if (storage_engine) {
        storage_engine_close(storage_engine);
        storage_engine_destroy(storage_engine);
        storage_engine = NULL;
    }
    
    // Close log file
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
    
    log_message("INFO", "MAIN", "Cleanup completed");
}

// Main function
int main(int argc, char* argv[]) {
    printf("PseudoCore v2.0 - High-Performance Data Management System\n");
    printf("========================================================\n\n");
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Open log file
    log_file = fopen(DEFAULT_LOG_FILE, "w");
    if (!log_file) {
        fprintf(stderr, "Warning: Could not open log file %s\n", DEFAULT_LOG_FILE);
    }
    
    log_message("INFO", "MAIN", "Starting PseudoCore v2.0");
    
    // Initialize components
    if (!initialize_storage()) {
        log_message("ERROR", "MAIN", "Failed to initialize storage");
        cleanup();
        return 1;
    }
    
    if (!initialize_compression()) {
        log_message("ERROR", "MAIN", "Failed to initialize compression");
        cleanup();
        return 1;
    }
    
    if (!initialize_cache()) {
        log_message("ERROR", "MAIN", "Failed to initialize cache");
        cleanup();
        return 1;
    }
    
    if (!initialize_core_manager()) {
        log_message("ERROR", "MAIN", "Failed to initialize core manager");
        cleanup();
        return 1;
    }
    
    log_message("INFO", "MAIN", "All components initialized successfully");
    
    // Start core manager
    if (!core_manager_start_cores(core_manager)) {
        log_message("ERROR", "MAIN", "Failed to start cores");
        cleanup();
        return 1;
    }
    
    // Start background threads
    if (!core_manager_start_background_threads(core_manager)) {
        log_message("ERROR", "MAIN", "Failed to start background threads");
        cleanup();
        return 1;
    }
    
    // Create core execution threads
    pthread_t core_threads[DEFAULT_CORES];
    for (int i = 0; i < DEFAULT_CORES; i++) {
        CoreEntity* core = core_manager_get_core(core_manager, i);
        if (core) {
            if (pthread_create(&core_threads[i], NULL, core_execution_thread, core) != 0) {
                log_message("ERROR", "MAIN", "Failed to create core thread");
                cleanup();
                return 1;
            }
        }
    }
    
    // Create statistics thread
    pthread_t stats_thread;
    if (pthread_create(&stats_thread, NULL, statistics_thread, NULL) != 0) {
        log_message("ERROR", "MAIN", "Failed to create statistics thread");
        cleanup();
        return 1;
    }
    
    // Generate initial tasks
    generate_synthetic_tasks();
    
    log_message("INFO", "MAIN", "System started successfully. Press Ctrl+C to stop.");
    
    // Main loop
    while (global_running) {
        sleep(1);
        
        // Check if any cores have failed
        for (int i = 0; i < DEFAULT_CORES; i++) {
            if (!core_manager_check_core_health(core_manager, i)) {
                log_message("WARNING", "MAIN", "Core health check failed, attempting recovery");
                core_manager_recover_core(core_manager, i);
            }
        }
    }
    
    log_message("INFO", "MAIN", "Shutting down...");
    
    // Wait for threads to complete
    for (int i = 0; i < DEFAULT_CORES; i++) {
        pthread_join(core_threads[i], NULL);
    }
    
    pthread_join(stats_thread, NULL);
    
    // Final statistics
    if (core_manager) {
        CoreManagerMetrics final_metrics = core_manager_get_metrics(core_manager);
        char final_stats[256];
        snprintf(final_stats, sizeof(final_stats),
                "FINAL STATS: Total tasks processed: %lu, Failed: %lu, Migrated: %lu",
                final_metrics.total_tasks_processed,
                final_metrics.total_tasks_failed,
                final_metrics.total_tasks_migrated);
        log_message("INFO", "MAIN", final_stats);
    }
    
    cleanup();
    
    printf("\nPseudoCore v2.0 stopped successfully.\n");
    return 0;
} 