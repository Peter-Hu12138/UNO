#ifndef GAME_ENTITIES_H
#define GAME_ENTITIES_H

#include <stdint.h>

/* ═══════════════════════════════════════════════════════════
 *  Constants
 * ═══════════════════════════════════════════════════════════ */
#define INITIAL_HAND    2
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

/*═══════════════════════════════════════════════════════════
*  Game methods
* ═══════════════════════════════════════════════════════════ */

/**
 * @brief Initialize a game state with default values.
 * @param g
 */
void game_init(GameState* g);
/**
 * @brief Find a player node by player id.
 * @param g
 * @param pid.
 * @return
 */
Player* game_find_player(GameState* g, int pid);
/**
 * @brief Free every player node
 * @param g
 */
void game_free_all_players(GameState* g);
/**
 * @brief Append a player to the circular doubly linked player list.
 * @param g
 * @param p new Player node to append.
 */
void game_append_player(GameState* g, Player* p);
/**
 * @brief Build and shuffle a standard UNO deck into the draw pile.
 * @param g
 *
 */
void game_build_deck(GameState* g);
/**
 * @brief Remove a card from a player's hand by index
 * @param g
 * @param pid
 * @param idx Zero-based card index in the player's hand.
 * @return Removed card, or a fallback invalid card when inputs are invalid.
 *
 */
Card game_remove_card(GameState* g, int pid, int idx);
/**
 * @brief Deal cards from draw pile to a player's hand.
 * @param g Game state containing draw pile and players.
 * @param pid
 * @param count Requested number of cards.
 * @return Number of cards actually dealt.
 *
 */
int game_deal_cards(GameState* g, int pid, int count);
/**
 * @brief Check whether a card can be legally played.
 * @param g
 * @param c Candidate card.
 * @return 1 when legal to play, otherwise 0.
 */
int game_can_play(const GameState* g, Card c);
/**
 * @brief TEMP UNUSED Determine whether a player has at least one playable card.
 */
int game_has_playable(const GameState* g, int pid);
/**
 * @brief Play a selected hand card for the current player.
 * @param g
 * @param pid
 * @param card_idx Index of the card in player's hand.
 * @param wild_color Chosen active color for wild cards.
 * @return 1 on successful play, otherwise 0.
 *
 */
int game_play_card(GameState* g, int pid, int card_idx, uint8_t wild_color);
/**
 * @brief Remove disconnected players and recycle their cards into draw pile.
 * @param g Current game state.
 *
 */
void game_remove_disconnected_players(GameState* g);
/**
 * @brief Advance to the next turn.
 *
 * This function also evaluates and applies the current top-card effect once:
 * - Reverse flips direction (and skips in 2-player games)
 * - Skip jumps over one player
 * - Draw2/Wild4 forces next player to draw, then skips them
 *
 * @param g
 */
void game_advance_turn(GameState* g);
/**
 * @brief Start a game by initializing deck, hands, discard, and first turn.
 * @param g Game state to start.
 * @param players Head of the circular player list.
 * @param player_cnt Number of players in the list.
 */
void game_start(GameState* g, Player* players, int player_cnt);

#endif
