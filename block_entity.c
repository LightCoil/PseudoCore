#include "block_entity.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <zlib.h>

// Internal validation constants
#define MAX_BLOCK_SIZE (1024ULL * 1024ULL * 1024ULL) // 1GB
#define MIN_BLOCK_SIZE 1
#define MAX_BLOCK_ID 0xFFFFFFFF
#define MAX_REFERENCE_COUNT 0xFFFFFFFF
#define MAX_VERSION 0xFFFFFFFF

// Internal error codes
typedef enum {
    BLOCK_ERROR_NONE = 0,
    BLOCK_ERROR_INVALID_PARAM,
    BLOCK_ERROR_MEMORY_ALLOCATION,
    BLOCK_ERROR_INVALID_STATE,
    BLOCK_ERROR_BUFFER_OVERFLOW,
    BLOCK_ERROR_CHECKSUM_MISMATCH,
    BLOCK_ERROR_LOCK_TIMEOUT,
    BLOCK_ERROR_CORRUPTION
} BlockError;

// Internal error tracking
static BlockError last_error = BLOCK_ERROR_NONE;

// Error handling
static void set_error(BlockError error) {
    last_error = error;
}

BlockError block_entity_get_last_error(void) {
    return last_error;
}

const char* block_entity_error_to_string(BlockError error) {
    switch (error) {
        case BLOCK_ERROR_NONE: return "No error";
        case BLOCK_ERROR_INVALID_PARAM: return "Invalid parameter";
        case BLOCK_ERROR_MEMORY_ALLOCATION: return "Memory allocation failed";
        case BLOCK_ERROR_INVALID_STATE: return "Invalid state transition";
        case BLOCK_ERROR_BUFFER_OVERFLOW: return "Buffer overflow";
        case BLOCK_ERROR_CHECKSUM_MISMATCH: return "Checksum mismatch";
        case BLOCK_ERROR_LOCK_TIMEOUT: return "Lock timeout";
        case BLOCK_ERROR_CORRUPTION: return "Data corruption detected";
        default: return "Unknown error";
    }
}

// Validation functions
static bool validate_parameters(uint64_t offset, uint32_t size, uint32_t block_id) {
    return size >= MIN_BLOCK_SIZE && size <= MAX_BLOCK_SIZE && 
           block_id > 0 && block_id <= MAX_BLOCK_ID;
}

static bool validate_block(const BlockEntity* block) {
    return block != NULL && block->is_initialized;
}

static bool validate_data_size(const BlockEntity* block, size_t size) {
    return size <= MAX_BLOCK_SIZE && size <= block->data_capacity;
}

// CRC32 calculation using zlib
static uint32_t calculate_crc32(const void* data, size_t size) {
    return crc32(0, (const Bytef*)data, size);
}

// Block entity creation with full validation
BlockEntity* block_entity_create(uint64_t offset, uint32_t size, uint32_t block_id) {
    // Parameter validation
    if (!validate_parameters(offset, size, block_id)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    // Allocate block entity
    BlockEntity* block = malloc(sizeof(BlockEntity));
    if (!block) {
        set_error(BLOCK_ERROR_MEMORY_ALLOCATION);
        return NULL;
    }
    
    // Initialize immutable properties
    *(uint64_t*)&block->offset = offset;
    *(uint32_t*)&block->size = size;
    *(uint32_t*)&block->block_id = block_id;
    
    // Initialize mutable state
    block->state = BLOCK_STATE_CLEAN;
    block->version = 1;
    block->reference_count = 0;
    
    // Initialize integrity
    block->data_checksum = 0;
    block->metadata_checksum = 0;
    block->crc32 = 0;
    
    // Initialize performance metadata
    memset(&block->compression_info, 0, sizeof(BlockCompressionInfo));
    memset(&block->cache_info, 0, sizeof(BlockCacheInfo));
    block->cache_info.last_access = time(NULL);
    block->cache_info.last_modified = time(NULL);
    block->cache_info.pattern = BLOCK_ACCESS_PATTERN_UNKNOWN;
    
    // Initialize data storage
    block->data = NULL;
    block->data_capacity = 0;
    block->data_owned = false;
    
    // Initialize locking
    if (pthread_mutex_init(&block->lock, NULL) != 0) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        free(block);
        return NULL;
    }
    
    if (pthread_cond_init(&block->condition, NULL) != 0) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        pthread_mutex_destroy(&block->lock);
        free(block);
        return NULL;
    }
    
    block->is_locked = false;
    block->lock_owner = 0;
    
    // Initialize validation and lifecycle
    block->is_initialized = true;
    block->created_time = time(NULL);
    block->last_modified = block->created_time;
    block->modification_count = 0;
    
    // Calculate initial checksums
    block_entity_update_checksums(block);
    
    set_error(BLOCK_ERROR_NONE);
    return block;
}

