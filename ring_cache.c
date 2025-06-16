#include "ring_cache.h"
#include <stdlib.h>
#include <string.h>

static size_t ring_pos;
static void *ring_buffer;

void ring_cache_init() {
    ring_buffer = malloc(RING_SIZE);
    ring_pos = 0;
}

void cache_to_ring(uint64_t off, const void *data) {
    // блок в кольце
    memcpy((char*)ring_buffer + ring_pos, data, BLOCK_SIZE);
    ring_pos = (ring_pos + BLOCK_SIZE) % RING_SIZE;
}

void ring_cache_destroy() {
    free(ring_buffer);
}
