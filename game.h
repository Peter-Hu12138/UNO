/*
 * game.h  --  UNO Game Logic
 *
 * Pure game state and rules.  No network I/O, no sockets.
 * All functions operate on the Game struct.
 */
#ifndef GAME_H
#define GAME_H

#include "protocol.h"

/* ═══════════════════════════════════════════════════════════
 *  Player
 * ═══════════════════════════════════════════════════════════ */

typedef struct {
    int       fd;                      /* socket fd (-1 if empty)     */
    char      name[MAX_NAME + 1];
    Card      hand[DECK_SIZE];
    int       hand_size;
    int       connected;
    int       said_uno;
    int       uno_vulnerable;
    int       has_drawn;
    uint8_t   buf[NET_BUF_SIZE];       /* per-player recv buffer      */
    int       buf_len;
} Player;

/* ═══════════════════════════════════════════════════════════
 *  Game State
 * ═══════════════════════════════════════════════════════════ */

typedef struct {
    Player  players[MAX_PLAYERS];
    int     num_connected;
    int     num_expected;
    int     started;
    int     over;

    /* Seat ring (join order) */
    int     seat_order[MAX_PLAYERS];
    int     seat_pos[MAX_PLAYERS];     /* pid → seat index            */
    int     seat_count;
    int     seat_idx;                  /* current turn's seat index   */

    /* Turn state */
    int     current;                   /* pid of active player        */
    int     direction;                 /* +1 CW, -1 CCW              */

    /* Table */
    Card    top_card;
    uint8_t top_color;                 /* effective color             */

    /* Draw pile */
    Card    draw_pile[DECK_SIZE];
    int     draw_top;
    int     draw_count;

    /* Discard pile */
    Card    discard[DECK_SIZE];
    int     discard_count;
} Game;

/* ═══════════════════════════════════════════════════════════
 *  Initialization
 * ═══════════════════════════════════════════════════════════ */

/* Zero out the entire Game struct, set defaults */
void game_init(Game *g, int num_expected);

/* ═══════════════════════════════════════════════════════════
 *  Seat Management
 * ═══════════════════════════════════════════════════════════ */

/* Register a player into the next available seat (call on JOIN) */
void game_seat_add(Game *g, int pid);

/* ═══════════════════════════════════════════════════════════
 *  Deck
 * ═══════════════════════════════════════════════════════════ */

/* Build + shuffle a standard 108-card UNO deck */
void game_build_deck(Game *g);

/* Draw one card (reshuffles discard if needed) */
Card game_draw_card(Game *g);

/* ═══════════════════════════════════════════════════════════
 *  Hand Manipulation
 * ═══════════════════════════════════════════════════════════ */

/* Remove and return the card at index in player's hand */
Card game_remove_card(Game *g, int pid, int idx);

/* Deal 'count' cards to player.  Fills drawn[] with dealt cards.
 * Returns actual number dealt. */
int  game_deal_cards(Game *g, int pid, int count, Card *drawn);

/* ═══════════════════════════════════════════════════════════
 *  Rules / Validation
 * ═══════════════════════════════════════════════════════════ */

/* Can this card be played on the current top card? */
int  game_can_play(const Game *g, Card c);

/* Does the player have at least one playable card? */
int  game_has_playable(const Game *g, int pid);

/* ═══════════════════════════════════════════════════════════
 *  Turn Management
 * ═══════════════════════════════════════════════════════════ */

/* Return the next connected player in the seat ring */
int  game_next_player(Game *g, int from_pid);

/* Advance turn to the next player; resets has_drawn */
void game_advance_turn(Game *g);

/* ═══════════════════════════════════════════════════════════
 *  Card Effect Results
 *
 *  handle_play validates + executes in server.c, but the
 *  effect of action cards (skip, reverse, draw2, wild4)
 *  needs to be known.  These helpers answer "what happens?"
 * ═══════════════════════════════════════════════════════════ */

typedef struct {
    int  skip;             /* 1 if next player should be skipped      */
    int  victim;           /* pid of victim (-1 if none)              */
    int  draw_penalty;     /* number of cards victim must draw        */
    int  reversed;         /* 1 if direction just flipped             */
} CardEffect;

/* Compute the effect of playing 'card'.  Does NOT mutate game.
 * Caller applies the effect afterward. */
CardEffect game_card_effect(Game *g, Card card);

/* Apply direction reversal */
void game_reverse(Game *g);

/* ═══════════════════════════════════════════════════════════
 *  Game Start Logic
 * ═══════════════════════════════════════════════════════════ */

/* Build deck, deal hands, flip first card, set first player.
 * Returns the first-card effect so server can notify. */
CardEffect game_start(Game *g);

#endif /* GAME_H */
