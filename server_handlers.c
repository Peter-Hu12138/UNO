#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "server_handlers.h"

#define OUT_MSG_SIZE 512

/* ============================================================
 *  Private Helpers: Player Lookup / Parsing
 * ============================================================ */

 /**
  * @brief Find a *connected* player by their name.
  *
  * @param g
  * @param name
  * @return Player*
  */
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

/**
 * @brief Parse a color string or return a default value.
 *
 * @param s
 * @param fallback
 * @return uint8_t
 */
static uint8_t parse_color_or_default(const char* s, uint8_t fallback) {
  if (s == NULL) {
    return fallback;
  }

  if (strcmp(s, "red") == 0 || strcmp(s, "r") == 0 || strcmp(s, "0") == 0) {
    return COLOR_RED;
  }
  if (strcmp(s, "blue") == 0 || strcmp(s, "b") == 0 || strcmp(s, "1") == 0) {
    return COLOR_BLUE;
  }
  if (strcmp(s, "green") == 0 || strcmp(s, "g") == 0 || strcmp(s, "2") == 0) {
    return COLOR_GREEN;
  }
  if (strcmp(s, "yellow") == 0 || strcmp(s, "y") == 0 || strcmp(s, "3") == 0) {
    return COLOR_YELLOW;
  }
  return fallback;
}

/**
 * @brief Convert an array of cards to a string representation.
 *
 * string must be in the form "color,value,wild_actual_color;...;..."
 * with each of color, value, wild_actual_color be integers defined in
 * the enum found in game_entities.h
 *
 * @param cards
 * @param count
 * @return char*
 *
 * Source - revised from lines from GPT-5.3-Codex
 * Retrieved 2026-04-01
 */
static char* cards_to_string(const Card* cards, int count) {
  if (cards == NULL || count <= 0) {
    char* out = (char*)malloc(1);
    if (out != NULL) {
      out[0] = '\0';
    }
    return out;
  }

  size_t cap = (size_t)count * 16 + 1;
  char* out = (char*)malloc(cap);
  if (out == NULL) {
    return NULL;
  }

  out[0] = '\0';
  size_t used = 0;
  for (int i = 0; i < count; i++) {
    char one[32];
    (void)snprintf(one, sizeof(one), "%d,%d,%d",
      (int)cards[i].color,
      (int)cards[i].value,
      (int)cards[i].wild_actual_color);

    size_t one_len = strlen(one);
    if (used + one_len + 2 > cap) {
      free(out);
      return NULL;
    }

    if (i > 0) {
      out[used++] = ';';
      out[used] = '\0';
    }

    memcpy(out + used, one, one_len + 1);
    used += one_len;
  }

  return out;
}

/* ============================================================
 *  Private Helpers: Outbound Messaging
 * ============================================================ */

 /**
  * @brief Send an error message to a client with fd.
  *
  * @param fd
  * @param text
  */
void send_error_fd(Player* p, const char* text) {
  if (p == NULL || p->sock_fd < 0) {
    return;
  }
  int err = write_in_chunks(p->sock_fd, "ERROR", text, NULL);
  if (err) {
    p->connected = 0;
  }
}

/**
 * @brief Send a message to all clients with type as the tag
 *
 * @param g
 * @param type
 * @param text
 */
void broadcast_to_all(GameState* g, const char* type, const char* text) {
  if (g == NULL || g->players == NULL || g->player_count <= 0) {
    return;
  }

  Player* p = g->players;
  for (int i = 0; i < g->player_count; i++) {
    if (p->connected && p->sock_fd >= 0) {
      int err = write_in_chunks(p->sock_fd, type, text, NULL);
      if (err) {
        p->connected = 0;
      }
    }
    p = p->next;
  }
}

/**
 * @brief Broadcast an ACTION line to all connected players.
 *
 * @param g
 * @param fmt
 * @param ... same as vnsprintf
 *
 * Source - revised from lines from GPT-5.3-Codex
 * Retrieved 2026-04-01
 */
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

 /**
  * @brief Handle the JOIN message from a client.
  * reject if name already exists
  */
void handle_msg_join(GameState* g, Player* player, const read_data* msg) {
  if (msg->num_chunks < 2) {
    return;
  }
  if (find_player_by_name(g, msg->data[1]) != NULL) {
    send_error_fd(player, "Name already exists");
    player->connected = 0;
    return;
  }
  strncpy(player->name, msg->data[1], MAX_NAME);
  player->name[MAX_NAME] = '\0';
  char out[OUT_MSG_SIZE];
  snprintf(out, sizeof(out), "Player %s joined successfully", player->name);
  broadcast_to_all(g, "INFO", out);
}

/**
 * @brief Handle START command and begin the game when preconditions are met.
 *
 * Source - revised from lines from GPT-5.3-Codex
 * Retrieved 2026-04-01
 */
