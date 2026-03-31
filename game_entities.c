/*
 * game_entities.c
 *
 * Pure game-state logic for UNO.
 */
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "game_entities.h"


 /* ═══════════════════════════════════════════════════════════
  *  private helpers
  * ═══════════════════════════════════════════════════════════ */
  /* Shuffle cards in-place with Fisher-Yates. */
static void shuffle_cards(Card* pile, int n) {
  for (int i = n - 1; i > 0; i--) {
    int j = rand() % (i + 1);
    Card temp = pile[i];
    pile[i] = pile[j];
    pile[j] = temp;
  }
}

/* Return the player node for pid, or NULL if not found. */
static Player* find_player(GameState* g, int pid) {
  if (g->players == NULL || g->player_count <= 0) {
    return NULL;
  }

  Player* p = g->players;
  for (int i = 0; i < g->player_count; i++) {
    if (p->id == pid) {
      return p;
    }
    p = p->next;
  }
  return NULL;
}

/* Count currently connected players. */
static int connected_count(GameState* g) {
  if (g->players == NULL || g->player_count <= 0) {
    return 0;
  }

  int count = 0;
  Player* p = g->players;
  for (int i = 0; i < g->player_count; i++) {
    if (p->connected) {
      count++;
    }
    p = p->next;
  }
  return count;
}

/* Move all discard cards except the top one back to draw pile and shuffle. */
static int reshuffle_discard_into_draw(GameState* g) {
  if (g->discard_top_idx <= 0) {
    return 0;
  }

  Card top_discard = g->discard_pile[g->discard_top_idx];
  int draw_count = g->discard_top_idx;

  for (int i = 0; i < draw_count; i++) {
    g->draw_pile[i] = g->discard_pile[i];
  }
  shuffle_cards(g->draw_pile, draw_count);

  g->draw_top_idx = draw_count - 1;
  g->discard_pile[0] = top_discard;
  g->discard_top_idx = 0;
  return 1;
}

/* Push one card onto discard pile. */
static void discard_push(GameState* g, Card c) {
  if (g->discard_top_idx < DECK_SIZE - 1) {
    g->discard_pile[++g->discard_top_idx] = c;
  }
  g->effect_applied = 0;
}

/* Draw one card from the shuffled deck area. */
static Card draw_one(GameState* g) {
  if (g->draw_top_idx < 0) {
    if (!reshuffle_discard_into_draw(g)) {
      Card fallback = { COLOR_WILD, CARD_WILD, COLOR_RED };
      return fallback;
    }
  }
  return g->draw_pile[g->draw_top_idx--];
}

/* ═══════════════════════════════════════════════════════════
 *  methods
 * ═══════════════════════════════════════════════════════════ */
 /* Initialize game defaults for a new match state. */
void game_init(GameState* g) {
  memset(g, 0, sizeof(*g));
  g->direction = 1;
  g->current_player_id = -1;
  g->draw_top_idx = -1;
  g->discard_top_idx = -1;
  g->effect_applied = 0;
}

/* Build and shuffle a standard UNO deck into draw_pile. */
void game_build_deck(GameState* g) {
  int idx = 0;

  for (int color = COLOR_RED; color <= COLOR_YELLOW; color++) {
    g->draw_pile[idx++] = (Card){ (uint8_t)color, CARD_0, (uint8_t)color };
    for (int value = CARD_1; value <= CARD_DRAW2; value++) {
      g->draw_pile[idx++] = (Card){ (uint8_t)color, (uint8_t)value, (uint8_t)color };
      g->draw_pile[idx++] = (Card){ (uint8_t)color, (uint8_t)value, (uint8_t)color };
    }
  }

  for (int i = 0; i < 4; i++) {
    g->draw_pile[idx++] = (Card){ COLOR_WILD, CARD_WILD, COLOR_RED };
    g->draw_pile[idx++] = (Card){ COLOR_WILD, CARD_WILD4, COLOR_RED };
  }

  shuffle_cards(g->draw_pile, idx);
  g->draw_top_idx = idx - 1;
}

