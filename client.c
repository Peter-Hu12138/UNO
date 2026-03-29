/*
 * client.c  --  UNO Game Client  (main loop / glue)
 *
 * Usage:  ./client <host> <port> <name>
 *
 * Architecture:
 *   input.c   — parse user input  →  Command struct
 *   ui.c      — render all output  (ANSI, cards, panels)
 *   client.c  — event loop:  read → parse → execute → render
 *
 * This file contains:
 *   1. Network connection
 *   2. Client state management
 *   3. Server message dispatch  (update state + call ui_*)
 *   4. Command execution        (Command → send_msg)
 *   5. Main select() loop
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>

#include "protocol.h"
#include "ui.h"
#include "input.h"

/* ═══════════════════════════════════════════════════════════
 *  Client State  (extends GameView with network buffer)
 * ═══════════════════════════════════════════════════════════ */

typedef struct {
    GameView view;                  /* everything the UI needs        */
    int      dirty;                 /* 1 = need to re-render          */
    uint8_t  netbuf[NET_BUF_SIZE];  /* partial-message ring buffer    */
    int      netbuf_len;
} ClientState;

/* ═══════════════════════════════════════════════════════════
 *  1. Network Connection
 * ═══════════════════════════════════════════════════════════ */

static int connect_to_server(const char *host, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid host address (IPv4 dotted-decimal only).\n");
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* ═══════════════════════════════════════════════════════════
 *  2. State Management
 * ═══════════════════════════════════════════════════════════ */

static void reset_state(ClientState *st)
{
    memset(st, 0, sizeof(*st));
    st->view.my_id          = -1;
    st->view.current_player = -1;
    st->view.direction      = 1;
    st->view.effective_color = COLOR_RED;
    st->view.connected      = 1;
}

static int find_player_by_name(const ClientState *st, const char *name)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!st->view.players[i].present) continue;
        if (!st->view.players[i].connected) continue;
        /* case-insensitive compare */
        const char *a = st->view.players[i].name;
        const char *b = name;
        int match = 1;
        while (*a && *b) {
            if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
                { match = 0; break; }
            a++; b++;
        }
        if (match && *a == '\0' && *b == '\0') return i;
    }
    return -1;
}

/* ═══════════════════════════════════════════════════════════
 *  3. Server Message Dispatch
 *
 *  Each handler:
 *    - Reads payload bytes → updates st->view fields
 *    - Calls ui_event() / ui_event_*() for inline feedback
 *    - Sets st->dirty if the full panel should redraw
 * ═══════════════════════════════════════════════════════════ */

static void on_welcome(ClientState *st, const uint8_t *p, uint16_t len)
{
    if (len < 2) return;
    GameView *v = &st->view;
    v->my_id = p[0];
    v->players[v->my_id].id        = v->my_id;
    v->players[v->my_id].present   = 1;
    v->players[v->my_id].connected = 1;
    st->dirty = 1;

    char msg[128];
    snprintf(msg, sizeof(msg),
             "Connected as player %d (%d player(s) in lobby).",
             p[0], p[1]);
    ui_event("[System]", FG_CYAN, msg);
}

static void on_player_join(ClientState *st, const uint8_t *p, uint16_t len)
{
    if (len < 1) return;
    int id = p[0];
    if (id < 0 || id >= MAX_PLAYERS) return;
    int nlen = (int)len - 1;
    if (nlen > MAX_NAME) nlen = MAX_NAME;

    PlayerInfo *pi = &st->view.players[id];
    pi->id = id;
    pi->present = 1;
    pi->connected = 1;
    memcpy(pi->name, p + 1, (size_t)nlen);
    pi->name[nlen] = '\0';
    st->dirty = 1;

    char msg[128];
    snprintf(msg, sizeof(msg), "%s%s%s joined the game.",
             BOLD, pi->name, RESET);
    ui_event("[+]", FG_GREEN, msg);
}

static void on_game_begin(ClientState *st, const uint8_t *p, uint16_t len)
{
    if (len < 4) return;
    GameView *v = &st->view;
    v->game_started     = 1;
    v->top_card.color   = p[1];
    v->top_card.value   = p[2];
    v->effective_color  = p[3];
    st->dirty = 1;
    ui_event("[System]", FG_CYAN, "Game started! Let's go!");
}

