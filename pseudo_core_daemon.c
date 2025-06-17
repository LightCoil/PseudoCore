#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include "config.h"
#include "cache.h"
#include "compress.h"
#include "ring_cache.h"
#include "scheduler.h"

// Конфигурация с пониженной нагрузкой
#define CORES 2  // Уменьшаем количество ядер
#define SEGMENT_MB 64  // Уменьшаем размер сегмента
#define BLOCK_SIZE 4096
#define LOAD_THRESHOLD 30
#define HIGH_LOAD_DELAY_NS 50000000  // 50ms
#define LOW_LOAD_DELAY_NS 25000000   // 25ms
#define BASE_LOAD_DELAY_NS 10000000  // 10ms
#define PID_FILE "/var/run/pseudo_core.pid"
#define LOG_FILE "/var/log/pseudo_core.log"

volatile int global_running = 1;
static pthread_t core_threads[CORES];
static core_arg_t core_args[CORES];
static pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        syslog(LOG_INFO, "Получен сигнал завершения, останавливаем сервис...");
        global_running = 0;
        
        // Ждем завершения всех потоков
        for (int i = 0; i < CORES; i++) {
            core_args[i].running = 0;
            pthread_join(core_threads[i], NULL);
        }
        
        closelog();
        unlink(PID_FILE);
        exit(0);
    }
}

void daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    umask(0);
    pid_t sid = setsid();
    if (sid < 0) {
        exit(EXIT_FAILURE);
    }

    if ((chdir("/")) < 0) {
        exit(EXIT_FAILURE);
    }

    // Закрываем стандартные файловые дескрипторы
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Открываем лог
    openlog("pseudo_core", LOG_PID, LOG_DAEMON);

    // Создаем PID файл
    FILE *pid_file = fopen(PID_FILE, "w");
    if (pid_file) {
        fprintf(pid_file, "%d\n", getpid());
        fclose(pid_file);
    }
}

void* core_run(void *v) {
    core_arg_t *c = v;
    cache_t cache;
    cache_init(&cache);
    ring_cache_init();

    while (c->running && global_running) {
        // Основная логика обработки с увеличенными задержками
        static uint64_t pos[CORES] = {0};
        uint64_t idx = pos[c->id]++;
        uint64_t offset = (uint64_t)c->id * c->seg_size + 
                         (idx % (c->seg_size / BLOCK_SIZE)) * BLOCK_SIZE;

        char buf[BLOCK_SIZE];
        char *page = cache_get(&cache, c->fd, offset, 1);
        if (!page) {
            syslog(LOG_ERR, "Core %d: Failed to get cache page", c->id);
            struct timespec delay = {0, HIGH_LOAD_DELAY_NS};
            nanosleep(&delay, NULL);
            continue;
        }

        memcpy(buf, page, BLOCK_SIZE);

        // Сокращенная обработка данных
        for (int i = 0; i < BLOCK_SIZE; i++) {
            buf[i] ^= c->id;
        }

        // Сжатие и запись
        char cmp[BLOCK_SIZE];
        int cs = compress_page(buf, BLOCK_SIZE, cmp, 1);
        if (cs > 0) {
            pwrite(c->fd, cmp, cs, offset);
        }

        cache_to_ring(offset, buf);

        // Увеличенная задержка для снижения нагрузки
        struct timespec delay = {0, BASE_LOAD_DELAY_NS * 2};
        nanosleep(&delay, NULL);
    }

    ring_cache_destroy();
    cache_destroy(&cache, c->fd);
    return NULL;
}

int main(void) {
    daemonize();
    syslog(LOG_INFO, "PseudoCore daemon запущен");

    // Устанавливаем обработчики сигналов
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    // Открываем файл хранилища
    int fd = open("storage_swap.img", O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        syslog(LOG_ERR, "Не удалось открыть файл хранилища");
        exit(EXIT_FAILURE);
    }

    // Запускаем потоки обработки
    for (int i = 0; i < CORES; i++) {
        core_args[i].id = i;
        core_args[i].fd = fd;
        core_args[i].seg_size = SEGMENT_MB * 1024 * 1024;
        core_args[i].running = 1;

        if (pthread_create(&core_threads[i], NULL, core_run, &core_args[i]) != 0) {
            syslog(LOG_ERR, "Не удалось создать поток для ядра %d", i);
            exit(EXIT_FAILURE);
        }
    }

    // Основной цикл демона
    while (global_running) {
        sleep(1);
    }

    close(fd);
    syslog(LOG_INFO, "PseudoCore daemon завершил работу");
    closelog();
    return 0;
}
