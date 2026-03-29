/*
 * client.c  --  UNO Game Client (Terminal UI)
 *
 * Usage:  ./client <host> <port> <name>
 *
 * Features ANSI-colored card rendering, box-drawing UI,
 * and a polished terminal experience.
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

/* ═══════════════════════════════════════════════════════════
 *  ANSI Color Codes
 * ═══════════════════════════════════════════════════════════ */

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

/* ═══════════════════════════════════════════════════════════
 *  Client State
 * ═══════════════════════════════════════════════════════════ */

typedef struct {
    int id;
    int present;
    int connected;
    char name[MAX_NAME + 1];
    int hand_count;
    int said_uno;
} PlayerInfo;

typedef struct {
    int my_id;
    int connected;
    int game_started;
    int game_over;
    int dirty;

    int current_player;
    int direction;          /* 1 = CW, -1 = CCW */
    Card top_card;
    uint8_t effective_color;

    Card hand[DECK_SIZE];
    int hand_size;

    PlayerInfo players[MAX_PLAYERS];

    /* Display order: player IDs in seat order (as sent by server) */
    int display_order[MAX_PLAYERS];
    int display_count;

    uint8_t netbuf[NET_BUF_SIZE];
    int netbuf_len;
} ClientState;

/* ═══════════════════════════════════════════════════════════
 *  Utility Helpers
 * ═══════════════════════════════════════════════════════════ */