/* Remove one card from pid hand by index and compact the array. */
Card game_remove_card(GameState* g, int pid, int idx) {
  Player* p = find_player(g, pid);
  if (p == NULL || idx < 0 || idx >= p->hand_count) {
    Card invalid = { COLOR_WILD, CARD_WILD, COLOR_RED };
    return invalid;
  }

  Card removed = p->hand[idx];
  for (int i = idx; i < p->hand_count - 1; i++) {
    p->hand[i] = p->hand[i + 1];
  }
  p->hand_count--;
  return removed;
}

/* Deal count cards to a player from the deck cursor. */
int game_deal_cards(GameState* g, int pid, int count) {
  Player* p = find_player(g, pid);
  if (p == NULL || count <= 0) {
    return 0;
  }

  int dealt = 0;
  for (int i = 0; i < count && p->hand_count < DECK_SIZE; i++) {
    p->hand[p->hand_count++] = draw_one(g);
    dealt++;
  }

  // drawing resets uno state
  p->called_uno = 0;
  p->drawn_this_turn = 1;
  return dealt;
}

/* Check if c is legal on the current top card. */
int game_can_play(const GameState* g, Card c) {
  if (g->discard_top_idx < 0) {
    return 1;
  }

  if (c.value == CARD_WILD || c.value == CARD_WILD4) {
    return 1;
  }

  Card top = g->discard_pile[g->discard_top_idx];
  uint8_t active_color = top.color;
  if ((top.value == CARD_WILD || top.value == CARD_WILD4) && top.wild_actual_color <= COLOR_YELLOW) {
    active_color = top.wild_actual_color;
  }

  if (c.color == active_color) {
    return 1;
  }
  if (c.value == top.value) {
    return 1;
  }
  return 0;
}

/* Return non-zero if pid has any playable card. */
int game_has_playable(const GameState* g, int pid) {
  if (g->players == NULL || g->player_count <= 0) {
    return 0;
  }

  const Player* p = g->players;
  for (int i = 0; i < g->player_count && p->id != pid; i++) {
    p = p->next;
  }
  if (p == NULL || p->id != pid) {
    return 0;
  }

  for (int i = 0; i < p->hand_count; i++) {
    if (game_can_play(g, p->hand[i])) {
      return 1;
    }
  }
  return 0;
}

/* let the player pid play a card in their hand */
int game_play_card(GameState* g, int pid, int card_idx, uint8_t wild_color) {
  if (g == NULL) {
    return 0;
  }

  Player* p = find_player(g, pid);
  if (p == NULL || !p->connected) {
    return 0;
  }

  /* enforce turn ownership */
  if (g->current_player_id != pid) {
    return 0;
  }

  if (card_idx < 0 || card_idx >= p->hand_count) {
    return 0;
  }

  Card chosen = p->hand[card_idx];
  if (!game_can_play(g, chosen)) {
    return 0;
  }

  /* caller should set wild_actual_color; clamp if missing */
  if (chosen.value == CARD_WILD || chosen.value == CARD_WILD4) {
    chosen.wild_actual_color = wild_color;
  }

  /* remove from hand and place on discard as the new top card */
  (void)game_remove_card(g, pid, card_idx);
  discard_push(g, chosen);   /* also resets g->effect_applied = 0 */

  /* UNO/game-over bookkeeping */
  if (p->hand_count == 1) {
    p->called_uno = 0;
  }
  else if (p->hand_count == 0) {
    g->game_over = 1;
    return 1;
  }

  p->drawn_this_turn = 0;

  /* action-card effects are checked/applied inside advance_turn */
  game_advance_turn(g);
  return 1;
}

