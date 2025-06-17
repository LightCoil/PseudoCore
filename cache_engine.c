#include "cache_engine.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <errno.h>

// Internal constants
#define DEFAULT_HASH_TABLE_SIZE 2048
#define DEFAULT_MUTEX_GROUPS 16
#define MIN_CACHE_ENTRIES 16
#define MAX_CACHE_ENTRIES (1024 * 1024) // 1M entries
#define CLEANUP_INTERVAL_SECONDS 30
#define EVICTION_BATCH_SIZE 10

// Internal error codes
typedef enum {
    CACHE_ERROR_NONE = 0,
    CACHE_ERROR_INVALID_PARAM,
    CACHE_ERROR_MEMORY_ALLOCATION,
    CACHE_ERROR_HASH_TABLE_FULL,
    CACHE_ERROR_ENTRY_NOT_FOUND,
    CACHE_ERROR_EVICTION_FAILED,
    CACHE_ERROR_THREAD_CREATION
} CacheError;

// Internal error tracking
static CacheError last_error = CACHE_ERROR_NONE;

// Error handling
static void set_error(CacheError error) {
    last_error = error;
}

CacheError cache_engine_get_last_error(void) {
    return last_error;
}

const char* cache_engine_error_to_string(CacheError error) {
    switch (error) {
        case CACHE_ERROR_NONE: return "No error";
        case CACHE_ERROR_INVALID_PARAM: return "Invalid parameter";
        case CACHE_ERROR_MEMORY_ALLOCATION: return "Memory allocation failed";
        case CACHE_ERROR_HASH_TABLE_FULL: return "Hash table full";
        case CACHE_ERROR_ENTRY_NOT_FOUND: return "Entry not found";
        case CACHE_ERROR_EVICTION_FAILED: return "Eviction failed";
        case CACHE_ERROR_THREAD_CREATION: return "Thread creation failed";
        default: return "Unknown error";
    }
}

// Validation functions
static bool validate_cache(const CacheEngine* cache) {
    return cache != NULL && cache->is_initialized;
}

static bool validate_config(const CacheConfig* config) {
    return config != NULL && 
           config->max_entries >= MIN_CACHE_ENTRIES &&
           config->max_entries <= MAX_CACHE_ENTRIES &&
           config->max_memory_bytes > 0;
}

// Hash function using FNV-1a
static uint64_t fnv1a_hash(uint64_t key) {
    const uint64_t FNV_PRIME = 1099511628211ULL;
    const uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    
    uint64_t hash = FNV_OFFSET_BASIS;
    uint8_t* bytes = (uint8_t*)&key;
    
    for (size_t i = 0; i < sizeof(key); i++) {
        hash ^= bytes[i];
        hash *= FNV_PRIME;
    }
    
    return hash;
}

// Calculate hash table index
static size_t get_hash_index(const CacheEngine* cache, uint64_t key) {
    return fnv1a_hash(key) % cache->hash_table_size;
}

// Calculate mutex group index
static size_t get_mutex_group(const CacheEngine* cache, size_t hash_index) {
    return hash_index % cache->mutex_group_count;
}

// Create cache entry
static CacheEntry* create_cache_entry(uint64_t key, BlockEntity* block) {
    CacheEntry* entry = malloc(sizeof(CacheEntry));
    if (!entry) {
        set_error(CACHE_ERROR_MEMORY_ALLOCATION);
        return NULL;
    }
    
    entry->key = key;
    entry->block = block;
    entry->state = CACHE_ENTRY_CLEAN;
    entry->last_access = time(NULL);
    entry->creation_time = entry->last_access;
    entry->access_count = 1;
    entry->hit_count = 1;
    entry->eviction_score = 0.0;
    entry->priority = 0;
    entry->memory_usage = block ? block_entity_get_memory_usage(block) : 0;
    entry->is_pinned = false;
    entry->next = NULL;
    entry->prev = NULL;
    entry->hash_next = NULL;
    
    return entry;
}

// Destroy cache entry
static void destroy_cache_entry(CacheEntry* entry) {
    if (entry) {
        // Don't destroy the block - it's managed externally
        free(entry);
    }
}