static void trim_newline(char *s)
{
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

static int equals_ignore_case(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int parse_color(const char *s, uint8_t *out)
{
    if (!s || !out) return -1;
    if (equals_ignore_case(s, "red")    || equals_ignore_case(s, "r")) { *out = COLOR_RED; return 0; }
    if (equals_ignore_case(s, "blue")   || equals_ignore_case(s, "b")) { *out = COLOR_BLUE; return 0; }
    if (equals_ignore_case(s, "green")  || equals_ignore_case(s, "g")) { *out = COLOR_GREEN; return 0; }
    if (equals_ignore_case(s, "yellow") || equals_ignore_case(s, "y")) { *out = COLOR_YELLOW; return 0; }
    return -1;
}

/* ═══════════════════════════════════════════════════════════
 *  Card Rendering with ANSI Colors
 * ═══════════════════════════════════════════════════════════ */

static const char *card_bg(uint8_t color)
{
    switch (color) {
    case COLOR_RED:    return BG_RED;
    case COLOR_BLUE:   return BG_BLUE;
    case COLOR_GREEN:  return BG_GREEN;
    case COLOR_YELLOW: return BG_YELLOW;
    case COLOR_WILD:   return BG_WILD;
    default:           return "";
    }
}

static const char *color_fg(uint8_t color)
{
    switch (color) {
    case COLOR_RED:    return FG_RED;
    case COLOR_BLUE:   return FG_BLUE;
    case COLOR_GREEN:  return FG_GREEN;
    case COLOR_YELLOW: return FG_YELLOW;
    case COLOR_WILD:   return FG_MAGENTA;
    default:           return "";
    }
}

/* Print a colored card inline: e.g. [Red 5] with background color */
static void print_card(Card c)
{
    const char *bg = card_bg(c.color);
    if (c.value == CARD_WILD || c.value == CARD_WILD4) {
        printf("%s %s %s", BG_WILD, value_name(c.value), RESET);
    } else {
        printf("%s %s %s %s", bg, color_name(c.color), short_value(c.value), RESET);
    }
}

/* Print card to a string buffer (no ANSI, for logging) */
static void card_to_str(Card c, char *buf, size_t n)
{
    if (c.value == CARD_WILD || c.value == CARD_WILD4)
        snprintf(buf, n, "%s", value_name(c.value));
    else
        snprintf(buf, n, "%s %s", color_name(c.color), value_name(c.value));
}

/* Print the effective color indicator dot */
static void print_color_dot(uint8_t color)
{
    printf("%s %s %s", card_bg(color), color_name(color), RESET);
}

/* ═══════════════════════════════════════════════════════════
 *  Event Output
 * ═══════════════════════════════════════════════════════════ */

static void print_event(const char *tag, const char *tag_color, const char *msg)
{
    printf("\r%s%s%s %s\n", tag_color, tag, RESET, msg);
    fflush(stdout);
}

static void print_prompt(void)
{
    printf("%s>%s ", FG_CYAN, RESET);
    fflush(stdout);
}

/* ═══════════════════════════════════════════════════════════
 *  Main UI Render
 * ═══════════════════════════════════════════════════════════ */

static void render_status(const ClientState *st)
{
    if (!st->connected) return;

    /* Header */
    printf("\n%s╔══════════════════════════════════════════════╗%s\n", FG_CYAN, RESET);
    printf("%s║%s %s U  N  O%s                                   %s║%s\n",
           FG_CYAN, RESET, BOLD, RESET, FG_CYAN, RESET);
    printf("%s╚══════════════════════════════════════════════╝%s\n", FG_CYAN, RESET);

    if (!st->game_started) {
        printf("\n  %sWaiting for players to join...%s\n", DIM, RESET);
        printf("  Players in lobby:\n");
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!st->players[i].present) continue;
            if (i == st->my_id)
                printf("    %s* %s (you)%s\n", FG_GREEN, st->players[i].name, RESET);
            else
                printf("    %s* %s%s\n", FG_WHITE, st->players[i].name, RESET);
        }
        printf("\n  Type %sstart%s to begin (if enough players), or wait.\n", BOLD, RESET);
        print_prompt();
        return;
    }

    if (st->game_over) {
        printf("\n  %s*** GAME OVER ***%s\n\n", BOLD, RESET);
        print_prompt();
        return;
    }

    /* ── Top Card & Color ────────────────────────── */
    printf("\n  %sTop Card:%s  ", DIM, RESET);
    print_card(st->top_card);
    if (st->top_card.value == CARD_WILD || st->top_card.value == CARD_WILD4) {
        printf("  %sColor:%s ", DIM, RESET);
        print_color_dot(st->effective_color);
    }
    printf("\n");

    /* ── Direction ────────────────────────────────── */
    printf("  %sDirection:%s %s\n", DIM, RESET,
           (st->direction == 1) ? "--> Clockwise" : "<-- Counter-clockwise");

    /* ── Players (in seat / join order) ──────────── */
    printf("\n  %s┌─ Players ──────────────────────────────────┐%s\n", FG_GRAY, RESET);
    for (int d = 0; d < st->display_count; d++) {
        int i = st->display_order[d];
        if (!st->players[i].present) continue;
        const PlayerInfo *p = &st->players[i];

        int is_current = (i == st->current_player);
        int is_me = (i == st->my_id);

        printf("  %s│%s ", FG_GRAY, RESET);

        /* Turn indicator */
        if (is_current)
            printf("%s>>%s ", FG_YELLOW, RESET);
        else
            printf("   ");

        /* Player name */
        if (is_me)
            printf("%s%s%s (you)", FG_GREEN, p->name, RESET);
        else if (!p->connected)
            printf("%s%s (left)%s", FG_GRAY, p->name, RESET);
        else
            printf("%s", p->name);

        /* Card count */
        printf("  %s[%d card%s]%s", DIM, p->hand_count,
               p->hand_count == 1 ? "" : "s", RESET);

        /* UNO indicator */
        if (p->said_uno)
            printf("  %s%sUNO!%s", BOLD, FG_RED, RESET);

        printf("\n");
    }
    printf("  %s└────────────────────────────────────────────┘%s\n", FG_GRAY, RESET);

    /* ── Your Hand ───────────────────────────────── */
    printf("\n  %s┌─ Your Hand ────────────────────────────────┐%s\n", FG_GRAY, RESET);
    if (st->hand_size == 0) {
        printf("  %s│%s   %s(empty)%s\n", FG_GRAY, RESET, DIM, RESET);
    } else {
        for (int i = 0; i < st->hand_size; i++) {
            Card c = st->hand[i];
            int playable = 0;

            /* Check if card is playable */
            if (c.value == CARD_WILD || c.value == CARD_WILD4)
                playable = 1;
            else if (c.color == st->effective_color)
                playable = 1;
            else if (c.value == st->top_card.value)
                playable = 1;

            printf("  %s│%s ", FG_GRAY, RESET);

            /* Index */
            if (playable && st->current_player == st->my_id)
                printf("  %s%s%2d%s ", BOLD, FG_GREEN, i, RESET);
            else
                printf("  %s%2d%s ", DIM, i, RESET);

            /* Card with color */
            print_card(c);

            /* Playable hint */
            if (playable && st->current_player == st->my_id)
                printf(" %s<-%s", FG_GREEN, RESET);

            printf("\n");
        }
    }
    printf("  %s└────────────────────────────────────────────┘%s\n", FG_GRAY, RESET);

    /* ── Turn prompt ─────────────────────────────── */
    if (st->current_player == st->my_id) {
        printf("\n  %s%s*** YOUR TURN ***%s  play <n> [color] | draw | pass | uno\n",
               BOLD, FG_YELLOW, RESET);
    } else {
        printf("\n  %sWaiting for %s...%s  (chat, uno, callout, status)\n",
               DIM,
               (st->current_player >= 0 && st->current_player < MAX_PLAYERS
                && st->players[st->current_player].present)
                   ? st->players[st->current_player].name : "...",
               RESET);
    }

    print_prompt();
}

