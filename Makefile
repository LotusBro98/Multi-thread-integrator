CC=gcc
CFLAGS=-W -Wall -std=c99 -Os
LDFLAGS=-L. -Wl,-rpath,. -lfunction -lm -lpthread

all: libfunction.so integrate

libfunction.so: function.c
	$(CC) $(CFLAGS) -shared -fPIC $< -o $@ -lm

integrate: integrate.c list.c ui.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

asm: integrate.s
integrate.s: integrate.c
	$(CC) $(CFLAGS) -S $< -o $@ -masm=intel

clean:
	rm -rf integrate integrate.s libfunction.so

integrate: list.h ui.h general.h
