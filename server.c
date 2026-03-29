/*
 * server.c  --  UNO Game Server  (main loop / glue)
 *
 * Usage:  ./server [port] [num_players]
 *
 * Architecture:
 *   game.c  — pure game logic   (deck, hands, turns, rules)
 *   net.c   — message layer     (broadcast, serialize state)
 *   server.c — event loop:  accept + select + dispatch
 *
 * This file contains:
 *   1. Socket setup
 *   2. Action handlers    (validate → mutate game → call net_*)
 *   3. Message dispatch   (MSG type → handler)
 *   4. Connection/disconnect management
 *   5. Main select() loop
 */

 /*
 * server.c -- UNO server main control file
 *
 * Responsibilities:
 * 1. Create and manage the listening socket
 * 2. Accept incoming client connections
 * 3. Use select() to handle multiple clients concurrently
 * 4. Parse client messages and dispatch them to handlers
 * 5. Call game logic functions to update authoritative state
 * 6. Call net functions to notify clients of state changes
 *
 * This file is the "controller" of the server.
 * game.c handles pure game rules.
 * net.c handles serialization and outgoing network messages.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#include "protocol.h"
#include "game.h"
#include "net.h"

/* ═══════════════════════════════════════════════════════════
 *  Global State
 * ═══════════════════════════════════════════════════════════ */

static Game g;            /* the authoritative game state */
static int  listen_fd;

/* ═══════════════════════════════════════════════════════════
 *  Helper: full turn advance (game logic + network)
 * ═══════════════════════════════════════════════════════════ */

static void do_advance_turn(void)
{
    game_advance_turn(&g);

    char buf[80];
    snprintf(buf, sizeof(buf), "It's %s's turn.",
             g.players[g.current].name);
    net_notify_all(&g, buf);
    net_broadcast_turn(&g);
    net_broadcast_state(&g);
}

/* ═══════════════════════════════════════════════════════════
 *  Helper: give cards (game logic + network)
 * ═══════════════════════════════════════════════════════════ */

static void do_give_cards(int pid, int count)
{
    Card drawn[DECK_SIZE];
    int dealt = game_deal_cards(&g, pid, count, drawn);
    net_broadcast_drew(&g, pid, drawn, dealt);
}

/* ═══════════════════════════════════════════════════════════
 *  2. Action Handlers
 * ═══════════════════════════════════════════════════════════ */

 /* Handle a PLAY request from one client.
 *
 * Steps:
 * 1. Verify it is this player's turn
 * 2. Validate message format and card index
 * 3. Check whether the chosen card is legally playable
 * 4. Update the server's authoritative game state
 * 5. Broadcast the played card to all clients
 * 6. Apply UNO rules (UNO vulnerability, win condition, special card effects)
 * 7. Advance the turn if the game is not over
 */