static void on_hand(ClientState *st, const uint8_t *p, uint16_t len)
{
    if (len < 1) return;
    GameView *v = &st->view;
    int count = p[0];
    if (len < (uint16_t)(1 + count * 2)) return;

    v->hand_size = count;
    for (int i = 0; i < count; i++) {
        v->hand[i].color = p[1 + i * 2];
        v->hand[i].value = p[1 + i * 2 + 1];
    }
    if (v->my_id >= 0 && v->my_id < MAX_PLAYERS)
        v->players[v->my_id].hand_count = count;
    st->dirty = 1;
}

static void on_state(ClientState *st, const uint8_t *p, uint16_t len)
{
    GameView *v = &st->view;
    int off = 0;
    if (len < 1) return;

    int active = p[off++];

    for (int i = 0; i < MAX_PLAYERS; i++) {
        v->players[i].present   = 0;
        v->players[i].connected = 0;
    }
    v->display_count = 0;

    for (int i = 0; i < active; i++) {
        if (off + 4 > len) return;
        int id       = p[off++];
        int name_len = p[off++];
        if (id < 0 || id >= MAX_PLAYERS) return;
        if (off + name_len + 2 > len) return;

        PlayerInfo *pi = &v->players[id];
        pi->id        = id;
        pi->present   = 1;
        pi->connected = 1;

        /* Preserve seat order from server */
        v->display_order[v->display_count++] = id;

        int copy = name_len > MAX_NAME ? MAX_NAME : name_len;
        memcpy(pi->name, p + off, (size_t)copy);
        pi->name[copy] = '\0';
        off += name_len;

        pi->hand_count = p[off++];
        pi->said_uno   = p[off++];
    }

    if (off + 5 > len) return;
    v->current_player  = p[off++];
    v->direction       = (p[off++] == 0) ? 1 : -1;
    v->top_card.color  = p[off++];
    v->top_card.value  = p[off++];
    v->effective_color = p[off++];
    v->game_started    = 1;
    st->dirty = 1;
}

static void on_turn(ClientState *st, const uint8_t *p, uint16_t len)
{
    if (len < 1) return;
    GameView *v = &st->view;
    v->current_player = p[0];
    st->dirty = 1;

    if (v->current_player == v->my_id) {
        ui_event("[Turn]", FG_YELLOW, "It's YOUR turn!");
    } else if (v->current_player >= 0 && v->current_player < MAX_PLAYERS
               && v->players[v->current_player].present) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s's turn.",
                 v->players[v->current_player].name);
        ui_event("[Turn]", FG_GRAY, msg);
    }
}

static void on_played(ClientState *st, const uint8_t *p, uint16_t len)
{
    if (len < 5) return;
    GameView *v = &st->view;
    int pid     = p[0];
    Card c      = { p[1], p[2] };
    uint8_t eff = p[3];
    int cnt     = p[4];

    if (pid >= 0 && pid < MAX_PLAYERS)
        v->players[pid].hand_count = cnt;
    v->top_card        = c;
    v->effective_color = eff;
    st->dirty = 1;

    const char *who = (pid == v->my_id) ? "You"
                                        : v->players[pid].name;
    ui_event_played(who, c, eff);
}

static void on_drew(ClientState *st, const uint8_t *p, uint16_t len)
{
    if (len < 2) return;
    GameView *v = &st->view;
    int pid   = p[0];
    int count = p[1];
    if (pid >= 0 && pid < MAX_PLAYERS)
        v->players[pid].hand_count += count;
    st->dirty = 1;

    const char *who = (pid == v->my_id) ? "You"
                                        : v->players[pid].name;
    char msg[128];
    snprintf(msg, sizeof(msg), "%s drew %d card%s.",
             who, count, count == 1 ? "" : "s");
    ui_event("[Draw]", FG_BLUE, msg);
}

static void on_draw_result(ClientState *st, const uint8_t *p, uint16_t len)
{
    (void)st;
    if (len < 2) return;
    int count = p[0];
    if (len < (uint16_t)(1 + count * 2 + 1)) return;

    Card cards[DECK_SIZE];
    for (int i = 0; i < count; i++) {
        cards[i].color = p[1 + i * 2];
        cards[i].value = p[1 + i * 2 + 1];
    }
    int playable = p[1 + count * 2];
    ui_event_draw_result(cards, count, playable);
}

static void on_notify(const uint8_t *p, uint16_t len)
{
    char msg[MAX_PAYLOAD + 1];
    int n = len > MAX_PAYLOAD ? MAX_PAYLOAD : (int)len;
    memcpy(msg, p, (size_t)n);
    msg[n] = '\0';
    ui_event("[Info]", FG_CYAN, msg);
}

