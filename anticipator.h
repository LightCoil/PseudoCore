#ifndef ANTICIPATOR_H
#define ANTICIPATOR_H
#include <stdint.h>
#include <time.h>
#define PRED 512
#define MARKOV_DEPTH 3 // Depth for Markov chain prediction (number of previous blocks to consider)

// Structure for prediction entry with offset, confidence, last access time, and access frequency
typedef struct {
    uint64_t off; // Block offset
    int conf;     // Confidence level for prediction
    time_t t;     // Last access time
    int freq;     // Access frequency for better prediction
    uint64_t next_off[MARKOV_DEPTH]; // Predicted next blocks based on access patterns
    int next_conf[MARKOV_DEPTH];     // Confidence for each predicted next block
} PE;

static PE table[PRED];
static uint64_t access_history[MARKOV_DEPTH] = {0}; // History of last accessed blocks for Markov chain
static int history_idx = 0; // Current index in access history

// Learn a new block access pattern to improve prediction using Markov chain
static void learn(uint64_t o) {
    int oldest_idx = 0;
    time_t oldest_time = table[0].t;
    for (int i = 0; i < PRED; i++) {
        if (table[i].off == o || table[i].off == 0) {
            table[i].off = o;
            table[i].conf++;
            table[i].t = time(NULL);
            table[i].freq++;
            // Update Markov chain prediction based on access history
            if (history_idx > 0) {
                uint64_t prev_off = access_history[(history_idx - 1) % MARKOV_DEPTH];
                for (int j = 0; j < PRED; j++) {
                    if (table[j].off == prev_off) {
                        for (int k = 0; k < MARKOV_DEPTH; k++) {
                            if (table[j].next_off[k] == 0 || table[j].next_off[k] == o) {
                                table[j].next_off[k] = o;
                                table[j].next_conf[k]++;
                                break;
                            }
                        }
                        break;
                    }
                }
            }
            // Update access history
            access_history[history_idx % MARKOV_DEPTH] = o;
            history_idx++;
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
    for (int k = 0; k < MARKOV_DEPTH; k++) {
        table[oldest_idx].next_off[k] = 0;
        table[oldest_idx].next_conf[k] = 0;
    }
    // Update access history
    access_history[history_idx % MARKOV_DEPTH] = o;
    history_idx++;
}

// Check if prefetching is recommended for a block based on confidence, recency, and Markov chain prediction
static int prefetch_ok(uint64_t o) {
    for (int i = 0; i < PRED; i++) {
        if (table[i].off == o && table[i].conf >= 3 && table[i].freq >= 2 && (time(NULL) - table[i].t < 15)) {
            return 1; // Prefetch recommended if confidence, frequency, and recency criteria are met
        }
        // Check Markov chain predictions for the last accessed block
        if (history_idx > 0) {
            uint64_t last_off = access_history[(history_idx - 1) % MARKOV_DEPTH];
            if (table[i].off == last_off) {
                for (int k = 0; k < MARKOV_DEPTH; k++) {
                    if (table[i].next_off[k] == o && table[i].next_conf[k] >= 2) {
                        return 1; // Prefetch recommended based on Markov chain prediction
                    }
                }
            }
        }
    }
    return 0; // Prefetch not recommended
}
#endif
