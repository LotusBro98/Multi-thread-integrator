CC=gcc
CFLAGS=-W -Wall -std=c99 -Os
LDFLAGS=-lm

all: integrate

integrate: integrate.c list.c ui.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf integrate

integrate: list.h ui.h general.h
