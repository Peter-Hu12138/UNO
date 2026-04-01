#ifndef SERVER_HANDLERS_H
#define SERVER_HANDLERS_H

#include "communication.h"
#include "game_entities.h"

void send_error_fd(int fd, const char* text);
void broadcast_to_all(GameState* g, const char* type, const char* text);

void handle_msg_join(GameState* g, Player* player, const read_data* msg);

void handle_msg_start(GameState* g, Player* player, const read_data* msg);
void handle_msg_play(GameState* g, Player* player, const read_data* msg);
void handle_msg_draw(GameState* g, Player* player, const read_data* msg);
void handle_msg_pass(GameState* g, Player* player, const read_data* msg);
void handle_msg_uno(GameState* g, Player* player, const read_data* msg);
void handle_msg_callout(GameState* g, Player* player, const read_data* msg);
void handle_msg_chat_send(GameState* g, Player* player, const read_data* msg);
void handle_msg_status(GameState* g, Player* player, const read_data* msg);

#endif