// Update eviction score based on strategy
static void update_eviction_score(CacheEntry* entry, CacheEvictionStrategy strategy) {
    time_t now = time(NULL);
    double time_factor = 1.0 / (1.0 + difftime(now, entry->last_access));
    
    switch (strategy) {
        case CACHE_EVICTION_LRU:
            entry->eviction_score = time_factor;
            break;
        case CACHE_EVICTION_LFU:
            entry->eviction_score = 1.0 / (1.0 + entry->access_count);
            break;
        case CACHE_EVICTION_FIFO:
            entry->eviction_score = 1.0 / (1.0 + difftime(now, entry->creation_time));
            break;
        case CACHE_EVICTION_RANDOM:
            entry->eviction_score = (double)rand() / RAND_MAX;
            break;
        case CACHE_EVICTION_ADAPTIVE:
            // Combine LRU and LFU with access frequency
            double frequency_factor = 1.0 / (1.0 + entry->access_frequency);
            entry->eviction_score = (time_factor + frequency_factor) / 2.0;
            break;
    }
}

// Cache engine creation
CacheEngine* cache_engine_create(const CacheConfig* config) {
    if (!validate_config(config)) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    CacheEngine* cache = malloc(sizeof(CacheEngine));
    if (!cache) {
        set_error(CACHE_ERROR_MEMORY_ALLOCATION);
        return NULL;
    }
    
    // Initialize configuration
    cache->config = *config;
    
    // Calculate hash table size (power of 2)
    cache->hash_table_size = DEFAULT_HASH_TABLE_SIZE;
    while (cache->hash_table_size < config->max_entries) {
        cache->hash_table_size *= 2;
    }
    
    // Allocate hash table
    cache->hash_table = calloc(cache->hash_table_size, sizeof(CacheEntry*));
    if (!cache->hash_table) {
        set_error(CACHE_ERROR_MEMORY_ALLOCATION);
        free(cache);
        return NULL;
    }
    
    // Initialize mutex groups
    cache->mutex_group_count = DEFAULT_MUTEX_GROUPS;
    cache->mutex_groups = malloc(cache->mutex_group_count * sizeof(pthread_mutex_t));
    if (!cache->mutex_groups) {
        set_error(CACHE_ERROR_MEMORY_ALLOCATION);
        free(cache->hash_table);
        free(cache);
        return NULL;
    }
    
    for (uint32_t i = 0; i < cache->mutex_group_count; i++) {
        if (pthread_mutex_init(&cache->mutex_groups[i], NULL) != 0) {
            set_error(CACHE_ERROR_INVALID_PARAM);
            for (uint32_t j = 0; j < i; j++) {
                pthread_mutex_destroy(&cache->mutex_groups[j]);
            }
            free(cache->mutex_groups);
            free(cache->hash_table);
            free(cache);
            return NULL;
        }
    }
    
    // Initialize other mutexes
    if (pthread_mutex_init(&cache->lru_mutex, NULL) != 0 ||
        pthread_mutex_init(&cache->metrics_mutex, NULL) != 0) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        for (uint32_t i = 0; i < cache->mutex_group_count; i++) {
            pthread_mutex_destroy(&cache->mutex_groups[i]);
        }
        free(cache->mutex_groups);
        free(cache->hash_table);
        free(cache);
        return NULL;
    }
    
    // Initialize state
    cache->lru_head = NULL;
    cache->lru_tail = NULL;
    cache->is_initialized = true;
    cache->current_entries = 0;
    cache->current_memory_usage = 0;
    cache->cleanup_running = false;
    
    // Initialize metrics
    memset(&cache->metrics, 0, sizeof(CacheMetrics));
    cache->metrics.last_reset = time(NULL);
    
    set_error(CACHE_ERROR_NONE);
    return cache;
}

// Cache engine destruction
void cache_engine_destroy(CacheEngine* cache) {
    if (!cache) return;
    
    // Stop cleanup thread
    cache_engine_stop_cleanup_thread(cache);
    
    // Clear all entries
    cache_engine_clear(cache);
    
    // Destroy mutexes
    for (uint32_t i = 0; i < cache->mutex_group_count; i++) {
        pthread_mutex_destroy(&cache->mutex_groups[i]);
    }
    pthread_mutex_destroy(&cache->lru_mutex);
    pthread_mutex_destroy(&cache->metrics_mutex);
    
    // Free memory
    free(cache->mutex_groups);
    free(cache->hash_table);
    free(cache);
}

