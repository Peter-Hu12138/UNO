#ifndef CLIENT_OUTPUT_H
#define CLIENT_OUTPUT_H

#include "game_entities.h"

void print_help(void);

void print_event(const char* tag, const char* tag_color, const char* msg);

void print_card(Card c);

void print_color_dot(uint8_t color);

void print_status(const Player* p, const GameState* st);

#endif