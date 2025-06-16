#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include "config.h"

#define MAX_QUEUE_SIZE 128 // Увеличенный размер очереди задач

typedef struct { uint64_t block; int hot; time_t last_seen; } WorkUnit;
typedef struct { WorkUnit w[MAX_QUEUE_SIZE]; int count; pthread_mutex_t mutex; } CoreQueue;

extern CoreQueue queues[CORES];

void scheduler_init(void); // Инициализация мьютексов
void scheduler_report_access(int core_id, uint64_t block);
int scheduler_should_migrate(int core_id);
uint64_t scheduler_get_migrated_task(int core_id);
void scheduler_balance_load(void); // Балансировка нагрузки между ядрами
void scheduler_destroy(void); // Уничтожение мьютексов

#endif // SCHEDULER_H