// Core cache operations
bool cache_engine_put(CacheEngine* cache, uint64_t key, BlockEntity* block) {
    if (!validate_cache(cache) || !block) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        return false;
    }
    
    size_t hash_index = get_hash_index(cache, key);
    size_t mutex_group = get_mutex_group(cache, hash_index);
    
    pthread_mutex_lock(&cache->mutex_groups[mutex_group]);
    
    // Check if entry already exists
    CacheEntry* existing = cache->hash_table[hash_index];
    while (existing) {
        if (existing->key == key) {
            // Update existing entry
            existing->block = block;
            existing->last_access = time(NULL);
            existing->access_count++;
            existing->hit_count++;
            existing->memory_usage = block_entity_get_memory_usage(block);
            
            // Move to front of LRU list
            pthread_mutex_lock(&cache->lru_mutex);
            if (existing != cache->lru_head) {
                // Remove from current position
                if (existing->prev) existing->prev->next = existing->next;
                if (existing->next) existing->next->prev = existing->prev;
                if (existing == cache->lru_tail) cache->lru_tail = existing->prev;
                
                // Add to front
                existing->next = cache->lru_head;
                existing->prev = NULL;
                if (cache->lru_head) cache->lru_head->prev = existing;
                cache->lru_head = existing;
                if (!cache->lru_tail) cache->lru_tail = existing;
            }
            pthread_mutex_unlock(&cache->lru_mutex);
            
            pthread_mutex_unlock(&cache->mutex_groups[mutex_group]);
            
            // Update metrics
            pthread_mutex_lock(&cache->metrics_mutex);
            cache->metrics.cache_hits++;
            cache->metrics.total_requests++;
            pthread_mutex_unlock(&cache->metrics_mutex);
            
            set_error(CACHE_ERROR_NONE);
            return true;
        }
        existing = existing->hash_next;
    }
    
    // Create new entry
    CacheEntry* entry = create_cache_entry(key, block);
    if (!entry) {
        pthread_mutex_unlock(&cache->mutex_groups[mutex_group]);
        return false;
    }
    
    // Check if we need to evict entries
    if (cache->current_entries >= cache->config.max_entries) {
        size_t evicted = cache_engine_evict_batch(cache, EVICTION_BATCH_SIZE);
        if (evicted == 0) {
            destroy_cache_entry(entry);
            pthread_mutex_unlock(&cache->mutex_groups[mutex_group]);
            set_error(CACHE_ERROR_EVICTION_FAILED);
            return false;
        }
    }
    
    // Add to hash table
    entry->hash_next = cache->hash_table[hash_index];
    cache->hash_table[hash_index] = entry;
    
    // Add to LRU list
    pthread_mutex_lock(&cache->lru_mutex);
    entry->next = cache->lru_head;
    entry->prev = NULL;
    if (cache->lru_head) cache->lru_head->prev = entry;
    cache->lru_head = entry;
    if (!cache->lru_tail) cache->lru_tail = entry;
    cache->current_entries++;
    cache->current_memory_usage += entry->memory_usage;
    pthread_mutex_unlock(&cache->lru_mutex);
    
    pthread_mutex_unlock(&cache->mutex_groups[mutex_group]);
    
    // Update metrics
    pthread_mutex_lock(&cache->metrics_mutex);
    cache->metrics.cache_misses++;
    cache->metrics.total_requests++;
    pthread_mutex_unlock(&cache->metrics_mutex);
    
    set_error(CACHE_ERROR_NONE);
    return true;
}

