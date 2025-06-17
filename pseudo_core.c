// Основной файл ядра PseudoCore
// Примечание: Если вы видите ошибку IntelliSense "#include errors detected", 
// это связано с конфигурацией includePath в VSCode. 
// Пожалуйста, обновите includePath, выбрав команду "C/C++: Select IntelliSense Configuration..." 
// или добавив необходимые пути в настройки c_cpp_properties.json.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>
#include <string.h>

#include "config.h"
#include "cache.h"
#include "compress.h"
#include "ring_cache.h"
#include "scheduler.h"

// Определения констант, которые могут отсутствовать в config.h
#ifndef LOAD_THRESHOLD
#define LOAD_THRESHOLD 50
#endif
#ifndef HIGH_LOAD_DELAY_NS
#define HIGH_LOAD_DELAY_NS 20000000 // 20ms
#endif
#ifndef LOW_LOAD_DELAY_NS
#define LOW_LOAD_DELAY_NS 10000000 // 10ms
#endif
#ifndef BASE_LOAD_DELAY_NS
#define BASE_LOAD_DELAY_NS 5000000 // 5ms
#endif
#ifndef COMPRESSION_MIN_LVL
#define COMPRESSION_MIN_LVL 1
#endif
#ifndef COMPRESSION_MAX_LVL
#define COMPRESSION_MAX_LVL 5
#endif
#ifndef COMPRESSION_ADAPTIVE_THRESHOLD
#define COMPRESSION_ADAPTIVE_THRESHOLD 0.8
#endif
#ifndef SEGMENT_MB
#define SEGMENT_MB 256
#endif
#ifndef CORES
#define CORES 4
#endif
#ifndef BLOCK_SIZE
#define BLOCK_SIZE 4096
#endif
#ifndef SWAP_IMG_PATH
#define SWAP_IMG_PATH "storage_swap.img"
#endif

#define TOTAL_SIZE_MB (SEGMENT_MB * CORES)

// Structure for core arguments passed to threads
typedef struct {
    int id;               // Core ID
    int fd;               // File descriptor for I/O operations
    uint64_t seg_size;    // Segment size for block selection
    volatile int running; // Flag to control thread termination
} core_arg_t;

volatile int global_running = 1; // Global flag to terminate all threads

// Performance statistics for visualization
static size_t total_operations[CORES] = {0};
static pthread_mutex_t stats_mutex;

// Function to prefetch a block of data from disk
void prefetch_block(int fd, uint64_t off) {
    char tmp[BLOCK_SIZE];
    ssize_t result = pread(fd, tmp, BLOCK_SIZE, off);
    if (result < 0) {
        fprintf(stderr, "Error prefetching block at offset %lu\n", off);
    }
}

// Determine adaptive compression level based on previous compression ratio
int determine_compression_level(size_t original_size, size_t compressed_size) {
    if (original_size == 0) return COMPRESSION_MIN_LVL;
    double ratio = (double)compressed_size / original_size;
    if (ratio > COMPRESSION_ADAPTIVE_THRESHOLD) {
        return COMPRESSION_MAX_LVL; // High compression level for poorly compressible data
    } else {
        return COMPRESSION_MIN_LVL; // Low compression level for already compressed data
    }
}

// Log basic information and errors to a file or stderr
static void log_message(const char *level, const char *message, int core_id) {
    time_t now = time(NULL);
    char timestamp[26];
    ctime_r(&now, timestamp);
    timestamp[24] = '\0'; // Remove newline from ctime
    fprintf(stderr, "[%s] [%s] Core %d: %s\n", timestamp, level, core_id, message);
}

// Display system-wide performance statistics
static void display_system_stats(void) {
    pthread_mutex_lock(&stats_mutex);
    fprintf(stderr, "[SYSTEM STATS] Operations per Core: ");
    for (int i = 0; i < CORES; i++) {
        fprintf(stderr, "Core %d: %zu ", i, total_operations[i]);
    }
    fprintf(stderr, "\n");
    pthread_mutex_unlock(&stats_mutex);
}

