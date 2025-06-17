// Модуль кэша для PseudoCore
// Примечание: Если вы видите ошибку IntelliSense "#include errors detected", 
// это связано с конфигурацией includePath в VSCode. 
// Пожалуйста, обновите includePath, выбрав команду "C/C++: Select IntelliSense Configuration..." 
// или добавив необходимые пути в настройки c_cpp_properties.json.
#include "cache.h"
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <errno.h>

// Cache statistics for monitoring performance
static size_t cache_hits = 0;
static size_t cache_misses = 0;
static pthread_mutex_t stats_mutex;

// Improved hash function using FNV-1a to reduce collisions
static size_t hash_func(uint64_t off) {
    const uint64_t FNV_PRIME = 1099511628211ULL;
    const uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    uint64_t hash = FNV_OFFSET_BASIS;
    uint8_t *bytes = (uint8_t*)&off;
    for (size_t i = 0; i < sizeof(off); i++) {
        hash ^= bytes[i];
        hash *= FNV_PRIME;
    }
    return (hash / PAGE_SIZE) % HASH_SIZE;
}

// Calculate mutex group for a hash index
static size_t mutex_group(size_t h) {
    return h % MUTEX_GROUPS;
}

// Log cache-related errors or information
static void log_cache_message(const char *level, const char *message) {
    time_t now = time(NULL);
    char timestamp[26];
    ctime_r(&now, timestamp);
    timestamp[24] = '\0'; // Remove newline from ctime
    fprintf(stderr, "[%s] [%s] Cache: %s\n", timestamp, level, message);
}

// Display cache statistics for monitoring
static void display_cache_stats(void) {
    pthread_mutex_lock(&stats_mutex);
    size_t total_requests = cache_hits + cache_misses;
    double hit_ratio = total_requests > 0 ? (double)cache_hits / total_requests * 100.0 : 0.0;
    fprintf(stderr, "[CACHE STATS] Hits: %zu, Misses: %zu, Hit Ratio: %.2f%%\n", 
            cache_hits, cache_misses, hit_ratio);
    pthread_mutex_unlock(&stats_mutex);
}

void cache_init(cache_t *c) {
    for (int i = 0; i < HASH_SIZE; i++) {
        c->hash[i] = NULL;
    }
    for (int i = 0; i < MUTEX_GROUPS; i++) {
        pthread_mutex_init(&c->mutex[i], NULL);
    }
    c->lru_head = NULL;
    c->lru_tail = NULL;
    c->entry_count = 0;
    pthread_mutex_init(&c->lru_mutex, NULL);
    pthread_mutex_init(&stats_mutex, NULL);
    log_cache_message("INFO", "Cache initialized");
}

char* cache_get(cache_t *c, int fd, uint64_t off, int write) {
    size_t h = hash_func(off);
    size_t mg = mutex_group(h);
    pthread_mutex_lock(&c->mutex[mg]);
    cache_entry_t *e = c->hash[h];
    while (e) {
        if (e->offset == off) {
            if (write) e->dirty = 1;
            e->last_access = time(NULL);
            pthread_mutex_lock(&c->lru_mutex);
            // Move to the front of LRU list (recently used)
            if (e != c->lru_head) {
                if (e->prev) e->prev->next = e->next;
                if (e->next) e->next->prev = e->prev;
                if (e == c->lru_tail) c->lru_tail = e->prev;
                e->next = c->lru_head;
                e->prev = NULL;
                if (c->lru_head) c->lru_head->prev = e;
                c->lru_head = e;
                if (!c->lru_tail) c->lru_tail = e;
            }
            pthread_mutex_unlock(&c->lru_mutex);
            pthread_mutex_unlock(&c->mutex[mg]);
            // Increment cache hit counter
            pthread_mutex_lock(&stats_mutex);
            cache_hits++;
            pthread_mutex_unlock(&stats_mutex);
            // Periodically display stats (every 100 hits for simplicity)
            if (cache_hits % 100 == 0) {
                display_cache_stats();
            }
            return e->data;
        }
        e = e->next;
    }
    // Cache miss - load from disk
    cache_entry_t *ne = malloc(sizeof(*ne));
    if (!ne) {
        pthread_mutex_unlock(&c->mutex[mg]);
        log_cache_message("ERROR", "Failed to allocate memory for cache entry");
        // Increment cache miss counter
        pthread_mutex_lock(&stats_mutex);
        cache_misses++;
        pthread_mutex_unlock(&stats_mutex);
        return NULL;
    }
    ne->offset = off;
    ne->dirty = write;
    ne->last_access = time(NULL);
    ne->next = c->hash[h];
    ne->prev = NULL;
    if (c->hash[h]) c->hash[h]->prev = ne;
    c->hash[h] = ne;
    // Read page from disk with detailed error handling
    ssize_t read_result = pread(fd, ne->data, PAGE_SIZE, off);
    if (read_result < 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to read page from disk at offset %lu (errno: %d)", off, errno);
        log_cache_message("ERROR", msg);
        free(ne);
        if (c->hash[h] == ne) c->hash[h] = ne->next;
        if (ne->next) ne->next->prev = NULL;
        pthread_mutex_unlock(&c->mutex[mg]);
        // Increment cache miss counter
        pthread_mutex_lock(&stats_mutex);
        cache_misses++;
        pthread_mutex_unlock(&stats_mutex);
        return NULL;
    } else if (read_result != PAGE_SIZE) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Partial read from disk at offset %lu (read %zd bytes instead of %d)", off, read_result, PAGE_SIZE);
        log_cache_message("WARNING", msg);
        // Fill the remaining part of the buffer with zeros to avoid undefined behavior
        memset(ne->data + read_result, 0, PAGE_SIZE - read_result);
    }
    // Add to LRU list
    pthread_mutex_lock(&c->lru_mutex);
    ne->next = c->lru_head;
    ne->prev = NULL;
    if (c->lru_head) c->lru_head->prev = ne;
    c->lru_head = ne;
    if (!c->lru_tail) c->lru_tail = ne;
    c->entry_count++;
    // Check if eviction is needed
    if (c->entry_count > MAX_CACHE_ENTRIES) {
        cache_evict(c, fd);
    }
    pthread_mutex_unlock(&c->lru_mutex);
    pthread_mutex_unlock(&c->mutex[mg]);
    // Increment cache miss counter
    pthread_mutex_lock(&stats_mutex);
    cache_misses++;
    pthread_mutex_unlock(&stats_mutex);
    return ne->data;
}

