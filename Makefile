CC = gcc
CFLAGS = -Wall -Wextra -g -O0 -fno-omit-frame-pointer -I.
LDFLAGS =

ifeq ($(TEST_HAND),1)
CFLAGS += -DTEST_HAND
endif

.PHONY: all clean test

all: client server test_comm test_game_ent

client: client.c communication.c client_input.c client_output.c game_entities.c
	$(CC) $(CFLAGS) -o $@ client.c communication.c client_input.c client_output.c game_entities.c $(LDFLAGS)

server: server.c server_handlers.c communication.c game_entities.c
	$(CC) $(CFLAGS) -o $@ server.c server_handlers.c communication.c game_entities.c $(LDFLAGS)


test_comm: communication.o test/test_comm.c
	$(CC) $(CFLAGS) -o $@ communication.o test/test_comm.c $(LDFLAGS)

communication.o: communication.c communication.h
	$(CC) $(CFLAGS) -c communication.c

test: test_comm
	./test_comm


test_game_ent: game_entities.o test/test_game_entities.c
	$(CC) $(CFLAGS) -o $@ game_entities.o test/test_game_entities.c $(LDFLAGS)

game_entities.o: game_entities.c game_entities.h
	$(CC) $(CFLAGS) -c game_entities.c

test2: test_game_ent
	./test_game_ent


clean:
	rm -f client server test_comm test_game_ent communication.o game_entities.o
