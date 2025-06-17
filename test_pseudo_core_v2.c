#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <assert.h>

// Include all our components
#include "core_entity.h"
#include "task_entity.h"
#include "block_entity.h"
#include "cache_engine.h"
#include "compression_engine.h"
#include "storage_engine.h"
#include "core_manager.h"

// Test configuration
#define TEST_CACHE_SIZE 1000
#define TEST_BLOCK_SIZE 4096
#define TEST_DATA_SIZE 1024
#define TEST_ITERATIONS 100

// Test utilities
void print_test_header(const char* test_name) {
    printf("\n=== %s ===\n", test_name);
    printf("================================\n");
}

void print_test_result(const char* test_name, bool passed) {
    printf("[%s] %s: %s\n", 
           passed ? "PASS" : "FAIL", 
           test_name,
           passed ? "✓" : "✗");
}

void print_metrics(const char* component, const char* metrics_name, double value) {
    printf("  %s %s: %.2f\n", component, metrics_name, value);
}

// Test data generation
void generate_test_data(void* buffer, size_t size, uint32_t seed) {
    char* data = (char*)buffer;
    for (size_t i = 0; i < size; i++) {
        data[i] = (char)((seed + i) % 256);
    }
}

uint32_t calculate_checksum(const void* data, size_t size) {
    uint32_t checksum = 0;
    const char* bytes = (const char*)data;
    for (size_t i = 0; i < size; i++) {
        checksum += bytes[i];
    }
    return checksum;
}

// Test 1: Core Entity
bool test_core_entity(void) {
    print_test_header("Core Entity Test");
    
    // Create core
    uint32_t core_id = 0;
    uint64_t segment_size = 512 * 1024 * 1024; // 512MB
    uint32_t max_tasks = 100;
    
    CoreEntity* core = core_entity_create(core_id, segment_size, max_tasks);
    if (!core) {
        print_test_result("Core creation", false);
        return false;
    }
    print_test_result("Core creation", true);
    
    // Test state management
    bool state_test = true;
    state_test &= core_entity_set_state(core, CORE_STATE_RUNNING);
    state_test &= (core_entity_get_state(core) == CORE_STATE_RUNNING);
    state_test &= core_entity_is_running(core);
    print_test_result("State management", state_test);
    
    // Test metrics
    CoreMetrics initial_metrics = core_entity_get_metrics(core);
    CoreMetrics delta = {0};
    delta.operations_completed = 10;
    delta.cache_hits = 8;
    delta.cache_misses = 2;
    
    core_entity_update_metrics(core, &delta);
    CoreMetrics updated_metrics = core_entity_get_metrics(core);
    
    bool metrics_test = (updated_metrics.operations_completed == 10) &&
                       (updated_metrics.cache_hits == 8) &&
                       (updated_metrics.cache_misses == 2);
    print_test_result("Metrics update", metrics_test);
    
    // Test task assignment
    TaskEntity* task = task_entity_create(1, TASK_TYPE_READ, TASK_PRIORITY_NORMAL, 0, TEST_BLOCK_SIZE);
    if (task) {
        bool task_test = core_entity_assign_task(core, task);
        task_test &= (core_entity_get_current_task(core) == task);
        print_test_result("Task assignment", task_test);
        
        core_entity_complete_current_task(core);
        task_entity_destroy(task);
    } else {
        print_test_result("Task assignment", false);
    }
    
    // Cleanup
    core_entity_destroy(core);
    print_test_result("Core destruction", true);
    
    return state_test && metrics_test;
}

