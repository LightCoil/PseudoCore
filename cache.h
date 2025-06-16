#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

// Константы
#include "config.h"

#define PAGE_SIZE BLOCK_SIZE
#define HASH_SIZE 2048

typedef struct cache_entry {
    uint64_t offset;
    char data[PAGE_SIZE];
    int dirty;
    struct cache_entry *next;
} cache_entry_t;

typedef struct {
    cache_entry_t *hash[HASH_SIZE];
} cache_t;

void cache_init(cache_t *c);
char* cache_get(cache_t *c, int fd, uint64_t offset, int write);
void cache_destroy(cache_t *c);

#endif // CACHE_H
