#include "compress.h"
#include <zstd.h>
#include <stdio.h>
#include <math.h>

// Calculate Shannon entropy of the input data to determine compressibility
static double calculate_entropy(const char *data, size_t sz) {
    if (sz == 0) return 0.0;
    unsigned int freq[256] = {0};
    for (size_t i = 0; i < sz; i++) {
        freq[(unsigned char)data[i]]++;
    }
    double entropy = 0.0;
    for (int i = 0; i < 256; i++) {
        if (freq[i] > 0) {
            double p = (double)freq[i] / sz;
            entropy -= p * log2(p);
        }
    }
    return entropy;
}

// Determine adaptive compression level based on data entropy
static int determine_compression_level(double entropy) {
    // Entropy range: 0 (highly compressible) to 8 (random data)
    if (entropy < 4.0) return 1; // Low level for highly ordered data
    else if (entropy < 6.0) return 3; // Medium level for moderately ordered data
    else return 5; // High level for random or high-entropy data
}

int compress_page(const char *in, size_t sz, char *out, int lvl) {
    // If lvl is 0, calculate adaptive level based on entropy
    if (lvl == 0) {
        double entropy = calculate_entropy(in, sz);
        lvl = determine_compression_level(entropy);
    }
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