BlockEntity* cache_engine_get(CacheEngine* cache, uint64_t key) {
    if (!validate_cache(cache)) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    size_t hash_index = get_hash_index(cache, key);
    size_t mutex_group = get_mutex_group(cache, hash_index);
    
    pthread_mutex_lock(&cache->mutex_groups[mutex_group]);
    
    CacheEntry* entry = cache->hash_table[hash_index];
    while (entry) {
        if (entry->key == key) {
            // Update access info
            entry->last_access = time(NULL);
            entry->access_count++;
            entry->hit_count++;
            
            // Move to front of LRU list
            pthread_mutex_lock(&cache->lru_mutex);
            if (entry != cache->lru_head) {
                // Remove from current position
                if (entry->prev) entry->prev->next = entry->next;
                if (entry->next) entry->next->prev = entry->prev;
                if (entry == cache->lru_tail) cache->lru_tail = entry->prev;
                
                // Add to front
                entry->next = cache->lru_head;
                entry->prev = NULL;
                if (cache->lru_head) cache->lru_head->prev = entry;
                cache->lru_head = entry;
                if (!cache->lru_tail) cache->lru_tail = entry;
            }
            pthread_mutex_unlock(&cache->lru_mutex);
            
            pthread_mutex_unlock(&cache->mutex_groups[mutex_group]);
            
            // Update metrics
            pthread_mutex_lock(&cache->metrics_mutex);
            cache->metrics.cache_hits++;
            cache->metrics.total_requests++;
            cache->metrics.hit_ratio = (double)cache->metrics.cache_hits / cache->metrics.total_requests;
            pthread_mutex_unlock(&cache->metrics_mutex);
            
            set_error(CACHE_ERROR_NONE);
            return entry->block;
        }
        entry = entry->hash_next;
    }
    
    pthread_mutex_unlock(&cache->mutex_groups[mutex_group]);
    
    // Update metrics
    pthread_mutex_lock(&cache->metrics_mutex);
    cache->metrics.cache_misses++;
    cache->metrics.total_requests++;
    cache->metrics.hit_ratio = (double)cache->metrics.cache_hits / cache->metrics.total_requests;
    pthread_mutex_unlock(&cache->metrics_mutex);
    
    set_error(CACHE_ERROR_ENTRY_NOT_FOUND);
    return NULL;
}

bool cache_engine_remove(CacheEngine* cache, uint64_t key) {
    if (!validate_cache(cache)) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        return false;
    }
    
    size_t hash_index = get_hash_index(cache, key);
    size_t mutex_group = get_mutex_group(cache, hash_index);
    
    pthread_mutex_lock(&cache->mutex_groups[mutex_group]);
    
    CacheEntry* entry = cache->hash_table[hash_index];
    CacheEntry* prev = NULL;
    
    while (entry) {
        if (entry->key == key) {
            // Remove from hash table
            if (prev) {
                prev->hash_next = entry->hash_next;
            } else {
                cache->hash_table[hash_index] = entry->hash_next;
            }
            
            // Remove from LRU list
            pthread_mutex_lock(&cache->lru_mutex);
            if (entry->prev) entry->prev->next = entry->next;
            if (entry->next) entry->next->prev = entry->prev;
            if (entry == cache->lru_head) cache->lru_head = entry->next;
            if (entry == cache->lru_tail) cache->lru_tail = entry->prev;
            cache->current_entries--;
            cache->current_memory_usage -= entry->memory_usage;
            pthread_mutex_unlock(&cache->lru_mutex);
            
            // Destroy entry
            destroy_cache_entry(entry);
            
            pthread_mutex_unlock(&cache->mutex_groups[mutex_group]);
            
            set_error(CACHE_ERROR_NONE);
            return true;
        }
        prev = entry;
        entry = entry->hash_next;
    }
    
    pthread_mutex_unlock(&cache->mutex_groups[mutex_group]);
    
    set_error(CACHE_ERROR_ENTRY_NOT_FOUND);
    return false;
}

bool cache_engine_contains(CacheEngine* cache, uint64_t key) {
    if (!validate_cache(cache)) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        return false;
    }
    
    size_t hash_index = get_hash_index(cache, key);
    size_t mutex_group = get_mutex_group(cache, hash_index);
    
    pthread_mutex_lock(&cache->mutex_groups[mutex_group]);
    
    CacheEntry* entry = cache->hash_table[hash_index];
    while (entry) {
        if (entry->key == key) {
            pthread_mutex_unlock(&cache->mutex_groups[mutex_group]);
            set_error(CACHE_ERROR_NONE);
            return true;
        }
        entry = entry->hash_next;
    }
    
    pthread_mutex_unlock(&cache->mutex_groups[mutex_group]);
    
    set_error(CACHE_ERROR_NONE);
    return false;
}

