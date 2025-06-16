#include "scheduler.h"
#include <pthread.h>
#include <time.h>
#include <stdio.h>

CoreQueue queues[CORES] = {0};

// Log scheduler-related events or errors
static void log_scheduler_message(const char *level, const char *message, int core_id) {
    time_t now = time(NULL);
    char timestamp[26];
    ctime_r(&now, timestamp);
    timestamp[24] = '\0'; // Remove newline from ctime
    fprintf(stderr, "[%s] [%s] Scheduler Core %d: %s\n", timestamp, level, core_id, message);
}

void scheduler_init(void) {
    for (int i = 0; i < CORES; i++) {
        queues[i].count = 0;
        pthread_mutex_init(&queues[i].mutex, NULL);
    }
    log_scheduler_message("INFO", "Scheduler initialized", -1);
}

void scheduler_report_access(int core_id, uint64_t block) {
    CoreQueue *q = &queues[core_id];
    pthread_mutex_lock(&q->mutex);
    for (int i = 0; i < q->count; i++) {
        if (q->w[i].block == block) {
            q->w[i].hot++;
            q->w[i].last_seen = time(NULL);
            pthread_mutex_unlock(&q->mutex);
            return;
        }
    }
    if (q->count < MAX_QUEUE_SIZE) {
        q->w[q->count].block = block;
        q->w[q->count].hot = 1;
        q->w[q->count].last_seen = time(NULL);
        q->count++;
    } else {
        // If queue is full, replace the least "hot" task
        int min_hot_idx = 0;
        for (int i = 1; i < q->count; i++) {
            if (q->w[i].hot < q->w[min_hot_idx].hot) {
                min_hot_idx = i;
            }
        }
        q->w[min_hot_idx].block = block;
        q->w[min_hot_idx].hot = 1;
        q->w[min_hot_idx].last_seen = time(NULL);
    }
    pthread_mutex_unlock(&q->mutex);
}

int scheduler_should_migrate(int core_id) {
    int total = 0;
    int hot_total = 0;
    for (int i = 0; i < CORES; i++) {
        if (i != core_id) {
            pthread_mutex_lock(&queues[i].mutex);
            total += queues[i].count;
            for (int j = 0; j < queues[i].count; j++) {
                if (queues[i].w[j].hot > 2 && (time(NULL) - queues[i].w[j].last_seen) < 10) {
                    hot_total++;
                }
            }
            pthread_mutex_unlock(&queues[i].mutex);
        }
    }
    int avg = total / (CORES - 1);
    pthread_mutex_lock(&queues[core_id].mutex);
    int current_load = queues[core_id].count;
    pthread_mutex_unlock(&queues[core_id].mutex);
    // Migration is needed if the load on the current core is significantly below average
    return (current_load < avg - MIGRATION_THRESHOLD);
}

uint64_t scheduler_get_migrated_task(int core_id) {
    int max_load_id = -1;
    int max_load = 0;
    int max_hot = 0;
    for (int i = 0; i < CORES; i++) {
        if (i == core_id) continue;
        pthread_mutex_lock(&queues[i].mutex);
        if (queues[i].count > max_load) {
            max_load = queues[i].count;
            max_load_id = i;
            max_hot = 0;
            for (int j = 0; j < queues[i].count; j++) {
                if (queues[i].w[j].hot > max_hot && (time(NULL) - queues[i].w[j].last_seen) < 10) {
                    max_hot = queues[i].w[j].hot;
                }
            }
        }
        pthread_mutex_unlock(&queues[i].mutex);
    }
    if (max_load_id >= 0 && max_load > MIGRATION_THRESHOLD) {
        pthread_mutex_lock(&queues[max_load_id].mutex);
        // Find the "hottest" task for migration
        int hot_idx = -1;
        int max_hotness = 0;
        for (int i = 0; i < queues[max_load_id].count; i++) {
            if (queues[max_load_id].w[i].hot > max_hotness && (time(NULL) - queues[max_load_id].w[i].last_seen) < 10) {
                max_hotness = queues[max_load_id].w[i].hot;
                hot_idx = i;
            }
        }
        if (hot_idx >= 0) {
            uint64_t b = queues[max_load_id].w[hot_idx].block;
            // Remove task from queue
            for (int i = hot_idx; i < queues[max_load_id].count - 1; i++) {
                queues[max_load_id].w[i] = queues[max_load_id].w[i + 1];
            }
            queues[max_load_id].count--;
            pthread_mutex_unlock(&queues[max_load_id].mutex);
            char msg[256];
            snprintf(msg, sizeof(msg), "Migrated task %lu from core %d to core %d", b, max_load_id, core_id);
            log_scheduler_message("INFO", msg, core_id);
            return b;
        }
        pthread_mutex_unlock(&queues[max_load_id].mutex);
    }
    return 0;
}

void scheduler_balance_load(void) {
    // Load balancing between cores
    int min_load_id = 0, max_load_id = 0;
    int min_load = queues[0].count;
    int max_load = queues[0].count;
    for (int i = 1; i < CORES; i++) {
        pthread_mutex_lock(&queues[i].mutex);
        if (queues[i].count < min_load) {
            min_load = queues[i].count;
            min_load_id = i;
        }
        if (queues[i].count > max_load) {
            max_load = queues[i].count;
            max_load_id = i;
        }
        pthread_mutex_unlock(&queues[i].mutex);
    }
    if (max_load - min_load > MIGRATION_THRESHOLD) {
        uint64_t task = scheduler_get_migrated_task(min_load_id);
        if (task) {
            scheduler_report_access(min_load_id, task);
            char msg[256];
            snprintf(msg, sizeof(msg), "Balanced load by migrating task %lu to core %d", task, min_load_id);
            log_scheduler_message("INFO", msg, min_load_id);
        }
    }
}

void scheduler_destroy(void) {
    for (int i = 0; i < CORES; i++) {
        pthread_mutex_destroy(&queues[i].mutex);
    }
    log_scheduler_message("INFO", "Scheduler destroyed", -1);
}
