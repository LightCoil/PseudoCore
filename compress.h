#ifndef COMPRESS_H
#define COMPRESS_H

#include <stddef.h>

int compress_page(const char *in, size_t sz, char *out, int lvl);
int decompress_page(const char *in, size_t sz, char *out);

#endif // COMPRESS_H