// Advanced operations
bool cache_engine_prefetch(CacheEngine* cache, uint64_t key, BlockEntity* block) {
    if (!validate_cache(cache) || !block) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        return false;
    }
    
    // Mark block as prefetch
    block_entity_set_state(block, BLOCK_STATE_CLEAN);
    
    bool result = cache_engine_put(cache, key, block);
    if (result) {
        // Mark entry as prefetch
        size_t hash_index = get_hash_index(cache, key);
        size_t mutex_group = get_mutex_group(cache, hash_index);
        
        pthread_mutex_lock(&cache->mutex_groups[mutex_group]);
        CacheEntry* entry = cache->hash_table[hash_index];
        while (entry) {
            if (entry->key == key) {
                entry->state = CACHE_ENTRY_PREFETCH;
                break;
            }
            entry = entry->hash_next;
        }
        pthread_mutex_unlock(&cache->mutex_groups[mutex_group]);
    }
    
    return result;
}

bool cache_engine_pin(CacheEngine* cache, uint64_t key) {
    if (!validate_cache(cache)) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        return false;
    }
    
    size_t hash_index = get_hash_index(cache, key);
    size_t mutex_group = get_mutex_group(cache, hash_index);
    
    pthread_mutex_lock(&cache->mutex_groups[mutex_group]);
    
    CacheEntry* entry = cache->hash_table[hash_index];
    while (entry) {
        if (entry->key == key) {
            entry->is_pinned = true;
            pthread_mutex_unlock(&cache->mutex_groups[mutex_group]);
            set_error(CACHE_ERROR_NONE);
            return true;
        }
        entry = entry->hash_next;
    }
    
    pthread_mutex_unlock(&cache->mutex_groups[mutex_group]);
    
    set_error(CACHE_ERROR_ENTRY_NOT_FOUND);
    return false;
}

bool cache_engine_unpin(CacheEngine* cache, uint64_t key) {
    if (!validate_cache(cache)) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        return false;
    }
    
    size_t hash_index = get_hash_index(cache, key);
    size_t mutex_group = get_mutex_group(cache, hash_index);
    
    pthread_mutex_lock(&cache->mutex_groups[mutex_group]);
    
    CacheEntry* entry = cache->hash_table[hash_index];
    while (entry) {
        if (entry->key == key) {
            entry->is_pinned = false;
            pthread_mutex_unlock(&cache->mutex_groups[mutex_group]);
            set_error(CACHE_ERROR_NONE);
            return true;
        }
        entry = entry->hash_next;
    }
    
    pthread_mutex_unlock(&cache->mutex_groups[mutex_group]);
    
    set_error(CACHE_ERROR_ENTRY_NOT_FOUND);
    return false;
}

bool cache_engine_mark_dirty(CacheEngine* cache, uint64_t key) {
    if (!validate_cache(cache)) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        return false;
    }
    
    size_t hash_index = get_hash_index(cache, key);
    size_t mutex_group = get_mutex_group(cache, hash_index);
    
    pthread_mutex_lock(&cache->mutex_groups[mutex_group]);
    
    CacheEntry* entry = cache->hash_table[hash_index];
    while (entry) {
        if (entry->key == key) {
            entry->state = CACHE_ENTRY_DIRTY;
            if (entry->block) {
                block_entity_set_state(entry->block, BLOCK_STATE_DIRTY);
            }
            pthread_mutex_unlock(&cache->mutex_groups[mutex_group]);
            set_error(CACHE_ERROR_NONE);
            return true;
        }
        entry = entry->hash_next;
    }
    
    pthread_mutex_unlock(&cache->mutex_groups[mutex_group]);
    
    set_error(CACHE_ERROR_ENTRY_NOT_FOUND);
    return false;
}

// Batch operations
bool cache_engine_put_batch(CacheEngine* cache, uint64_t* keys, BlockEntity** blocks, size_t count) {
    if (!validate_cache(cache) || !keys || !blocks || count == 0) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        return false;
    }
    
    bool all_success = true;
    for (size_t i = 0; i < count; i++) {
        if (!cache_engine_put(cache, keys[i], blocks[i])) {
            all_success = false;
        }
    }
    
    set_error(all_success ? CACHE_ERROR_NONE : CACHE_ERROR_INVALID_PARAM);
    return all_success;
}