void handle_msg_start(GameState* g, Player* player, const read_data* msg) {
  (void)msg;
  if (player == NULL || !player->connected) {
    return;
  }
  if (g == NULL) {
    send_error_fd(player, "Invalid game state");
    return;
  }
  if (g->game_started) {
    send_error_fd(player, "Game already started");
    return;
  }
  if (g->player_count < 2) {
    send_error_fd(player, "Need at least 2 players to start");
    return;
  }

  game_start(g, g->players, g->player_count);
  broadcast_to_all(g, "ACTION", "Game started");
}

/**
 * @brief Handle PLAY command for the active player.
 *
 * Source - revised from lines from GPT-5.3-Codex
 * Retrieved 2026-04-01
 */
void handle_msg_play(GameState* g, Player* player, const read_data* msg) {
  if (g == NULL || msg == NULL) {
    if (player != NULL) {
      send_error_fd(player, "Invalid request");
    }
    return;
  }
  if (player == NULL || !player->connected) {
    return;
  }
  if (!g->game_started || g->game_over) {
    send_error_fd(player, "Game is not active");
    return;
  }

  if (g->current_player_id != player->id) {
    send_error_fd(player, "Not your turn");
    return;
  }
  if (msg->num_chunks < 2) {
    send_error_fd(player, "play requires index");
    return;
  }

  int card_idx = atoi(msg->data[1]);
  uint8_t wild_color = COLOR_RED;
  if (msg->num_chunks >= 3) {
    wild_color = parse_color_or_default(msg->data[2], COLOR_RED);
  }

  // fix reading invalid index undefined bahaviour
  if (card_idx < 0 || card_idx >= player->hand_count) {
    send_error_fd(player, "Invalid card index");
    return;
  }

  Card card = player->hand[card_idx];
  char* color_str;
  char* val_str;

  switch (card.color) {
  case COLOR_RED:    color_str = "Red";break;
  case COLOR_BLUE:   color_str = "Blue";break;
  case COLOR_GREEN:  color_str = "Green";break;
  case COLOR_YELLOW: color_str = "Yellow";break;
  case COLOR_WILD:   color_str = "Wild";break;
  default:           color_str = "???";
  }
  char* t[] = {
    "0","1","2","3","4","5","6","7","8","9",
    "Skip","Reverse","+2","Wild","Wild+4"
  };
  val_str = t[card.value];

  if (!game_play_card(g, player->id, card_idx, wild_color)) {
    send_error_fd(player, "Invalid play, cannot play this card");
    return;
  }

  send_action(g, "%s played card %s %s, %s", player->name, color_str, val_str, g->game_over ? "that was the last card!" : "");
}

/**
 * @brief Handle DRAW command for the active player.
 *
 * Source - revised from lines from GPT-5.3-Codex
 * Retrieved 2026-04-01
 */
void handle_msg_draw(GameState* g, Player* player, const read_data* msg) {
  (void)msg;
  if (player == NULL || !player->connected) {
    return;
  }
  if (g == NULL) {
    send_error_fd(player, "Invalid game state");
    return;
  }
  if (!g->game_started || g->game_over) {
    send_error_fd(player, "Game is not active");
    return;
  }

  if (g->current_player_id != player->id) {
    send_error_fd(player, "Not your turn");
    return;
  }
  if (player->drawn_this_turn) {
    send_error_fd(player, "Already drew this turn");
    return;
  }

  (void)game_deal_cards(g, player->id, 1);

  send_action(g, "%s drew a card", player->name);
}

/**
 * @brief Handle PASS command and advance to the next turn.
 *
 * Source - revised from lines from GPT-5.3-Codex
 * Retrieved 2026-04-01
 */
void handle_msg_pass(GameState* g, Player* player, const read_data* msg) {
  (void)msg;
  if (player == NULL || !player->connected) {
    return;
  }
  if (g == NULL) {
    send_error_fd(player, "Invalid game state");
    return;
  }
  if (!g->game_started || g->game_over) {
    send_error_fd(player, "Game is not active");
    return;
  }
  if (g->current_player_id != player->id) {
    send_error_fd(player, "Not your turn");
    return;
  }
  if (!player->drawn_this_turn) {
    send_error_fd(player, "Cannot pass before drawing when playable");
    return;
  }

  game_advance_turn(g);

  send_action(g, "%s passed", player->name);
}

/**
 * @brief Handle UNO command for players with exactly one card.
 *
 * Source - revised from lines from GPT-5.3-Codex
 * Retrieved 2026-04-01
 */
void handle_msg_uno(GameState* g, Player* player, const read_data* msg) {
  (void)msg;
  if (player == NULL || !player->connected) {
    return;
  }
  if (g == NULL) {
    send_error_fd(player, "Invalid game state");
    return;
  }
  if (player->hand_count != 1) {
    send_error_fd(player, "UNO is only valid with one card");
    return;
  }

  player->called_uno = 1;

  send_action(g, "%s called UNO", player->name);
}

