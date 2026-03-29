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

#include "protocol.h"

/* ═══════════════════════════════════════════════════════════
 *  Player & Game State
 * ═══════════════════════════════════════════════════════════ */

typedef struct {
    int       fd;
    char      name[MAX_NAME + 1];
    Card      hand[DECK_SIZE];
    int       hand_size;
    int       connected;
    int       said_uno;          /* declared UNO while at <=2 cards   */
    int       uno_vulnerable;    /* hand==1 and hasn't said UNO       */
    int       has_drawn;         /* drew a card this turn already     */
    uint8_t   buf[NET_BUF_SIZE];
    int       buf_len;
} Player;

static Player  players[MAX_PLAYERS];
static int     num_connected  = 0;
static int     num_expected;
static int     game_started   = 0;
static int     game_over      = 0;

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

/* ═══════════════════════════════════════════════════════════
 *  Deck Management
 * ═══════════════════════════════════════════════════════════ */

static void init_deck(void)
{
    draw_count = 0;
    for (int c = 0; c < NUM_COLORS; c++) {
        draw_pile[draw_count++] = (Card){(uint8_t)c, CARD_0};
        for (int v = CARD_1; v <= CARD_DRAW2; v++) {
            draw_pile[draw_count++] = (Card){(uint8_t)c, (uint8_t)v};
            draw_pile[draw_count++] = (Card){(uint8_t)c, (uint8_t)v};
        }
    }
    for (int i = 0; i < 4; i++) {
        draw_pile[draw_count++] = (Card){COLOR_WILD, CARD_WILD};
        draw_pile[draw_count++] = (Card){COLOR_WILD, CARD_WILD4};
    }
    draw_top = 0;
}

static void shuffle(Card *pile, int n)
{
    for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        Card t = pile[i]; pile[i] = pile[j]; pile[j] = t;
    }
}

static Card draw_card(void)
{
    if (draw_top >= draw_count) {
        /* reshuffle discard into draw pile, keep top */
        if (discard_count <= 1)
            return (Card){COLOR_WILD, CARD_WILD};
        Card save = discard[discard_count - 1];
        draw_count = discard_count - 1;
        memcpy(draw_pile, discard, (size_t)draw_count * sizeof(Card));
        discard_count = 1;
        discard[0] = save;
        draw_top = 0;
        shuffle(draw_pile, draw_count);
        printf("[Server] Reshuffled discard pile into draw pile (%d cards).\n",
               draw_count);
    }
    return draw_pile[draw_top++];
}

/* ═══════════════════════════════════════════════════════════
 *  Messaging Helpers
 * ═══════════════════════════════════════════════════════════ */

static void broadcast(uint8_t type, const void *p, uint16_t len)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (players[i].connected)
            send_msg(players[i].fd, type, p, len);
}

static void broadcast_except(int ex, uint8_t type, const void *p, uint16_t len)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (players[i].connected && i != ex)
            send_msg(players[i].fd, type, p, len);
}

static void notify_one(int pid, const char *msg)
{
    send_msg(players[pid].fd, MSG_NOTIFY, msg, (uint16_t)strlen(msg));
}

static void notify_all(const char *msg)
{
    broadcast(MSG_NOTIFY, msg, (uint16_t)strlen(msg));
}

static void send_error(int pid, const char *msg)
{
    send_msg(players[pid].fd, MSG_ERROR, msg, (uint16_t)strlen(msg));
}

/* ═══════════════════════════════════════════════════════════
 *  Send Hand / State
 * ═══════════════════════════════════════════════════════════ */

static void send_hand(int pid)
{
    uint8_t buf[1 + DECK_SIZE * 2];
    buf[0] = (uint8_t)players[pid].hand_size;
    for (int i = 0; i < players[pid].hand_size; i++) {
        buf[1 + i * 2]     = players[pid].hand[i].color;
        buf[1 + i * 2 + 1] = players[pid].hand[i].value;
    }
    send_msg(players[pid].fd, MSG_HAND, buf,
             (uint16_t)(1 + players[pid].hand_size * 2));
}