// Core execution function running in a separate thread
void* core_run(void *v) {
    core_arg_t *c = v;
    cache_t cache;
    cache_init(&cache);
    ring_cache_init();
    int compression_level = COMPRESSION_MIN_LVL;
    size_t last_compressed_size = BLOCK_SIZE;
    int load_counter = 0; // Counter for adaptive delay
    const int load_check_interval = 100; // Check load every 100 iterations
    char log_msg[256];

    snprintf(log_msg, sizeof(log_msg), "Started core execution");
    log_message("INFO", log_msg, c->id);

    while (c->running && global_running) {
        // Circular block selection with adaptive segment size
        static uint64_t pos[CORES] = {0};
        uint64_t idx = pos[c->id]++;
        // Adjust segment size dynamically based on system load
        uint64_t adaptive_seg_size = c->seg_size;
        int current_load = 0; // Placeholder for load check
        if (load_counter >= load_check_interval) {
            // Here we would check the load via scheduler, but for simplicity, we assume it's available
            current_load = 0; // Placeholder
        }
        if (current_load > LOAD_THRESHOLD) {
            adaptive_seg_size = c->seg_size / 2; // Reduce segment size under high load to lower I/O latency
            snprintf(log_msg, sizeof(log_msg), "Reduced segment size to %lu due to high load: %d tasks", adaptive_seg_size, current_load);
            log_message("INFO", log_msg, c->id);
        }
        uint64_t offset = (uint64_t)c->id * adaptive_seg_size + (idx % (adaptive_seg_size / BLOCK_SIZE)) * BLOCK_SIZE;

        scheduler_report_access(c->id, offset);

        // Task migration
        if (scheduler_should_migrate(c->id)) {
            uint64_t m = scheduler_get_migrated_task(c->id);
            if (m) offset = m;
        }

        // Caching
        char buf[BLOCK_SIZE];
        char *page = cache_get(&cache, c->fd, offset, 1);
        if (!page) {
            snprintf(log_msg, sizeof(log_msg), "Failed to get cache page");
            log_message("ERROR", log_msg, c->id);
            continue;
        }
        memcpy(buf, page, BLOCK_SIZE);

        // Prefetch neighboring block
        if (c->running && global_running) {
            prefetch_block(c->fd, offset + BLOCK_SIZE);
        }

        // Simulate workload with vectorized XOR operation for performance
        // Use a loop unrolling and vectorization-friendly approach
        int id_xor = c->id;
        for (int i = 0; i < BLOCK_SIZE; i += 8) {
            buf[i + 0] ^= id_xor;
            buf[i + 1] ^= id_xor;
            buf[i + 2] ^= id_xor;
            buf[i + 3] ^= id_xor;
            buf[i + 4] ^= id_xor;
            buf[i + 5] ^= id_xor;
            buf[i + 6] ^= id_xor;
            buf[i + 7] ^= id_xor;
        }
        // Repeat the operation to simulate workload (reduced iterations due to unrolling)
        for (int repeat = 0; repeat < 125; repeat++) {
            for (int i = 0; i < BLOCK_SIZE; i += 8) {
                buf[i + 0] ^= id_xor;
                buf[i + 1] ^= id_xor;
                buf[i + 2] ^= id_xor;
                buf[i + 3] ^= id_xor;
                buf[i + 4] ^= id_xor;
                buf[i + 5] ^= id_xor;
                buf[i + 6] ^= id_xor;
                buf[i + 7] ^= id_xor;
            }
        }

        // Adaptive compression and write
        char cmp[BLOCK_SIZE];
        compression_level = determine_compression_level(BLOCK_SIZE, last_compressed_size);
        int cs = compress_page(buf, BLOCK_SIZE, cmp, compression_level);
        if (cs > 0) {
            ssize_t write_result = pwrite(c->fd, cmp, BLOCK_SIZE, offset);
            if (write_result < 0) {
                snprintf(log_msg, sizeof(log_msg), "Failed to write compressed data at offset %lu", offset);
                log_message("ERROR", log_msg, c->id);
            }
            last_compressed_size = cs;
        } else {
            snprintf(log_msg, sizeof(log_msg), "Compression failed");
            log_message("ERROR", log_msg, c->id);
        }

        cache_to_ring(offset, buf);

        // Update performance statistics
        pthread_mutex_lock(&stats_mutex);
        total_operations[c->id]++;
        pthread_mutex_unlock(&stats_mutex);

        // Adaptive delay and throttling based on system load
        load_counter++;
        if (load_counter >= load_check_interval) {
            load_counter = 0;
            // Check current load via scheduler (placeholder)
            current_load = 0;
            long delay_ns = (current_load > LOAD_THRESHOLD) ? HIGH_LOAD_DELAY_NS : LOW_LOAD_DELAY_NS;
            // Additional throttling if system load is extremely high
            if (current_load > LOAD_THRESHOLD * 2) {
                delay_ns *= 2; // Double the delay for throttling
                snprintf(log_msg, sizeof(log_msg), "Throttling core due to extreme load: %d tasks", current_load);
                log_message("WARNING", log_msg, c->id);
            }
            struct timespec delay = {0, delay_ns};
            nanosleep(&delay, NULL);
            // Display system stats periodically
            if (total_operations[c->id] % 500 == 0) {
                display_system_stats();
            }
        } else {
            // Base minimal delay
            struct timespec delay = {0, BASE_LOAD_DELAY_NS};
            nanosleep(&delay, NULL);
        }
    }

    ring_cache_destroy();
    cache_destroy(&cache, c->fd); // Pass fd to write dirty pages
    snprintf(log_msg, sizeof(log_msg), "Core execution terminated");
    log_message("INFO", log_msg, c->id);
    return NULL;
}

// Signal handler for graceful shutdown
void signal_handler(int sig) {
    (void)sig;
    global_running = 0;
    fprintf(stdout, "Received termination signal. Shutting down threads...\n");
}

int main() {
    int fd = open(SWAP_IMG_PATH, O_RDWR);
    if (fd < 0) {
        perror("Error opening swap file");
        exit(1);
    }

    uint64_t seg_bytes = (uint64_t)SEGMENT_MB * 1024 * 1024;

    pthread_t th[CORES];
    core_arg_t args[CORES];

    // Initialize mutex for stats
    pthread_mutex_init(&stats_mutex, NULL);

    // Set up signal handler for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    for (int i = 0; i < CORES; i++) {
        args[i].id = i;
        args[i].fd = fd;
        args[i].seg_size = seg_bytes;
        args[i].running = 1;
        if (pthread_create(&th[i], NULL, core_run, &args[i]) != 0) {
            fprintf(stderr, "Error creating thread for core %d\n", i);
            global_running = 0;
            for (int j = 0; j < i; j++) {
                args[j].running = 0;
            }
            for (int j = 0; j < i; j++) {
                pthread_join(th[j], NULL);
            }
            pthread_mutex_destroy(&stats_mutex);
            scheduler_destroy();
            close(fd);
            exit(1);
        }
    }

    // Wait for threads to complete
    for (int i = 0; i < CORES; i++) {
        pthread_join(th[i], NULL);
    }

    // Очистка ресурсов планировщика
    scheduler_destroy();
    
    close(fd);
    fprintf(stdout, "Program terminated successfully.\n");
    return 0;
}