/**
 * @brief Handle CALLOUT command and apply UNO penalty when valid.
 *
 * Source - revised from lines from GPT-5.3-Codex
 * Retrieved 2026-04-01
 */
void handle_msg_callout(GameState* g, Player* player, const read_data* msg) {
  if (g == NULL || msg == NULL) {
    if (player != NULL) {
      send_error_fd(player, "Invalid request");
    }
    return;
  }
  if (player == NULL || !player->connected) {
    return;
  }
  if (msg->num_chunks < 2) {
    send_error_fd(player, "callout requires target name");
    return;
  }

  Player* target = find_player_by_name(g, msg->data[1]);
  if (target == NULL || !target->connected) {
    send_error_fd(player, "Target player not found");
    return;
  }
  if (target->hand_count != 1) {
    send_error_fd(player, "Callout invalid: target not at 1 card");
    return;
  }
  if (target->called_uno) {
    send_error_fd(player, "Callout invalid: target called UNO");
    return;
  }

  (void)game_deal_cards(g, target->id, 2);
  target->called_uno = 1;

  send_action(g, "%s called out %s, +2 cards", player->name, target->name);
}

/**
 * @brief Handle CHAT command and broadcast a player message.
 *
 * Source - revised from lines from GPT-5.3-Codex
 * Retrieved 2026-04-01
 */
void handle_msg_chat_send(GameState* g, Player* player, const read_data* msg) {
  if (g == NULL || msg == NULL || msg->num_chunks < 2) {
    if (player != NULL) {
      send_error_fd(player, "chat requires message text");
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

/**
 * @brief Handle STATUS command and send serialized game/player state.
 *
 * Source - revised from lines from GPT-5.3-Codex
 * Retrieved 2026-04-01
 */
void handle_msg_status(GameState* g, Player* player, const read_data* msg) {
  (void)msg;
  if (g == NULL || player == NULL || !player->connected || player->sock_fd < 0) {
    return;
  }


  // order of the data are defined in this order
  // int game_started;
  // int game_over;
  // int current_player_id;
  // int direction;
  // int draw_top_idx;
  // int discard_top_idx;
  // Card draw_pile[DECK_SIZE];
  // Card discard_pile[DECK_SIZE];
  // int player_count;
  // int effect_applied;

  char game_started[16];
  char game_over[16];
  char current_player_id[16];
  char direction[16];
  char draw_top_idx[16];
  char discard_top_idx[16];
  char player_count[16];
  char effect_applied[16];

  (void)snprintf(game_started, sizeof(game_started), "%d", g->game_started);
  (void)snprintf(game_over, sizeof(game_over), "%d", g->game_over);
  (void)snprintf(current_player_id, sizeof(current_player_id), "%d", g->current_player_id);
  (void)snprintf(direction, sizeof(direction), "%d", g->direction);
  (void)snprintf(draw_top_idx, sizeof(draw_top_idx), "%d", g->draw_top_idx);
  (void)snprintf(discard_top_idx, sizeof(discard_top_idx), "%d", g->discard_top_idx);
  (void)snprintf(player_count, sizeof(player_count), "%d", g->player_count);
  (void)snprintf(effect_applied, sizeof(effect_applied), "%d", g->effect_applied);

  char* draw_cards = cards_to_string(g->draw_pile, g->draw_top_idx + 1);
  char* discard_cards = cards_to_string(g->discard_pile, g->discard_top_idx + 1);

  if (draw_cards == NULL || discard_cards == NULL) {
    send_error_fd(player, "Failed to build state payload");
    goto cleanup_state_strings;
  }

  (void)write_in_chunks(
    player->sock_fd,
    "STATE_UPDATE",
    "GAME",
    game_started,
    game_over,
    current_player_id,
    direction,
    draw_top_idx,
    discard_top_idx,
    draw_cards,
    discard_cards,
    player_count,
    effect_applied,
    NULL
  );

  Player* p = g->players;
  for (int i = 0; i < g->player_count && p != NULL; i++) {
    char pid[16];
    char hand_count[16];

    (void)snprintf(pid, sizeof(pid), "%d", p->id);
    (void)snprintf(hand_count, sizeof(hand_count), "%d", p->hand_count);

    char* hand_cards = cards_to_string(p->hand, p->hand_count);
    if (hand_cards == NULL) {
      send_error_fd(player, "Failed to build player payload");
      break;
    }

    // players only get id, name, hand cards, hand count
    (void)write_in_chunks(
      player->sock_fd,
      "STATE_UPDATE",
      "PLAYER",
      pid,
      p->name,
      hand_cards,
      hand_count,
      NULL
    );

    free(hand_cards);
    p = p->next;
  }

cleanup_state_strings:
  free(draw_cards);
  free(discard_cards);
}