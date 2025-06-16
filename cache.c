#include "cache.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

// Improved hash function to reduce collisions
static size_t hash_func(uint64_t off) {
    off = (off >> 16) ^ off;
    off = off * 0x45d9f3b;
    off = (off >> 16) ^ off;
    off = off * 0x45d9f3b;
    off = (off >> 16) ^ off;
    return (off / PAGE_SIZE) % HASH_SIZE;
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
            return e->data;
        }
        e = e->next;
    }
    // Cache miss - load from disk
    cache_entry_t *ne = malloc(sizeof(*ne));
    if (!ne) {
        pthread_mutex_unlock(&c->mutex[mg]);
        log_cache_message("ERROR", "Failed to allocate memory for cache entry");
        return NULL;
    }
    ne->offset = off;
    ne->dirty = write;
    ne->last_access = time(NULL);
    ne->next = c->hash[h];
    ne->prev = NULL;
    if (c->hash[h]) c->hash[h]->prev = ne;
    c->hash[h] = ne;
    // Read page from disk
    ssize_t read_result = pread(fd, ne->data, PAGE_SIZE, off);
    if (read_result < 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to read page from disk at offset %lu", off);
        log_cache_message("ERROR", msg);
        free(ne);
        if (c->hash[h] == ne) c->hash[h] = ne->next;
        if (ne->next) ne->next->prev = NULL;
        pthread_mutex_unlock(&c->mutex[mg]);
        return NULL;
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
    // If entry is dirty, write back to disk
    if (evict->dirty) {
        ssize_t write_result = pwrite(fd, evict->data, PAGE_SIZE, evict->offset);
        if (write_result < 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Failed to write dirty page at offset %lu", evict->offset);
            log_cache_message("ERROR", msg);
        }
        evict->dirty = 0; // Reset dirty flag after write
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
                    snprintf(msg, sizeof(msg), "Failed to write dirty page at offset %lu during shutdown", e->offset);
                    log_cache_message("ERROR", msg);
                }
                e->dirty = 0; // Reset dirty flag after write
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
