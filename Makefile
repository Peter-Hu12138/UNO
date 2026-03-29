PORT ?= 4242
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g

all: server client

server: server.c protocol.h
	$(CC) $(CFLAGS) server.c -o server

client: client.c input.c ui.c protocol.h input.h ui.h
	$(CC) $(CFLAGS) client.c input.c ui.c -o client

clean:
	rm -f server client *.o

.PHONY: all clean