/* remove offline players, move their hands back to draw pile */
void game_remove_disconnected_players(GameState* g) {
  if (g == NULL || g->players == NULL || g->player_count <= 0) {
    return;
  }

  int original_count = g->player_count;
  Player* p = g->players;

  for (int i = 0; i < original_count && g->player_count > 0; i++) {
    Player* next = p->next;

    if (!p->connected) {
      /* return this player's hand to draw pile */
      for (int h = 0; h < p->hand_count && g->draw_top_idx < DECK_SIZE - 1; h++) {
        g->draw_pile[++g->draw_top_idx] = p->hand[h];
      }
      p->hand_count = 0;

      /* unlink from circular list */
      if (g->player_count == 1) {
        g->players = NULL;
        g->player_count = 0;
        if (g->current_player_id == p->id) {
          g->current_player_id = -1;
        }
        free(p);
        break;
      }
      else {
        p->prev->next = p->next;
        p->next->prev = p->prev;

        if (g->players == p) {
          g->players = p->next;
        }
        if (g->current_player_id == p->id) {
          g->current_player_id = p->next->id;
        }

        g->player_count--;
        free(p);
      }
    }

    p = next;
  }

  /* keep draw pile random after adding returned cards */
  if (g->draw_top_idx > 0) {
    shuffle_cards(g->draw_pile, g->draw_top_idx + 1);
  }

  /* if current player became invalid, pick first connected player */
  if (g->player_count == 0 || g->players == NULL) {
    g->current_player_id = -1;
    g->game_over = 1;
    return;
  }

  Player* cur = find_player(g, g->current_player_id);
  if (cur == NULL || !cur->connected) {
    Player* start = g->players;
    Player* t = start;
    int found = 0;

    for (int i = 0; i < g->player_count; i++) {
      if (t->connected) {
        g->current_player_id = t->id;
        found = 1;
        break;
      }
      t = t->next;
    }

    if (!found) {
      g->current_player_id = -1;
      g->game_over = 1;
    }
  }
}

/*
 * Advance to the next turn.
 * This function also evaluates and applies the current top-card effect once:
 * - Reverse flips direction (and skips in 2-player games)
 * - Skip jumps over one player
 * - Draw2/Wild4 forces next player to draw, then skips them
 */
void game_advance_turn(GameState* g) {
  Player* current = find_player(g, g->current_player_id);

  int steps = 1;
  // apply effect when there is a discarded card and 
  // effect is not applied this turn
  if (g->discard_top_idx >= 0 && !g->effect_applied) {
    Card top = g->discard_pile[g->discard_top_idx];

    if (top.value == CARD_REVERSE) {
      g->direction = -g->direction;

      // for 2 players
      if (connected_count(g) == 2) {
        steps = 2;
      }
    }
    else if (top.value == CARD_SKIP) {
      steps = 2;
    }
    else if (top.value == CARD_DRAW2 || top.value == CARD_WILD4) {
      Player* victim = current->next;
      int penalty = (top.value == CARD_DRAW2) ? 2 : 4;
      if (victim != NULL) {
        game_deal_cards(g, victim->id, penalty);
      }
      steps = 2;
    }

    g->effect_applied = 1;
  }

  Player* next = current;
  for (int i = 0; i < steps; i++) {
    next = next->next;
  }

  if (next != NULL) {
    g->current_player_id = next->id;
    next->drawn_this_turn = 0;
  }
}

/* Build deck, deal initial hands, reveal top card, and pick first turn. */
void game_start(GameState* g, Player* players, int player_cnt) {
  g->game_started = 1;
  g->game_over = 0;

  game_build_deck(g);

  g->players = players;
  g->player_count = player_cnt;

  if (g->players == NULL || g->player_count <= 0) {
    g->current_player_id = -1;
    return;
  }

  Player* first_player = g->players;
  int found_first = 0;
  for (int i = 0; i < g->player_count; i++) {
    if (first_player->connected) {
      found_first = 1;
      break;
    }
    first_player = first_player->next;
  }
  if (!found_first) {
    g->current_player_id = -1;
    g->game_over = 1;
    return;
  }

  g->current_player_id = first_player->id;

  Player* p = g->players;
  for (int n = 0; n < g->player_count; n++) {
    if (p->connected) {
      p->hand_count = 0;
      p->called_uno = 0;
      p->drawn_this_turn = 0;
      for (int i = 0; i < INITIAL_HAND; i++) {
        p->hand[p->hand_count++] = draw_one(g);
      }
    }
    p = p->next;
  }

  Card first;
  do {
    first = draw_one(g);
  } while (first.value == CARD_WILD4);

  discard_push(g, first);

  if (first.value == CARD_REVERSE) {
    g->direction = -g->direction;
    if (connected_count(g) == 2) {
      g->current_player_id = first_player->next->id;
    }
  }
  else if (first.value == CARD_SKIP) {
    g->current_player_id = first_player->next->id;
  }
  else if (first.value == CARD_DRAW2 || first.value == CARD_WILD4) {
    (void)game_deal_cards(g, first_player->id, (first.value == CARD_DRAW2) ? 2 : 4);
    g->current_player_id = first_player->next->id;
  }

  g->effect_applied = 1;
}