// Test 2: Task Entity
bool test_task_entity(void) {
    print_test_header("Task Entity Test");
    
    // Create task
    uint64_t task_id = 1;
    TaskType type = TASK_TYPE_READ;
    TaskPriority priority = TASK_PRIORITY_HIGH;
    uint64_t block_offset = 0x1000;
    size_t data_size = TEST_DATA_SIZE;
    
    TaskEntity* task = task_entity_create(task_id, type, priority, block_offset, data_size);
    if (!task) {
        print_test_result("Task creation", false);
        return false;
    }
    print_test_result("Task creation", true);
    
    // Test basic properties
    bool properties_test = true;
    properties_test &= (task_entity_get_id(task) == task_id);
    properties_test &= (task_entity_get_type(task) == type);
    properties_test &= (task_entity_get_priority(task) == priority);
    properties_test &= (task_entity_get_block_offset(task) == block_offset);
    properties_test &= (task_entity_get_data_size(task) == data_size);
    print_test_result("Basic properties", properties_test);
    
    // Test state management
    bool state_test = true;
    state_test &= task_entity_set_state(task, TASK_STATE_RUNNING);
    state_test &= (task_entity_get_state(task) == TASK_STATE_RUNNING);
    state_test &= task_entity_is_running(task);
    print_test_result("State management", state_test);
    
    // Test priority management
    bool priority_test = true;
    priority_test &= task_entity_set_priority(task, TASK_PRIORITY_LOW);
    priority_test &= (task_entity_get_priority(task) == TASK_PRIORITY_LOW);
    priority_test &= task_entity_promote_priority(task);
    priority_test &= (task_entity_get_priority(task) == TASK_PRIORITY_NORMAL);
    print_test_result("Priority management", priority_test);
    
    // Test dependencies
    TaskEntity* dependency = task_entity_create(2, TASK_TYPE_WRITE, TASK_PRIORITY_NORMAL, 0x2000, TEST_DATA_SIZE);
    if (dependency) {
        bool dependency_test = true;
        dependency_test &= task_entity_add_dependency(task, dependency);
        dependency_test &= task_entity_has_dependencies(task);
        dependency_test &= task_entity_remove_dependency(task, dependency);
        dependency_test &= !task_entity_has_dependencies(task);
        print_test_result("Dependency management", dependency_test);
        task_entity_destroy(dependency);
    } else {
        print_test_result("Dependency management", false);
    }
    
    // Test metrics
    TaskMetrics metrics = {0};
    metrics.bytes_processed = TEST_DATA_SIZE;
    metrics.operations_performed = 5;
    metrics.execution_time_ms = 100;
    
    task_entity_update_metrics(task, &metrics);
    TaskMetrics retrieved_metrics = task_entity_get_metrics(task);
    
    bool metrics_test = (retrieved_metrics.bytes_processed == TEST_DATA_SIZE) &&
                       (retrieved_metrics.operations_performed == 5) &&
                       (retrieved_metrics.execution_time_ms == 100);
    print_test_result("Metrics update", metrics_test);
    
    // Cleanup
    task_entity_destroy(task);
    print_test_result("Task destruction", true);
    
    return properties_test && state_test && priority_test && metrics_test;
}

