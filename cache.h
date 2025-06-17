#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>

// Константы
#include "config.h"

#ifndef PAGE_SIZE
#define PAGE_SIZE BLOCK_SIZE
#endif
#ifndef HASH_SIZE
#define HASH_SIZE 2048
#endif
#ifndef MUTEX_GROUPS
#define MUTEX_GROUPS 16
#endif
#ifndef MAX_CACHE_ENTRIES
#define MAX_CACHE_ENTRIES 1024
#endif

typedef struct cache_entry {
    uint64_t offset;
    char data[PAGE_SIZE];
    int dirty;
    time_t last_access;
    struct cache_entry *next;
    struct cache_entry *prev;
} cache_entry_t;

typedef struct {
    cache_entry_t *hash[HASH_SIZE];
    pthread_mutex_t mutex[MUTEX_GROUPS];
    pthread_mutex_t lru_mutex;
    cache_entry_t *lru_head;
    cache_entry_t *lru_tail;
    size_t entry_count;
} cache_t;

void cache_init(cache_t *c);
char* cache_get(cache_t *c, int fd, uint64_t offset, int write);
void cache_evict(cache_t *c, int fd);
void cache_destroy(cache_t *c, int fd);

#endif // CACHE_H
