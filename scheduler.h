#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <time.h>
#include "config.h"

typedef struct { uint64_t block; int hot; time_t last_seen; } WorkUnit;
typedef struct { WorkUnit w[64]; int count; } CoreQueue;

extern CoreQueue queues[CORES];

void scheduler_report_access(int core_id, uint64_t block);
int scheduler_should_migrate(int core_id);
uint64_t scheduler_get_migrated_task(int core_id);

#endif // SCHEDULER_H
