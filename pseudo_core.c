#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#include "config.h"
#include "cache.h"
#include "compress.h"
#include "ring_cache.h"
#include "scheduler.h"

#define TOTAL_SIZE_MB (SEGMENT_MB * CORES)

// Structure for core arguments passed to threads
typedef struct {
    int id;               // Core ID
    int fd;               // File descriptor for I/O operations
    uint64_t seg_size;    // Segment size for block selection
    volatile int running; // Flag to control thread termination
} core_arg_t;

volatile int global_running = 1; // Global flag to terminate all threads

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
        // Circular block selection
        static uint64_t pos[CORES] = {0};
        uint64_t idx = pos[c->id]++;
        uint64_t offset = (uint64_t)c->id * c->seg_size + (idx % (c->seg_size / BLOCK_SIZE)) * BLOCK_SIZE;

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

        // Simulate workload
        for (int i = 0; i < 1000; i++) {
            buf[i % BLOCK_SIZE] ^= c->id;
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

        // Load balancing
        scheduler_balance_load();

        // Adaptive delay based on load
        load_counter++;
        if (load_counter >= load_check_interval) {
            load_counter = 0;
            // Check current load via scheduler
            pthread_mutex_lock(&queues[c->id].mutex);
            int current_load = queues[c->id].count;
            pthread_mutex_unlock(&queues[c->id].mutex);
            long delay_ns = (current_load > LOAD_THRESHOLD) ? HIGH_LOAD_DELAY_NS : LOW_LOAD_DELAY_NS;
            struct timespec delay = {0, delay_ns};
            nanosleep(&delay, NULL);
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

#include <signal.h>

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

    // Initialize scheduler
    scheduler_init();

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
            close(fd);
            scheduler_destroy();
            exit(1);
        }
    }

    // Wait for threads to complete
    for (int i = 0; i < CORES; i++) {
        pthread_join(th[i], NULL);
    }

    close(fd);
    scheduler_destroy();
    fprintf(stdout, "Program terminated successfully.\n");
    return 0;
}
