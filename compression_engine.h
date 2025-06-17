#ifndef COMPRESSION_ENGINE_H
#define COMPRESSION_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

// Compression algorithms
typedef enum {
    COMPRESSION_ALGORITHM_ZSTD = 0,
    COMPRESSION_ALGORITHM_LZ4,
    COMPRESSION_ALGORITHM_GZIP,
    COMPRESSION_ALGORITHM_BROTLI,
    COMPRESSION_ALGORITHM_ADAPTIVE
} CompressionAlgorithm;

// Compression quality levels
typedef enum {
    COMPRESSION_QUALITY_FASTEST = 0,
    COMPRESSION_QUALITY_FAST,
    COMPRESSION_QUALITY_DEFAULT,
    COMPRESSION_QUALITY_BEST,
    COMPRESSION_QUALITY_MAXIMUM
} CompressionQuality;

// Compression result
typedef struct {
    bool success;
    size_t original_size;
    size_t compressed_size;
    double compression_ratio;
    double compression_speed_mbps;
    double decompression_speed_mbps;
    CompressionAlgorithm algorithm_used;
    CompressionQuality quality_used;
    uint32_t checksum;
    time_t timestamp;
} CompressionResult;

// Compression statistics
typedef struct {
    uint64_t total_compressions;
    uint64_t total_decompressions;
    uint64_t successful_compressions;
    uint64_t successful_decompressions;
    uint64_t failed_compressions;
    uint64_t failed_decompressions;
    
    double average_compression_ratio;
    double average_compression_speed;
    double average_decompression_speed;
    
    uint64_t total_bytes_compressed;
    uint64_t total_bytes_decompressed;
    uint64_t total_bytes_saved;
    
    time_t last_reset;
    time_t last_compression;
    time_t last_decompression;
} CompressionStats;

// Compression configuration
typedef struct {
    CompressionAlgorithm default_algorithm;
    CompressionQuality default_quality;
    bool enable_adaptive_compression;
    bool enable_parallel_compression;
    uint32_t max_compression_threads;
    size_t min_size_for_compression;
    size_t max_size_for_compression;
    double target_compression_ratio;
    uint32_t compression_timeout_ms;
    bool enable_checksum_validation;
} CompressionConfig;

// Compression engine interface
typedef struct CompressionEngine {
    // Configuration
    CompressionConfig config;
    
    // Statistics
    CompressionStats stats;
    
    // Adaptive compression state
    double* algorithm_performance;
    uint64_t* algorithm_usage_count;
    time_t* algorithm_last_used;
    
    // Threading
    bool is_initialized;
    uint32_t active_threads;
    
    // Error tracking
    uint32_t last_error_code;
    char last_error_message[256];
} CompressionEngine;

// Compression engine interface functions
CompressionEngine* compression_engine_create(const CompressionConfig* config);
void compression_engine_destroy(CompressionEngine* engine);

// Core compression operations
CompressionResult compression_engine_compress(CompressionEngine* engine, 
                                             const void* input, size_t input_size,
                                             void* output, size_t output_capacity);
CompressionResult compression_engine_decompress(CompressionEngine* engine,
                                               const void* input, size_t input_size,
                                               void* output, size_t output_capacity);

// Advanced compression operations
CompressionResult compression_engine_compress_adaptive(CompressionEngine* engine,
                                                      const void* input, size_t input_size,
                                                      void* output, size_t output_capacity);
CompressionResult compression_engine_compress_with_algorithm(CompressionEngine* engine,
                                                            const void* input, size_t input_size,
                                                            void* output, size_t output_capacity,
                                                            CompressionAlgorithm algorithm,
                                                            CompressionQuality quality);

// Batch operations
bool compression_engine_compress_batch(CompressionEngine* engine,
                                      const void** inputs, size_t* input_sizes, size_t count,
                                      void** outputs, size_t* output_capacities,
                                      CompressionResult* results);
bool compression_engine_decompress_batch(CompressionEngine* engine,
                                        const void** inputs, size_t* input_sizes, size_t count,
                                        void** outputs, size_t* output_capacities,
                                        CompressionResult* results);

// Algorithm selection and optimization
CompressionAlgorithm compression_engine_select_best_algorithm(CompressionEngine* engine,
                                                              const void* sample_data,
                                                              size_t sample_size);
bool compression_engine_optimize_for_data_type(CompressionEngine* engine,
                                               const void* sample_data,
                                               size_t sample_size);
double compression_engine_predict_compression_ratio(CompressionEngine* engine,
                                                   const void* data,
                                                   size_t data_size,
                                                   CompressionAlgorithm algorithm);

// Performance analysis
CompressionStats compression_engine_get_stats(const CompressionEngine* engine);
void compression_engine_reset_stats(CompressionEngine* engine);
bool compression_engine_print_stats(const CompressionEngine* engine, FILE* stream);

// Configuration management
bool compression_engine_update_config(CompressionEngine* engine, const CompressionConfig* new_config);
CompressionConfig compression_engine_get_config(const CompressionEngine* engine);
bool compression_engine_validate_config(const CompressionConfig* config);

// Memory management
size_t compression_engine_get_max_compressed_size(CompressionEngine* engine, size_t original_size);
size_t compression_engine_get_required_buffer_size(CompressionEngine* engine, size_t data_size);
bool compression_engine_allocate_workspace(CompressionEngine* engine, size_t size);
void compression_engine_free_workspace(CompressionEngine* engine);

// Error handling
uint32_t compression_engine_get_last_error_code(const CompressionEngine* engine);
const char* compression_engine_get_last_error_message(const CompressionEngine* engine);
const char* compression_engine_error_code_to_string(uint32_t error_code);

// Utility functions
const char* compression_engine_algorithm_to_string(CompressionAlgorithm algorithm);
const char* compression_engine_quality_to_string(CompressionQuality quality);
bool compression_engine_is_algorithm_supported(CompressionAlgorithm algorithm);
uint32_t compression_engine_calculate_checksum(const void* data, size_t size);

// Validation
bool compression_engine_validate_input(const CompressionEngine* engine,
                                      const void* input, size_t input_size);
bool compression_engine_validate_output_buffer(const CompressionEngine* engine,
                                              const void* output, size_t output_capacity,
                                              size_t required_size);

// Benchmarking
CompressionResult compression_engine_benchmark_algorithm(CompressionEngine* engine,
                                                        CompressionAlgorithm algorithm,
                                                        CompressionQuality quality,
                                                        const void* test_data,
                                                        size_t test_data_size);

// Threading support
bool compression_engine_set_thread_count(CompressionEngine* engine, uint32_t thread_count);
uint32_t compression_engine_get_thread_count(const CompressionEngine* engine);
bool compression_engine_is_parallel_supported(const CompressionEngine* engine);

#endif // COMPRESSION_ENGINE_H 