bool cache_engine_get_batch(CacheEngine* cache, uint64_t* keys, BlockEntity** blocks, size_t count) {
    if (!validate_cache(cache) || !keys || !blocks || count == 0) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        return false;
    }
    
    bool all_success = true;
    for (size_t i = 0; i < count; i++) {
        blocks[i] = cache_engine_get(cache, keys[i]);
        if (!blocks[i]) {
            all_success = false;
        }
    }
    
    set_error(all_success ? CACHE_ERROR_NONE : CACHE_ERROR_ENTRY_NOT_FOUND);
    return all_success;
}

size_t cache_engine_evict_batch(CacheEngine* cache, size_t target_count) {
    if (!validate_cache(cache) || target_count == 0) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        return 0;
    }
    
    size_t evicted = 0;
    pthread_mutex_lock(&cache->lru_mutex);
    
    CacheEntry* entry = cache->lru_tail;
    while (entry && evicted < target_count) {
        if (!entry->is_pinned) {
            CacheEntry* to_evict = entry;
            entry = entry->prev;
            
            // Remove from LRU list
            if (to_evict->prev) to_evict->prev->next = to_evict->next;
            if (to_evict->next) to_evict->next->prev = to_evict->prev;
            if (to_evict == cache->lru_head) cache->lru_head = to_evict->next;
            if (to_evict == cache->lru_tail) cache->lru_tail = to_evict->prev;
            
            // Remove from hash table
            size_t hash_index = get_hash_index(cache, to_evict->key);
            size_t mutex_group = get_mutex_group(cache, hash_index);
            
            pthread_mutex_lock(&cache->mutex_groups[mutex_group]);
            CacheEntry* hash_entry = cache->hash_table[hash_index];
            CacheEntry* hash_prev = NULL;
            
            while (hash_entry) {
                if (hash_entry == to_evict) {
                    if (hash_prev) {
                        hash_prev->hash_next = hash_entry->hash_next;
                    } else {
                        cache->hash_table[hash_index] = hash_entry->hash_next;
                    }
                    break;
                }
                hash_prev = hash_entry;
                hash_entry = hash_entry->hash_next;
            }
            pthread_mutex_unlock(&cache->mutex_groups[mutex_group]);
            
            // Update metrics
            cache->current_entries--;
            cache->current_memory_usage -= to_evict->memory_usage;
            
            // Write back if dirty
            if (to_evict->state == CACHE_ENTRY_DIRTY && to_evict->block) {
                // Here you would write back to storage
                cache->metrics.write_backs++;
            }
            
            destroy_cache_entry(to_evict);
            evicted++;
        } else {
            entry = entry->prev;
        }
    }
    
    pthread_mutex_unlock(&cache->lru_mutex);
    
    // Update metrics
    pthread_mutex_lock(&cache->metrics_mutex);
    cache->metrics.evictions += evicted;
    pthread_mutex_unlock(&cache->metrics_mutex);
    
    set_error(CACHE_ERROR_NONE);
    return evicted;
}

// Memory management
bool cache_engine_resize(CacheEngine* cache, size_t new_max_entries) {
    if (!validate_cache(cache) || new_max_entries < MIN_CACHE_ENTRIES) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        return false;
    }
    
    // For now, just update the config
    // A full resize would require rehashing all entries
    cache->config.max_entries = new_max_entries;
    
    set_error(CACHE_ERROR_NONE);
    return true;
}

bool cache_engine_compact(CacheEngine* cache) {
    if (!validate_cache(cache)) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        return false;
    }
    
    // Evict some entries to free memory
    size_t target_evict = cache->current_entries / 4; // Evict 25%
    cache_engine_evict_batch(cache, target_evict);
    
    set_error(CACHE_ERROR_NONE);
    return true;
}

bool cache_engine_clear(CacheEngine* cache) {
    if (!validate_cache(cache)) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        return false;
    }
    
    // Clear all entries
    for (size_t i = 0; i < cache->hash_table_size; i++) {
        CacheEntry* entry = cache->hash_table[i];
        while (entry) {
            CacheEntry* next = entry->hash_next;
            destroy_cache_entry(entry);
            entry = next;
        }
        cache->hash_table[i] = NULL;
    }
    
    pthread_mutex_lock(&cache->lru_mutex);
    cache->lru_head = NULL;
    cache->lru_tail = NULL;
    cache->current_entries = 0;
    cache->current_memory_usage = 0;
    pthread_mutex_unlock(&cache->lru_mutex);
    
    set_error(CACHE_ERROR_NONE);
    return true;
}

