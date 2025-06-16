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

typedef struct {
    int id;
    int fd;
    uint64_t seg_size;
} core_arg_t;

void prefetch_block(int fd, uint64_t off) {
    char tmp[BLOCK_SIZE];
    pread(fd, tmp, BLOCK_SIZE, off);
}

void* core_run(void *v) {
    core_arg_t *c = v;
    cache_t cache;
    cache_init(&cache);
    ring_cache_init();

    while (1) {
        // кольцевой выбор блока
        static uint64_t pos[CORES] = {0};
        uint64_t idx = pos[c->id]++;
        uint64_t offset = (uint64_t)c->id * c->seg_size + (idx % (c->seg_size / BLOCK_SIZE)) * BLOCK_SIZE;

        scheduler_report_access(c->id, offset);

        // миграция
        if (scheduler_should_migrate(c->id)) {
            uint64_t m = scheduler_get_migrated_task(c->id);
            if (m) offset = m;
        }

        // кеширование
        char buf[BLOCK_SIZE];
        char *page = cache_get(&cache, c->fd, offset, 1);
        memcpy(buf, page, BLOCK_SIZE);

        // предвыборка соседнего
        prefetch_block(c->fd, offset + BLOCK_SIZE);

        // симуляция работы
        for (int i = 0; i < 1000; i++) buf[i % BLOCK_SIZE] ^= c->id;

        // компрессия и запись
        char cmp[BLOCK_SIZE];
        int lvl = 1; // можно усложнить адаптивно
        int cs = compress_page(buf, BLOCK_SIZE, cmp, lvl);
        if (cs > 0) {
            pwrite(c->fd, cmp, BLOCK_SIZE, offset);
        }

        cache_to_ring(offset, buf);
        usleep(10000);
    }

    ring_cache_destroy();
    return NULL;
}

int main() {
    int fd = open(SWAP_IMG_PATH, O_RDWR);
    if (fd < 0) { perror("open swap"); exit(1); }

    uint64_t seg_bytes = (uint64_t)SEGMENT_MB * 1024 * 1024;

    pthread_t th[CORES];
    core_arg_t args[CORES];

    for (int i = 0; i < CORES; i++) {
        args[i].id = i;
        args[i].fd = fd;
        args[i].seg_size = seg_bytes;
        pthread_create(&th[i], NULL, core_run, &args[i]);
    }
    for (int i = 0; i < CORES; i++)
        pthread_join(th[i], NULL);

    close(fd);
    return 0;
}