// Test 3: Block Entity
bool test_block_entity(void) {
    print_test_header("Block Entity Test");
    
    // Create block
    uint64_t block_offset = 0x1000;
    size_t data_size = TEST_DATA_SIZE;
    uint32_t core_id = 0;
    
    BlockEntity* block = block_entity_create(block_offset, data_size, core_id);
    if (!block) {
        print_test_result("Block creation", false);
        return false;
    }
    print_test_result("Block creation", true);
    
    // Test basic properties
    bool properties_test = true;
    properties_test &= (block_entity_get_offset(block) == block_offset);
    properties_test &= (block_entity_get_data_size(block) == data_size);
    properties_test &= (block_entity_get_core_id(block) == core_id);
    print_test_result("Basic properties", properties_test);
    
    // Test data operations
    void* data = block_entity_get_data(block);
    if (data) {
        // Generate test data
        generate_test_data(data, data_size, 12345);
        
        // Test checksum
        uint32_t original_checksum = calculate_checksum(data, data_size);
        block_entity_update_checksum(block);
        uint32_t stored_checksum = block_entity_get_checksum(block);
        
        bool checksum_test = (stored_checksum == original_checksum);
        print_test_result("Checksum validation", checksum_test);
        
        // Test integrity
        bool integrity_test = block_entity_validate_integrity(block);
        print_test_result("Integrity validation", integrity_test);
        
        // Corrupt data and test integrity
        ((char*)data)[0] ^= 0xFF;
        bool corruption_test = !block_entity_validate_integrity(block);
        print_test_result("Corruption detection", corruption_test);
        
        // Restore data
        ((char*)data)[0] ^= 0xFF;
    } else {
        print_test_result("Data operations", false);
    }
    
    // Test state management
    bool state_test = true;
    state_test &= block_entity_set_state(block, BLOCK_STATE_DIRTY);
    state_test &= (block_entity_get_state(block) == BLOCK_STATE_DIRTY);
    state_test &= block_entity_is_dirty(block);
    print_test_result("State management", state_test);
    
    // Test compression
    bool compression_test = true;
    compression_test &= block_entity_set_compression(block, COMPRESSION_ALGORITHM_ZSTD, 3);
    compression_test &= (block_entity_get_compression_algorithm(block) == COMPRESSION_ALGORITHM_ZSTD);
    compression_test &= (block_entity_get_compression_level(block) == 3);
    print_test_result("Compression settings", compression_test);
    
    // Test cache info
    bool cache_test = true;
    cache_test &= block_entity_update_cache_info(block, true);
    cache_test &= block_entity_is_cached(block);
    cache_test &= block_entity_update_cache_info(block, false);
    cache_test &= !block_entity_is_cached(block);
    print_test_result("Cache info", cache_test);
    
    // Cleanup
    block_entity_destroy(block);
    print_test_result("Block destruction", true);
    
    return properties_test && checksum_test && integrity_test && corruption_test && state_test && compression_test && cache_test;
}

