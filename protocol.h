/*
 * protocol.h  --  UNO Game Network Protocol
 *
 * Wire format:  [type : 1 byte][length : 2 bytes BE][payload : length bytes]
 *
 * Shared between server and client.
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <unistd.h>

/* ═══════════════════════════════════════════════════════════
 *  Card Definitions
 * ═══════════════════════════════════════════════════════════ */

#define COLOR_RED     0
#define COLOR_BLUE    1
#define COLOR_GREEN   2
#define COLOR_YELLOW  3
#define COLOR_WILD    4
#define NUM_COLORS    4

#define CARD_0         0
#define CARD_1         1
#define CARD_2         2
#define CARD_3         3
#define CARD_4         4
#define CARD_5         5
#define CARD_6         6
#define CARD_7         7
#define CARD_8         8
#define CARD_9         9
#define CARD_SKIP     10
#define CARD_REVERSE  11
#define CARD_DRAW2    12
#define CARD_WILD     13
#define CARD_WILD4    14

typedef struct {
    uint8_t color;
    uint8_t value;
} Card;

/* ═══════════════════════════════════════════════════════════
 *  Message Types
 * ═══════════════════════════════════════════════════════════ */

/* --- Client -> Server ----------------------------------- */
#define MSG_JOIN        0x01   /* name (string)                       */
#define MSG_PLAY        0x02   /* index(1) + chosen_color(1)          */
#define MSG_DRAW        0x03   /* (empty)                             */
#define MSG_PASS        0x04   /* (empty)                             */
#define MSG_UNO         0x05   /* (empty)                             */
#define MSG_CALLOUT     0x06   /* target_id(1)                        */
#define MSG_CHAT_SEND   0x07   /* text (string)                       */
#define MSG_START       0x08   /* (empty) — request game start        */

/* --- Server -> Client ----------------------------------- */
#define MSG_WELCOME     0x10   /* your_id(1) + num_players(1)         */
#define MSG_PLAYER_JOIN 0x11   /* id(1) + name (string)               */
#define MSG_GAME_BEGIN  0x12   /* num_players(1)+top_card(2)+color(1)  */
#define MSG_HAND        0x13   /* count(1) + Card[count]              */
#define MSG_STATE       0x14   /* (see state format in docs)          */
#define MSG_TURN        0x15   /* player_id(1)                        */
#define MSG_PLAYED      0x16   /* pid(1)+card(2)+eff_color(1)+cnt(1)  */
#define MSG_DREW        0x17   /* pid(1) + count(1)                   */
#define MSG_DRAW_RESULT 0x18   /* count(1)+Card[count]+playable(1)    */
#define MSG_NOTIFY      0x19   /* text (string)                       */
#define MSG_ERROR       0x1A   /* text (string)                       */
#define MSG_GAME_OVER   0x1B   /* winner_id(1)                        */
#define MSG_CHAT_RECV   0x1C   /* sender_id(1) + text (string)        */
#define MSG_PLAYER_LEFT 0x1D   /* id(1)                               */

/* ═══════════════════════════════════════════════════════════
 *  Constants
 * ═══════════════════════════════════════════════════════════ */

#define MAX_PLAYERS     4
#define MIN_PLAYERS     2
#define INITIAL_HAND    7
#define DECK_SIZE       108
#define MAX_NAME        20
#define DEFAULT_PORT    4242
#define NET_BUF_SIZE    4096
#define MAX_PAYLOAD     1024
#define MSG_HDR_SIZE    3

/*
 * State Payload Format (MSG_STATE):
 *   num_players     : 1 byte
 *   per player (repeated num_players times):
 *       id          : 1 byte
 *       name_len    : 1 byte
 *       name        : name_len bytes
 *       hand_count  : 1 byte
 *       has_uno     : 1 byte
 *   current_player  : 1 byte
 *   direction       : 1 byte (0 = CW, 1 = CCW)
 *   top_card        : 2 bytes (color, value)
 *   effective_color : 1 byte
 */

/* ═══════════════════════════════════════════════════════════
 *  Wire Helpers
 * ═══════════════════════════════════════════════════════════ */

/* Send a complete message.  Returns 0 on success, -1 on error. */
static inline int send_msg(int fd, uint8_t type,
                           const void *payload, uint16_t len)
{
    uint8_t hdr[MSG_HDR_SIZE];
    hdr[0] = type;
    hdr[1] = (len >> 8) & 0xFF;
    hdr[2] = len & 0xFF;

    ssize_t sent = 0;
    while (sent < MSG_HDR_SIZE) {
        ssize_t n = write(fd, hdr + sent, (size_t)(MSG_HDR_SIZE - sent));
        if (n <= 0) return -1;
        sent += n;
    }
    sent = 0;
    while (sent < (ssize_t)len) {
        ssize_t n = write(fd, (const uint8_t *)payload + sent,
                          (size_t)(len - sent));
        if (n <= 0) return -1;
        sent += n;
    }
    return 0;
}

/*
 * Try to extract one complete message from a byte buffer.
 * Returns 1 if a message was consumed, 0 if more data needed.
 */
static inline int recv_msg(uint8_t *buf, int *buf_len,
                           uint8_t *type, uint8_t *payload,
                           uint16_t *pay_len)
{
    if (*buf_len < MSG_HDR_SIZE) return 0;

    *type    = buf[0];
    *pay_len = ((uint16_t)buf[1] << 8) | buf[2];

    if (*pay_len > MAX_PAYLOAD) { *buf_len = 0; return 0; }

    int total = MSG_HDR_SIZE + *pay_len;
    if (*buf_len < total) return 0;

    if (*pay_len > 0)
        memcpy(payload, buf + MSG_HDR_SIZE, *pay_len);

    memmove(buf, buf + total, (size_t)(*buf_len - total));
    *buf_len -= total;
    return 1;
}

/* ═══════════════════════════════════════════════════════════
 *  Card-Name Helpers
 * ═══════════════════════════════════════════════════════════ */

static inline const char *color_name(uint8_t c)
{
    switch (c) {
    case COLOR_RED:    return "Red";
    case COLOR_BLUE:   return "Blue";
    case COLOR_GREEN:  return "Green";
    case COLOR_YELLOW: return "Yellow";
    case COLOR_WILD:   return "Wild";
    default:           return "???";
    }
}

static inline const char *value_name(uint8_t v)
{
    static const char *t[] = {
        "0","1","2","3","4","5","6","7","8","9",
        "Skip","Reverse","+2","Wild","Wild+4"
    };
    return (v <= CARD_WILD4) ? t[v] : "???";
}

static inline const char *short_value(uint8_t v)
{
    static const char *t[] = {
        " 0"," 1"," 2"," 3"," 4"," 5"," 6"," 7"," 8"," 9",
        "Sk","Re","+2"," W","+4"
    };
    return (v <= CARD_WILD4) ? t[v] : "??";
}

static inline char color_letter(uint8_t c)
{
    const char map[] = "RBGYW";
    return (c <= COLOR_WILD) ? map[c] : '?';
}

#endif /* PROTOCOL_H */