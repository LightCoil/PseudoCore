#ifndef ANTICIPATOR_H
#define ANTICIPATOR_H
#include <stdint.h>
#include <time.h>
#define PRED 512
// Structure for prediction entry with offset, confidence, last access time, and access frequency
typedef struct {
    uint64_t off; // Block offset
    int conf;     // Confidence level for prediction
    time_t t;     // Last access time
    int freq;     // Access frequency for better prediction
} PE;

static PE table[PRED];

// Learn a new block access pattern to improve prediction
static void learn(uint64_t o) {
    int oldest_idx = 0;
    time_t oldest_time = table[0].t;
    for (int i = 0; i < PRED; i++) {
        if (table[i].off == o || table[i].off == 0) {
            table[i].off = o;
            table[i].conf++;
            table[i].t = time(NULL);
            table[i].freq++;
            return;
        }
        // Track the oldest entry for replacement if table is full
        if (table[i].t < oldest_time) {
            oldest_time = table[i].t;
            oldest_idx = i;
        }
    }
    // Replace the oldest entry if no match found
    table[oldest_idx].off = o;
    table[oldest_idx].conf = 1;
    table[oldest_idx].t = time(NULL);
    table[oldest_idx].freq = 1;
}

// Check if prefetching is recommended for a block based on confidence and recency
static int prefetch_ok(uint64_t o) {
    for (int i = 0; i < PRED; i++) {
        if (table[i].off == o && table[i].conf >= 3 && table[i].freq >= 2 && (time(NULL) - table[i].t < 15)) {
            return 1; // Prefetch recommended if confidence, frequency, and recency criteria are met
        }
    }
    return 0; // Prefetch not recommended
}
#endif