static void send_state_to(int pid)
{
    uint8_t buf[MAX_PAYLOAD];
    int off = 0;

    /* count active players */
    int active = 0;
    for (int s = 0; s < seat_count; s++)
        if (players[seat_order[s]].connected) active++;

    buf[off++] = (uint8_t)active;

    /* send players in seat order (join order) so client display is consistent */
    for (int s = 0; s < seat_count; s++) {
        int id = seat_order[s];
        if (!players[id].connected) continue;
        buf[off++] = (uint8_t)id;
        int nlen = (int)strlen(players[id].name);
        buf[off++] = (uint8_t)nlen;
        memcpy(buf + off, players[id].name, (size_t)nlen);
        off += nlen;
        buf[off++] = (uint8_t)players[id].hand_size;
        buf[off++] = (uint8_t)players[id].said_uno;
    }
    buf[off++] = (uint8_t)current;
    buf[off++] = (direction == 1) ? 0 : 1;
    buf[off++] = top_card.color;
    buf[off++] = top_card.value;
    buf[off++] = top_color;

    send_msg(players[pid].fd, MSG_STATE, buf, (uint16_t)off);
}

static void broadcast_state(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!players[i].connected) {
            continue;
        }
        send_state_to(i);
        send_hand(i);
    }
}

/* ═══════════════════════════════════════════════════════════
 *  Turn Management
 * ═══════════════════════════════════════════════════════════ */

/*
 * Walk the seat ring from 'from_pid' in the current direction,
 * skipping disconnected players.  Returns the next active pid.
 *
 *  seat_order:  [P0] -> [P1] -> [P2] -> [P3] -> [P0] -> ...
 *                        direction=+1  ───────>
 *                        direction=-1  <───────
 */
static int next_player(int from_pid)
{
    int si = seat_pos[from_pid];
    for (int step = 0; step < seat_count; step++) {
        si = (si + direction + seat_count) % seat_count;
        int pid = seat_order[si];
        if (players[pid].connected) {
            seat_idx = si;            /* keep seat_idx in sync */
            return pid;
        }
    }
    return from_pid;                  /* nobody else alive      */
}

static void advance_turn(void)
{
    /* close UNO vulnerability window for non-current players */
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (i != current) players[i].uno_vulnerable = 0;

    current = next_player(current);
    players[current].has_drawn = 0;

    char buf[80];
    snprintf(buf, sizeof(buf), "It's %s's turn.", players[current].name);
    notify_all(buf);

    uint8_t tbuf[1] = {(uint8_t)current};
    broadcast(MSG_TURN, tbuf, 1);
    broadcast_state();
}

/* ═══════════════════════════════════════════════════════════
 *  Card Validation
 * ═══════════════════════════════════════════════════════════ */

static int can_play(Card c)
{
    if (c.value == CARD_WILD || c.value == CARD_WILD4) return 1;
    if (c.color == top_color)       return 1;
    if (c.value == top_card.value)  return 1;
    return 0;
}

