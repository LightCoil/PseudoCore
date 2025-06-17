#include "compression_engine.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <zstd.h>
#include <lz4.h>
#include <zlib.h>

// Internal constants
#define MAX_COMPRESSION_THREADS 16
#define MIN_COMPRESSION_SIZE 64
#define MAX_COMPRESSION_SIZE (1024ULL * 1024ULL * 1024ULL) // 1GB
#define ALGORITHM_COUNT 5
#define PERFORMANCE_WINDOW 100
#define ADAPTIVE_LEARNING_RATE 0.1

// Internal error codes
typedef enum {
    COMPRESSION_ERROR_NONE = 0,
    COMPRESSION_ERROR_INVALID_PARAM,
    COMPRESSION_ERROR_MEMORY_ALLOCATION,
    COMPRESSION_ERROR_BUFFER_TOO_SMALL,
    COMPRESSION_ERROR_COMPRESSION_FAILED,
    COMPRESSION_ERROR_DECOMPRESSION_FAILED,
    COMPRESSION_ERROR_UNSUPPORTED_ALGORITHM,
    COMPRESSION_ERROR_TIMEOUT
} CompressionError;

// Internal error tracking
static CompressionError last_error = COMPRESSION_ERROR_NONE;

// Error handling
static void set_error(CompressionError error) {
    last_error = error;
}

CompressionError compression_engine_get_last_error(void) {
    return last_error;
}

const char* compression_engine_error_to_string(CompressionError error) {
    switch (error) {
        case COMPRESSION_ERROR_NONE: return "No error";
        case COMPRESSION_ERROR_INVALID_PARAM: return "Invalid parameter";
        case COMPRESSION_ERROR_MEMORY_ALLOCATION: return "Memory allocation failed";
        case COMPRESSION_ERROR_BUFFER_TOO_SMALL: return "Buffer too small";
        case COMPRESSION_ERROR_COMPRESSION_FAILED: return "Compression failed";
        case COMPRESSION_ERROR_DECOMPRESSION_FAILED: return "Decompression failed";
        case COMPRESSION_ERROR_UNSUPPORTED_ALGORITHM: return "Unsupported algorithm";
        case COMPRESSION_ERROR_TIMEOUT: return "Operation timeout";
        default: return "Unknown error";
    }
}

// Validation functions
static bool validate_engine(const CompressionEngine* engine) {
    return engine != NULL && engine->is_initialized;
}

static bool validate_config(const CompressionConfig* config) {
    return config != NULL && 
           config->max_compression_threads > 0 &&
           config->max_compression_threads <= MAX_COMPRESSION_THREADS &&
           config->min_size_for_compression >= MIN_COMPRESSION_SIZE &&
           config->max_size_for_compression <= MAX_COMPRESSION_SIZE &&
           config->min_size_for_compression <= config->max_size_for_compression;
}

static bool validate_input(const CompressionEngine* engine, const void* input, size_t input_size) {
    return input != NULL && input_size > 0 && input_size <= MAX_COMPRESSION_SIZE;
}

static bool validate_output_buffer(const CompressionEngine* engine, const void* output, size_t output_capacity, size_t required_size) {
    return output != NULL && output_capacity >= required_size;
}

// Algorithm performance tracking
typedef struct {
    double compression_ratio;
    double compression_speed;
    double decompression_speed;
    uint64_t usage_count;
    time_t last_used;
    double performance_score;
} AlgorithmPerformance;

