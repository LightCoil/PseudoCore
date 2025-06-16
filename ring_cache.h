#ifndef RING_CACHE_H
#define RING_CACHE_H

#include <stdint.h>
#include "config.h"

#define RING_SIZE (CACHE_MB * 1024 * 1024)

void ring_cache_init();
void cache_to_ring(uint64_t off, const void *data);
void ring_cache_destroy();

#endif // RING_CACHE_H