// Safe destruction with cleanup
void block_entity_destroy(BlockEntity* block) {
    if (!block) return;
    
    // Wait for any locks to be released
    pthread_mutex_lock(&block->lock);
    while (block->is_locked) {
        pthread_cond_wait(&block->condition, &block->lock);
    }
    pthread_mutex_unlock(&block->lock);
    
    // Free data if owned
    if (block->data_owned && block->data) {
        free(block->data);
    }
    
    // Cleanup threading primitives
    pthread_mutex_destroy(&block->lock);
    pthread_cond_destroy(&block->condition);
    
    // Clear sensitive data
    memset(block, 0, sizeof(BlockEntity));
    free(block);
}

// State management
BlockState block_entity_get_state(const BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return BLOCK_STATE_INVALID;
    }
    
    set_error(BLOCK_ERROR_NONE);
    return block->state;
}

bool block_entity_set_state(BlockEntity* block, BlockState new_state) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&block->lock);
    block->state = new_state;
    block->last_modified = time(NULL);
    block->modification_count++;
    pthread_mutex_unlock(&block->lock);
    
    block_entity_update_checksums(block);
    
    set_error(BLOCK_ERROR_NONE);
    return true;
}

bool block_entity_is_valid_state(const BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    set_error(BLOCK_ERROR_NONE);
    return block->state != BLOCK_STATE_INVALID && block->state != BLOCK_STATE_CORRUPTED;
}

bool block_entity_is_dirty(const BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    set_error(BLOCK_ERROR_NONE);
    return block->state == BLOCK_STATE_DIRTY;
}

bool block_entity_is_compressed(const BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    set_error(BLOCK_ERROR_NONE);
    return block->state == BLOCK_STATE_COMPRESSED;
}

// Data management
bool block_entity_set_data(BlockEntity* block, const void* data, size_t size) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    if (!validate_data_size(block, size)) {
        set_error(BLOCK_ERROR_BUFFER_OVERFLOW);
        return false;
    }
    
    pthread_mutex_lock(&block->lock);
    
    // Allocate buffer if needed
    if (!block->data || block->data_capacity < size) {
        void* new_data = realloc(block->data, size);
        if (!new_data) {
            pthread_mutex_unlock(&block->lock);
            set_error(BLOCK_ERROR_MEMORY_ALLOCATION);
            return false;
        }
        block->data = new_data;
        block->data_capacity = size;
        block->data_owned = true;
    }
    
    // Copy data
    if (data) {
        memcpy(block->data, data, size);
    } else {
        memset(block->data, 0, size);
    }
    
    // Update state and metadata
    block->state = BLOCK_STATE_DIRTY;
    block->last_modified = time(NULL);
    block->modification_count++;
    
    pthread_mutex_unlock(&block->lock);
    
    // Update checksums
    block_entity_update_checksums(block);
    
    set_error(BLOCK_ERROR_NONE);
    return true;
}

const void* block_entity_get_data(const BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    set_error(BLOCK_ERROR_NONE);
    return block->data;
}

size_t block_entity_get_data_size(const BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return 0;
    }
    
    set_error(BLOCK_ERROR_NONE);
    return block->data_capacity;
}