// Compression engine creation
CompressionEngine* compression_engine_create(const CompressionConfig* config) {
    if (!validate_config(config)) {
        set_error(COMPRESSION_ERROR_INVALID_PARAM);
        return NULL;
    }
    
    CompressionEngine* engine = malloc(sizeof(CompressionEngine));
    if (!engine) {
        set_error(COMPRESSION_ERROR_MEMORY_ALLOCATION);
        return NULL;
    }
    
    // Initialize configuration
    engine->config = *config;
    
    // Initialize statistics
    memset(&engine->stats, 0, sizeof(CompressionStats));
    engine->stats.last_reset = time(NULL);
    
    // Initialize adaptive compression state
    engine->algorithm_performance = malloc(ALGORITHM_COUNT * sizeof(double));
    engine->algorithm_usage_count = malloc(ALGORITHM_COUNT * sizeof(uint64_t));
    engine->algorithm_last_used = malloc(ALGORITHM_COUNT * sizeof(time_t));
    
    if (!engine->algorithm_performance || !engine->algorithm_usage_count || !engine->algorithm_last_used) {
        set_error(COMPRESSION_ERROR_MEMORY_ALLOCATION);
        free(engine->algorithm_performance);
        free(engine->algorithm_usage_count);
        free(engine->algorithm_last_used);
        free(engine);
        return NULL;
    }
    
    // Initialize performance tracking
    for (int i = 0; i < ALGORITHM_COUNT; i++) {
        engine->algorithm_performance[i] = 1.0;
        engine->algorithm_usage_count[i] = 0;
        engine->algorithm_last_used[i] = 0;
    }
    
    // Initialize threading
    engine->is_initialized = true;
    engine->active_threads = 0;
    
    // Initialize error tracking
    engine->last_error_code = 0;
    memset(engine->last_error_message, 0, sizeof(engine->last_error_message));
    
    set_error(COMPRESSION_ERROR_NONE);
    return engine;
}

// Compression engine destruction
void compression_engine_destroy(CompressionEngine* engine) {
    if (!engine) return;
    
    free(engine->algorithm_performance);
    free(engine->algorithm_usage_count);
    free(engine->algorithm_last_used);
    free(engine);
}

// Core compression operations
CompressionResult compression_engine_compress(CompressionEngine* engine, 
                                             const void* input, size_t input_size,
                                             void* output, size_t output_capacity) {
    if (!validate_engine(engine)) {
        set_error(COMPRESSION_ERROR_INVALID_PARAM);
        return (CompressionResult){0};
    }
    
    if (!validate_input(engine, input, input_size)) {
        set_error(COMPRESSION_ERROR_INVALID_PARAM);
        return (CompressionResult){0};
    }
    
    // Check if compression is worthwhile
    if (input_size < engine->config.min_size_for_compression) {
        // Copy data without compression
        if (output_capacity < input_size) {
            set_error(COMPRESSION_ERROR_BUFFER_TOO_SMALL);
            return (CompressionResult){0};
        }
        
        memcpy(output, input, input_size);
        
        CompressionResult result = {0};
        result.success = true;
        result.original_size = input_size;
        result.compressed_size = input_size;
        result.compression_ratio = 1.0;
        result.compression_speed_mbps = 0.0;
        result.decompression_speed_mbps = 0.0;
        result.algorithm_used = COMPRESSION_ALGORITHM_ZSTD;
        result.quality_used = engine->config.default_quality;
        result.checksum = compression_engine_calculate_checksum(input, input_size);
        result.timestamp = time(NULL);
        
        return result;
    }
    
    // Use default algorithm
    return compression_engine_compress_with_algorithm(engine, input, input_size, output, output_capacity,
                                                     engine->config.default_algorithm, engine->config.default_quality);
}