static void handle_play(int pid, uint8_t *payload, uint16_t len)
{
    if (pid != g.current) {
        net_send_error(&g, pid, "Not your turn.");
        return;
    }
    if (len < 2) {
        net_send_error(&g, pid, "Bad play msg.");
        return;
    }

    int idx       = payload[0];
    uint8_t chosen = payload[1];

    if (idx < 0 || idx >= g.players[pid].hand_size) {
        net_send_error(&g, pid, "Invalid card index.");
        return;
    }

    Card card = g.players[pid].hand[idx];
    if (!game_can_play(&g, card)) {
        net_send_error(&g, pid, "That card can't be played here.");
        return;
    }

    if ((card.value == CARD_WILD || card.value == CARD_WILD4)
        && chosen >= NUM_COLORS) {
        net_send_error(&g, pid, "Wild card requires a valid color (0-3).");
        return;
    }

    /* ── Execute the play ────────────────────────── */
    game_remove_card(&g, pid, idx);
    g.discard[g.discard_count++] = card;
    g.top_card  = card;
    g.top_color = (card.value == CARD_WILD || card.value == CARD_WILD4)
                      ? chosen : card.color;

    net_broadcast_played(&g, pid, card);

    /* UNO vulnerability */
    if (g.players[pid].hand_size == 1) {
        if (!g.players[pid].said_uno)
            g.players[pid].uno_vulnerable = 1;
    } else {
        g.players[pid].uno_vulnerable = 0;
        if (g.players[pid].hand_size > 1)
            g.players[pid].said_uno = 0;
    }

    /* Win check */
    if (g.players[pid].hand_size == 0) {
        char w[64];
        snprintf(w, sizeof(w), "%s wins the game!", g.players[pid].name);
        net_notify_all(&g, w);
        net_broadcast_game_over(&g, pid);
        g.over = 1;
        return;
    }

    /* ── Apply card effects ──────────────────────── */
    CardEffect eff = game_card_effect(&g, card);

    if (eff.reversed) {
        game_reverse(&g);
        net_notify_all(&g, g.direction == 1
                       ? "Direction: clockwise."
                       : "Direction: counter-clockwise.");
    }

    if (eff.draw_penalty > 0 && eff.victim >= 0) {
        do_give_cards(eff.victim, eff.draw_penalty);
        char db[80];
        snprintf(db, sizeof(db), "%s draws %d and is skipped!",
                 g.players[eff.victim].name, eff.draw_penalty);
        net_notify_all(&g, db);
    } else if (eff.skip && eff.victim >= 0) {
        char sb[64];
        snprintf(sb, sizeof(sb), "%s is skipped!",
                 g.players[eff.victim].name);
        net_notify_all(&g, sb);
    }

    if (eff.skip)
        g.current = game_next_player(&g, g.current);

    do_advance_turn();
}

/* Handle a DRAW request.
 * A player may draw only on their own turn and only once before passing.
 * After drawing, the player receives the detailed drawn-card result,
 * while other players receive only a summary notification.
 */
static void handle_draw(int pid)
{
    if (pid != g.current) {
        net_send_error(&g, pid, "Not your turn.");
        return;
    }
    if (g.players[pid].has_drawn) {
        net_send_error(&g, pid, "Already drew. Play a card or pass.");
        return;
    }

    g.players[pid].has_drawn = 1;

    Card drawn[1];
    game_deal_cards(&g, pid, 1, drawn);
    net_broadcast_drew(&g, pid, drawn, 1);

    char buf[64];
    snprintf(buf, sizeof(buf), "%s drew a card.", g.players[pid].name);
    net_broadcast_except(&g, pid, MSG_NOTIFY, buf, (uint16_t)strlen(buf));
    net_notify_one(&g, pid, "You drew a card. Play or pass.");

    net_send_hand(&g, pid);
    net_send_state(&g, pid);
}

static void handle_pass(int pid)
{
    if (pid != g.current) {
        net_send_error(&g, pid, "Not your turn.");
        return;
    }
    if (!g.players[pid].has_drawn) {
        net_send_error(&g, pid, "You must draw before passing.");
        return;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%s passed.", g.players[pid].name);
    net_notify_all(&g, buf);
    do_advance_turn();
}

static void handle_uno(int pid)
{
    if (g.players[pid].hand_size <= 2) {
        g.players[pid].said_uno       = 1;
        g.players[pid].uno_vulnerable = 0;
        char buf[64];
        snprintf(buf, sizeof(buf), "%s declared UNO!",
                 g.players[pid].name);
        net_notify_all(&g, buf);
        net_broadcast_state(&g);
    } else {
        net_send_error(&g, pid, "UNO: you need 1-2 cards.");
    }
}

static void handle_callout(int pid, uint8_t *payload, uint16_t len)
{
    if (len < 1) { net_send_error(&g, pid, "Bad callout."); return; }

    int target = payload[0];
    if (target < 0 || target >= MAX_PLAYERS
        || !g.players[target].connected) {
        net_send_error(&g, pid, "Invalid player id.");
        return;
    }
    if (target == pid) {
        net_send_error(&g, pid, "Can't call out yourself.");
        return;
    }

    if (g.players[target].uno_vulnerable) {
        g.players[target].uno_vulnerable = 0;
        do_give_cards(target, 2);
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "%s caught %s forgetting UNO! %s draws 2 penalty cards!",
                 g.players[pid].name, g.players[target].name,
                 g.players[target].name);
        net_notify_all(&g, buf);
        net_broadcast_state(&g);
    } else {
        net_send_error(&g, pid, "That player isn't vulnerable.");
    }
}

