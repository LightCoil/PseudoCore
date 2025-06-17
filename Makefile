CC = gcc
CFLAGS = -O3 -pthread -I.
LDLIBS = -lzstd -lm

SOURCES = pseudo_core.c cache.c compress.c ring_cache.c scheduler.c
OBJECTS = $(SOURCES:.c=.o)

all: pseudo_core

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

pseudo_core: $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f *.o pseudo_core
