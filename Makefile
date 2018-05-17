CC=gcc
CFLAGS=-W -Wall -std=c99 -Os
LDFLAGS=-lm

all: integrate

SOURCES=integrate.c list.c ui.c cpuconf.c
integrate: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o $@ $(LDFLAGS)

clean:
	rm -rf integrate

integrate: list.h ui.h general.h cpuconf.h
