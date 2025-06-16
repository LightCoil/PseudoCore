#ifndef BLOCK_PRIORITY_H
#define BLOCK_PRIORITY_H
#include <stdint.h>
#include <time.h>
#define MAX_STAT 4096
// Structure for block access statistics with offset, last access time, and access count
typedef struct {
    uint64_t off; // Block offset
    time_t t;     // Last access time
    int cnt;      // Access count for priority calculation
    int freq;     // Access frequency for enhanced priority
} Stat;

static Stat stats[MAX_STAT];

// Update statistics for a block access to track priority
static void update_stat(uint64_t o) {
    int oldest_idx = 0;
    time_t oldest_time = stats[0].t;
    for (int i = 0; i < MAX_STAT; i++) {
        if (stats[i].off == o || stats[i].off == 0) {
            stats[i].off = o;
            stats[i].t = time(NULL);
            stats[i].cnt++;
            stats[i].freq++;
            return;
        }
        // Track the oldest entry for replacement if table is full
        if (stats[i].t < oldest_time) {
            oldest_time = stats[i].t;
            oldest_idx = i;
        }
    }
    // Replace the oldest entry if no match found
    stats[oldest_idx].off = o;
    stats[oldest_idx].t = time(NULL);
    stats[oldest_idx].cnt = 1;
    stats[oldest_idx].freq = 1;
}

// Check if a block is considered "hot" based on access count and recency
static int is_hot(uint64_t o) {
    for (int i = 0; i < MAX_STAT; i++) {
        if (stats[i].off == o) {
            return (stats[i].cnt >= 3 && stats[i].freq >= 2 && (time(NULL) - stats[i].t < 8));
            // Block is hot if accessed frequently and recently
        }
    }
    return 0; // Block is not hot
}
#endif