CompressionResult compression_engine_decompress(CompressionEngine* engine,
                                               const void* input, size_t input_size,
                                               void* output, size_t output_capacity) {
    if (!validate_engine(engine)) {
        set_error(COMPRESSION_ERROR_INVALID_PARAM);
        return (CompressionResult){0};
    }
    
    if (!validate_input(engine, input, input_size)) {
        set_error(COMPRESSION_ERROR_INVALID_PARAM);
        return (CompressionResult){0};
    }
    
    // For now, assume ZSTD decompression
    // In a real implementation, you'd need to store algorithm info with compressed data
    
    clock_t start_time = clock();
    
    size_t decompressed_size = ZSTD_decompress(output, output_capacity, input, input_size);
    
    clock_t end_time = clock();
    double time_ms = ((double)(end_time - start_time)) / CLOCKS_PER_SEC * 1000.0;
    
    if (ZSTD_isError(decompressed_size)) {
        set_error(COMPRESSION_ERROR_DECOMPRESSION_FAILED);
        return (CompressionResult){0};
    }
    
    CompressionResult result = {0};
    result.success = true;
    result.original_size = decompressed_size;
    result.compressed_size = input_size;
    result.compression_ratio = (double)input_size / decompressed_size;
    result.compression_speed_mbps = 0.0;
    result.decompression_speed_mbps = (decompressed_size / 1024.0 / 1024.0) / (time_ms / 1000.0);
    result.algorithm_used = COMPRESSION_ALGORITHM_ZSTD;
    result.quality_used = COMPRESSION_QUALITY_DEFAULT;
    result.checksum = compression_engine_calculate_checksum(output, decompressed_size);
    result.timestamp = time(NULL);
    
    // Update statistics
    pthread_mutex_lock(&engine->stats_mutex);
    engine->stats.total_decompressions++;
    engine->stats.successful_decompressions++;
    engine->stats.total_bytes_decompressed += decompressed_size;
    engine->stats.last_decompression = time(NULL);
    pthread_mutex_unlock(&engine->stats_mutex);
    
    set_error(COMPRESSION_ERROR_NONE);
    return result;
}

// Advanced compression operations
CompressionResult compression_engine_compress_adaptive(CompressionEngine* engine,
                                                      const void* input, size_t input_size,
                                                      void* output, size_t output_capacity) {
    if (!validate_engine(engine)) {
        set_error(COMPRESSION_ERROR_INVALID_PARAM);
        return (CompressionResult){0};
    }
    
    // Select best algorithm based on performance history
    CompressionAlgorithm best_algorithm = compression_engine_select_best_algorithm(engine, input, input_size);
    
    return compression_engine_compress_with_algorithm(engine, input, input_size, output, output_capacity,
                                                     best_algorithm, engine->config.default_quality);
}