size_t cache_engine_get_memory_usage(const CacheEngine* cache) {
    if (!validate_cache(cache)) {
        return 0;
    }
    
    return cache->current_memory_usage;
}

// Eviction control
bool cache_engine_set_eviction_strategy(CacheEngine* cache, CacheEvictionStrategy strategy) {
    if (!validate_cache(cache)) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        return false;
    }
    
    cache->config.eviction_strategy = strategy;
    
    set_error(CACHE_ERROR_NONE);
    return true;
}

bool cache_engine_force_eviction(CacheEngine* cache, size_t count) {
    return cache_engine_evict_batch(cache, count) == count;
}

bool cache_engine_protect_from_eviction(CacheEngine* cache, uint64_t key) {
    return cache_engine_pin(cache, key);
}

// Metrics and monitoring
CacheMetrics cache_engine_get_metrics(const CacheEngine* cache) {
    CacheMetrics metrics = {0};
    
    if (!validate_cache(cache)) {
        return metrics;
    }
    
    pthread_mutex_lock(&cache->metrics_mutex);
    metrics = cache->metrics;
    pthread_mutex_unlock(&cache->metrics_mutex);
    
    return metrics;
}

void cache_engine_reset_metrics(CacheEngine* cache) {
    if (!validate_cache(cache)) {
        return;
    }
    
    pthread_mutex_lock(&cache->metrics_mutex);
    memset(&cache->metrics, 0, sizeof(CacheMetrics));
    cache->metrics.last_reset = time(NULL);
    pthread_mutex_unlock(&cache->metrics_mutex);
}

bool cache_engine_print_stats(const CacheEngine* cache, FILE* stream) {
    if (!validate_cache(cache) || !stream) {
        return false;
    }
    
    CacheMetrics metrics = cache_engine_get_metrics(cache);
    
    fprintf(stream, "Cache Statistics:\n");
    fprintf(stream, "  Total Requests: %lu\n", metrics.total_requests);
    fprintf(stream, "  Cache Hits: %lu\n", metrics.cache_hits);
    fprintf(stream, "  Cache Misses: %lu\n", metrics.cache_misses);
    fprintf(stream, "  Hit Ratio: %.2f%%\n", metrics.hit_ratio * 100.0);
    fprintf(stream, "  Evictions: %lu\n", metrics.evictions);
    fprintf(stream, "  Write Backs: %lu\n", metrics.write_backs);
    fprintf(stream, "  Memory Usage: %zu bytes\n", metrics.memory_usage);
    fprintf(stream, "  Max Memory Usage: %zu bytes\n", metrics.max_memory_usage);
    fprintf(stream, "  Current Entries: %zu\n", cache->current_entries);
    fprintf(stream, "  Max Entries: %zu\n", cache->config.max_entries);
    
    return true;
}

// Configuration
bool cache_engine_update_config(CacheEngine* cache, const CacheConfig* new_config) {
    if (!validate_cache(cache) || !validate_config(new_config)) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        return false;
    }
    
    cache->config = *new_config;
    
    set_error(CACHE_ERROR_NONE);
    return true;
}

CacheConfig cache_engine_get_config(const CacheEngine* cache) {
    CacheConfig config = {0};
    
    if (!validate_cache(cache)) {
        return config;
    }
    
    return cache->config;
}

// Background operations
static void* cleanup_thread_func(void* arg) {
    CacheEngine* cache = (CacheEngine*)arg;
    
    while (cache->cleanup_running) {
        sleep(cache->config.cleanup_interval_seconds);
        
        if (cache->cleanup_running) {
            cache_engine_run_cleanup(cache);
        }
    }
    
    return NULL;
}

bool cache_engine_start_cleanup_thread(CacheEngine* cache) {
    if (!validate_cache(cache)) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        return false;
    }
    
    if (cache->cleanup_running) {
        set_error(CACHE_ERROR_NONE);
        return true; // Already running
    }
    
    cache->cleanup_running = true;
    
    if (pthread_create(&cache->cleanup_thread, NULL, cleanup_thread_func, cache) != 0) {
        cache->cleanup_running = false;
        set_error(CACHE_ERROR_THREAD_CREATION);
        return false;
    }
    
    set_error(CACHE_ERROR_NONE);
    return true;
}