static void on_error(const uint8_t *p, uint16_t len)
{
    char msg[MAX_PAYLOAD + 1];
    int n = len > MAX_PAYLOAD ? MAX_PAYLOAD : (int)len;
    memcpy(msg, p, (size_t)n);
    msg[n] = '\0';
    ui_event("[Error]", FG_RED, msg);
}

static void on_chat(ClientState *st, const uint8_t *p, uint16_t len)
{
    if (len < 1) return;
    GameView *v = &st->view;
    int sender = p[0];
    char msg[MAX_PAYLOAD + 1];
    int n = (int)len - 1;
    if (n > MAX_PAYLOAD) n = MAX_PAYLOAD;
    memcpy(msg, p + 1, (size_t)n);
    msg[n] = '\0';

    const char *who = (sender == v->my_id) ? "you"
                                           : v->players[sender].name;
    char line[MAX_PAYLOAD + 64];
    snprintf(line, sizeof(line), "%s%s%s: %s", BOLD, who, RESET, msg);
    ui_event("[Chat]", FG_WHITE, line);
}

static void on_player_left(ClientState *st, const uint8_t *p, uint16_t len)
{
    if (len < 1) return;
    int id = p[0];
    if (id < 0 || id >= MAX_PLAYERS) return;

    char msg[128];
    snprintf(msg, sizeof(msg), "%s left the game.",
             st->view.players[id].name[0]
                 ? st->view.players[id].name : "A player");
    st->view.players[id].connected = 0;
    st->dirty = 1;
    ui_event("[-]", FG_RED, msg);
}

static void on_game_over(ClientState *st, const uint8_t *p, uint16_t len)
{
    if (len < 1) return;
    GameView *v = &st->view;
    int winner = p[0];
    v->game_over = 1;
    st->dirty = 1;

    if (winner == v->my_id) {
        ui_event_win();
    } else if (winner >= 0 && winner < MAX_PLAYERS
               && v->players[winner].present) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s wins the game!",
                 v->players[winner].name);
        ui_event("[Game Over]", FG_YELLOW, msg);
    } else {
        ui_event("[Game Over]", FG_YELLOW, "Game over.");
    }
}

/* Master dispatch table */
static void dispatch(ClientState *st, uint8_t type,
                     const uint8_t *payload, uint16_t len)
{
    switch (type) {
    case MSG_WELCOME:     on_welcome(st, payload, len);     break;
    case MSG_PLAYER_JOIN: on_player_join(st, payload, len); break;
    case MSG_GAME_BEGIN:  on_game_begin(st, payload, len);  break;
    case MSG_HAND:        on_hand(st, payload, len);        break;
    case MSG_STATE:       on_state(st, payload, len);       break;
    case MSG_TURN:        on_turn(st, payload, len);        break;
    case MSG_PLAYED:      on_played(st, payload, len);      break;
    case MSG_DREW:        on_drew(st, payload, len);        break;
    case MSG_DRAW_RESULT: on_draw_result(st, payload, len); break;
    case MSG_NOTIFY:      on_notify(payload, len);          break;
    case MSG_ERROR:       on_error(payload, len);           break;
    case MSG_GAME_OVER:   on_game_over(st, payload, len);   break;
    case MSG_CHAT_RECV:   on_chat(st, payload, len);        break;
    case MSG_PLAYER_LEFT: on_player_left(st, payload, len); break;
    default: break;
    }
}

/* ═══════════════════════════════════════════════════════════
 *  4. Command Execution
 *
 *  Takes a parsed Command, performs validation that
 *  requires game state, and sends the message to server.
 * ═══════════════════════════════════════════════════════════ */

