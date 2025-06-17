// Примечание: Если вы видите ошибку IntelliSense "#include errors detected", 
// это связано с конфигурацией includePath в VSCode. 
// Пожалуйста, обновите includePath, выбрав команду "C/C++: Select IntelliSense Configuration..." 
// или добавив необходимые пути в настройки c_cpp_properties.json.
#include "scheduler.h"

CoreQueue queues[CORES] = {0};

void scheduler_init() {
    for (int i = 0; i < CORES; i++) {
        queues[i].count = 0;
        pthread_mutex_init(&queues[i].mutex, NULL);
    }
}

void scheduler_report_access(int core_id, uint64_t block) {
    pthread_mutex_lock(&queues[core_id].mutex);
    CoreQueue *q = &queues[core_id];
    for (int i = 0; i < q->count; i++) {
        if (q->w[i].block == block) {
            q->w[i].hot++;
            q->w[i].last_seen = time(NULL);
            pthread_mutex_unlock(&queues[core_id].mutex);
            return;
        }
    }
    if (q->count < 64) {
        q->w[q->count].block = block;
        q->w[q->count].hot = 1;
        q->w[q->count].last_seen = time(NULL);
        q->count++;
    }
    pthread_mutex_unlock(&queues[core_id].mutex);
}

int scheduler_should_migrate(int core_id) {
    int total = 0;
    int other_cores = 0;
    for (int i = 0; i < CORES; i++) {
        if (i != core_id) {
            pthread_mutex_lock(&queues[i].mutex);
            total += queues[i].count;
            pthread_mutex_unlock(&queues[i].mutex);
            other_cores++;
        }
    }
    int avg = (other_cores > 0) ? (total / other_cores) : 0;
    pthread_mutex_lock(&queues[core_id].mutex);
    int result = (queues[core_id].count < avg - MIGRATION_THRESHOLD);
    pthread_mutex_unlock(&queues[core_id].mutex);
    return result;
}

uint64_t scheduler_get_migrated_task(int core_id) {
    for (int i = 0; i < CORES; i++) {
        if (i == core_id) continue;
        pthread_mutex_lock(&queues[i].mutex);
        CoreQueue *q = &queues[i];
        if (q->count > 0) {
            uint64_t b = q->w[--q->count].block;
            pthread_mutex_unlock(&queues[i].mutex);
            return b;
        }
        pthread_mutex_unlock(&queues[i].mutex);
    }
    return 0;
}

void scheduler_destroy() {
    for (int i = 0; i < CORES; i++) {
        pthread_mutex_destroy(&queues[i].mutex);
    }
}
