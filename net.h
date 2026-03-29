/*
 * net.h  --  Server-side Network / Messaging Layer
 *
 * Wraps protocol.h's send_msg into higher-level broadcast
 * and state-serialization functions.  Depends on game.h
 * for the Game struct, but does NOT mutate game state.
 */
#ifndef NET_H
#define NET_H

#include "protocol.h"
#include "game.h"

/* ═══════════════════════════════════════════════════════════
 *  Broadcast Helpers
 * ═══════════════════════════════════════════════════════════ */

/* Send a raw message to all connected players */
void net_broadcast(Game *g, uint8_t type, const void *p, uint16_t len);

/* Send a raw message to all connected players except 'ex' */
void net_broadcast_except(Game *g, int ex, uint8_t type,
                          const void *p, uint16_t len);

/* ═══════════════════════════════════════════════════════════
 *  Notification Helpers
 * ═══════════════════════════════════════════════════════════ */

/* Send a MSG_NOTIFY text to one player */
void net_notify_one(Game *g, int pid, const char *msg);

/* Send a MSG_NOTIFY text to all players */
void net_notify_all(Game *g, const char *msg);

/* Send a MSG_ERROR text to one player */
void net_send_error(Game *g, int pid, const char *msg);

/* ═══════════════════════════════════════════════════════════
 *  State Serialization
 * ═══════════════════════════════════════════════════════════ */

/* Send the player's hand (MSG_HAND) */
void net_send_hand(Game *g, int pid);

/* Send the full game state snapshot (MSG_STATE) to one player */
void net_send_state(Game *g, int pid);

/* Send STATE + HAND to all connected players */
void net_broadcast_state(Game *g);

/* ═══════════════════════════════════════════════════════════
 *  High-Level Game Events
 * ═══════════════════════════════════════════════════════════ */

/* Broadcast a MSG_PLAYED event */
void net_broadcast_played(Game *g, int pid, Card card);

/* Broadcast MSG_DRAW_RESULT to the player, MSG_DREW to others.
 * 'drawn' is the array of dealt cards, 'count' is how many. */
void net_broadcast_drew(Game *g, int pid, const Card *drawn, int count);

/* Broadcast MSG_TURN to all */
void net_broadcast_turn(Game *g);

/* Broadcast MSG_GAME_OVER to all */
void net_broadcast_game_over(Game *g, int winner_pid);

/* Broadcast MSG_GAME_BEGIN to all */
void net_broadcast_game_begin(Game *g);

/* Send MSG_PLAYER_JOIN for 'pid' to all except 'pid' */
void net_broadcast_player_join(Game *g, int pid);

/* Send existing players to a newly joined player */
void net_send_existing_players(Game *g, int new_pid);

/* Send MSG_WELCOME to a player */
void net_send_welcome(Game *g, int pid);

/* Broadcast MSG_PLAYER_LEFT */
void net_broadcast_player_left(Game *g, int pid);

/* Broadcast MSG_CHAT_RECV */
void net_broadcast_chat(Game *g, int sender, uint8_t *text, uint16_t len);

#endif /* NET_H */