/* ═══════════════════════════════════════════════════════════
 *  State Management
 * ═══════════════════════════════════════════════════════════ */

static void reset_state(ClientState *st)
{
    memset(st, 0, sizeof(*st));
    st->my_id = -1;
    st->current_player = -1;
    st->direction = 1;
    st->effective_color = COLOR_RED;
    st->connected = 1;
}

static int connect_to_server(const char *host, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

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

static int find_player_by_name(const ClientState *st, const char *name)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!st->players[i].present || !st->players[i].connected) continue;
        if (equals_ignore_case(st->players[i].name, name)) return i;
    }
    return -1;
}

static void update_player_join(ClientState *st, int id, const char *name)
{
    if (id < 0 || id >= MAX_PLAYERS) return;
    st->players[id].id = id;
    st->players[id].present = 1;
    st->players[id].connected = 1;
    strncpy(st->players[id].name, name, MAX_NAME);
    st->players[id].name[MAX_NAME] = '\0';
}

/* ═══════════════════════════════════════════════════════════
 *  Message Handlers
 * ═══════════════════════════════════════════════════════════ */

static void handle_welcome(ClientState *st, const uint8_t *payload, uint16_t len)
{
    if (len < 2) return;
    st->my_id = payload[0];
    st->players[st->my_id].id = st->my_id;
    st->players[st->my_id].present = 1;
    st->players[st->my_id].connected = 1;
    st->dirty = 1;

    char msg[128];
    snprintf(msg, sizeof(msg), "Connected as player %d (%d player(s) in lobby).",
             payload[0], payload[1]);
    print_event("[System]", FG_CYAN, msg);
}

static void handle_player_join(ClientState *st, const uint8_t *payload, uint16_t len)
{
    if (len < 1) return;
    int id = payload[0];
    char name[MAX_NAME + 1];
    int nlen = (int)len - 1;
    if (nlen > MAX_NAME) nlen = MAX_NAME;
    memcpy(name, payload + 1, (size_t)nlen);
    name[nlen] = '\0';

    update_player_join(st, id, name);

    char msg[128];
    snprintf(msg, sizeof(msg), "%s%s%s joined the game.", BOLD, name, RESET);
    print_event("[+]", FG_GREEN, msg);
    st->dirty = 1;
}

static void handle_game_begin(ClientState *st, const uint8_t *payload, uint16_t len)
{
    if (len < 4) return;
    st->game_started = 1;
    st->top_card.color = payload[1];
    st->top_card.value = payload[2];
    st->effective_color = payload[3];
    st->dirty = 1;
    print_event("[System]", FG_CYAN, "Game started! Let's go!");
}

static void handle_hand(ClientState *st, const uint8_t *payload, uint16_t len)
{
    if (len < 1) return;
    int count = payload[0];
    if (len < (uint16_t)(1 + count * 2)) return;
    st->hand_size = count;
    for (int i = 0; i < count; i++) {
        st->hand[i].color = payload[1 + i * 2];
        st->hand[i].value = payload[1 + i * 2 + 1];
    }
    if (st->my_id >= 0 && st->my_id < MAX_PLAYERS)
        st->players[st->my_id].hand_count = count;
    st->dirty = 1;
}