bool block_entity_resize_data(BlockEntity* block, size_t new_size) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    if (new_size > MAX_BLOCK_SIZE) {
        set_error(BLOCK_ERROR_BUFFER_OVERFLOW);
        return false;
    }
    
    pthread_mutex_lock(&block->lock);
    
    if (new_size == block->data_capacity) {
        pthread_mutex_unlock(&block->lock);
        set_error(BLOCK_ERROR_NONE);
        return true; // No change needed
    }
    
    void* new_data = realloc(block->data, new_size);
    if (!new_data) {
        pthread_mutex_unlock(&block->lock);
        set_error(BLOCK_ERROR_MEMORY_ALLOCATION);
        return false;
    }
    
    block->data = new_data;
    block->data_capacity = new_size;
    block->data_owned = true;
    
    // Zero out new memory if expanding
    if (new_size > block->data_capacity) {
        memset((char*)block->data + block->data_capacity, 0, 
               new_size - block->data_capacity);
    }
    
    block->last_modified = time(NULL);
    block->modification_count++;
    
    pthread_mutex_unlock(&block->lock);
    
    block_entity_update_checksums(block);
    
    set_error(BLOCK_ERROR_NONE);
    return true;
}

bool block_entity_clear_data(BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&block->lock);
    
    if (block->data) {
        memset(block->data, 0, block->data_capacity);
    }
    
    block->state = BLOCK_STATE_CLEAN;
    block->last_modified = time(NULL);
    block->modification_count++;
    
    pthread_mutex_unlock(&block->lock);
    
    block_entity_update_checksums(block);
    
    set_error(BLOCK_ERROR_NONE);
    return true;
}

// Integrity checking
bool block_entity_verify_integrity(const BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    // Verify data checksum
    uint64_t calculated_data_checksum = block_entity_calculate_data_checksum(block);
    if (calculated_data_checksum != block->data_checksum) {
        set_error(BLOCK_ERROR_CHECKSUM_MISMATCH);
        return false;
    }
    
    // Verify metadata checksum
    uint64_t calculated_metadata_checksum = block_entity_calculate_metadata_checksum(block);
    if (calculated_metadata_checksum != block->metadata_checksum) {
        set_error(BLOCK_ERROR_CHECKSUM_MISMATCH);
        return false;
    }
    
    // Verify CRC32
    if (!block_entity_validate_crc32(block)) {
        set_error(BLOCK_ERROR_CHECKSUM_MISMATCH);
        return false;
    }
    
    set_error(BLOCK_ERROR_NONE);
    return true;
}

uint64_t block_entity_calculate_data_checksum(const BlockEntity* block) {
    if (!validate_block(block) || !block->data) {
        return 0;
    }
    
    // Use FNV-1a hash for data checksum
    const uint64_t FNV_PRIME = 1099511628211ULL;
    const uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    
    uint64_t hash = FNV_OFFSET_BASIS;
    const uint8_t* bytes = (const uint8_t*)block->data;
    size_t size = block->data_capacity;
    
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= FNV_PRIME;
    }
    
    return hash;
}

uint64_t block_entity_calculate_metadata_checksum(const BlockEntity* block) {
    if (!validate_block(block)) {
        return 0;
    }
    
    // Calculate checksum over metadata fields
    uint64_t checksum = 0;
    
    checksum ^= block->offset;
    checksum ^= (uint64_t)block->size << 32;
    checksum ^= (uint64_t)block->block_id << 48;
    checksum ^= (uint64_t)block->state << 56;
    checksum ^= block->version;
    checksum ^= (uint64_t)block->reference_count << 32;
    checksum ^= block->created_time;
    checksum ^= block->last_modified;
    checksum ^= (uint64_t)block->modification_count << 32;
    
    return checksum;
}

bool block_entity_update_checksums(BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    block->data_checksum = block_entity_calculate_data_checksum(block);
    block->metadata_checksum = block_entity_calculate_metadata_checksum(block);
    block->crc32 = block_entity_calculate_crc32(block);
    
    set_error(BLOCK_ERROR_NONE);
    return true;
}

