#ifndef CLIENT_OUTPUT_H
#define CLIENT_OUTPUT_H

#define RESET       "\033[0m"
#define BOLD        "\033[1m"
#define DIM         "\033[2m"

 /* Card background colors */
#define BG_RED      "\033[41m\033[97m"   /* white text on red   */
#define BG_BLUE     "\033[44m\033[97m"   /* white text on blue  */
#define BG_GREEN    "\033[42m\033[97m"   /* white text on green */
#define BG_YELLOW   "\033[43m\033[30m"   /* black text on yellow*/
#define BG_WILD     "\033[45m\033[97m"   /* white text on magenta */

/* Text colors */
#define FG_RED      "\033[91m"
#define FG_BLUE     "\033[94m"
#define FG_GREEN    "\033[92m"
#define FG_YELLOW   "\033[93m"
#define FG_MAGENTA  "\033[95m"
#define FG_CYAN     "\033[96m"
#define FG_WHITE    "\033[97m"
#define FG_GRAY     "\033[90m"

#include "game_entities.h"

void print_help(void);

void print_event(const char* tag, const char* tag_color, const char* msg);

void print_card(Card c);

void print_color_dot(uint8_t color);

void print_status(const Player* p, const GameState* st);

#endif