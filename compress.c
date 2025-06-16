#include "compress.h"
#include <zstd.h>
#include <stdio.h>

int compress_page(const char *in, size_t sz, char *out, int lvl) {
    size_t c = ZSTD_compress(out, ZSTD_compressBound(sz), in, sz, lvl);
    if (ZSTD_isError(c)) {
        fprintf(stderr, "ZSTD compression error: %s\n", ZSTD_getErrorName(c));
        return -1;
    }
    return (int)c;
}

int decompress_page(const char *in, size_t sz, char *out) {
    size_t d = ZSTD_decompress(out, sz, in, sz);
    if (ZSTD_isError(d)) {
        fprintf(stderr, "ZSTD decompression error: %s\n", ZSTD_getErrorName(d));
        return -1;
    }
    return (int)d;
}