static void handle_state(ClientState *st, const uint8_t *payload, uint16_t len)
{
    int off = 0;
    if (len < 1) return;

    int active = payload[off++];

    for (int i = 0; i < MAX_PLAYERS; i++) {
        st->players[i].present = 0;
        st->players[i].connected = 0;
    }
    st->display_count = 0;

    for (int i = 0; i < active; i++) {
        if (off + 4 > len) return;
        int id = payload[off++];
        int name_len = payload[off++];
        if (id < 0 || id >= MAX_PLAYERS) return;
        if (off + name_len + 2 > len) return;

        st->players[id].id = id;
        st->players[id].present = 1;
        st->players[id].connected = 1;

        /* Record display order (server sends in seat/join order) */
        st->display_order[st->display_count++] = id;

        int copy_len = name_len > MAX_NAME ? MAX_NAME : name_len;
        memcpy(st->players[id].name, payload + off, (size_t)copy_len);
        st->players[id].name[copy_len] = '\0';
        off += name_len;

        st->players[id].hand_count = payload[off++];
        st->players[id].said_uno = payload[off++];
    }

    if (off + 5 > len) return;
    st->current_player = payload[off++];
    st->direction = (payload[off++] == 0) ? 1 : -1;
    st->top_card.color = payload[off++];
    st->top_card.value = payload[off++];
    st->effective_color = payload[off++];
    st->game_started = 1;
    st->dirty = 1;
}

static void handle_turn(ClientState *st, const uint8_t *payload, uint16_t len)
{
    if (len < 1) return;
    st->current_player = payload[0];
    st->dirty = 1;

    if (st->current_player == st->my_id)
        print_event("[Turn]", FG_YELLOW, "It's YOUR turn!");
    else if (st->current_player >= 0 && st->current_player < MAX_PLAYERS
             && st->players[st->current_player].present) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s's turn.", st->players[st->current_player].name);
        print_event("[Turn]", FG_GRAY, msg);
    }
}

static void handle_played(ClientState *st, const uint8_t *payload, uint16_t len)
{
    if (len < 5) return;
    int pid = payload[0];
    Card c = { payload[1], payload[2] };
    uint8_t eff = payload[3];
    int new_count = payload[4];

    if (pid >= 0 && pid < MAX_PLAYERS)
        st->players[pid].hand_count = new_count;
    st->top_card = c;
    st->effective_color = eff;
    st->dirty = 1;

    char cardbuf[64];
    card_to_str(c, cardbuf, sizeof(cardbuf));
    const char *who = (pid == st->my_id) ? "You" : st->players[pid].name;

    /* Print with inline colored card */
    printf("\r%s[Play]%s %s played ", FG_MAGENTA, RESET, who);
    print_card(c);
    if (c.value == CARD_WILD || c.value == CARD_WILD4) {
        printf(" -> ");
        print_color_dot(eff);
    }
    printf("\n");
    fflush(stdout);
}

static void handle_drew(ClientState *st, const uint8_t *payload, uint16_t len)
{
    if (len < 2) return;
    int pid = payload[0];
    int count = payload[1];
    if (pid >= 0 && pid < MAX_PLAYERS)
        st->players[pid].hand_count += count;
    st->dirty = 1;

    char msg[128];
    const char *who = (pid == st->my_id) ? "You" : st->players[pid].name;
    snprintf(msg, sizeof(msg), "%s drew %d card%s.", who, count, count == 1 ? "" : "s");
    print_event("[Draw]", FG_BLUE, msg);
}

static void handle_draw_result(ClientState *st, const uint8_t *payload, uint16_t len)
{
    (void)st;
    if (len < 2) return;
    int count = payload[0];
    if (len < (uint16_t)(1 + count * 2 + 1)) return;

    printf("\r%s[Draw]%s You drew: ", FG_BLUE, RESET);
    for (int i = 0; i < count; i++) {
        Card c = { payload[1 + i * 2], payload[1 + i * 2 + 1] };
        if (i > 0) printf(" ");
        print_card(c);
    }

    uint8_t playable = payload[1 + count * 2];
    if (playable)
        printf("  %s(playable! use: play <index> or pass)%s", FG_GREEN, RESET);
    else
        printf("  %s(not playable, type: pass)%s", FG_GRAY, RESET);
    printf("\n");
    fflush(stdout);
}