CompressionResult compression_engine_compress_with_algorithm(CompressionEngine* engine,
                                                            const void* input, size_t input_size,
                                                            void* output, size_t output_capacity,
                                                            CompressionAlgorithm algorithm,
                                                            CompressionQuality quality) {
    if (!validate_engine(engine)) {
        set_error(COMPRESSION_ERROR_INVALID_PARAM);
        return (CompressionResult){0};
    }
    
    if (!validate_input(engine, input, input_size)) {
        set_error(COMPRESSION_ERROR_INVALID_PARAM);
        return (CompressionResult){0};
    }
    
    clock_t start_time = clock();
    CompressionResult result = {0};
    
    switch (algorithm) {
        case COMPRESSION_ALGORITHM_ZSTD: {
            int level = quality == COMPRESSION_QUALITY_FASTEST ? 1 :
                       quality == COMPRESSION_QUALITY_FAST ? 3 :
                       quality == COMPRESSION_QUALITY_DEFAULT ? 6 :
                       quality == COMPRESSION_QUALITY_BEST ? 19 : 22;
            
            size_t compressed_size = ZSTD_compress(output, output_capacity, input, input_size, level);
            
            if (ZSTD_isError(compressed_size)) {
                set_error(COMPRESSION_ERROR_COMPRESSION_FAILED);
                return (CompressionResult){0};
            }
            
            result.compressed_size = compressed_size;
            break;
        }
        
        case COMPRESSION_ALGORITHM_LZ4: {
            int level = quality == COMPRESSION_QUALITY_FASTEST ? 1 :
                       quality == COMPRESSION_QUALITY_FAST ? 5 :
                       quality == COMPRESSION_QUALITY_DEFAULT ? 9 :
                       quality == COMPRESSION_QUALITY_BEST ? 12 : 16;
            
            int compressed_size = LZ4_compress_default(input, output, input_size, output_capacity);
            
            if (compressed_size <= 0) {
                set_error(COMPRESSION_ERROR_COMPRESSION_FAILED);
                return (CompressionResult){0};
            }
            
            result.compressed_size = compressed_size;
            break;
        }
        
        case COMPRESSION_ALGORITHM_GZIP: {
            int level = quality == COMPRESSION_QUALITY_FASTEST ? 1 :
                       quality == COMPRESSION_QUALITY_FAST ? 3 :
                       quality == COMPRESSION_QUALITY_DEFAULT ? 6 :
                       quality == COMPRESSION_QUALITY_BEST ? 9 : 9;
            
            uLong compressed_size = compressBound(input_size);
            if (compressed_size > output_capacity) {
                set_error(COMPRESSION_ERROR_BUFFER_TOO_SMALL);
                return (CompressionResult){0};
            }
            
            int result_code = compress2(output, &compressed_size, input, input_size, level);
            
            if (result_code != Z_OK) {
                set_error(COMPRESSION_ERROR_COMPRESSION_FAILED);
                return (CompressionResult){0};
            }
            
            result.compressed_size = compressed_size;
            break;
        }
        
        case COMPRESSION_ALGORITHM_BROTLI: {
            // Brotli implementation would go here
            // For now, fall back to ZSTD
            set_error(COMPRESSION_ERROR_UNSUPPORTED_ALGORITHM);
            return (CompressionResult){0};
        }
        
        case COMPRESSION_ALGORITHM_ADAPTIVE: {
            // Recursive call with best algorithm
            CompressionAlgorithm best = compression_engine_select_best_algorithm(engine, input, input_size);
            return compression_engine_compress_with_algorithm(engine, input, input_size, output, output_capacity,
                                                             best, quality);
        }
        
        default:
            set_error(COMPRESSION_ERROR_UNSUPPORTED_ALGORITHM);
            return (CompressionResult){0};
    }
    
    clock_t end_time = clock();
    double time_ms = ((double)(end_time - start_time)) / CLOCKS_PER_SEC * 1000.0;
    
    // Fill result structure
    result.success = true;
    result.original_size = input_size;
    result.compression_ratio = (double)result.compressed_size / input_size;
    result.compression_speed_mbps = (input_size / 1024.0 / 1024.0) / (time_ms / 1000.0);
    result.decompression_speed_mbps = 0.0; // Would be measured separately
    result.algorithm_used = algorithm;
    result.quality_used = quality;
    result.checksum = compression_engine_calculate_checksum(output, result.compressed_size);
    result.timestamp = time(NULL);
    
    // Update performance tracking
    if (algorithm < ALGORITHM_COUNT) {
        engine->algorithm_performance[algorithm] = result.compression_ratio;
        engine->algorithm_usage_count[algorithm]++;
        engine->algorithm_last_used[algorithm] = time(NULL);
    }
    
    // Update statistics
    pthread_mutex_lock(&engine->stats_mutex);
    engine->stats.total_compressions++;
    engine->stats.successful_compressions++;
    engine->stats.total_bytes_compressed += input_size;
    engine->stats.total_bytes_saved += (input_size - result.compressed_size);
    engine->stats.average_compression_ratio = 
        (engine->stats.average_compression_ratio * (engine->stats.successful_compressions - 1) + result.compression_ratio) /
        engine->stats.successful_compressions;
    engine->stats.average_compression_speed = 
        (engine->stats.average_compression_speed * (engine->stats.successful_compressions - 1) + result.compression_speed_mbps) /
        engine->stats.successful_compressions;
    engine->stats.last_compression = time(NULL);
    pthread_mutex_unlock(&engine->stats_mutex);
    
    set_error(COMPRESSION_ERROR_NONE);
    return result;
}

// Batch operations
bool compression_engine_compress_batch(CompressionEngine* engine,
                                      const void** inputs, size_t* input_sizes, size_t count,
                                      void** outputs, size_t* output_capacities,
                                      CompressionResult* results) {
    if (!validate_engine(engine) || !inputs || !input_sizes || !outputs || !output_capacities || !results) {
        set_error(COMPRESSION_ERROR_INVALID_PARAM);
        return false;
    }
    
    bool all_success = true;
    for (size_t i = 0; i < count; i++) {
        results[i] = compression_engine_compress(engine, inputs[i], input_sizes[i], 
                                                outputs[i], output_capacities[i]);
        if (!results[i].success) {
            all_success = false;
        }
    }
    
    set_error(all_success ? COMPRESSION_ERROR_NONE : COMPRESSION_ERROR_COMPRESSION_FAILED);
    return all_success;
}

