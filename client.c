/*
 * client.c  --  UNO Game Client  (main loop / glue)
 *
 * Usage:  ./client <host> <port> <name>
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>

#include "game_entities.h"
#include "communication.h"
#include "client_input.h"
#include "client_output.h"

static GameState st;
static int connected = 1;
static int id = -1;
static int expected_players = 0;
static int received_players = 0;

static int parse_int_or_default(const char* s, int fallback) {
  if (s == NULL) {
    return fallback;
  }

  char* end = NULL;
  long v = strtol(s, &end, 10);
  if (end == s) {
    return fallback;
  }
  return (int)v;
}

static int string_to_cards_local(const char* s, Card* out, int max_cards) {
  if (s == NULL || out == NULL || max_cards <= 0 || s[0] == '\0') {
    return 0;
  }

  size_t n = strlen(s) + 1;
  char* tmp = (char*)malloc(n);
  if (tmp == NULL) {
    return 0;
  }
  memcpy(tmp, s, n);

  int count = 0;
  char* save = NULL;
  char* tok = strtok_r(tmp, ";", &save);
  while (tok != NULL && count < max_cards) {
    int color = 0;
    int value = 0;
    int wild = 0;
    if (sscanf(tok, "%d,%d,%d", &color, &value, &wild) == 3) {
      if (color < COLOR_RED) {
        color = COLOR_RED;
      }
      if (color > COLOR_WILD) {
        color = COLOR_WILD;
      }
      if (value < CARD_0) {
        value = CARD_0;
      }
      if (value > CARD_WILD4) {
        value = CARD_WILD4;
      }
      if (wild < COLOR_RED) {
        wild = COLOR_RED;
      }
      if (wild > COLOR_WILD) {
        wild = COLOR_WILD;
      }

      out[count++] = (Card){
        (CardColor)color,
        (CardValue)value,
        (CardColor)wild
      };
    }
    tok = strtok_r(NULL, ";", &save);
  }

  free(tmp);
  return count;
}

static void free_local_players(GameState* g) {
  if (g == NULL || g->players == NULL || g->player_count <= 0) {
    return;
  }

  Player* p = g->players;
  for (int i = 0; i < g->player_count; i++) {
    Player* next = p->next;
    free(p);
    p = next;
  }

  g->players = NULL;
  g->player_count = 0;
}

static void append_local_player(GameState* g, Player* p) {
  if (g == NULL || p == NULL) {
    return;
  }

  if (g->players == NULL) {
    g->players = p;
    p->next = p;
    p->prev = p;
    g->player_count = 1;
    return;
  }

  Player* tail = g->players->prev;
  tail->next = p;
  p->prev = tail;
  p->next = g->players;
  g->players->prev = p;
  g->player_count++;
}

static void handle_server_message(const read_data* msg) {
  if (msg == NULL || msg->num_chunks <= 0 || msg->data == NULL || msg->data[0] == NULL) {
    return;
  }

  const char* type = msg->data[0];
  const char* text = (msg->num_chunks >= 2 && msg->data[1] != NULL) ? msg->data[1] : "";

  if (strcmp(type, "ID") == 0) {
    id = (int)strtol(text, NULL, 10);
    return;
  }
  if (strcmp(type, "STATE_UPDATE") == 0) {
    if (msg->num_chunks < 3 || msg->data[1] == NULL) {
      return;
    }

    const char* subtype = msg->data[1];
    if (strcmp(subtype, "GAME") == 0) {
      if (msg->num_chunks < 12) {
        return;
      }

      free_local_players(&st);

      st.game_started = parse_int_or_default(msg->data[2], 0);
      st.game_over = parse_int_or_default(msg->data[3], 0);
      st.current_player_id = parse_int_or_default(msg->data[4], -1);
      st.direction = parse_int_or_default(msg->data[5], 1);
      st.draw_top_idx = parse_int_or_default(msg->data[6], -1);
      st.discard_top_idx = parse_int_or_default(msg->data[7], -1);

      int draw_count = string_to_cards_local(msg->data[8], st.draw_pile, DECK_SIZE);
      int discard_count = string_to_cards_local(msg->data[9], st.discard_pile, DECK_SIZE);
      st.player_count = 0;
      expected_players = parse_int_or_default(msg->data[10], 0);
      st.effect_applied = parse_int_or_default(msg->data[11], 0);

      if (expected_players < 0) {
        expected_players = 0;
      }
      if (expected_players > MAX_PLAYERS) {
        expected_players = MAX_PLAYERS;
      }

      st.draw_top_idx = draw_count - 1;
      st.discard_top_idx = discard_count - 1;
      received_players = 0;

      if (expected_players == 0) {
        Player* me = game_find_player(&st, id);
        if (me != NULL) {
          print_status(me, &st);
        }
      }
      return;
    }

    if (strcmp(subtype, "PLAYER") == 0) {
      if (msg->num_chunks < 6) {
        return;
      }

      Player* p = (Player*)calloc(1, sizeof(Player));
      if (p == NULL) {
        return;
      }

      p->sock_fd = -1;
      p->connected = 1;
      p->id = parse_int_or_default(msg->data[2], -1);

      const char* name = msg->data[3] == NULL ? "" : msg->data[3];
      strncpy(p->name, name, MAX_NAME);
      p->name[MAX_NAME] = '\0';

      int parsed_hand = string_to_cards_local(msg->data[4], p->hand, DECK_SIZE);
      int hand_count = parse_int_or_default(msg->data[5], 0);
      if (hand_count < 0) {
        hand_count = 0;
      }
      if (hand_count > parsed_hand) {
        hand_count = parsed_hand;
      }
      p->hand_count = hand_count;

      append_local_player(&st, p);
      received_players++;

      if (received_players >= expected_players) {
        Player* me = game_find_player(&st, id);
        if (me != NULL) {
          print_status(me, &st);
        }
      }
      return;
    }
  }
  if (strcmp(type, "GAME_STATE") == 0) {
    Player* me = game_find_player(&st, id);
    if (me != NULL) {
      print_status(me, &st);
    }
  }
  if (strcmp(type, "ERROR") == 0) {
    print_event("[Error]", FG_RED, text);
    return;
  }
  if (strcmp(type, "INFO") == 0) {
    print_event("[Info]", FG_CYAN, text);
    return;
  }
  if (strcmp(type, "ACTION") == 0) {
    print_event("[Action]", FG_GREEN, text);
    return;
  }
  if (strcmp(type, "CHAT") == 0) {
    print_event("[Chat]", FG_BLUE, text);
    return;
  }

  print_event("[Unknown]", FG_GRAY, type);
}

// return 0 if successfully sent to server (or not sent if caught invalid)
static int send_command(int fd, Command cmd) {
  switch (cmd.type) {
  case CMD_NONE:
    print_event("[Input]", FG_GRAY, "Empty command");
    return 0;
  case CMD_PLAY:
    return write_in_chunks(fd, "MSG_PLAY", cmd.card_index_str, cmd.chosen_color_str, NULL);
  case CMD_DRAW:
    return write_in_chunks(fd, "MSG_DRAW", NULL);
  case CMD_PASS:
    return write_in_chunks(fd, "MSG_PASS", NULL);
  case CMD_UNO:
    return write_in_chunks(fd, "MSG_UNO", NULL);
  case CMD_CALLOUT:
    return write_in_chunks(fd, "MSG_CALLOUT", cmd.arg, NULL);
  case CMD_CHAT:
    return write_in_chunks(fd, "MSG_CHAT", cmd.arg, NULL);
  case CMD_START:
    return write_in_chunks(fd, "MSG_START", NULL);
  case CMD_STATUS:
    return write_in_chunks(fd, "MSG_STATUS", NULL);
  case CMD_HELP:
    print_help();
    return 0;
  case CMD_INVALID:
    print_event("[Input]", FG_GRAY, cmd.error);
    return 0;
  default:
    return 1;
  }
}

// connect to the server using the standard procedure
static int connect_to_server(const char* host, int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);

  if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
    fprintf(stderr, "Invalid host\n");
    close(fd);
    return -1;
  }
  if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    fprintf(stderr, "Usage: %s <host> <port> <name>\n", argv[0]);
    return 1;
  }

  const char* host = argv[1];
  int port = atoi(argv[2]);
  const char* name = argv[3];

  if ((int)strlen(name) > MAX_NAME) {
    fprintf(stderr, "Name too long (max %d characters).\n", MAX_NAME);
    return 1;
  }

  // connect to the server 
  int fd = connect_to_server(host, port);
  if (fd < 0) {
    perror("connect"); return 1;
  }

  game_init(&st);

  // send the user name
  if (write_in_chunks(fd, "MSG_JOIN", name, NULL) == 1) {
    perror("send join");
    close(fd);
    return 1;
  }

  print_help();

  char last_cmd_str[MAX_PAYLOAD] = "";

  while (connected) {
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(STDIN_FILENO, &rset);
    FD_SET(fd, &rset);
    int maxfd = (fd > STDIN_FILENO) ? fd : STDIN_FILENO;

    if (select(maxfd + 1, &rset, NULL, NULL, NULL) < 0) {
      if (errno == EINTR) continue;
      perror("select");
      break;
    }

    /* ── Network data from server ────────────── */
    if (FD_ISSET(fd, &rset)) {
      read_data msg = { 0 };
      if (read_in_chunks(fd, &msg) == 1) {
        print_event("[System]", FG_GRAY, "Disconnected from server.");
        connected = 0;
        free_read_data(&msg);
        break;
      }

      handle_server_message(&msg);
      free_read_data(&msg);
    }

    /* ── User keyboard input ─────────────────── */
    if (FD_ISSET(STDIN_FILENO, &rset)) {
      char line[MAX_PAYLOAD];
      if (!fgets(line, sizeof(line), stdin)) {
        print_event("[System]", FG_GRAY, "Input closed. Exiting.");
        connected = 0;
        break;
      }

      if (strcmp(line, "\x1b[A") == 0 || strcmp(line, "\x1b[A\n") == 0) {
        if (last_cmd_str[0] == '\0') {
          continue;
        }
        strncpy(line, last_cmd_str, sizeof(line) - 1);
        line[sizeof(line) - 1] = '\0';
      }

      Command cmd = parse_command(line);
      if (send_command(fd, cmd) == 1) {
        print_event("[System]", FG_GRAY, "Failed to send command.");
        connected = 0;
        break;
      }

      if (cmd.type != CMD_NONE && cmd.type != CMD_INVALID) {
        strncpy(last_cmd_str, line, sizeof(last_cmd_str) - 1);
        last_cmd_str[sizeof(last_cmd_str) - 1] = '\0';
      }
    }

  }

  close(fd);
  return 0;
}