static void handle_notify(const uint8_t *payload, uint16_t len)
{
    char msg[MAX_PAYLOAD + 1];
    int n = len > MAX_PAYLOAD ? MAX_PAYLOAD : (int)len;
    memcpy(msg, payload, (size_t)n);
    msg[n] = '\0';
    print_event("[Info]", FG_CYAN, msg);
}

static void handle_error_msg(const uint8_t *payload, uint16_t len)
{
    char msg[MAX_PAYLOAD + 1];
    int n = len > MAX_PAYLOAD ? MAX_PAYLOAD : (int)len;
    memcpy(msg, payload, (size_t)n);
    msg[n] = '\0';
    print_event("[Error]", FG_RED, msg);
}

static void handle_chat_recv(ClientState *st, const uint8_t *payload, uint16_t len)
{
    if (len < 1) return;
    int sender = payload[0];
    char msg[MAX_PAYLOAD + 1];
    int n = (int)len - 1;
    if (n > MAX_PAYLOAD) n = MAX_PAYLOAD;
    memcpy(msg, payload + 1, (size_t)n);
    msg[n] = '\0';

    const char *who = (sender == st->my_id) ? "you" : st->players[sender].name;
    char line[MAX_PAYLOAD + 64];
    snprintf(line, sizeof(line), "%s%s%s: %s", BOLD, who, RESET, msg);
    print_event("[Chat]", FG_WHITE, line);
}

static void handle_player_left(ClientState *st, const uint8_t *payload, uint16_t len)
{
    if (len < 1) return;
    int id = payload[0];
    if (id < 0 || id >= MAX_PLAYERS) return;

    char msg[128];
    snprintf(msg, sizeof(msg), "%s left the game.",
             st->players[id].name[0] ? st->players[id].name : "A player");
    st->players[id].connected = 0;
    st->dirty = 1;
    print_event("[-]", FG_RED, msg);
}

static void handle_game_over(ClientState *st, const uint8_t *payload, uint16_t len)
{
    if (len < 1) return;
    int winner = payload[0];
    st->game_over = 1;
    st->dirty = 1;

    if (winner == st->my_id) {
        printf("\n");
        printf("  %s%s", BOLD, FG_YELLOW);
        printf("  ╔═══════════════════════════════╗\n");
        printf("  ║     YOU WIN!  🏆              ║\n");
        printf("  ╚═══════════════════════════════╝\n");
        printf("  %s\n", RESET);
    } else if (winner >= 0 && winner < MAX_PLAYERS && st->players[winner].present) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s wins the game!", st->players[winner].name);
        print_event("[Game Over]", FG_YELLOW, msg);
    } else {
        print_event("[Game Over]", FG_YELLOW, "Game over.");
    }
}

static void dispatch_message(ClientState *st, uint8_t type,
                             const uint8_t *payload, uint16_t len)
{
    switch (type) {
    case MSG_WELCOME:     handle_welcome(st, payload, len); break;
    case MSG_PLAYER_JOIN: handle_player_join(st, payload, len); break;
    case MSG_GAME_BEGIN:  handle_game_begin(st, payload, len); break;
    case MSG_HAND:        handle_hand(st, payload, len); break;
    case MSG_STATE:       handle_state(st, payload, len); break;
    case MSG_TURN:        handle_turn(st, payload, len); break;
    case MSG_PLAYED:      handle_played(st, payload, len); break;
    case MSG_DREW:        handle_drew(st, payload, len); break;
    case MSG_DRAW_RESULT: handle_draw_result(st, payload, len); break;
    case MSG_NOTIFY:      handle_notify(payload, len); break;
    case MSG_ERROR:       handle_error_msg(payload, len); break;
    case MSG_GAME_OVER:   handle_game_over(st, payload, len); break;
    case MSG_CHAT_RECV:   handle_chat_recv(st, payload, len); break;
    case MSG_PLAYER_LEFT: handle_player_left(st, payload, len); break;
    default: break;
    }
}

