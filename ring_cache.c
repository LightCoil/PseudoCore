#include "ring_cache.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static size_t ring_pos;
static void *ring_buffer;
static pthread_mutex_t ring_mutex;

void ring_cache_init(void) {
    ring_buffer = malloc(RING_SIZE);
    if (!ring_buffer) {
        fprintf(stderr, "Error allocating memory for ring buffer\n");
        exit(1);
    }
    ring_pos = 0;
    pthread_mutex_init(&ring_mutex, NULL);
}

void cache_to_ring(uint64_t off, const void *data) {
    if (!data || !ring_buffer) {
        fprintf(stderr, "Invalid data or ring buffer not initialized for offset %lu\n", off);
        return;
    }
    pthread_mutex_lock(&ring_mutex);
    // Copy block data into the ring buffer
    if (ring_pos + BLOCK_SIZE <= RING_SIZE) {
        memcpy((char*)ring_buffer + ring_pos, data, BLOCK_SIZE);
        ring_pos = (ring_pos + BLOCK_SIZE) % RING_SIZE;
    } else {
        fprintf(stderr, "Ring buffer overflow prevented for offset %lu\n", off);
    }
    pthread_mutex_unlock(&ring_mutex);
}

void ring_cache_destroy(void) {
    pthread_mutex_destroy(&ring_mutex);
    free(ring_buffer);
    ring_buffer = NULL;
    ring_pos = 0;
}
