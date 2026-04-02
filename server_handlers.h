#ifndef SERVER_HANDLERS_H
#define SERVER_HANDLERS_H

#include "communication.h"
#include "game_entities.h"

void send_error_fd(Player* player, const char* text);
void broadcast_to_all(GameState* g, const char* type, const char* text);

/**
 * @brief Handle JOIN command from a client.
 */
void handle_msg_join(GameState* g, Player* player, const read_data* msg);

/**
 * @brief Handle START command and begin a new game.
 */
void handle_msg_start(GameState* g, Player* player, const read_data* msg);
/**
 * @brief Handle PLAY command for a player's selected card.
 */
void handle_msg_play(GameState* g, Player* player, const read_data* msg);
/**
 * @brief Handle DRAW command for the current player.
 */
void handle_msg_draw(GameState* g, Player* player, const read_data* msg);
/**
 * @brief Handle PASS command and end the current turn.
 */
void handle_msg_pass(GameState* g, Player* player, const read_data* msg);
/**
 * @brief Handle UNO command for a player at one card.
 */
void handle_msg_uno(GameState* g, Player* player, const read_data* msg);
/**
 * @brief Handle CALLOUT command against another player.
 */
void handle_msg_callout(GameState* g, Player* player, const read_data* msg);
/**
 * @brief Handle CHAT command and broadcast message text.
 */
void handle_msg_chat_send(GameState* g, Player* player, const read_data* msg);
/**
 * @brief Handle STATUS command and send game state updates.
 */
void handle_msg_status(GameState* g, Player* player, const read_data* msg);

#endif