/*
 * server.c  --  UNO Game Server  (main loop / glue)
 *
 * Usage:  ./server [port]
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#include "communication.h"
#include "game_entities.h"
#include "server_handlers.h"

static GameState g;
static int listen_fd = -1;

static Player* find_player_by_fd(int fd) {
  if (g.players == NULL || g.player_count <= 0) {
    return NULL;
  }

  Player* p = g.players;
  for (int i = 0; i < g.player_count; i++) {
    if (p->sock_fd == fd) {
      return p;
    }
    p = p->next;
  }
  return NULL;
}

static void append_player(Player* p) {
  if (p == NULL) {
    return;
  }

  if (g.players == NULL) {
    g.players = p;
    p->next = p;
    p->prev = p;
    g.player_count = 1;
    return;
  }

  Player* tail = g.players->prev;
  tail->next = p;
  p->prev = tail;
  p->next = g.players;
  g.players->prev = p;
  g.player_count++;
}


static void process_client_command(Player* p, const read_data* msg) {
  if (p == NULL || msg == NULL || msg->num_chunks <= 0 || msg->data == NULL || msg->data[0] == NULL) {
    if (p != NULL) {
      send_error_fd(p->sock_fd, "Invalid command packet");
    }
    return;
  }

  const char* cmd = msg->data[0];
  /*
  for each client command
  CMD_START
  CMD_PLAY
  CMD_DRAW
  CMD_PASS
  CMD_UNO
  CMD_CALLOUT
  CMD_CHAT
  CMD_STATUS
  */
  if (strcmp(cmd, "MSG_START") == 0) {
    handle_msg_start(&g, p, msg);
  }
  else if (strcmp(cmd, "MSG_PLAY") == 0) {
    handle_msg_play(&g, p, msg);
  }
  else if (strcmp(cmd, "MSG_DRAW") == 0) {
    handle_msg_draw(&g, p, msg);
  }
  else if (strcmp(cmd, "MSG_PASS") == 0) {
    handle_msg_pass(&g, p, msg);
  }
  else if (strcmp(cmd, "MSG_UNO") == 0) {
    handle_msg_uno(&g, p, msg);
  }
  else if (strcmp(cmd, "MSG_CALLOUT") == 0) {
    handle_msg_callout(&g, p, msg);
  }
  else if (strcmp(cmd, "MSG_CHAT") == 0) {
    handle_msg_chat_send(&g, p, msg);
  }
  else if (strcmp(cmd, "MSG_STATUS") == 0) {
    handle_msg_status(&g, p, msg);
  }
  else {
    send_error_fd(p->sock_fd, "Unknown command");
  }
}


int main(int argc, char* argv[]) {
  /* Connection table used by select(). */
  int client_fds[MAX_PLAYERS];
  for (int i = 0; i < MAX_PLAYERS; i++) {
    client_fds[i] = -1;
  }

  /* Parse CLI args. */
  int port = DEFAULT_PORT;
  if (argc > 1) {
    port = atoi(argv[1]);
  }

  /* Process initialization. */
  srand((unsigned)time(NULL));

  // ignoring SIGPIPE prevents a single failed write from crashing the server
  signal(SIGPIPE, SIG_IGN);

  game_init(&g);

  /* Socket setup. */
  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("socket");
    return 1;
  }

  int opt = 1;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt");
    close(listen_fd);
    return 1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons((uint16_t)port);

  if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(listen_fd);
    return 1;
  }
  if (listen(listen_fd, 5) < 0) {
    perror("listen");
    close(listen_fd);
    return 1;
  }

  printf("=== UNO Server on port %d ready ===\n", port);

  /* Main event loop: accept + read + dispatch. */
  while (!g.game_over) {
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(listen_fd, &rset);
    int maxfd = listen_fd;

    // add all the players fds to the set to listen
    for (int i = 0; i < MAX_PLAYERS; i++) {
      if (client_fds[i] >= 0) {
        FD_SET(client_fds[i], &rset);
        if (client_fds[i] > maxfd) {
          maxfd = client_fds[i];
        }
      }
    }

    if (select(maxfd + 1, &rset, NULL, NULL, NULL) < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("select");
      break;
    }

    // on client joined
    if (FD_ISSET(listen_fd, &rset)) {
      struct sockaddr_in ca;
      socklen_t cl = sizeof(ca);
      int cfd = accept(listen_fd, (struct sockaddr*)&ca, &cl);
      if (cfd >= 0) {
        // find a free spot in the client fds array
        int slot = -1;
        for (int i = 0; i < MAX_PLAYERS; i++) {
          if (client_fds[i] < 0) {
            slot = i;
            break;
          }
        }

        if (slot < 0 || g.game_started) {
          (void)write_in_chunks(cfd, "ERROR", "Game is full / already started", NULL);
          close(cfd);
        }
        else {
          Player* p = (Player*)malloc(sizeof(Player));

          if (p == NULL) {
            (void)write_in_chunks(cfd, "ERROR", "Server memory error", NULL);
            close(cfd);
            continue;
          }

          p->sock_fd = cfd;
          p->id = slot;
          char id_str[16];
          snprintf(id_str, sizeof(id_str), "%d", slot);
          (void)write_in_chunks(cfd, "ID", id_str, NULL);
          p->connected = 1;
          append_player(p);

          client_fds[slot] = cfd;
          broadcast_to_all(&g, "INFO", "A player joined the server");
        }
      }
    }

    /* Read chunked messages from joined clients. */
    for (int i = 0; i < MAX_PLAYERS; i++) {
      if (client_fds[i] < 0 || !FD_ISSET(client_fds[i], &rset)) {
        continue;
      }

      read_data msg = { 0 };
      // on read error
      Player* p = find_player_by_fd(client_fds[i]);
      if (read_in_chunks(client_fds[i], &msg) == 1) {
        if (p != NULL) {
          p->connected = 0;
        }
        close(client_fds[i]);
        client_fds[i] = -1;
        free_read_data(&msg);
        
        game_remove_disconnected_players();
        broadcast_to_all(&g, "INFO", "A player left the server");
        continue;
      }

      process_client_command(p, &msg);
      free_read_data(&msg);
    }
  }

  /* Cleanup open sockets. */
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (client_fds[i] >= 0) {
      close(client_fds[i]);
    }
  }
  if (listen_fd >= 0) {
    close(listen_fd);
  }

  printf("[Server] Shutdown.\n");
  return 0;
}