static int has_playable(int pid)
{
    for (int i = 0; i < players[pid].hand_size; i++)
        if (can_play(players[pid].hand[i])) return 1;
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 *  Hand Manipulation
 * ═══════════════════════════════════════════════════════════ */

static Card remove_card(int pid, int idx)
{
    Card c = players[pid].hand[idx];
    for (int i = idx; i < players[pid].hand_size - 1; i++)
        players[pid].hand[i] = players[pid].hand[i + 1];
    players[pid].hand_size--;
    return c;
}

static void give_cards(int pid, int count)
{
    uint8_t buf[1 + DECK_SIZE * 2 + 1];
    int n = 0;
    for (int i = 0; i < count; i++) {
        Card c = draw_card();
        players[pid].hand[players[pid].hand_size++] = c;
        buf[1 + n * 2]     = c.color;
        buf[1 + n * 2 + 1] = c.value;
        n++;
    }
    buf[0] = (uint8_t)n;
    buf[1 + n * 2] = (uint8_t)has_playable(pid);
    send_msg(players[pid].fd, MSG_DRAW_RESULT, buf,
             (uint16_t)(1 + n * 2 + 1));

    uint8_t nb[2] = {(uint8_t)pid, (uint8_t)n};
    broadcast_except(pid, MSG_DREW, nb, 2);

    players[pid].said_uno = 0;
    players[pid].uno_vulnerable = 0;
}

/* ═══════════════════════════════════════════════════════════
 *  Action Handlers
 * ═══════════════════════════════════════════════════════════ */

static void handle_play(int pid, uint8_t *payload, uint16_t len)
{
    if (pid != current) { send_error(pid, "Not your turn."); return; }
    if (len < 2)        { send_error(pid, "Bad play msg.");  return; }

    int idx = payload[0];
    uint8_t chosen = payload[1];

    if (idx < 0 || idx >= players[pid].hand_size) {
        send_error(pid, "Invalid card index.");
        return;
    }

    Card card = players[pid].hand[idx];
    if (!can_play(card)) {
        send_error(pid, "That card can't be played here.");
        return;
    }

    if ((card.value == CARD_WILD || card.value == CARD_WILD4)
        && chosen >= NUM_COLORS) {
        send_error(pid, "Wild card requires a valid color (0-3).");
        return;
    }

    /* --- execute the play --- */
    remove_card(pid, idx);
    discard[discard_count++] = card;
    top_card = card;
    top_color = (card.value == CARD_WILD || card.value == CARD_WILD4)
                    ? chosen : card.color;

    /* broadcast played-card struct */
    uint8_t pb[5] = {(uint8_t)pid, card.color, card.value,
                     top_color, (uint8_t)players[pid].hand_size};
    broadcast(MSG_PLAYED, pb, 5);

    /* UNO vulnerability check */
    if (players[pid].hand_size == 1) {
        if (!players[pid].said_uno)
            players[pid].uno_vulnerable = 1;
    } else {
        players[pid].uno_vulnerable = 0;
        if (players[pid].hand_size > 1)
            players[pid].said_uno = 0;
    }

    /* win check */
    if (players[pid].hand_size == 0) {
        char w[64];
        snprintf(w, sizeof(w), "%s wins the game!", players[pid].name);
        notify_all(w);
        uint8_t wb[1] = {(uint8_t)pid};
        broadcast(MSG_GAME_OVER, wb, 1);
        game_over = 1;
        return;
    }

    /* apply card effects */
    int skip = 0;
    switch (card.value) {
    case CARD_SKIP: {
        int v = next_player(current);
        char sb[64];
        snprintf(sb, sizeof(sb), "%s is skipped!", players[v].name);
        notify_all(sb);
        skip = 1;
        break;
    }
    case CARD_REVERSE:
        direction = -direction;
        notify_all(direction == 1
                   ? "Direction: clockwise."
                   : "Direction: counter-clockwise.");
        if (num_connected == 2) skip = 1;
        break;
    case CARD_DRAW2: {
        int v = next_player(current);
        give_cards(v, 2);
        char db[80];
        snprintf(db, sizeof(db), "%s draws 2 and is skipped!",
                 players[v].name);
        notify_all(db);
        skip = 1;
        break;
    }
    case CARD_WILD4: {
        int v = next_player(current);
        give_cards(v, 4);
        char db[80];
        snprintf(db, sizeof(db), "%s draws 4 and is skipped!",
                 players[v].name);
        notify_all(db);
        skip = 1;
        break;
    }
    default:
        break;
    }

    if (skip) current = next_player(current);
    advance_turn();
}

static void handle_draw(int pid)
{
    if (pid != current)       { send_error(pid, "Not your turn."); return; }
    if (players[pid].has_drawn) {
        send_error(pid, "Already drew. Play a card or pass.");
        return;
    }

    players[pid].has_drawn = 1;
    give_cards(pid, 1);

    char buf[64];
    snprintf(buf, sizeof(buf), "%s drew a card.", players[pid].name);
    broadcast_except(pid, MSG_NOTIFY, buf, (uint16_t)strlen(buf));
    notify_one(pid, "You drew a card. Play or pass.");

    /* send updated hand + state */
    send_hand(pid);
    send_state_to(pid);
}

static void handle_pass(int pid)
{
    if (pid != current) { send_error(pid, "Not your turn."); return; }
    if (!players[pid].has_drawn) {
        send_error(pid, "You must draw before passing.");
        return;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%s passed.", players[pid].name);
    notify_all(buf);
    advance_turn();
}

static void handle_uno(int pid)
{
    if (players[pid].hand_size <= 2) {
        players[pid].said_uno = 1;
        players[pid].uno_vulnerable = 0;
        char buf[64];
        snprintf(buf, sizeof(buf), "%s declared UNO!", players[pid].name);
        notify_all(buf);
        broadcast_state();
    } else {
        send_error(pid, "UNO: you need 1-2 cards.");
    }
}

static void handle_callout(int pid, uint8_t *payload, uint16_t len)
{
    if (len < 1) { send_error(pid, "Bad callout."); return; }

    int target = payload[0];
    if (target < 0 || target >= MAX_PLAYERS || !players[target].connected) {
        send_error(pid, "Invalid player id.");
        return;
    }
    if (target == pid) {
        send_error(pid, "Can't call out yourself.");
        return;
    }

    if (players[target].uno_vulnerable) {
        players[target].uno_vulnerable = 0;
        give_cards(target, 2);
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "%s caught %s forgetting UNO! %s draws 2 penalty cards!",
                 players[pid].name, players[target].name,
                 players[target].name);
        notify_all(buf);
        broadcast_state();
    } else {
        send_error(pid, "That player isn't vulnerable.");
    }
}

static void handle_chat(int pid, uint8_t *payload, uint16_t len)
{
    uint8_t buf[MAX_PAYLOAD];
    buf[0] = (uint8_t)pid;
    if (len > MAX_PAYLOAD - 1) len = MAX_PAYLOAD - 1;
    memcpy(buf + 1, payload, len);
    broadcast(MSG_CHAT_RECV, buf, (uint16_t)(1 + len));
}

/* ═══════════════════════════════════════════════════════════
 *  Game Start
 * ═══════════════════════════════════════════════════════════ */

static void start_game(void)
{
    game_started = 1;
    init_deck();
    shuffle(draw_pile, draw_count);

    /* deal hands */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!players[i].connected) continue;
        for (int j = 0; j < INITIAL_HAND; j++)
            players[i].hand[j] = draw_card();
        players[i].hand_size = INITIAL_HAND;
    }

    /* flip first card (skip Wild+4) */
    do { top_card = draw_card(); } while (top_card.value == CARD_WILD4);
    top_color = top_card.color;
    if (top_card.value == CARD_WILD)
        top_color = (uint8_t)(rand() % NUM_COLORS);
    discard[0] = top_card;
    discard_count = 1;

    /* first player = first seat in join order */
    seat_idx = 0;
    current = seat_order[seat_idx];

    /* apply first-card effects */
    switch (top_card.value) {
    case CARD_SKIP:
        current = next_player(current);
        break;
    case CARD_REVERSE:
        direction = -1;
        break;
    case CARD_DRAW2:
        give_cards(current, 2);
        current = next_player(current);
        break;
    default:
        break;
    }

    players[current].has_drawn = 0;

    /* broadcast game begin */
    uint8_t gb[4] = {(uint8_t)num_connected, top_card.color,
                     top_card.value, top_color};
    broadcast(MSG_GAME_BEGIN, gb, 4);

    broadcast_state();

    char note[80];
    snprintf(note, sizeof(note),
             "Game started! %s goes first.", players[current].name);
    notify_all(note);

    uint8_t tb[1] = {(uint8_t)current};
    broadcast(MSG_TURN, tb, 1);

    printf("[Server] Game started with %d players.\n", num_connected);
}

