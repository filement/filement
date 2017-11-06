export CC=gcc
export CFLAGS=-std=c99 -pthread -g -DOS_MAC -DOS_BSD -Wno-parentheses -Wno-empty-body
export LDFLAGS=-pthread

all: index ffind

index: buffer.o magic.o indexing.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o indexing

ffind: ffind.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o ffind

clean:
	rm -f *.o
	rm -f indexing ffind