// Test 4: Cache Engine
bool test_cache_engine(void) {
    print_test_header("Cache Engine Test");
    
    // Create cache configuration
    CacheConfig config = {0};
    config.max_entries = TEST_CACHE_SIZE;
    config.max_memory_bytes = TEST_CACHE_SIZE * TEST_BLOCK_SIZE;
    config.eviction_strategy = CACHE_EVICTION_LRU;
    config.prefetch_distance = 2;
    config.enable_compression = true;
    config.compression_level = 3;
    config.write_back_threshold = 10;
    config.cleanup_interval_seconds = 30;
    
    CacheEngine* cache = cache_engine_create(&config);
    if (!cache) {
        print_test_result("Cache creation", false);
        return false;
    }
    print_test_result("Cache creation", true);
    
    // Test basic operations
    bool basic_test = true;
    
    // Create test blocks
    BlockEntity* block1 = block_entity_create(0x1000, TEST_DATA_SIZE, 0);
    BlockEntity* block2 = block_entity_create(0x2000, TEST_DATA_SIZE, 0);
    
    if (block1 && block2) {
        // Generate test data
        generate_test_data(block_entity_get_data(block1), TEST_DATA_SIZE, 11111);
        generate_test_data(block_entity_get_data(block2), TEST_DATA_SIZE, 22222);
        
        // Test put/get operations
        basic_test &= cache_engine_put(cache, 0x1000, block1);
        basic_test &= cache_engine_put(cache, 0x2000, block2);
        
        BlockEntity* retrieved1 = cache_engine_get(cache, 0x1000);
        BlockEntity* retrieved2 = cache_engine_get(cache, 0x2000);
        
        basic_test &= (retrieved1 != NULL);
        basic_test &= (retrieved2 != NULL);
        
        // Verify data integrity
        if (retrieved1 && retrieved2) {
            uint32_t checksum1 = calculate_checksum(block_entity_get_data(retrieved1), TEST_DATA_SIZE);
            uint32_t checksum2 = calculate_checksum(block_entity_get_data(retrieved2), TEST_DATA_SIZE);
            basic_test &= (checksum1 == calculate_checksum(block_entity_get_data(block1), TEST_DATA_SIZE));
            basic_test &= (checksum2 == calculate_checksum(block_entity_get_data(block2), TEST_DATA_SIZE));
        }
        
        print_test_result("Basic operations", basic_test);
        
        // Test cache miss
        BlockEntity* not_found = cache_engine_get(cache, 0x3000);
        bool miss_test = (not_found == NULL);
        print_test_result("Cache miss", miss_test);
        
        // Test remove operation
        bool remove_test = cache_engine_remove(cache, 0x1000);
        BlockEntity* removed = cache_engine_get(cache, 0x1000);
        remove_test &= (removed == NULL);
        print_test_result("Remove operation", remove_test);
        
        // Test batch operations
        uint64_t keys[] = {0x4000, 0x5000, 0x6000};
        BlockEntity* blocks[3];
        for (int i = 0; i < 3; i++) {
            blocks[i] = block_entity_create(keys[i], TEST_DATA_SIZE, 0);
            generate_test_data(block_entity_get_data(blocks[i]), TEST_DATA_SIZE, 30000 + i);
        }
        
        bool batch_test = cache_engine_put_batch(cache, keys, blocks, 3);
        BlockEntity* retrieved_blocks[3];
        batch_test &= cache_engine_get_batch(cache, keys, retrieved_blocks, 3);
        
        for (int i = 0; i < 3; i++) {
            batch_test &= (retrieved_blocks[i] != NULL);
        }
        print_test_result("Batch operations", batch_test);
        
        // Cleanup test blocks
        for (int i = 0; i < 3; i++) {
            block_entity_destroy(blocks[i]);
        }
        
        block_entity_destroy(block1);
        block_entity_destroy(block2);
    } else {
        print_test_result("Basic operations", false);
    }
    
    // Test metrics
    CacheMetrics metrics = cache_engine_get_metrics(cache);
    bool metrics_test = (metrics.total_entries >= 0) && (metrics.hit_ratio >= 0.0);
    print_test_result("Metrics collection", metrics_test);
    
    // Cleanup
    cache_engine_destroy(cache);
    print_test_result("Cache destruction", true);
    
    return basic_test && miss_test && remove_test && batch_test && metrics_test;
}

