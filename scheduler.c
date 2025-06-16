#include "scheduler.h"

CoreQueue queues[CORES] = {0};

void scheduler_report_access(int core_id, uint64_t block) {
    CoreQueue *q = &queues[core_id];
    for (int i = 0; i < q->count; i++) {
        if (q->w[i].block == block) {
            q->w[i].hot++;
            q->w[i].last_seen = time(NULL);
            return;
        }
    }
    if (q->count < 64) {
        q->w[q->count].block = block;
        q->w[q->count].hot = 1;
        q->w[q->count].last_seen = time(NULL);
        q->count++;
    }
}

int scheduler_should_migrate(int core_id) {
    int total = 0;
    for (int i = 0; i < CORES; i++)
        if (i != core_id) total += queues[i].count;
    int avg = total / (CORES - 1);
    return (queues[core_id].count < avg - 5);
}

uint64_t scheduler_get_migrated_task(int core_id) {
    for (int i = 0; i < CORES; i++) {
        if (i == core_id) continue;
        CoreQueue *q = &queues[i];
        if (q->count > 10) {
            uint64_t b = q->w[--q->count].block;
            return b;
        }
    }
    return 0;
}
