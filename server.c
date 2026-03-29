/*
 * server.c  --  UNO Game Server
 *
 * Usage:  ./server [port] [num_players]
 *         port         (default 4242)
 *         num_players  (default 2, max 4)
 *
 * Uses select() to handle multiple clients concurrently.
 * The server maintains all authoritative game state and
 * validates every action before broadcasting updates.
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
#include <server.h>
#include "protocol.h"

#define DEFAULT_PORT 4242


static Player  players[MAX_PLAYERS];
static int     num_connected  = 0;
static int     num_expected;
static int     game_started   = 0;
static int     game_over      = 0;

/* ═══════════════════════════════════════════════════════════
 *  Player & Game State
 * ═══════════════════════════════════════════════════════════ */

/*
 * Seat ring — maintains join-order for turn rotation.
 *
 *   seat_order[0..seat_count-1]  = player IDs in join order
 *   seat_pos[pid]                = index into seat_order (reverse map)
 *   seat_idx                     = current turn position in seat_order
 *
 * next_player() walks this ring in +1 / -1 direction,
 * skipping disconnected players.  This guarantees a clean,
 * deterministic turn order regardless of which slot id a
 * player was assigned.
 */
static int     seat_order[MAX_PLAYERS];  /* pid list in join order     */
static int     seat_pos[MAX_PLAYERS];    /* pid -> index in seat_order */
static int     seat_count     = 0;       /* number of seats filled     */
static int     seat_idx       = 0;       /* current turn seat index    */

static int     current        = 0;    /* pid of active player         */
static int     direction      = 1;    /* +1 CW, -1 CCW               */
static Card    top_card;
static uint8_t top_color;             /* effective color              */

static Card    draw_pile[DECK_SIZE];
static int     draw_top       = 0;
static int     draw_count     = 0;

static Card    discard[DECK_SIZE];
static int     discard_count  = 0;

static int     listen_fd;


int main(int argc, char *argv[])
{
    int port = DEFAULT_PORT;
    // init
    num_expected = MIN_PLAYERS;
    if (argc > 1) port = atoi(argv[1]);
    if (argc > 2) num_expected = atoi(argv[2]);
    if (num_expected < MIN_PLAYERS) num_expected = MIN_PLAYERS;
    if (num_expected > MAX_PLAYERS) num_expected = MAX_PLAYERS;

    srand((unsigned)time(NULL));
    signal(SIGPIPE, SIG_IGN);

    for (int i = 0; i < MAX_PLAYERS; i++) {
        players[i].fd = -1;
        players[i].connected = 0;
        players[i].buf_len = 0;
    }

    /* --- socket setup --- */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt,
                   sizeof(opt)) < 0) {
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
           port, num_expected);

    /* --- select loop --- */
    while (!game_over) {
        fd_set rset;
        FD_ZERO(&rset);
        int maxfd = -1;

        if (!game_started) {
            FD_SET(listen_fd, &rset);
            maxfd = listen_fd;
        }
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (players[i].connected) {
                FD_SET(players[i].fd, &rset);
                if (players[i].fd > maxfd) maxfd = players[i].fd;
            }
        }

        if (maxfd < 0) break;   /* no fds to watch */

        if (select(maxfd + 1, &rset, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            perror("select"); break;
        }

        /* --- new connections --- */
        if (!game_started && FD_ISSET(listen_fd, &rset)) {
            //
        }

        /* --- client data --- */
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!players[i].connected) continue;
            if (!FD_ISSET(players[i].fd, &rset)) continue;

            int space = NET_BUF_SIZE - players[i].buf_len;
            if (space <= 0) { disconnect_player(i); continue; }

            ssize_t n = read(players[i].fd,
                             players[i].buf + players[i].buf_len,
                             (size_t)space);
            if (n <= 0) { disconnect_player(i); continue; }
            players[i].buf_len += (int)n;

            uint8_t type;
            uint8_t payload[MAX_PAYLOAD];
            uint16_t plen;
            while (recv_msg(players[i].buf, &players[i].buf_len,
                            &type, payload, &plen)) {
                process(i, type, payload, plen);
                if (game_over) break;
            }
        }
    }

    /* cleanup */
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (players[i].connected) close(players[i].fd);
    close(listen_fd);
    printf("[Server] Shutdown.\n");
    return 0;
}