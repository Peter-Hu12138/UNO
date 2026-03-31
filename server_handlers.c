#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "server_handlers.h"

#define OUT_MSG_SIZE 512

/* ============================================================
 *  Private Helpers: Player Lookup / Parsing
 * ============================================================ */

static Player* find_player_by_name(GameState* g, const char* name) {
  if (g == NULL || g->players == NULL || g->player_count <= 0) {
    return NULL;
  }

  Player* p = g->players;
  for (int i = 0; i < g->player_count; i++) {
    if (p->connected && strcmp(p->name, name) == 0) {
      return p;
    }
    p = p->next;
  }
  return NULL;
}

static uint8_t parse_color_or_default(const char* s, uint8_t fallback) {
  if (s == NULL) {
    return fallback;
  }

  if (strcmp(s, "red") == 0 || strcmp(s, "RED") == 0 || strcmp(s, "0") == 0) {
    return COLOR_RED;
  }
  if (strcmp(s, "blue") == 0 || strcmp(s, "BLUE") == 0 || strcmp(s, "1") == 0) {
    return COLOR_BLUE;
  }
  if (strcmp(s, "green") == 0 || strcmp(s, "GREEN") == 0 || strcmp(s, "2") == 0) {
    return COLOR_GREEN;
  }
  if (strcmp(s, "yellow") == 0 || strcmp(s, "YELLOW") == 0 || strcmp(s, "3") == 0) {
    return COLOR_YELLOW;
  }
  return fallback;
}

/* ============================================================
 *  Private Helpers: Outbound Messaging
 * ============================================================ */

void send_error_fd(int fd, const char* text) {
  if (fd < 0) {
    return;
  }
  (void)write_in_chunks(fd, "ERROR", text, NULL);
}

void broadcast_to_all(GameState* g, const char* type, const char* text) {
  if (g == NULL || g->players == NULL || g->player_count <= 0) {
    return;
  }

  Player* p = g->players;
  for (int i = 0; i < g->player_count; i++) {
    if (p->connected && p->sock_fd >= 0) {
      (void)write_in_chunks(p->sock_fd, type, text, NULL);
    }
    p = p->next;
  }
}

/* Broadcast an ACTION line to all connected players. */
static void send_action(GameState* g, const char* fmt, ...) {
  char out[OUT_MSG_SIZE];
  va_list ap;

  va_start(ap, fmt);
  (void)vsnprintf(out, sizeof(out), fmt, ap);
  va_end(ap);

  broadcast_to_all(g, "ACTION", out);
}

/* ============================================================
 *  Command Handlers
 * ============================================================ */

void handle_msg_start(GameState* g, Player* player, const read_data* msg) {
  (void)msg;
  if (player == NULL || !player->connected) {
    return;
  }
  if (g == NULL) {
    send_error_fd(player->sock_fd, "Invalid game state");
    return;
  }
  if (g->game_started) {
    send_error_fd(player->sock_fd, "Game already started");
    return;
  }
  if (g->player_count < 2) {
    send_error_fd(player->sock_fd, "Need at least 2 players to start");
    return;
  }

  game_start(g, g->players, g->player_count);
  broadcast_to_all(g, "INFO", "Game started");
}

void handle_msg_play(GameState* g, Player* player, const read_data* msg) {
  if (g == NULL || msg == NULL) {
    if (player != NULL) {
      send_error_fd(player->sock_fd, "Invalid request");
    }
    return;
  }
  if (player == NULL || !player->connected) {
    return;
  }
  if (!g->game_started || g->game_over) {
    send_error_fd(player->sock_fd, "Game is not active");
    return;
  }
  if (msg->num_chunks < 2) {
    send_error_fd(player->sock_fd, "play requires index");
    return;
  }

  int card_idx = atoi(msg->data[1]);
  uint8_t wild_color = COLOR_RED;
  if (msg->num_chunks >= 3) {
    wild_color = parse_color_or_default(msg->data[2], COLOR_RED);
  }

  if (!game_play_card(g, player->id, card_idx, wild_color)) {
    send_error_fd(player->sock_fd, "Invalid play");
    return;
  }

  send_action(g, "%s played card index %d", player->name, card_idx);
}

void handle_msg_draw(GameState* g, Player* player, const read_data* msg) {
  (void)msg;
  if (player == NULL || !player->connected) {
    return;
  }
  if (g == NULL) {
    send_error_fd(player->sock_fd, "Invalid game state");
    return;
  }
  if (!g->game_started || g->game_over) {
    send_error_fd(player->sock_fd, "Game is not active");
    return;
  }

  if (g->current_player_id != player->id) {
    send_error_fd(player->sock_fd, "Not your turn");
    return;
  }
  if (player->drawn_this_turn) {
    send_error_fd(player->sock_fd, "Already drew this turn");
    return;
  }

  (void)game_deal_cards(g, player->id, 1);

  send_action(g, "%s drew a card", player->name);
}

void handle_msg_pass(GameState* g, Player* player, const read_data* msg) {
  (void)msg;
  if (player == NULL || !player->connected) {
    return;
  }
  if (g == NULL) {
    send_error_fd(player->sock_fd, "Invalid game state");
    return;
  }
  if (!g->game_started || g->game_over) {
    send_error_fd(player->sock_fd, "Game is not active");
    return;
  }
  if (g->current_player_id != player->id) {
    send_error_fd(player->sock_fd, "Not your turn");
    return;
  }
  if (!player->drawn_this_turn && game_has_playable(g, player->id)) {
    send_error_fd(player->sock_fd, "Cannot pass before drawing when playable");
    return;
  }

  game_advance_turn(g);

  send_action(g, "%s passed", player->name);
}

void handle_msg_uno(GameState* g, Player* player, const read_data* msg) {
  (void)msg;
  if (player == NULL || !player->connected) {
    return;
  }
  if (g == NULL) {
    send_error_fd(player->sock_fd, "Invalid game state");
    return;
  }
  if (player->hand_count != 1) {
    send_error_fd(player->sock_fd, "UNO is only valid with one card");
    return;
  }

  player->called_uno = 1;

  send_action(g, "%s called UNO", player->name);
}

void handle_msg_callout(GameState* g, Player* player, const read_data* msg) {
  if (g == NULL || msg == NULL) {
    if (player != NULL) {
      send_error_fd(player->sock_fd, "Invalid request");
    }
    return;
  }
  if (player == NULL || !player->connected) {
    return;
  }
  if (msg->num_chunks < 2) {
    send_error_fd(player->sock_fd, "callout requires target name");
    return;
  }

  Player* target = find_player_by_name(g, msg->data[1]);
  if (target == NULL || !target->connected) {
    send_error_fd(player->sock_fd, "Target player not found");
    return;
  }
  if (target->hand_count != 1 || target->called_uno) {
    send_error_fd(player->sock_fd, "Callout invalid");
    return;
  }

  (void)game_deal_cards(g, target->id, 2);
  target->called_uno = 1;

  send_action(g, "%s called out %s", player->name, target->name);
}

void handle_msg_chat_send(GameState* g, Player* player, const read_data* msg) {
  if (g == NULL || msg == NULL || msg->num_chunks < 2) {
    if (player != NULL) {
      send_error_fd(player->sock_fd, "chat requires message text");
    }
    return;
  }

  if (player == NULL || !player->connected) {
    return;
  }

  char out[OUT_MSG_SIZE];
  snprintf(out, sizeof(out), "%s: %s", player->name, msg->data[1]);
  broadcast_to_all(g, "CHAT", out);
}