static void handle_chat(int pid, uint8_t *payload, uint16_t len)
{
    net_broadcast_chat(&g, pid, payload, len);
}

/* ═══════════════════════════════════════════════════════════
 *  Game Start  (orchestrates game.c + net.c)
 * ═══════════════════════════════════════════════════════════ */

static void start_game(void)
{
    game_start(&g);

    net_broadcast_game_begin(&g);
    net_broadcast_state(&g);

    char note[80];
    snprintf(note, sizeof(note), "Game started! %s goes first.",
             g.players[g.current].name);
    net_notify_all(&g, note);
    net_broadcast_turn(&g);

    printf("[Server] Game started with %d players.\n", g.num_connected);
}

/* ═══════════════════════════════════════════════════════════
 *  4. Disconnect
 * ═══════════════════════════════════════════════════════════ */

 /* Cleanly remove one disconnected player:
 * - close the socket
 * - mark the slot as disconnected
 * - notify the remaining players
 * - if necessary, advance the turn or end the game
 */
static void disconnect_player(int pid)
{
    if (!g.players[pid].connected) return;
    printf("[Server] %s (id=%d) disconnected.\n",
           g.players[pid].name, pid);
    close(g.players[pid].fd);
    g.players[pid].fd        = -1;
    g.players[pid].connected = 0;
    g.num_connected--;

    net_broadcast_player_left(&g, pid);

    char note[64];
    snprintf(note, sizeof(note), "%s left the game.",
             g.players[pid].name);
    net_notify_all(&g, note);

    if (g.started && !g.over) {
        if (pid == g.current && g.num_connected >= MIN_PLAYERS)
            do_advance_turn();

        if (g.num_connected < MIN_PLAYERS) {
            int w = -1;
            for (int i = 0; i < MAX_PLAYERS; i++)
                if (g.players[i].connected) { w = i; break; }
            if (w >= 0) {
                char wb_s[64];
                snprintf(wb_s, sizeof(wb_s),
                         "%s wins by default!", g.players[w].name);
                net_notify_all(&g, wb_s);
                net_broadcast_game_over(&g, w);
                g.over = 1;
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════
 *  3. Message Dispatch
 * ═══════════════════════════════════════════════════════════ */

static void process(int pid, uint8_t type, uint8_t *payload, uint16_t len)
{
    switch (type) {

    case MSG_JOIN: {
        if (g.started) {
            net_send_error(&g, pid, "Game in progress.");
            return;
        }
        int n = (len > MAX_NAME) ? MAX_NAME : (int)len;
        memcpy(g.players[pid].name, payload, (size_t)n);
        g.players[pid].name[n] = '\0';
        printf("[Server] '%s' joined (id=%d, seat=%d).\n",
               g.players[pid].name, pid, g.seat_count);

        game_seat_add(&g, pid);
        net_send_welcome(&g, pid);
        net_broadcast_player_join(&g, pid);
        net_send_existing_players(&g, pid);

        char nb[80];
        snprintf(nb, sizeof(nb), "%s joined. (%d/%d players)",
                 g.players[pid].name, g.num_connected, g.num_expected);
        net_notify_all(&g, nb);

        if (g.num_connected >= g.num_expected)
            start_game();
        break;
    }

    case MSG_START:
        if (g.started)
            net_send_error(&g, pid, "Already started.");
        else if (g.num_connected < MIN_PLAYERS)
            net_send_error(&g, pid, "Need 2+ players.");
        else
            start_game();
        break;

    case MSG_PLAY:
        if (!g.started || g.over) return;
        handle_play(pid, payload, len);
        break;
    case MSG_DRAW:
        if (!g.started || g.over) return;
        handle_draw(pid);
        break;
    case MSG_PASS:
        if (!g.started || g.over) return;
        handle_pass(pid);
        break;
    case MSG_UNO:
        if (!g.started || g.over) return;
        handle_uno(pid);
        break;
    case MSG_CALLOUT:
        if (!g.started || g.over) return;
        handle_callout(pid, payload, len);
        break;
    case MSG_CHAT_SEND:
        handle_chat(pid, payload, len);
        break;
    default:
        net_send_error(&g, pid, "Unknown command.");
        break;
    }
}

/* ═══════════════════════════════════════════════════════════
 *  5. Main Event Loop
 * ═══════════════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    int port = DEFAULT_PORT;
    int expected = MIN_PLAYERS;
    if (argc > 1) port     = atoi(argv[1]);
    if (argc > 2) expected = atoi(argv[2]);
    if (expected < MIN_PLAYERS) expected = MIN_PLAYERS;
    if (expected > MAX_PLAYERS) expected = MAX_PLAYERS;

    srand((unsigned)time(NULL));
    signal(SIGPIPE, SIG_IGN);

    game_init(&g, expected);

    /* ── Socket setup ────────────────────────────── */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0) {
        perror("setsockopt"); exit(1);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(listen_fd, 5) < 0) {
        perror("listen"); exit(1);
    }

    printf("=== UNO Server on port %d  (waiting for %d players) ===\n",
           port, expected);

    /* ── select loop ─────────────────────────────── */
    while (!g.over) {
        fd_set rset;
        FD_ZERO(&rset);
        int maxfd = -1;

        if (!g.started) {
            FD_SET(listen_fd, &rset);
            maxfd = listen_fd;
        }
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (g.players[i].connected) {
                FD_SET(g.players[i].fd, &rset);
                if (g.players[i].fd > maxfd)
                    maxfd = g.players[i].fd;
            }
        }
        if (maxfd < 0) break;

        if (select(maxfd + 1, &rset, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        /* ── New connections ─────────────────────── */
        if (!g.started && FD_ISSET(listen_fd, &rset)) {
            struct sockaddr_in ca;
            socklen_t cl = sizeof(ca);
            int cfd = accept(listen_fd, (struct sockaddr *)&ca, &cl);
            if (cfd >= 0) {
                if (g.num_connected >= g.num_expected) {
                    const char *m = "Game is full.";
                    send_msg(cfd, MSG_ERROR, m, (uint16_t)strlen(m));
                    close(cfd);
                } else {
                    int slot = -1;
                    for (int i = 0; i < MAX_PLAYERS; i++)
                        if (!g.players[i].connected) { slot = i; break; }
                    if (slot >= 0) {
                        memset(&g.players[slot], 0, sizeof(Player));
                        g.players[slot].fd        = cfd;
                        g.players[slot].connected = 1;
                        g.num_connected++;
                        printf("[Server] Connection in slot %d  (%d/%d)\n",
                               slot, g.num_connected, g.num_expected);
                    } else {
                        close(cfd);
                    }
                }
            }
        }

        /* ── Client data ─────────────────────────── */
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!g.players[i].connected) continue;
            if (!FD_ISSET(g.players[i].fd, &rset)) continue;

            int space = NET_BUF_SIZE - g.players[i].buf_len;
            if (space <= 0) { disconnect_player(i); continue; }

            ssize_t n = read(g.players[i].fd,
                             g.players[i].buf + g.players[i].buf_len,
                             (size_t)space);
            if (n <= 0) { disconnect_player(i); continue; }
            g.players[i].buf_len += (int)n;

            uint8_t type;
            uint8_t payload[MAX_PAYLOAD];
            uint16_t plen;
            while (recv_msg(g.players[i].buf, &g.players[i].buf_len,
                            &type, payload, &plen)) {
                process(i, type, payload, plen);
                if (g.over) break;
            }
        }
    }

    /* ── Cleanup ─────────────────────────────────── */
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (g.players[i].connected) close(g.players[i].fd);
    close(listen_fd);
    printf("[Server] Shutdown.\n");
    return 0;
}
