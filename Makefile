PORT ?= 4242
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g

TARGETS = server client

all: $(TARGETS)

server: server.c protocol.h
	$(CC) $(CFLAGS) server.c -o server

client: client.c protocol.h
	$(CC) $(CFLAGS) client.c -o client

clean:
	rm -f $(TARGETS) *.o

.PHONY: all clean
