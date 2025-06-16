#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>

// Константы
#include "config.h"

#define PAGE_SIZE BLOCK_SIZE
#define HASH_SIZE 2048
#define MUTEX_GROUPS 16 // Уменьшенное количество мьютексов для групп корзин

typedef struct cache_entry {
    uint64_t offset;
    char data[PAGE_SIZE];
    int dirty;
    time_t last_access; // Для LRU-вытеснения
    struct cache_entry *next;
    struct cache_entry *prev; // Для двусвязного списка LRU
} cache_entry_t;

typedef struct {
    cache_entry_t *hash[HASH_SIZE];
    pthread_mutex_t mutex[MUTEX_GROUPS]; // Мьютекс для групп корзин хэша
    cache_entry_t *lru_head; // Голова списка LRU (самые недавно использованные)
    cache_entry_t *lru_tail; // Хвост списка LRU (наименее недавно использованные)
    size_t entry_count; // Текущее количество записей в кэше
    pthread_mutex_t lru_mutex; // Мьютекс для списка LRU
} cache_t;

void cache_init(cache_t *c);
char* cache_get(cache_t *c, int fd, uint64_t offset, int write);
void cache_destroy(cache_t *c, int fd); // Добавлен параметр fd для записи грязных страниц
void cache_evict(cache_t *c, int fd); // Вытеснение наименее используемых записей с записью грязных страниц

#endif // CACHE_H
