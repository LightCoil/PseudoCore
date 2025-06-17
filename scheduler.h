#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>

#include "config.h"

#ifndef CORES
#define CORES 4
#endif

typedef struct { 
    uint64_t block; 
    int hot; 
    time_t last_seen; 
} WorkUnit;

typedef struct { 
    WorkUnit w[64]; 
    int count; 
    pthread_mutex_t mutex;
} CoreQueue;

extern CoreQueue queues[CORES];

void scheduler_init();
void scheduler_report_access(int core_id, uint64_t block);
int scheduler_should_migrate(int core_id);
uint64_t scheduler_get_migrated_task(int core_id);
void scheduler_destroy();

#endif // SCHEDULER_H