// Test 5: Compression Engine
bool test_compression_engine(void) {
    print_test_header("Compression Engine Test");
    
    // Create compression configuration
    CompressionConfig config = {0};
    config.default_algorithm = COMPRESSION_ALGORITHM_ZSTD;
    config.default_quality = COMPRESSION_QUALITY_DEFAULT;
    config.enable_adaptive_compression = true;
    config.enable_parallel_compression = false;
    config.max_compression_threads = 1;
    config.min_size_for_compression = 1024;
    config.max_size_for_compression = 1024 * 1024;
    config.target_compression_ratio = 0.7;
    config.compression_timeout_ms = 1000;
    config.enable_checksum_validation = true;
    
    CompressionEngine* engine = compression_engine_create(&config);
    if (!engine) {
        print_test_result("Compression engine creation", false);
        return false;
    }
    print_test_result("Compression engine creation", true);
    
    // Test data
    size_t test_size = TEST_DATA_SIZE;
    void* original_data = malloc(test_size);
    void* compressed_data = malloc(test_size * 2); // Extra space for compression
    void* decompressed_data = malloc(test_size);
    
    if (!original_data || !compressed_data || !decompressed_data) {
        print_test_result("Memory allocation", false);
        return false;
    }
    
    // Generate test data
    generate_test_data(original_data, test_size, 54321);
    uint32_t original_checksum = calculate_checksum(original_data, test_size);
    
    // Test compression
    CompressionResult comp_result = compression_engine_compress(
        engine, original_data, test_size, compressed_data, test_size * 2);
    
    bool compression_test = comp_result.success && (comp_result.compressed_size > 0);
    print_test_result("Compression", compression_test);
    
    if (compression_test) {
        // Test decompression
        DecompressionResult decomp_result = compression_engine_decompress(
            engine, compressed_data, comp_result.compressed_size, 
            decompressed_data, test_size);
        
        bool decompression_test = decomp_result.success && 
                                 (decomp_result.decompressed_size == test_size);
        
        // Verify data integrity
        uint32_t decompressed_checksum = calculate_checksum(decompressed_data, test_size);
        bool integrity_test = (decompressed_checksum == original_checksum);
        
        print_test_result("Decompression", decompression_test);
        print_test_result("Data integrity", integrity_test);
        
        // Test compression ratio
        double ratio = (double)comp_result.compressed_size / test_size;
        printf("  Compression ratio: %.2f\n", ratio);
        
        // Test adaptive compression
        CompressionResult adaptive_result = compression_engine_compress_adaptive(
            engine, original_data, test_size, compressed_data, test_size * 2);
        
        bool adaptive_test = adaptive_result.success;
        print_test_result("Adaptive compression", adaptive_test);
        
        compression_test &= decompression_test && integrity_test && adaptive_test;
    }
    
    // Test different algorithms
    bool algorithm_test = true;
    CompressionAlgorithm algorithms[] = {COMPRESSION_ALGORITHM_LZ4, COMPRESSION_ALGORITHM_GZIP};
    
    for (int i = 0; i < 2; i++) {
        CompressionResult algo_result = compression_engine_compress_with_algorithm(
            engine, algorithms[i], COMPRESSION_QUALITY_DEFAULT,
            original_data, test_size, compressed_data, test_size * 2);
        
        algorithm_test &= algo_result.success;
    }
    print_test_result("Multiple algorithms", algorithm_test);
    
    // Test statistics
    CompressionStats stats = compression_engine_get_stats(engine);
    bool stats_test = (stats.total_compressions > 0) && (stats.average_compression_ratio > 0.0);
    print_test_result("Statistics collection", stats_test);
    
    // Cleanup
    free(original_data);
    free(compressed_data);
    free(decompressed_data);
    compression_engine_destroy(engine);
    print_test_result("Compression engine destruction", true);
    
    return compression_test && algorithm_test && stats_test;
}