bool block_entity_validate_crc32(const BlockEntity* block) {
    if (!validate_block(block) || !block->data) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    uint32_t calculated = calculate_crc32(block->data, block->data_capacity);
    bool valid = (calculated == block->crc32);
    
    if (!valid) {
        set_error(BLOCK_ERROR_CHECKSUM_MISMATCH);
    } else {
        set_error(BLOCK_ERROR_NONE);
    }
    
    return valid;
}

uint32_t block_entity_calculate_crc32(const BlockEntity* block) {
    if (!validate_block(block) || !block->data) {
        return 0;
    }
    
    return calculate_crc32(block->data, block->data_capacity);
}

// Compression management
bool block_entity_set_compression_info(BlockEntity* block, const BlockCompressionInfo* info) {
    if (!validate_block(block) || !info) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&block->lock);
    block->compression_info = *info;
    block->last_modified = time(NULL);
    block->modification_count++;
    pthread_mutex_unlock(&block->lock);
    
    block_entity_update_checksums(block);
    
    set_error(BLOCK_ERROR_NONE);
    return true;
}

BlockCompressionInfo block_entity_get_compression_info(const BlockEntity* block) {
    BlockCompressionInfo info = {0};
    
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return info;
    }
    
    set_error(BLOCK_ERROR_NONE);
    return block->compression_info;
}

bool block_entity_is_compressed_data(const BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    set_error(BLOCK_ERROR_NONE);
    return block->compression_info.compressed_size > 0 && 
           block->compression_info.compressed_size < block->compression_info.original_size;
}

double block_entity_get_compression_ratio(const BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return 0.0;
    }
    
    if (block->compression_info.original_size == 0) {
        set_error(BLOCK_ERROR_NONE);
        return 0.0;
    }
    
    double ratio = (double)block->compression_info.compressed_size / 
                   block->compression_info.original_size;
    
    set_error(BLOCK_ERROR_NONE);
    return ratio;
}

// Cache management
void block_entity_update_cache_info(BlockEntity* block, bool is_hit) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return;
    }
    
    pthread_mutex_lock(&block->lock);
    
    time_t now = time(NULL);
    block->cache_info.last_access = now;
    block->cache_info.access_count++;
    
    if (is_hit) {
        block->cache_info.hit_count++;
    }
    
    // Calculate access frequency (accesses per second over last 100 accesses)
    if (block->cache_info.access_count > 1) {
        double time_diff = difftime(now, block->cache_info.last_access);
        if (time_diff > 0) {
            block->cache_info.access_frequency = block->cache_info.access_count / time_diff;
        }
    }
    
    pthread_mutex_unlock(&block->lock);
    
    set_error(BLOCK_ERROR_NONE);
}

BlockCacheInfo block_entity_get_cache_info(const BlockEntity* block) {
    BlockCacheInfo info = {0};
    
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return info;
    }
    
    set_error(BLOCK_ERROR_NONE);
    return block->cache_info;
}

bool block_entity_is_hot(const BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    set_error(BLOCK_ERROR_NONE);
    return block->cache_info.access_count > 10 && 
           block->cache_info.access_frequency > 1.0;
}

double block_entity_get_access_frequency(const BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return 0.0;
    }
    
    set_error(BLOCK_ERROR_NONE);
    return block->cache_info.access_frequency;
}

// Locking and synchronization
bool block_entity_lock(BlockEntity* block, uint32_t owner_id) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&block->lock);
    
    if (block->is_locked && block->lock_owner != owner_id) {
        pthread_mutex_unlock(&block->lock);
        set_error(BLOCK_ERROR_LOCK_TIMEOUT);
        return false;
    }
    
    block->is_locked = true;
    block->lock_owner = owner_id;
    
    pthread_mutex_unlock(&block->lock);
    
    set_error(BLOCK_ERROR_NONE);
    return true;
}

