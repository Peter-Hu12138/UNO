#ifndef GAME_ENTITIES_H
#define GAME_ENTITIES_H

#include <stdint.h>

/* ═══════════════════════════════════════════════════════════
 *  Constants
 * ═══════════════════════════════════════════════════════════ */
#define INITIAL_HAND    7
#define DECK_SIZE       108
#define MAX_NAME        20
#define MAX_PLAYERS     8

typedef enum {
  COLOR_RED,
  COLOR_BLUE,
  COLOR_GREEN,
  COLOR_YELLOW,
  COLOR_WILD
} CardColor;
typedef enum {
  CARD_0,
  CARD_1,
  CARD_2,
  CARD_3,
  CARD_4,
  CARD_5,
  CARD_6,
  CARD_7,
  CARD_8,
  CARD_9,
  CARD_SKIP,
  CARD_REVERSE,
  CARD_DRAW2,
  CARD_WILD,
  CARD_WILD4,
} CardValue;
typedef struct {
  CardColor color;
  CardValue value;
  CardColor wild_actual_color;
} Card;

struct player {
  int sock_fd;
  int id;
  int connected;
  char name[MAX_NAME + 1];

  Card hand[DECK_SIZE];
  int hand_count;

  int called_uno;
  int drawn_this_turn;

  struct player* next;
  struct player* prev;
};
typedef struct player Player;

typedef struct {
  int game_started;
  int game_over;

  int current_player_id;
  int direction;          /* 1 = CW, -1 = CCW */

  Card draw_pile[DECK_SIZE];
  int draw_top_idx;

  Card discard_pile[DECK_SIZE];
  int discard_top_idx;

  Player* players; // circularly linked
  int player_count;

  int effect_applied; // prevent effect on pass
} GameState;

/* ═══════════════════════════════════════════════════════════
 *  Game methods
 * ═══════════════════════════════════════════════════════════ */
 /* Zero out the entire Game struct, set defaults */
void game_init(GameState* g);

/* find a player by id */
Player* game_find_player(GameState* g, int pid);

/* Build + shuffle a standard 108-card UNO deck */
void game_build_deck(GameState* g);

/* Remove and return the card at index in player's hand */
Card game_remove_card(GameState* g, int pid, int idx);

/* Deal 'count' cards to player. */
int game_deal_cards(GameState* g, int pid, int count);

/* Can this card be played on the current top card? */
int game_can_play(const GameState* g, Card c);

/* Does the player have at least one playable card? */
int game_has_playable(const GameState* g, int pid);

/* let the player pid play a card */
int game_play_card(GameState* g, int pid, int card_idx, uint8_t wild_color);

/* remove offline players, move its hand to draw pile*/
void game_remove_disconnected_players();

/* Advance turn to the next player; resets has_drawn */
void game_advance_turn(GameState* g);

/* Build deck, deal hands, flip first card, set first player.
 * Returns the first-card effect so server can notify. */
void game_start(GameState* g, Player* players, int player_cnt);

#endif