// Test 6: Storage Engine
bool test_storage_engine(void) {
    print_test_header("Storage Engine Test");
    
    // Create storage configuration
    StorageConfig config = {0};
    config.file_path = "./test_storage.img";
    config.access_mode = STORAGE_ACCESS_READ_WRITE;
    config.block_size = TEST_BLOCK_SIZE;
    config.buffer_size = TEST_BLOCK_SIZE * 4;
    config.max_concurrent_operations = 4;
    config.enable_checksum_validation = true;
    config.enable_async_io = false;
    config.enable_direct_io = false;
    config.operation_timeout_ms = 5000;
    config.retry_count = 3;
    config.retry_delay_ms = 100;
    
    StorageEngine* storage = storage_engine_create(&config);
    if (!storage) {
        print_test_result("Storage engine creation", false);
        return false;
    }
    print_test_result("Storage engine creation", true);
    
    // Open storage
    bool open_test = storage_engine_open(storage);
    print_test_result("Storage open", open_test);
    
    if (open_test) {
        // Create test block
        BlockEntity* block = block_entity_create(0x1000, TEST_DATA_SIZE, 0);
        if (block) {
            // Generate test data
            generate_test_data(block_entity_get_data(block), TEST_DATA_SIZE, 98765);
            uint32_t original_checksum = calculate_checksum(block_entity_get_data(block), TEST_DATA_SIZE);
            
            // Test write operation
            StorageResult write_result = storage_engine_write_block(storage, 0x1000, block);
            bool write_test = write_result.success;
            print_test_result("Write operation", write_test);
            
            if (write_test) {
                // Create new block for reading
                BlockEntity* read_block = block_entity_create(0x1000, TEST_DATA_SIZE, 0);
                
                // Test read operation
                StorageResult read_result = storage_engine_read_block(storage, 0x1000, read_block);
                bool read_test = read_result.success;
                print_test_result("Read operation", read_test);
                
                if (read_test) {
                    // Verify data integrity
                    uint32_t read_checksum = calculate_checksum(block_entity_get_data(read_block), TEST_DATA_SIZE);
                    bool integrity_test = (read_checksum == original_checksum);
                    print_test_result("Data integrity", integrity_test);
                    
                    read_test &= integrity_test;
                }
                
                block_entity_destroy(read_block);
            }
            
            // Test batch operations
            uint64_t offsets[] = {0x2000, 0x3000, 0x4000};
            BlockEntity* blocks[3];
            for (int i = 0; i < 3; i++) {
                blocks[i] = block_entity_create(offsets[i], TEST_DATA_SIZE, 0);
                generate_test_data(block_entity_get_data(blocks[i]), TEST_DATA_SIZE, 100000 + i);
            }
            
            StorageResult batch_write = storage_engine_write_blocks(storage, offsets, blocks, 3);
            bool batch_write_test = batch_write.success;
            
            BlockEntity* read_blocks[3];
            for (int i = 0; i < 3; i++) {
                read_blocks[i] = block_entity_create(offsets[i], TEST_DATA_SIZE, 0);
            }
            
            StorageResult batch_read = storage_engine_read_blocks(storage, offsets, read_blocks, 3);
            bool batch_read_test = batch_read.success;
            
            print_test_result("Batch operations", batch_write_test && batch_read_test);
            
            // Cleanup batch blocks
            for (int i = 0; i < 3; i++) {
                block_entity_destroy(blocks[i]);
                block_entity_destroy(read_blocks[i]);
            }
            
            block_entity_destroy(block);
        } else {
            print_test_result("Write operation", false);
        }
        
        // Test metrics
        StorageMetrics metrics = storage_engine_get_metrics(storage);
        bool metrics_test = (metrics.total_operations > 0);
        print_test_result("Metrics collection", metrics_test);
        
        // Close storage
        storage_engine_close(storage);
        print_test_result("Storage close", true);
    }
    
    // Cleanup
    storage_engine_destroy(storage);
    print_test_result("Storage engine destruction", true);
    
    // Remove test file
    remove("./test_storage.img");
    
    return open_test;
}