bool compression_engine_decompress_batch(CompressionEngine* engine,
                                        const void** inputs, size_t* input_sizes, size_t count,
                                        void** outputs, size_t* output_capacities,
                                        CompressionResult* results) {
    if (!validate_engine(engine) || !inputs || !input_sizes || !outputs || !output_capacities || !results) {
        set_error(COMPRESSION_ERROR_INVALID_PARAM);
        return false;
    }
    
    bool all_success = true;
    for (size_t i = 0; i < count; i++) {
        results[i] = compression_engine_decompress(engine, inputs[i], input_sizes[i], 
                                                  outputs[i], output_capacities[i]);
        if (!results[i].success) {
            all_success = false;
        }
    }
    
    set_error(all_success ? COMPRESSION_ERROR_NONE : COMPRESSION_ERROR_DECOMPRESSION_FAILED);
    return all_success;
}

// Algorithm selection and optimization
CompressionAlgorithm compression_engine_select_best_algorithm(CompressionEngine* engine,
                                                              const void* sample_data,
                                                              size_t sample_size) {
    if (!validate_engine(engine)) {
        return COMPRESSION_ALGORITHM_ZSTD;
    }
    
    // Simple heuristic: use performance history
    double best_score = 0.0;
    CompressionAlgorithm best_algorithm = COMPRESSION_ALGORITHM_ZSTD;
    
    for (int i = 0; i < ALGORITHM_COUNT; i++) {
        double score = engine->algorithm_performance[i];
        if (score > best_score) {
            best_score = score;
            best_algorithm = (CompressionAlgorithm)i;
        }
    }
    
    return best_algorithm;
}

bool compression_engine_optimize_for_data_type(CompressionEngine* engine,
                                               const void* sample_data,
                                               size_t sample_size) {
    if (!validate_engine(engine)) {
        set_error(COMPRESSION_ERROR_INVALID_PARAM);
        return false;
    }
    
    // Test all algorithms on sample data
    void* test_output = malloc(compressBound(sample_size));
    if (!test_output) {
        set_error(COMPRESSION_ERROR_MEMORY_ALLOCATION);
        return false;
    }
    
    double best_ratio = 1.0;
    CompressionAlgorithm best_algorithm = COMPRESSION_ALGORITHM_ZSTD;
    
    for (int i = 0; i < ALGORITHM_COUNT; i++) {
        CompressionResult result = compression_engine_compress_with_algorithm(engine, sample_data, sample_size,
                                                                             test_output, compressBound(sample_size),
                                                                             (CompressionAlgorithm)i, COMPRESSION_QUALITY_DEFAULT);
        if (result.success && result.compression_ratio < best_ratio) {
            best_ratio = result.compression_ratio;
            best_algorithm = (CompressionAlgorithm)i;
        }
    }
    
    free(test_output);
    
    // Update performance tracking
    if (best_algorithm < ALGORITHM_COUNT) {
        engine->algorithm_performance[best_algorithm] = best_ratio;
    }
    
    set_error(COMPRESSION_ERROR_NONE);
    return true;
}

double compression_engine_predict_compression_ratio(CompressionEngine* engine,
                                                   const void* data,
                                                   size_t data_size,
                                                   CompressionAlgorithm algorithm) {
    if (!validate_engine(engine)) {
        return 1.0;
    }
    
    if (algorithm < ALGORITHM_COUNT) {
        return engine->algorithm_performance[algorithm];
    }
    
    return 1.0;
}

// Performance analysis
CompressionStats compression_engine_get_stats(const CompressionEngine* engine) {
    CompressionStats stats = {0};
    
    if (!validate_engine(engine)) {
        return stats;
    }
    
    return engine->stats;
}

