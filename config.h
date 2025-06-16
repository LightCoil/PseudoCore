#ifndef CONFIG_H
#define CONFIG_H

#define CORES        4
#define CACHE_MB     128       // RAM-кэш в МБ
#define SEGMENT_MB   512       // сегмент swap на ядро в МБ
#define BLOCK_SIZE   4096      // размер блока 4 КБ
#define MAX_CACHE_ENTRIES 8192 // Максимальное количество записей в кэше для LRU
#define MIGRATION_THRESHOLD 5  // Порог для миграции задач (разница от среднего)
#define COMPRESSION_MIN_LVL 1  // Минимальный уровень сжатия
#define COMPRESSION_MAX_LVL 9  // Максимальный уровень сжатия
#define COMPRESSION_ADAPTIVE_THRESHOLD 0.5 // Порог для адаптивного сжатия (коэффициент сжатия)

#define SWAP_IMG_PATH "./storage_swap.img"

#endif // CONFIG_H
