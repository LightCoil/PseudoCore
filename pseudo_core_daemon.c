#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <string.h>

#define MAX_CORES 8

void* pseudo_core(void* arg) {
    int core_id = *(int*)arg;
        char log_entry[128];
            struct timespec delay = {0, 100000000}; // 100ms

                while (1) {
                        snprintf(log_entry, sizeof(log_entry),
                                         "[pseudo_core_%d] active at %ld\n", core_id, time(NULL));
                                                 FILE* log = fopen("/tmp/pseudo_core.log", "a");
                                                         if (log) {
                                                                     fputs(log_entry, log);
                                                                                 fclose(log);
                                                                                         }
                                                                                                 nanosleep(&delay, NULL);
                                                                                                     }
                                                                                                         return NULL;
                                                                                                         }

                                                                                                         int main(int argc, char* argv[]) {
                                                                                                             if (argc != 2) {
                                                                                                                     fprintf(stderr, "Usage: %s <num_cores>\n", argv[0]);
                                                                                                                             return 1;
                                                                                                                                 }

                                                                                                                                     int cores = atoi(argv[1]);
                                                                                                                                         if (cores < 1 || cores > MAX_CORES) {
                                                                                                                                                 fprintf(stderr, "Allowed cores: 1 to %d\n", MAX_CORES);
                                                                                                                                                         return 1;
                                                                                                                                                             }

                                                                                                                                                                 pthread_t threads[MAX_CORES];
                                                                                                                                                                     int ids[MAX_CORES];

                                                                                                                                                                         for (int i = 0; i < cores; i++) {
                                                                                                                                                                                 ids[i] = i;
                                                                                                                                                                                         if (pthread_create(&threads[i], NULL, pseudo_core, &ids[i]) != 0) {
                                                                                                                                                                                                     perror("pthread_create failed");
                                                                                                                                                                                                                 return 1;
                                                                                                                                                                                                                         }
                                                                                                                                                                                                                             }

                                                                                                                                                                                                                                 for (int i = 0; i < cores; i++) {
                                                                                                                                                                                                                                         pthread_join(threads[i], NULL);
                                                                                                                                                                                                                                             }

                                                                                                                                                                                                                                                 return 0;
                                                                                                                                                                                                                                                 }
                                                                                                                                                                                                                                                 