void compression_engine_reset_stats(CompressionEngine* engine) {
    if (!validate_engine(engine)) {
        return;
    }
    
    pthread_mutex_lock(&engine->stats_mutex);
    memset(&engine->stats, 0, sizeof(CompressionStats));
    engine->stats.last_reset = time(NULL);
    pthread_mutex_unlock(&engine->stats_mutex);
}

bool compression_engine_print_stats(const CompressionEngine* engine, FILE* stream) {
    if (!validate_engine(engine) || !stream) {
        return false;
    }
    
    CompressionStats stats = compression_engine_get_stats(engine);
    
    fprintf(stream, "Compression Statistics:\n");
    fprintf(stream, "  Total Compressions: %lu\n", stats.total_compressions);
    fprintf(stream, "  Successful Compressions: %lu\n", stats.successful_compressions);
    fprintf(stream, "  Failed Compressions: %lu\n", stats.failed_compressions);
    fprintf(stream, "  Total Decompressions: %lu\n", stats.total_decompressions);
    fprintf(stream, "  Successful Decompressions: %lu\n", stats.successful_decompressions);
    fprintf(stream, "  Failed Decompressions: %lu\n", stats.failed_decompressions);
    fprintf(stream, "  Average Compression Ratio: %.3f\n", stats.average_compression_ratio);
    fprintf(stream, "  Average Compression Speed: %.2f MB/s\n", stats.average_compression_speed);
    fprintf(stream, "  Average Decompression Speed: %.2f MB/s\n", stats.average_decompression_speed);
    fprintf(stream, "  Total Bytes Compressed: %lu\n", stats.total_bytes_compressed);
    fprintf(stream, "  Total Bytes Decompressed: %lu\n", stats.total_bytes_decompressed);
    fprintf(stream, "  Total Bytes Saved: %lu\n", stats.total_bytes_saved);
    
    return true;
}

// Configuration management
bool compression_engine_update_config(CompressionEngine* engine, const CompressionConfig* new_config) {
    if (!validate_engine(engine) || !validate_config(new_config)) {
        set_error(COMPRESSION_ERROR_INVALID_PARAM);
        return false;
    }
    
    engine->config = *new_config;
    
    set_error(COMPRESSION_ERROR_NONE);
    return true;
}

CompressionConfig compression_engine_get_config(const CompressionEngine* engine) {
    CompressionConfig config = {0};
    
    if (!validate_engine(engine)) {
        return config;
    }
    
    return engine->config;
}

bool compression_engine_validate_config(const CompressionConfig* config) {
    return validate_config(config);
}

// Memory management
size_t compression_engine_get_max_compressed_size(CompressionEngine* engine, size_t original_size) {
    if (!validate_engine(engine)) {
        return original_size;
    }
    
    // Return the maximum possible compressed size
    return compressBound(original_size);
}

size_t compression_engine_get_required_buffer_size(CompressionEngine* engine, size_t data_size) {
    return compression_engine_get_max_compressed_size(engine, data_size);
}

bool compression_engine_allocate_workspace(CompressionEngine* engine, size_t size) {
    // This would allocate workspace for parallel compression
    // For now, just return success
    return true;
}

void compression_engine_free_workspace(CompressionEngine* engine) {
    // This would free workspace
}

// Error handling
uint32_t compression_engine_get_last_error_code(const CompressionEngine* engine) {
    if (!validate_engine(engine)) {
        return 0;
    }
    
    return engine->last_error_code;
}

const char* compression_engine_get_last_error_message(const CompressionEngine* engine) {
    if (!validate_engine(engine)) {
        return "Invalid engine";
    }
    
    return engine->last_error_message;
}

const char* compression_engine_error_code_to_string(uint32_t error_code) {
    return compression_engine_error_to_string((CompressionError)error_code);
}