void cache_evict(cache_t *c, int fd) {
    // Evict the least recently used entry
    if (!c->lru_tail) return;
    cache_entry_t *evict = c->lru_tail;
    size_t h = hash_func(evict->offset);
    size_t mg = mutex_group(h);
    pthread_mutex_lock(&c->mutex[mg]);
    // Remove from hash table
    if (evict->prev) evict->prev->next = evict->next;
    if (evict->next) evict->next->prev = evict->prev;
    if (c->hash[h] == evict) c->hash[h] = evict->next;
    // If entry is dirty, write back to disk with detailed error handling
    if (evict->dirty) {
        ssize_t write_result = pwrite(fd, evict->data, PAGE_SIZE, evict->offset);
        if (write_result < 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Failed to write dirty page at offset %lu (errno: %d)", evict->offset, errno);
            log_cache_message("ERROR", msg);
        } else if (write_result != PAGE_SIZE) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Partial write to disk at offset %lu (wrote %zd bytes instead of %d)", evict->offset, write_result, PAGE_SIZE);
            log_cache_message("WARNING", msg);
        }
        evict->dirty = 0; // Reset dirty flag after write attempt
    }
    free(evict);
    c->entry_count--;
    // Update LRU tail
    c->lru_tail = c->lru_tail->prev;
    if (c->lru_tail) c->lru_tail->next = NULL;
    pthread_mutex_unlock(&c->mutex[mg]);
}

void cache_destroy(cache_t *c, int fd) {
    for (int i = 0; i < HASH_SIZE; i++) {
        size_t mg = mutex_group(i);
        pthread_mutex_lock(&c->mutex[mg]);
        cache_entry_t *e = c->hash[i];
        while (e) {
            cache_entry_t *n = e->next;
            if (e->dirty) {
                ssize_t write_result = pwrite(fd, e->data, PAGE_SIZE, e->offset);
                if (write_result < 0) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Failed to write dirty page at offset %lu during shutdown (errno: %d)", e->offset, errno);
                    log_cache_message("ERROR", msg);
                } else if (write_result != PAGE_SIZE) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Partial write during shutdown at offset %lu (wrote %zd bytes instead of %d)", e->offset, write_result, PAGE_SIZE);
                    log_cache_message("WARNING", msg);
                }
                e->dirty = 0; // Reset dirty flag after write attempt
            }
            free(e);
            e = n;
        }
        c->hash[i] = NULL;
        pthread_mutex_unlock(&c->mutex[mg]);
    }
    for (int i = 0; i < MUTEX_GROUPS; i++) {
        pthread_mutex_destroy(&c->mutex[i]);
    }
    pthread_mutex_destroy(&c->lru_mutex);
    c->lru_head = NULL;
    c->lru_tail = NULL;
    c->entry_count = 0;
    log_cache_message("INFO", "Cache destroyed");
}