bool block_entity_unlock(BlockEntity* block, uint32_t owner_id) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&block->lock);
    
    if (!block->is_locked || block->lock_owner != owner_id) {
        pthread_mutex_unlock(&block->lock);
        set_error(BLOCK_ERROR_INVALID_STATE);
        return false;
    }
    
    block->is_locked = false;
    block->lock_owner = 0;
    
    pthread_cond_broadcast(&block->condition);
    pthread_mutex_unlock(&block->lock);
    
    set_error(BLOCK_ERROR_NONE);
    return true;
}

bool block_entity_is_locked(const BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    set_error(BLOCK_ERROR_NONE);
    return block->is_locked;
}

uint32_t block_entity_get_lock_owner(const BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return 0;
    }
    
    set_error(BLOCK_ERROR_NONE);
    return block->lock_owner;
}

bool block_entity_wait_for_unlock(BlockEntity* block, uint32_t timeout_ms) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&block->lock);
    
    if (!block->is_locked) {
        pthread_mutex_unlock(&block->lock);
        set_error(BLOCK_ERROR_NONE);
        return true;
    }
    
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
    timeout.tv_sec += timeout_ms / 1000;
    
    int result = pthread_cond_timedwait(&block->condition, &block->lock, &timeout);
    
    pthread_mutex_unlock(&block->lock);
    
    if (result == ETIMEDOUT) {
        set_error(BLOCK_ERROR_LOCK_TIMEOUT);
        return false;
    }
    
    set_error(BLOCK_ERROR_NONE);
    return true;
}

// Reference counting
uint32_t block_entity_increment_reference(BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return 0;
    }
    
    pthread_mutex_lock(&block->lock);
    
    if (block->reference_count < MAX_REFERENCE_COUNT) {
        block->reference_count++;
    }
    
    uint32_t count = block->reference_count;
    pthread_mutex_unlock(&block->lock);
    
    set_error(BLOCK_ERROR_NONE);
    return count;
}

uint32_t block_entity_decrement_reference(BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return 0;
    }
    
    pthread_mutex_lock(&block->lock);
    
    if (block->reference_count > 0) {
        block->reference_count--;
    }
    
    uint32_t count = block->reference_count;
    pthread_mutex_unlock(&block->lock);
    
    set_error(BLOCK_ERROR_NONE);
    return count;
}

uint32_t block_entity_get_reference_count(const BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return 0;
    }
    
    set_error(BLOCK_ERROR_NONE);
    return block->reference_count;
}

bool block_entity_is_referenced(const BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    set_error(BLOCK_ERROR_NONE);
    return block->reference_count > 0;
}

// Version management
uint32_t block_entity_get_version(const BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return 0;
    }
    
    set_error(BLOCK_ERROR_NONE);
    return block->version;
}

bool block_entity_increment_version(BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&block->lock);
    
    if (block->version < MAX_VERSION) {
        block->version++;
    }
    
    block->last_modified = time(NULL);
    block->modification_count++;
    
    pthread_mutex_unlock(&block->lock);
    
    block_entity_update_checksums(block);
    
    set_error(BLOCK_ERROR_NONE);
    return true;
}

bool block_entity_set_version(BlockEntity* block, uint32_t version) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    if (version > MAX_VERSION) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&block->lock);
    block->version = version;
    block->last_modified = time(NULL);
    block->modification_count++;
    pthread_mutex_unlock(&block->lock);
    
    block_entity_update_checksums(block);
    
    set_error(BLOCK_ERROR_NONE);
    return true;
}

// Validation
bool block_entity_is_valid(const BlockEntity* block) {
    return validate_block(block);
}

bool block_entity_validate_parameters(uint64_t offset, uint32_t size, uint32_t block_id) {
    return validate_parameters(offset, size, block_id);
}

bool block_entity_validate_data_size(const BlockEntity* block, size_t size) {
    return validate_data_size(block, size);
}

// Utility functions
const char* block_entity_state_to_string(BlockState state) {
    switch (state) {
        case BLOCK_STATE_INVALID: return "INVALID";
        case BLOCK_STATE_CLEAN: return "CLEAN";
        case BLOCK_STATE_DIRTY: return "DIRTY";
        case BLOCK_STATE_LOCKED: return "LOCKED";
        case BLOCK_STATE_COMPRESSED: return "COMPRESSED";
        case BLOCK_STATE_CORRUPTED: return "CORRUPTED";
        default: return "UNKNOWN";
    }
}