// Utility functions
const char* compression_engine_algorithm_to_string(CompressionAlgorithm algorithm) {
    switch (algorithm) {
        case COMPRESSION_ALGORITHM_ZSTD: return "ZSTD";
        case COMPRESSION_ALGORITHM_LZ4: return "LZ4";
        case COMPRESSION_ALGORITHM_GZIP: return "GZIP";
        case COMPRESSION_ALGORITHM_BROTLI: return "BROTLI";
        case COMPRESSION_ALGORITHM_ADAPTIVE: return "ADAPTIVE";
        default: return "UNKNOWN";
    }
}

const char* compression_engine_quality_to_string(CompressionQuality quality) {
    switch (quality) {
        case COMPRESSION_QUALITY_FASTEST: return "FASTEST";
        case COMPRESSION_QUALITY_FAST: return "FAST";
        case COMPRESSION_QUALITY_DEFAULT: return "DEFAULT";
        case COMPRESSION_QUALITY_BEST: return "BEST";
        case COMPRESSION_QUALITY_MAXIMUM: return "MAXIMUM";
        default: return "UNKNOWN";
    }
}

bool compression_engine_is_algorithm_supported(CompressionAlgorithm algorithm) {
    switch (algorithm) {
        case COMPRESSION_ALGORITHM_ZSTD:
        case COMPRESSION_ALGORITHM_LZ4:
        case COMPRESSION_ALGORITHM_GZIP:
            return true;
        case COMPRESSION_ALGORITHM_BROTLI:
        case COMPRESSION_ALGORITHM_ADAPTIVE:
            return false; // Not implemented yet
        default:
            return false;
    }
}

uint32_t compression_engine_calculate_checksum(const void* data, size_t size) {
    if (!data || size == 0) {
        return 0;
    }
    
    // Simple checksum using FNV-1a
    const uint32_t FNV_PRIME = 16777619;
    const uint32_t FNV_OFFSET_BASIS = 2166136261;
    
    uint32_t hash = FNV_OFFSET_BASIS;
    const uint8_t* bytes = (const uint8_t*)data;
    
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= FNV_PRIME;
    }
    
    return hash;
}

// Validation
bool compression_engine_validate_input(const CompressionEngine* engine,
                                      const void* input, size_t input_size) {
    return validate_input(engine, input, input_size);
}

bool compression_engine_validate_output_buffer(const CompressionEngine* engine,
                                              const void* output, size_t output_capacity,
                                              size_t required_size) {
    return validate_output_buffer(engine, output, output_capacity, required_size);
}

// Benchmarking
CompressionResult compression_engine_benchmark_algorithm(CompressionEngine* engine,
                                                        CompressionAlgorithm algorithm,
                                                        CompressionQuality quality,
                                                        const void* test_data,
                                                        size_t test_data_size) {
    if (!validate_engine(engine) || !validate_input(engine, test_data, test_data_size)) {
        set_error(COMPRESSION_ERROR_INVALID_PARAM);
        return (CompressionResult){0};
    }
    
    size_t max_compressed_size = compression_engine_get_max_compressed_size(engine, test_data_size);
    void* compressed_buffer = malloc(max_compressed_size);
    
    if (!compressed_buffer) {
        set_error(COMPRESSION_ERROR_MEMORY_ALLOCATION);
        return (CompressionResult){0};
    }
    
    CompressionResult result = compression_engine_compress_with_algorithm(engine, test_data, test_data_size,
                                                                         compressed_buffer, max_compressed_size,
                                                                         algorithm, quality);
    
    free(compressed_buffer);
    return result;
}

// Threading support
bool compression_engine_set_thread_count(CompressionEngine* engine, uint32_t thread_count) {
    if (!validate_engine(engine) || thread_count > MAX_COMPRESSION_THREADS) {
        set_error(COMPRESSION_ERROR_INVALID_PARAM);
        return false;
    }
    
    engine->config.max_compression_threads = thread_count;
    
    set_error(COMPRESSION_ERROR_NONE);
    return true;
}

uint32_t compression_engine_get_thread_count(const CompressionEngine* engine) {
    if (!validate_engine(engine)) {
        return 0;
    }
    
    return engine->config.max_compression_threads;
}

bool compression_engine_is_parallel_supported(const CompressionEngine* engine) {
    // ZSTD supports parallel compression
    return true;
} 