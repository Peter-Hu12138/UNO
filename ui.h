/*
 * ui.h  --  Terminal UI Rendering for UNO Client
 *
 * Pure output layer: all ANSI coloring, card rendering,
 * game status display, help text, and banner.
 * No network I/O, no state mutation.
 */
#ifndef UI_H
#define UI_H

#include <stdint.h>
#include "protocol.h"

/* ═══════════════════════════════════════════════════════════
 *  ANSI Color Codes  (usable by other modules if needed)
 * ═══════════════════════════════════════════════════════════ */

#define RESET       "\033[0m"
#define BOLD        "\033[1m"
#define DIM         "\033[2m"

#define BG_RED      "\033[41m\033[97m"
#define BG_BLUE     "\033[44m\033[97m"
#define BG_GREEN    "\033[42m\033[97m"
#define BG_YELLOW   "\033[43m\033[30m"
#define BG_WILD     "\033[45m\033[97m"

#define FG_RED      "\033[91m"
#define FG_BLUE     "\033[94m"
#define FG_GREEN    "\033[92m"
#define FG_YELLOW   "\033[93m"
#define FG_MAGENTA  "\033[95m"
#define FG_CYAN     "\033[96m"
#define FG_WHITE    "\033[97m"
#define FG_GRAY     "\033[90m"

/* ═══════════════════════════════════════════════════════════
 *  Types shared between UI and client logic
 * ═══════════════════════════════════════════════════════════ */

typedef struct {
    int  id;
    int  present;
    int  connected;
    char name[MAX_NAME + 1];
    int  hand_count;
    int  said_uno;
} PlayerInfo;

typedef struct {
    int my_id;
    int connected;
    int game_started;
    int game_over;

    int current_player;
    int direction;             /* 1 = CW, -1 = CCW */
    Card top_card;
    uint8_t effective_color;

    Card hand[DECK_SIZE];
    int hand_size;

    PlayerInfo players[MAX_PLAYERS];

    int display_order[MAX_PLAYERS];
    int display_count;
} GameView;

/* ═══════════════════════════════════════════════════════════
 *  Card Rendering
 * ═══════════════════════════════════════════════════════════ */

/* Return ANSI background escape for a card color */
const char *ui_card_bg(uint8_t color);

/* Print a single colored card inline (e.g.  Red  5 ) */
void ui_print_card(Card c);

/* Print the effective-color indicator */
void ui_print_color_dot(uint8_t color);

/* Write a card description to a plain-text buffer (no ANSI) */
void ui_card_to_str(Card c, char *buf, int n);

/* ═══════════════════════════════════════════════════════════
 *  Event / Status Output
 * ═══════════════════════════════════════════════════════════ */

/* Print a tagged event line:  [Tag] message */
void ui_event(const char *tag, const char *tag_color, const char *msg);

/* Print the input prompt  > */
void ui_prompt(void);

/* Print the startup ASCII banner */
void ui_banner(void);

/* Print the help / command reference */
void ui_help(void);

/* ═══════════════════════════════════════════════════════════
 *  Full Game-State Render
 * ═══════════════════════════════════════════════════════════ */

/* Render the complete game status panel (top card, players, hand) */
void ui_render(const GameView *view);

/* ═══════════════════════════════════════════════════════════
 *  Inline Event Helpers  (for specific game events)
 * ═══════════════════════════════════════════════════════════ */

/* "[Play] Alice played  Red 5 " — includes colored card */
void ui_event_played(const char *who, Card c, uint8_t eff_color);

/* "[Draw] You drew:  Blue 3   Green Skip  (playable!)" */
void ui_event_draw_result(const Card *cards, int count, int playable);

/* "[Game Over] YOU WIN!" banner */
void ui_event_win(void);

#endif /* UI_H */