static void execute_command(ClientState *st, int fd, Command cmd)
{
    GameView *v = &st->view;

    switch (cmd.type) {

    case CMD_NONE:
        ui_prompt();
        return;

    case CMD_INVALID:
        ui_event("[Error]", FG_RED, cmd.error);
        ui_prompt();
        return;

    case CMD_HELP:
        ui_help();
        return;

    case CMD_STATUS:
        st->dirty = 1;
        ui_prompt();
        return;

    case CMD_PLAY: {
        /* Wild card without color?  Catch client-side. */
        if (!cmd.has_color && cmd.card_index < v->hand_size) {
            Card c = v->hand[cmd.card_index];
            if (c.value == CARD_WILD || c.value == CARD_WILD4) {
                ui_event("[Error]", FG_RED,
                    "Wild card needs a color: play <n> red|blue|green|yellow");
                ui_prompt();
                return;
            }
        }
        uint8_t payload[2];
        payload[0] = (uint8_t)cmd.card_index;
        payload[1] = cmd.has_color ? cmd.chosen_color : 255;
        if (send_msg(fd, MSG_PLAY, payload, 2) < 0) {
            perror("send play");
            v->connected = 0;
        }
        break;
    }

    case CMD_DRAW:
        if (send_msg(fd, MSG_DRAW, NULL, 0) < 0) {
            perror("send draw");
            v->connected = 0;
        }
        break;

    case CMD_PASS:
        if (send_msg(fd, MSG_PASS, NULL, 0) < 0) {
            perror("send pass");
            v->connected = 0;
        }
        break;

    case CMD_UNO:
        if (send_msg(fd, MSG_UNO, NULL, 0) < 0) {
            perror("send uno");
            v->connected = 0;
        }
        break;

    case CMD_CALLOUT: {
        int target = find_player_by_name(st, cmd.arg);
        if (target < 0) {
            ui_event("[Error]", FG_RED, "Unknown player name.");
            ui_prompt();
            return;
        }
        uint8_t payload[1] = { (uint8_t)target };
        if (send_msg(fd, MSG_CALLOUT, payload, 1) < 0) {
            perror("send callout");
            v->connected = 0;
        }
        break;
    }

    case CMD_CHAT: {
        uint16_t slen = (uint16_t)strlen(cmd.arg);
        if (send_msg(fd, MSG_CHAT_SEND, cmd.arg, slen) < 0) {
            perror("send chat");
            v->connected = 0;
        }
        break;
    }

    case CMD_START:
        if (send_msg(fd, MSG_START, NULL, 0) < 0) {
            perror("send start");
            v->connected = 0;
        }
        break;
    }

    ui_prompt();
}

/* ═══════════════════════════════════════════════════════════
 *  5. Main Event Loop
 * ═══════════════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <host> <port> <name>\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port         = atoi(argv[2]);
    const char *name = argv[3];

    if ((int)strlen(name) > MAX_NAME) {
        fprintf(stderr, "Name too long (max %d characters).\n", MAX_NAME);
        return 1;
    }

    int fd = connect_to_server(host, port);
    if (fd < 0) { perror("connect"); return 1; }

    ClientState st;
    reset_state(&st);

    if (send_msg(fd, MSG_JOIN, name, (uint16_t)strlen(name)) < 0) {
        perror("send join");
        close(fd);
        return 1;
    }

    ui_banner();

    while (st.view.connected) {
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(STDIN_FILENO, &rset);
        FD_SET(fd, &rset);
        int maxfd = (fd > STDIN_FILENO) ? fd : STDIN_FILENO;

        if (select(maxfd + 1, &rset, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        /* ── Network data from server ────────────── */
        if (FD_ISSET(fd, &rset)) {
            int space = NET_BUF_SIZE - st.netbuf_len;
            if (space <= 0) {
                fprintf(stderr, "Network buffer overflow.\n");
                break;
            }
            ssize_t n = read(fd, st.netbuf + st.netbuf_len, (size_t)space);
            if (n <= 0) {
                ui_event("[System]", FG_RED, "Disconnected from server.");
                break;
            }
            st.netbuf_len += (int)n;

            uint8_t type;
            uint8_t payload[MAX_PAYLOAD];
            uint16_t pay_len;
            while (recv_msg(st.netbuf, &st.netbuf_len,
                            &type, payload, &pay_len))
                dispatch(&st, type, payload, pay_len);
        }

        /* ── User keyboard input ─────────────────── */
        if (FD_ISSET(STDIN_FILENO, &rset)) {
            char line[1024];
            if (!fgets(line, sizeof(line), stdin)) {
                ui_event("[System]", FG_CYAN, "Input closed. Exiting.");
                break;
            }
            Command cmd = parse_command(line);       /* input.c */
            execute_command(&st, fd, cmd);           /* → send  */
        }

        /* ── Re-render if state changed ──────────── */
        if (st.dirty) {
            ui_render(&st.view);                     /* ui.c    */
            st.dirty = 0;
        }
    }

    close(fd);
    return 0;
}
