#include "cache.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

static size_t hash_func(uint64_t off) {
    return (off / PAGE_SIZE) % HASH_SIZE;
}

void cache_init(cache_t *c) {
    for (int i = 0; i < HASH_SIZE; i++)
        c->hash[i] = NULL;
}

char* cache_get(cache_t *c, int fd, uint64_t off, int write) {
    size_t h = hash_func(off);
    cache_entry_t *e = c->hash[h];
    while (e) {
        if (e->offset == off) {
            if (write) e->dirty = 1;
            return e->data;
        }
        e = e->next;
    }
    // промах — загружаем с диска
    cache_entry_t *ne = malloc(sizeof(*ne));
    ne->offset = off;
    ne->dirty = write;
    ne->next = c->hash[h];
    c->hash[h] = ne;
    // чтение страницы
    pread(fd, ne->data, PAGE_SIZE, off);
    return ne->data;
}

void cache_destroy(cache_t *c) {
    for (int i = 0; i < HASH_SIZE; i++) {
        cache_entry_t *e = c->hash[i];
        while (e) {
            cache_entry_t *n = e->next;
            if (e->dirty) {
                // при желании можно записать обратно
                // pwrite(fd, e->data, PAGE_SIZE, e->offset);
            }
            free(e);
            e = n;
        }
    }
}
