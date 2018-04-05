CC=gcc
CFLAGS=-W -Wall -std=c99
LDFLAGS=-L. -Wl,-rpath,. -lfunction -lm

all: libfunction.so integrate

libfunction.so: function.c
	$(CC) $(CFLAGS) -shared -fPIC $< -o $@ -lm

integrate: integrate.c 
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

asm: integrate.s
integrate.s: integrate.c
	$(CC) $(CFLAGS) -S $< -o $@ -masm=intel

clean:
	rm -rf integrate integrate.s libfunction.so