const char* block_entity_access_pattern_to_string(BlockAccessPattern pattern) {
    switch (pattern) {
        case BLOCK_ACCESS_RANDOM: return "RANDOM";
        case BLOCK_ACCESS_SEQUENTIAL: return "SEQUENTIAL";
        case BLOCK_ACCESS_STRIDED: return "STRIDED";
        case BLOCK_ACCESS_PATTERN_UNKNOWN: return "UNKNOWN";
        default: return "UNKNOWN";
    }
}

uint64_t block_entity_calculate_hash(const BlockEntity* block) {
    if (!validate_block(block)) {
        return 0;
    }
    
    // Simple hash combining offset and block_id
    return block->offset ^ ((uint64_t)block->block_id << 32);
}

bool block_entity_compare(const BlockEntity* block1, const BlockEntity* block2) {
    if (!validate_block(block1) || !validate_block(block2)) {
        return false;
    }
    
    return block1->offset == block2->offset && 
           block1->block_id == block2->block_id &&
           block1->version == block2->version;
}

// Memory management
size_t block_entity_get_memory_usage(const BlockEntity* block) {
    if (!validate_block(block)) {
        return 0;
    }
    
    size_t usage = sizeof(BlockEntity);
    if (block->data_owned && block->data) {
        usage += block->data_capacity;
    }
    
    return usage;
}

bool block_entity_optimize_memory(BlockEntity* block) {
    if (!validate_block(block)) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    pthread_mutex_lock(&block->lock);
    
    // Shrink buffer if it's much larger than needed
    if (block->data && block->data_capacity > block->size * 2) {
        size_t new_capacity = block->size;
        void* new_data = realloc(block->data, new_capacity);
        if (new_data) {
            block->data = new_data;
            block->data_capacity = new_capacity;
        }
    }
    
    pthread_mutex_unlock(&block->lock);
    
    set_error(BLOCK_ERROR_NONE);
    return true;
}

bool block_entity_compact_data(BlockEntity* block) {
    return block_entity_optimize_memory(block);
}

// Serialization (basic implementation)
bool block_entity_serialize(const BlockEntity* block, void* buffer, size_t buffer_size, size_t* written) {
    if (!validate_block(block) || !buffer || !written) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    size_t required_size = sizeof(BlockEntity) + block->data_capacity;
    if (buffer_size < required_size) {
        set_error(BLOCK_ERROR_BUFFER_OVERFLOW);
        return false;
    }
    
    // Copy block metadata
    memcpy(buffer, block, sizeof(BlockEntity));
    
    // Copy data if present
    if (block->data && block->data_capacity > 0) {
        memcpy((char*)buffer + sizeof(BlockEntity), block->data, block->data_capacity);
    }
    
    *written = required_size;
    
    set_error(BLOCK_ERROR_NONE);
    return true;
}

bool block_entity_deserialize(BlockEntity* block, const void* buffer, size_t buffer_size) {
    if (!validate_block(block) || !buffer) {
        set_error(BLOCK_ERROR_INVALID_PARAM);
        return false;
    }
    
    if (buffer_size < sizeof(BlockEntity)) {
        set_error(BLOCK_ERROR_BUFFER_OVERFLOW);
        return false;
    }
    
    // Copy block metadata
    memcpy(block, buffer, sizeof(BlockEntity));
    
    // Reinitialize threading primitives
    pthread_mutex_init(&block->lock, NULL);
    pthread_cond_init(&block->condition, NULL);
    
    // Copy data if present
    size_t data_offset = sizeof(BlockEntity);
    if (data_offset < buffer_size && block->data_capacity > 0) {
        block->data = malloc(block->data_capacity);
        if (block->data) {
            memcpy(block->data, (char*)buffer + data_offset, block->data_capacity);
            block->data_owned = true;
        }
    }
    
    set_error(BLOCK_ERROR_NONE);
    return true;
} 