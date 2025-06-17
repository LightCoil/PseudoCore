#ifndef CACHE_ENGINE_H
#define CACHE_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <pthread.h>

// Forward declarations
typedef struct BlockEntity BlockEntity;

// Cache eviction strategies
typedef enum {
    CACHE_EVICTION_LRU = 0,    // Least Recently Used
    CACHE_EVICTION_LFU,        // Least Frequently Used
    CACHE_EVICTION_FIFO,       // First In, First Out
    CACHE_EVICTION_RANDOM,     // Random eviction
    CACHE_EVICTION_ADAPTIVE    // Adaptive based on access patterns
} CacheEvictionStrategy;

// Cache entry state
typedef enum {
    CACHE_ENTRY_CLEAN = 0,
    CACHE_ENTRY_DIRTY,
    CACHE_ENTRY_PINNED,
    CACHE_ENTRY_PREFETCH
} CacheEntryState;

// Cache performance metrics
typedef struct {
    uint64_t total_requests;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t evictions;
    uint64_t prefetch_hits;
    uint64_t write_backs;
    double hit_ratio;
    size_t memory_usage;
    size_t max_memory_usage;
    time_t last_reset;
} CacheMetrics;

// Cache configuration
typedef struct {
    size_t max_entries;
    size_t max_memory_bytes;
    CacheEvictionStrategy eviction_strategy;
    uint32_t prefetch_distance;
    bool enable_compression;
    uint8_t compression_level;
    uint32_t write_back_threshold;
    uint32_t cleanup_interval_seconds;
} CacheConfig;

// Cache entry with full metadata
typedef struct CacheEntry {
    uint64_t key;
    BlockEntity* block;
    CacheEntryState state;
    
    // Access tracking
    time_t last_access;
    time_t creation_time;
    uint32_t access_count;
    uint32_t hit_count;
    
    // Eviction metadata
    double eviction_score;
    uint32_t priority;
    
    // Memory management
    size_t memory_usage;
    bool is_pinned;
    
    // Linked list pointers
    struct CacheEntry* next;
    struct CacheEntry* prev;
    struct CacheEntry* hash_next;
} CacheEntry;

// Cache engine interface
typedef struct CacheEngine {
    // Configuration
    CacheConfig config;
    
    // Storage
    CacheEntry** hash_table;
    size_t hash_table_size;
    CacheEntry* lru_head;
    CacheEntry* lru_tail;
    
    // Statistics
    CacheMetrics metrics;
    
    // Synchronization
    pthread_mutex_t* mutex_groups;
    pthread_mutex_t lru_mutex;
    pthread_mutex_t metrics_mutex;
    uint32_t mutex_group_count;
    
    // State
    bool is_initialized;
    size_t current_entries;
    size_t current_memory_usage;
    
    // Background tasks
    pthread_t cleanup_thread;
    bool cleanup_running;
} CacheEngine;

// Cache engine interface functions
CacheEngine* cache_engine_create(const CacheConfig* config);
void cache_engine_destroy(CacheEngine* cache);

// Core cache operations
bool cache_engine_put(CacheEngine* cache, uint64_t key, BlockEntity* block);
BlockEntity* cache_engine_get(CacheEngine* cache, uint64_t key);
bool cache_engine_remove(CacheEngine* cache, uint64_t key);
bool cache_engine_contains(CacheEngine* cache, uint64_t key);

// Advanced operations
bool cache_engine_prefetch(CacheEngine* cache, uint64_t key, BlockEntity* block);
bool cache_engine_pin(CacheEngine* cache, uint64_t key);
bool cache_engine_unpin(CacheEngine* cache, uint64_t key);
bool cache_engine_mark_dirty(CacheEngine* cache, uint64_t key);

// Batch operations
bool cache_engine_put_batch(CacheEngine* cache, uint64_t* keys, BlockEntity** blocks, size_t count);
bool cache_engine_get_batch(CacheEngine* cache, uint64_t* keys, BlockEntity** blocks, size_t count);
size_t cache_engine_evict_batch(CacheEngine* cache, size_t target_count);

// Memory management
bool cache_engine_resize(CacheEngine* cache, size_t new_max_entries);
bool cache_engine_compact(CacheEngine* cache);
bool cache_engine_clear(CacheEngine* cache);
size_t cache_engine_get_memory_usage(const CacheEngine* cache);

// Eviction control
bool cache_engine_set_eviction_strategy(CacheEngine* cache, CacheEvictionStrategy strategy);
bool cache_engine_force_eviction(CacheEngine* cache, size_t count);
bool cache_engine_protect_from_eviction(CacheEngine* cache, uint64_t key);

// Metrics and monitoring
CacheMetrics cache_engine_get_metrics(const CacheEngine* cache);
void cache_engine_reset_metrics(CacheEngine* cache);
bool cache_engine_print_stats(const CacheEngine* cache, FILE* stream);

// Configuration
bool cache_engine_update_config(CacheEngine* cache, const CacheConfig* new_config);
CacheConfig cache_engine_get_config(const CacheEngine* cache);

// Background operations
bool cache_engine_start_cleanup_thread(CacheEngine* cache);
bool cache_engine_stop_cleanup_thread(CacheEngine* cache);
bool cache_engine_run_cleanup(CacheEngine* cache);

// Validation and debugging
bool cache_engine_validate_integrity(const CacheEngine* cache);
bool cache_engine_dump_state(const CacheEngine* cache, FILE* stream);
size_t cache_engine_count_entries(const CacheEngine* cache);

// Utility functions
uint64_t cache_engine_calculate_hash(uint64_t key);
const char* cache_engine_eviction_strategy_to_string(CacheEvictionStrategy strategy);
const char* cache_engine_entry_state_to_string(CacheEntryState state);

// Thread safety
bool cache_engine_lock_entry(CacheEngine* cache, uint64_t key);
bool cache_engine_unlock_entry(CacheEngine* cache, uint64_t key);

#endif // CACHE_ENGINE_H 