bool cache_engine_stop_cleanup_thread(CacheEngine* cache) {
    if (!validate_cache(cache)) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        return false;
    }
    
    if (!cache->cleanup_running) {
        set_error(CACHE_ERROR_NONE);
        return true; // Already stopped
    }
    
    cache->cleanup_running = false;
    pthread_join(cache->cleanup_thread, NULL);
    
    set_error(CACHE_ERROR_NONE);
    return true;
}

bool cache_engine_run_cleanup(CacheEngine* cache) {
    if (!validate_cache(cache)) {
        set_error(CACHE_ERROR_INVALID_PARAM);
        return false;
    }
    
    // Update eviction scores for all entries
    pthread_mutex_lock(&cache->lru_mutex);
    CacheEntry* entry = cache->lru_head;
    while (entry) {
        update_eviction_score(entry, cache->config.eviction_strategy);
        entry = entry->next;
    }
    pthread_mutex_unlock(&cache->lru_mutex);
    
    // Evict some entries if needed
    if (cache->current_entries > cache->config.max_entries * 0.9) {
        size_t to_evict = cache->current_entries - cache->config.max_entries * 0.8;
        cache_engine_evict_batch(cache, to_evict);
    }
    
    set_error(CACHE_ERROR_NONE);
    return true;
}

// Validation and debugging
bool cache_engine_validate_integrity(const CacheEngine* cache) {
    if (!validate_cache(cache)) {
        return false;
    }
    
    // Check hash table consistency
    size_t counted_entries = 0;
    for (size_t i = 0; i < cache->hash_table_size; i++) {
        CacheEntry* entry = cache->hash_table[i];
        while (entry) {
            counted_entries++;
            entry = entry->hash_next;
        }
    }
    
    return counted_entries == cache->current_entries;
}

bool cache_engine_dump_state(const CacheEngine* cache, FILE* stream) {
    if (!validate_cache(cache) || !stream) {
        return false;
    }
    
    fprintf(stream, "Cache State Dump:\n");
    fprintf(stream, "  Hash Table Size: %zu\n", cache->hash_table_size);
    fprintf(stream, "  Current Entries: %zu\n", cache->current_entries);
    fprintf(stream, "  Memory Usage: %zu bytes\n", cache->current_memory_usage);
    fprintf(stream, "  LRU Head: %p\n", (void*)cache->lru_head);
    fprintf(stream, "  LRU Tail: %p\n", (void*)cache->lru_tail);
    
    return true;
}

size_t cache_engine_count_entries(const CacheEngine* cache) {
    if (!validate_cache(cache)) {
        return 0;
    }
    
    return cache->current_entries;
}

// Utility functions
uint64_t cache_engine_calculate_hash(uint64_t key) {
    return fnv1a_hash(key);
}

const char* cache_engine_eviction_strategy_to_string(CacheEvictionStrategy strategy) {
    switch (strategy) {
        case CACHE_EVICTION_LRU: return "LRU";
        case CACHE_EVICTION_LFU: return "LFU";
        case CACHE_EVICTION_FIFO: return "FIFO";
        case CACHE_EVICTION_RANDOM: return "RANDOM";
        case CACHE_EVICTION_ADAPTIVE: return "ADAPTIVE";
        default: return "UNKNOWN";
    }
}

const char* cache_engine_entry_state_to_string(CacheEntryState state) {
    switch (state) {
        case CACHE_ENTRY_CLEAN: return "CLEAN";
        case CACHE_ENTRY_DIRTY: return "DIRTY";
        case CACHE_ENTRY_PINNED: return "PINNED";
        case CACHE_ENTRY_PREFETCH: return "PREFETCH";
        default: return "UNKNOWN";
    }
}

// Thread safety
bool cache_engine_lock_entry(CacheEngine* cache, uint64_t key) {
    // This would implement entry-level locking
    // For now, just return success
    return true;
}

bool cache_engine_unlock_entry(CacheEngine* cache, uint64_t key) {
    // This would implement entry-level unlocking
    // For now, just return success
    return true;
} 