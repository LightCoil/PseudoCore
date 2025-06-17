#ifndef BLOCK_ENTITY_H
#define BLOCK_ENTITY_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Block state enumeration
typedef enum {
    BLOCK_STATE_INVALID = 0,
    BLOCK_STATE_CLEAN,
    BLOCK_STATE_DIRTY,
    BLOCK_STATE_LOCKED,
    BLOCK_STATE_COMPRESSED,
    BLOCK_STATE_CORRUPTED
} BlockState;

// Block access pattern
typedef enum {
    BLOCK_ACCESS_RANDOM = 0,
    BLOCK_ACCESS_SEQUENTIAL,
    BLOCK_ACCESS_STRIDED,
    BLOCK_ACCESS_PATTERN_UNKNOWN
} BlockAccessPattern;

// Block compression metadata
typedef struct {
    uint32_t original_size;
    uint32_t compressed_size;
    uint8_t compression_level;
    uint8_t compression_algorithm;
    uint32_t compression_checksum;
    time_t compression_time;
} BlockCompressionInfo;

// Block cache metadata
typedef struct {
    time_t last_access;
    time_t last_modified;
    uint32_t access_count;
    uint32_t hit_count;
    double access_frequency;
    BlockAccessPattern pattern;
} BlockCacheInfo;

// Block entity with full encapsulation and integrity checking
typedef struct BlockEntity {
    // Immutable properties
    const uint64_t offset;
    const uint32_t size;
    const uint32_t block_id;
    
    // Mutable state
    BlockState state;
    uint32_t version;
    uint32_t reference_count;
    
    // Data integrity
    uint64_t data_checksum;
    uint64_t metadata_checksum;
    uint32_t crc32;
    
    // Performance metadata
    BlockCompressionInfo compression_info;
    BlockCacheInfo cache_info;
    
    // Data storage
    void* data;
    size_t data_capacity;
    bool data_owned;
    
    // Locking and synchronization
    pthread_mutex_t lock;
    pthread_cond_t condition;
    bool is_locked;
    uint32_t lock_owner;
    
    // Validation and lifecycle
    bool is_initialized;
    time_t created_time;
    time_t last_modified;
    uint32_t modification_count;
} BlockEntity;

// Block entity interface functions
BlockEntity* block_entity_create(uint64_t offset, uint32_t size, uint32_t block_id);
void block_entity_destroy(BlockEntity* block);

// State management
BlockState block_entity_get_state(const BlockEntity* block);
bool block_entity_set_state(BlockEntity* block, BlockState new_state);
bool block_entity_is_valid_state(const BlockEntity* block);
bool block_entity_is_dirty(const BlockEntity* block);
bool block_entity_is_compressed(const BlockEntity* block);

// Data management
bool block_entity_set_data(BlockEntity* block, const void* data, size_t size);
const void* block_entity_get_data(const BlockEntity* block);
size_t block_entity_get_data_size(const BlockEntity* block);
bool block_entity_resize_data(BlockEntity* block, size_t new_size);
bool block_entity_clear_data(BlockEntity* block);

// Integrity checking
bool block_entity_verify_integrity(const BlockEntity* block);
uint64_t block_entity_calculate_data_checksum(const BlockEntity* block);
uint64_t block_entity_calculate_metadata_checksum(const BlockEntity* block);
bool block_entity_update_checksums(BlockEntity* block);
bool block_entity_validate_crc32(const BlockEntity* block);
uint32_t block_entity_calculate_crc32(const BlockEntity* block);

// Compression management
bool block_entity_set_compression_info(BlockEntity* block, const BlockCompressionInfo* info);
BlockCompressionInfo block_entity_get_compression_info(const BlockEntity* block);
bool block_entity_is_compressed_data(const BlockEntity* block);
double block_entity_get_compression_ratio(const BlockEntity* block);

// Cache management
void block_entity_update_cache_info(BlockEntity* block, bool is_hit);
BlockCacheInfo block_entity_get_cache_info(const BlockEntity* block);
bool block_entity_is_hot(const BlockEntity* block);
double block_entity_get_access_frequency(const BlockEntity* block);

// Locking and synchronization
bool block_entity_lock(BlockEntity* block, uint32_t owner_id);
bool block_entity_unlock(BlockEntity* block, uint32_t owner_id);
bool block_entity_is_locked(const BlockEntity* block);
uint32_t block_entity_get_lock_owner(const BlockEntity* block);
bool block_entity_wait_for_unlock(BlockEntity* block, uint32_t timeout_ms);

// Reference counting
uint32_t block_entity_increment_reference(BlockEntity* block);
uint32_t block_entity_decrement_reference(BlockEntity* block);
uint32_t block_entity_get_reference_count(const BlockEntity* block);
bool block_entity_is_referenced(const BlockEntity* block);

// Version management
uint32_t block_entity_get_version(const BlockEntity* block);
bool block_entity_increment_version(BlockEntity* block);
bool block_entity_set_version(BlockEntity* block, uint32_t version);

// Validation
bool block_entity_is_valid(const BlockEntity* block);
bool block_entity_validate_parameters(uint64_t offset, uint32_t size, uint32_t block_id);
bool block_entity_validate_data_size(const BlockEntity* block, size_t size);

// Utility functions
const char* block_entity_state_to_string(BlockState state);
const char* block_entity_access_pattern_to_string(BlockAccessPattern pattern);
uint64_t block_entity_calculate_hash(const BlockEntity* block);
bool block_entity_compare(const BlockEntity* block1, const BlockEntity* block2);

// Memory management
size_t block_entity_get_memory_usage(const BlockEntity* block);
bool block_entity_optimize_memory(BlockEntity* block);
bool block_entity_compact_data(BlockEntity* block);

// Serialization (for persistence)
bool block_entity_serialize(const BlockEntity* block, void* buffer, size_t buffer_size, size_t* written);
bool block_entity_deserialize(BlockEntity* block, const void* buffer, size_t buffer_size);

#endif // BLOCK_ENTITY_H 