// Test 7: Core Manager Integration
bool test_core_manager_integration(void) {
    print_test_header("Core Manager Integration Test");
    
    // Create engines
    CacheConfig cache_config = {0};
    cache_config.max_entries = 100;
    cache_config.max_memory_bytes = 1024 * 1024;
    cache_config.eviction_strategy = CACHE_EVICTION_LRU;
    
    CompressionConfig compression_config = {0};
    compression_config.default_algorithm = COMPRESSION_ALGORITHM_ZSTD;
    compression_config.default_quality = COMPRESSION_QUALITY_DEFAULT;
    
    StorageConfig storage_config = {0};
    storage_config.file_path = "./test_manager_storage.img";
    storage_config.access_mode = STORAGE_ACCESS_READ_WRITE;
    storage_config.block_size = TEST_BLOCK_SIZE;
    
    CacheEngine* cache = cache_engine_create(&cache_config);
    CompressionEngine* compression = compression_engine_create(&compression_config);
    StorageEngine* storage = storage_engine_create(&storage_config);
    
    if (!cache || !compression || !storage) {
        print_test_result("Engine creation", false);
        return false;
    }
    
    // Create core manager
    CoreManagerConfig manager_config = {0};
    manager_config.max_cores = 2;
    manager_config.min_cores = 2;
    manager_config.balance_strategy = LOAD_BALANCE_ROUND_ROBIN;
    manager_config.balance_interval_ms = 1000;
    manager_config.task_timeout_ms = 10000;
    manager_config.core_health_check_interval_ms = 5000;
    
    CoreManager* manager = core_manager_create(&manager_config, cache, compression, storage);
    if (!manager) {
        print_test_result("Manager creation", false);
        return false;
    }
    print_test_result("Manager creation", true);
    
    // Initialize cores
    bool init_test = core_manager_initialize_cores(manager);
    print_test_result("Core initialization", init_test);
    
    if (init_test) {
        // Start cores
        bool start_test = core_manager_start_cores(manager);
        print_test_result("Core startup", start_test);
        
        if (start_test) {
            // Create and submit tasks
            bool task_test = true;
            for (int i = 0; i < 5; i++) {
                TaskEntity* task = task_entity_create(i + 1, TASK_TYPE_READ, TASK_PRIORITY_NORMAL, 
                                                    i * TEST_BLOCK_SIZE, TEST_BLOCK_SIZE);
                if (task) {
                    task_test &= core_manager_submit_task(manager, task);
                    task_entity_destroy(task);
                } else {
                    task_test = false;
                }
            }
            print_test_result("Task submission", task_test);
            
            // Test core selection
            TaskEntity* test_task = task_entity_create(100, TASK_TYPE_READ, TASK_PRIORITY_NORMAL, 
                                                      0x1000, TEST_BLOCK_SIZE);
            if (test_task) {
                uint32_t selected_core = core_manager_select_optimal_core(manager, test_task);
                bool selection_test = (selected_core < manager_config.max_cores);
                print_test_result("Core selection", selection_test);
                task_entity_destroy(test_task);
            }
            
            // Test health monitoring
            bool health_test = true;
            for (uint32_t i = 0; i < manager_config.max_cores; i++) {
                health_test &= core_manager_check_core_health(manager, i);
            }
            print_test_result("Health monitoring", health_test);
            
            // Test metrics
            CoreManagerMetrics metrics = core_manager_get_metrics(manager);
            bool metrics_test = (metrics.total_cores == manager_config.max_cores);
            print_test_result("Metrics collection", metrics_test);
            
            // Stop cores
            bool stop_test = core_manager_stop_cores(manager);
            print_test_result("Core shutdown", stop_test);
        }
    }
    
    // Cleanup
    core_manager_destroy(manager);
    cache_engine_destroy(cache);
    compression_engine_destroy(compression);
    storage_engine_destroy(storage);
    print_test_result("Manager destruction", true);
    
    // Remove test file
    remove("./test_manager_storage.img");
    
    return init_test;
}

// Main test runner
int main(void) {
    printf("PseudoCore v2.0 Test Suite\n");
    printf("==========================\n");
    printf("Running comprehensive tests...\n");
    
    time_t start_time = time(NULL);
    
    // Run all tests
    bool all_tests_passed = true;
    
    all_tests_passed &= test_core_entity();
    all_tests_passed &= test_task_entity();
    all_tests_passed &= test_block_entity();
    all_tests_passed &= test_cache_engine();
    all_tests_passed &= test_compression_engine();
    all_tests_passed &= test_storage_engine();
    all_tests_passed &= test_core_manager_integration();
    
    time_t end_time = time(NULL);
    double duration = difftime(end_time, start_time);
    
    printf("\n=== Test Summary ===\n");
    printf("===================\n");
    printf("Total test duration: %.2f seconds\n", duration);
    printf("Overall result: %s\n", all_tests_passed ? "ALL TESTS PASSED ✓" : "SOME TESTS FAILED ✗");
    
    if (all_tests_passed) {
        printf("\n🎉 Congratulations! All tests passed successfully.\n");
        printf("PseudoCore v2.0 is ready for production use.\n");
    } else {
        printf("\n⚠️  Some tests failed. Please review the output above.\n");
    }
    
    return all_tests_passed ? 0 : 1;
} 