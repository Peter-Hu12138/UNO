CC = gcc
CFLAGS = -Wall -Wextra -I.
LDFLAGS =

.PHONY: all clean test

all: test_comm

test_comm: communication.o test/test_comm.c
	$(CC) $(CFLAGS) -o $@ communication.o test/test_comm.c $(LDFLAGS)

communication.o: communication.c communication.h
	$(CC) $(CFLAGS) -c communication.c

test: test_comm
	./test_comm

clean:
	rm -f test_comm communication.o