/* ═══════════════════════════════════════════════════════════
 *  Disconnect
 * ═══════════════════════════════════════════════════════════ */

static void disconnect_player(int pid)
{
    if (!players[pid].connected) return;
    printf("[Server] %s (id=%d) disconnected.\n", players[pid].name, pid);
    close(players[pid].fd);
    players[pid].fd = -1;
    players[pid].connected = 0;
    num_connected--;

    uint8_t lb[1] = {(uint8_t)pid};
    broadcast(MSG_PLAYER_LEFT, lb, 1);

    char note[64];
    snprintf(note, sizeof(note), "%s left the game.", players[pid].name);
    notify_all(note);

    if (game_started && !game_over) {
        if (pid == current && num_connected >= MIN_PLAYERS)
            advance_turn();

        if (num_connected < MIN_PLAYERS) {
            int w = -1;
            for (int i = 0; i < MAX_PLAYERS; i++)
                if (players[i].connected) { w = i; break; }
            if (w >= 0) {
                char wb_s[64];
                snprintf(wb_s, sizeof(wb_s),
                         "%s wins by default!", players[w].name);
                notify_all(wb_s);
                uint8_t wb[1] = {(uint8_t)w};
                broadcast(MSG_GAME_OVER, wb, 1);
                game_over = 1;
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════
 *  Message Dispatch
 * ═══════════════════════════════════════════════════════════ */

static void process(int pid, uint8_t type, uint8_t *payload, uint16_t len)
{
    switch (type) {
    case MSG_JOIN: {
        if (game_started) { send_error(pid, "Game in progress."); return; }
        int n = (len > MAX_NAME) ? MAX_NAME : (int)len;
        memcpy(players[pid].name, payload, (size_t)n);
        players[pid].name[n] = '\0';
        printf("[Server] '%s' joined (id=%d, seat=%d).\n",
               players[pid].name, pid, seat_count);

        /* Register in the seat ring (join order) */
        seat_order[seat_count] = pid;
        seat_pos[pid] = seat_count;
        seat_count++;

        uint8_t wb[2] = {(uint8_t)pid, (uint8_t)num_connected};
        send_msg(players[pid].fd, MSG_WELCOME, wb, 2);

        uint8_t jb[1 + MAX_NAME];
        jb[0] = (uint8_t)pid;
        memcpy(jb + 1, players[pid].name, (size_t)n);
        broadcast_except(pid, MSG_PLAYER_JOIN, jb, (uint16_t)(1 + n));

        /* send existing players to the new player (in seat order) */
        for (int s = 0; s < seat_count; s++) {
            int id = seat_order[s];
            if (id == pid || !players[id].connected) continue;
            uint8_t eb[1 + MAX_NAME];
            eb[0] = (uint8_t)id;
            int elen = (int)strlen(players[id].name);
            memcpy(eb + 1, players[id].name, (size_t)elen);
            send_msg(players[pid].fd, MSG_PLAYER_JOIN, eb,
                     (uint16_t)(1 + elen));
        }

        char nb[80];
        snprintf(nb, sizeof(nb), "%s joined. (%d/%d players)",
                 players[pid].name, num_connected, num_expected);
        notify_all(nb);

        if (num_connected >= num_expected)
            start_game();
        break;
    }
    case MSG_START:
        if (game_started) send_error(pid, "Already started.");
        else if (num_connected < MIN_PLAYERS)
            send_error(pid, "Need 2+ players.");
        else start_game();
        break;

    case MSG_PLAY:
        if (!game_started || game_over) return;
        handle_play(pid, payload, len);
        break;
    case MSG_DRAW:
        if (!game_started || game_over) return;
        handle_draw(pid);
        break;
    case MSG_PASS:
        if (!game_started || game_over) return;
        handle_pass(pid);
        break;
    case MSG_UNO:
        if (!game_started || game_over) return;
        handle_uno(pid);
        break;
    case MSG_CALLOUT:
        if (!game_started || game_over) return;
        handle_callout(pid, payload, len);
        break;
    case MSG_CHAT_SEND:
        handle_chat(pid, payload, len);
        break;
    default:
        send_error(pid, "Unknown command.");
        break;
    }
}

/* ═══════════════════════════════════════════════════════════
 *  Main
 * ═══════════════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    int port = DEFAULT_PORT;
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
            struct sockaddr_in ca;
            socklen_t cl = sizeof(ca);
            int cfd = accept(listen_fd, (struct sockaddr *)&ca, &cl);
            if (cfd >= 0) {
                if (num_connected >= num_expected) {
                    const char *m = "Game is full.";
                    send_msg(cfd, MSG_ERROR, m, (uint16_t)strlen(m));
                    close(cfd);
                } else {
                    int slot = -1;
                    for (int i = 0; i < MAX_PLAYERS; i++)
                        if (!players[i].connected) { slot = i; break; }
                    if (slot >= 0) {
                        memset(&players[slot], 0, sizeof(Player));
                        players[slot].fd = cfd;
                        players[slot].connected = 1;
                        num_connected++;
                        printf("[Server] Connection in slot %d  (%d/%d)\n",
                               slot, num_connected, num_expected);
                    } else {
                        close(cfd);
                    }
                }
            }
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