/* ═══════════════════════════════════════════════════════════
 *  User Input Handling
 * ═══════════════════════════════════════════════════════════ */

static void print_help(void)
{
    printf("\n");
    printf("  %s┌─ Commands ─────────────────────────────────┐%s\n", FG_GRAY, RESET);
    printf("  %s│%s  %splay <n>%s          play card at index n    %s│%s\n",
           FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
    printf("  %s│%s  %splay <n> <color>%s  play Wild, choose color %s│%s\n",
           FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
    printf("  %s│%s  %sdraw%s              draw a card             %s│%s\n",
           FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
    printf("  %s│%s  %spass%s              pass after drawing      %s│%s\n",
           FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
    printf("  %s│%s  %suno%s               declare UNO             %s│%s\n",
           FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
    printf("  %s│%s  %scallout <name>%s    catch someone's UNO     %s│%s\n",
           FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
    printf("  %s│%s  %schat <msg>%s        send a chat message     %s│%s\n",
           FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
    printf("  %s│%s  %sstatus%s            refresh game display    %s│%s\n",
           FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
    printf("  %s│%s  %sstart%s             request game start      %s│%s\n",
           FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
    printf("  %s│%s  %shelp%s              show this help          %s│%s\n",
           FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
    printf("  %s│%s                                            %s│%s\n",
           FG_GRAY, RESET, FG_GRAY, RESET);
    printf("  %s│%s  %sColors:%s r(ed) b(lue) g(reen) y(ellow)    %s│%s\n",
           FG_GRAY, RESET, DIM, RESET, FG_GRAY, RESET);
    printf("  %s└────────────────────────────────────────────┘%s\n", FG_GRAY, RESET);
    print_prompt();
}

static void handle_user_command(ClientState *st, int fd, char *line)
{
    trim_newline(line);
    while (*line && isspace((unsigned char)*line)) line++;
    if (*line == '\0') { print_prompt(); return; }

    char *cmd = strtok(line, " \t");
    if (!cmd) { print_prompt(); return; }

    if (equals_ignore_case(cmd, "play")) {
        char *idxs = strtok(NULL, " \t");
        char *colors = strtok(NULL, " \t");
        if (!idxs) {
            print_event("[Error]", FG_RED, "Usage: play <index> [color]");
            print_prompt();
            return;
        }

        long idx = strtol(idxs, NULL, 10);
        if (idx < 0 || idx > 255) {
            print_event("[Error]", FG_RED, "Invalid index.");
            print_prompt();
            return;
        }

        uint8_t payload[2];
        payload[0] = (uint8_t)idx;
        payload[1] = 255;

        /* Auto-prompt for color if playing a wild card without specifying */
        if (!colors && idx < st->hand_size) {
            Card c = st->hand[idx];
            if (c.value == CARD_WILD || c.value == CARD_WILD4) {
                print_event("[Error]", FG_RED,
                    "Wild card needs a color: play <n> red|blue|green|yellow");
                print_prompt();
                return;
            }
        }

        if (colors) {
            if (parse_color(colors, &payload[1]) < 0) {
                print_event("[Error]", FG_RED,
                    "Color must be: red(r), blue(b), green(g), or yellow(y).");
                print_prompt();
                return;
            }
        }

        if (send_msg(fd, MSG_PLAY, payload, 2) < 0) {
            perror("send play");
            st->connected = 0;
        }
    } else if (equals_ignore_case(cmd, "draw")) {
        if (send_msg(fd, MSG_DRAW, NULL, 0) < 0) {
            perror("send draw");
            st->connected = 0;
        }
    } else if (equals_ignore_case(cmd, "pass")) {
        if (send_msg(fd, MSG_PASS, NULL, 0) < 0) {
            perror("send pass");
            st->connected = 0;
        }
    } else if (equals_ignore_case(cmd, "uno")) {
        if (send_msg(fd, MSG_UNO, NULL, 0) < 0) {
            perror("send uno");
            st->connected = 0;
        }
    } else if (equals_ignore_case(cmd, "callout")) {
        char *name = strtok(NULL, "");
        if (!name) {
            print_event("[Error]", FG_RED, "Usage: callout <name>");
            print_prompt();
            return;
        }
        while (*name && isspace((unsigned char)*name)) name++;
        int target = find_player_by_name(st, name);
        if (target < 0) {
            print_event("[Error]", FG_RED, "Unknown player name.");
            print_prompt();
            return;
        }
        uint8_t payload[1] = { (uint8_t)target };
        if (send_msg(fd, MSG_CALLOUT, payload, 1) < 0) {
            perror("send callout");
            st->connected = 0;
        }
    } else if (equals_ignore_case(cmd, "chat")) {
        char *msg = strtok(NULL, "");
        if (!msg) {
            print_event("[Error]", FG_RED, "Usage: chat <message>");
            print_prompt();
            return;
        }
        while (*msg && isspace((unsigned char)*msg)) msg++;
        if (send_msg(fd, MSG_CHAT_SEND, msg, (uint16_t)strlen(msg)) < 0) {
            perror("send chat");
            st->connected = 0;
        }
    } else if (equals_ignore_case(cmd, "status")) {
        st->dirty = 1;
    } else if (equals_ignore_case(cmd, "help") || equals_ignore_case(cmd, "h")) {
        print_help();
        return;
    } else if (equals_ignore_case(cmd, "start")) {
        if (send_msg(fd, MSG_START, NULL, 0) < 0) {
            perror("send start");
            st->connected = 0;
        }
    } else {
        print_event("[Error]", FG_RED, "Unknown command. Type 'help' for commands.");
    }

    print_prompt();
}

/* ═══════════════════════════════════════════════════════════
 *  Welcome Banner
 * ═══════════════════════════════════════════════════════════ */

static void print_banner(void)
{
    printf("\n");
    printf("  %s%s", BOLD, FG_RED);
    printf("   _   _ _   _  ___  \n");
    printf("  | | | | \\ | |/ _ \\ \n");
    printf("  | | | |  \\| | | | |\n");
    printf("  | |_| | |\\  | |_| |\n");
    printf("   \\___/|_| \\_|\\___/ \n");
    printf("  %s\n", RESET);
    printf("  %sMultiplayer Card Game%s\n", DIM, RESET);
    printf("  %sType 'help' for commands%s\n\n", DIM, RESET);
}

/* ═══════════════════════════════════════════════════════════
 *  Main
 * ═══════════════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <host> <port> <name>\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);
    const char *name = argv[3];

    if ((int)strlen(name) > MAX_NAME) {
        fprintf(stderr, "Name too long (max %d characters).\n", MAX_NAME);
        return 1;
    }

    int fd = connect_to_server(host, port);
    if (fd < 0) {
        perror("connect");
        return 1;
    }

    ClientState st;
    reset_state(&st);

    if (send_msg(fd, MSG_JOIN, name, (uint16_t)strlen(name)) < 0) {
        perror("send join");
        close(fd);
        return 1;
    }

    print_banner();

    while (st.connected) {
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

        if (FD_ISSET(fd, &rset)) {
            int space = NET_BUF_SIZE - st.netbuf_len;
            if (space <= 0) {
                fprintf(stderr, "Network buffer overflow.\n");
                break;
            }

            ssize_t n = read(fd, st.netbuf + st.netbuf_len, (size_t)space);
            if (n <= 0) {
                print_event("[System]", FG_RED, "Disconnected from server.");
                break;
            }
            st.netbuf_len += (int)n;

            uint8_t type;
            uint8_t payload[MAX_PAYLOAD];
            uint16_t pay_len;
            while (recv_msg(st.netbuf, &st.netbuf_len, &type, payload, &pay_len))
                dispatch_message(&st, type, payload, pay_len);
        }

        if (FD_ISSET(STDIN_FILENO, &rset)) {
            char line[1024];
            if (!fgets(line, sizeof(line), stdin)) {
                print_event("[System]", FG_CYAN, "Input closed. Exiting.");
                break;
            }
            handle_user_command(&st, fd, line);
        }

        if (st.dirty) {
            render_status(&st);
            st.dirty = 0;
        }
    }

    close(fd);
    return 0;
}
