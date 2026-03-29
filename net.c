/*
 * net.c  --  Server-side Network / Messaging Layer
 *
 * All socket writes live here.  Functions read from Game
 * but do NOT mutate it (except trivially through send_msg
 * side effects on the wire).
 */
#include <string.h>
#include "net.h"

/* ═══════════════════════════════════════════════════════════
 *  Broadcast Helpers
 * ═══════════════════════════════════════════════════════════ */

 /* 向所有在线玩家广播一条消息。
 * type 表示消息类型，p/len 是消息 payload。
 * 例如，net_broadcast(g, MSG_NOTIFY, "Hello", 5) 会给每个在线玩家发一条通知消息，内容是 "Hello"。
 */
void net_broadcast(Game *g, uint8_t type, const void *p, uint16_t len)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (g->players[i].connected)
            send_msg(g->players[i].fd, type, p, len);
}


/* 向除 ex 之外的所有在线玩家广播消息。
 * 常用于“某个玩家已经知道自己的详细结果，
 * 其他人只需要收到简化通知”。
 */
void net_broadcast_except(Game *g, int ex, uint8_t type,
                          const void *p, uint16_t len)
{
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (g->players[i].connected && i != ex)
            send_msg(g->players[i].fd, type, p, len);
}

/* ═══════════════════════════════════════════════════════════
 *  Notification Helpers
 * ═══════════════════════════════════════════════════════════ */
// 向一个玩家发通知消息，内容是 msg 字符串。
void net_notify_one(Game *g, int pid, const char *msg)
{
    send_msg(g->players[pid].fd, MSG_NOTIFY, msg, (uint16_t)strlen(msg));
}

// 向所有在线玩家广播一条通知消息，内容是 msg 字符串。
void net_notify_all(Game *g, const char *msg)
{
    net_broadcast(g, MSG_NOTIFY, msg, (uint16_t)strlen(msg));
}

// 向除 ex 之外的所有在线玩家广播一条通知消息，内容是 msg 字符串。
void net_send_error(Game *g, int pid, const char *msg)
{
    send_msg(g->players[pid].fd, MSG_ERROR, msg, (uint16_t)strlen(msg));
}

/* ═══════════════════════════════════════════════════════════
 *  State Serialization
 * ═══════════════════════════════════════════════════════════ */

 /* 把某个玩家自己的手牌完整发给他。
 * 这是私有信息，不能广播给别人。
 *
 * payload 格式：
 * [hand_size][color,value][color,value]...
 */
void net_send_hand(Game *g, int pid)
{
    Player *p = &g->players[pid];
    uint8_t buf[1 + DECK_SIZE * 2];
    buf[0] = (uint8_t)p->hand_size;
    for (int i = 0; i < p->hand_size; i++) {
        buf[1 + i * 2]     = p->hand[i].color;
        buf[1 + i * 2 + 1] = p->hand[i].value;
    }
    send_msg(p->fd, MSG_HAND, buf, (uint16_t)(1 + p->hand_size * 2));
}


/* 把当前公共游戏状态序列化并发给某个玩家：
 * - 当前有多少在线玩家
 * - 每位玩家的 id / 名字 / 手牌数量 / 是否喊过 UNO
 * - 当前轮到谁
 * - 当前方向
 * - 顶牌和当前有效颜色
 *
 * 注意：这里只发送公共信息，不包含其他人的具体手牌。
 */
void net_send_state(Game *g, int pid)
{
    uint8_t buf[MAX_PAYLOAD];
    int off = 0;

    /* Count active players */
    int active = 0;
    for (int s = 0; s < g->seat_count; s++)
        if (g->players[g->seat_order[s]].connected) active++;

    buf[off++] = (uint8_t)active;

    /* Players in seat order */
    for (int s = 0; s < g->seat_count; s++) {
        int id = g->seat_order[s];
        if (!g->players[id].connected) continue;
        buf[off++] = (uint8_t)id;
        int nlen = (int)strlen(g->players[id].name);
        buf[off++] = (uint8_t)nlen;
        memcpy(buf + off, g->players[id].name, (size_t)nlen);
        off += nlen;
        buf[off++] = (uint8_t)g->players[id].hand_size;
        buf[off++] = (uint8_t)g->players[id].said_uno;
    }

    buf[off++] = (uint8_t)g->current;
    buf[off++] = (g->direction == 1) ? 0 : 1;
    buf[off++] = g->top_card.color;
    buf[off++] = g->top_card.value;
    buf[off++] = g->top_color;

    send_msg(g->players[pid].fd, MSG_STATE, buf, (uint16_t)off);
}

/* 向所有在线玩家刷新状态：
 * - 每个人都收到当前公共状态
 * - 每个人另外收到自己的完整手牌
 */
void net_broadcast_state(Game *g)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!g->players[i].connected) continue;
        net_send_state(g, i);
        net_send_hand(g, i);
    }
}

/* ═══════════════════════════════════════════════════════════
 *  High-Level Game Events
 * ═══════════════════════════════════════════════════════════ */

/* 广播“某位玩家打出了一张牌”事件。
 * 同时把更新后的 top_color 和该玩家剩余手牌数也一起发出，
 * 方便客户端立即更新界面。
 */
void net_broadcast_played(Game *g, int pid, Card card)
{
    uint8_t pb[5] = {
        (uint8_t)pid,
        card.color,
        card.value,
        g->top_color,
        (uint8_t)g->players[pid].hand_size
    };
    net_broadcast(g, MSG_PLAYED, pb, 5);
}

/* 处理抽牌后的双层通知：
 * 1. 给抽牌的玩家发送详细结果（具体抽到了哪些牌、是否有可出的牌）
 * 2. 给其他玩家只发送“某人抽了几张牌”的公共信息
 */
void net_broadcast_drew(Game *g, int pid, const Card *drawn, int count)
{
    /* Send detailed result to the player who drew */
    uint8_t buf[1 + DECK_SIZE * 2 + 1];
    buf[0] = (uint8_t)count;
    for (int i = 0; i < count; i++) {
        buf[1 + i * 2]     = drawn[i].color;
        buf[1 + i * 2 + 1] = drawn[i].value;
    }
    buf[1 + count * 2] = (uint8_t)game_has_playable(g, pid);
    send_msg(g->players[pid].fd, MSG_DRAW_RESULT, buf,
             (uint16_t)(1 + count * 2 + 1));

    /* Tell everyone else how many were drawn */
    uint8_t nb[2] = {(uint8_t)pid, (uint8_t)count};
    net_broadcast_except(g, pid, MSG_DREW, nb, 2);
}

/* 广播当前轮到哪位玩家行动 */
void net_broadcast_turn(Game *g)
{
    uint8_t tb[1] = {(uint8_t)g->current};
    net_broadcast(g, MSG_TURN, tb, 1);
}

/* 广播游戏结束，以及赢家是谁 */
void net_broadcast_game_over(Game *g, int winner_pid)
{
    uint8_t wb[1] = {(uint8_t)winner_pid};
    net_broadcast(g, MSG_GAME_OVER, wb, 1);
}

/* 广播游戏正式开始。
 * 同时把开局时的一些基础状态（玩家数、顶牌、有效颜色）发给客户端。
 */
void net_broadcast_game_begin(Game *g)
{
    uint8_t gb[4] = {
        (uint8_t)g->num_connected,
        g->top_card.color,
        g->top_card.value,
        g->top_color
    };
    net_broadcast(g, MSG_GAME_BEGIN, gb, 4);
}

/* 告诉其他玩家：有新玩家加入了房间 */
void net_broadcast_player_join(Game *g, int pid)
{
    int nlen = (int)strlen(g->players[pid].name);
    uint8_t jb[1 + MAX_NAME];
    jb[0] = (uint8_t)pid;
    memcpy(jb + 1, g->players[pid].name, (size_t)nlen);
    net_broadcast_except(g, pid, MSG_PLAYER_JOIN, jb, (uint16_t)(1 + nlen));
}

/* 给新加入的玩家补发当前已经在房间里的玩家列表，
 * 让新客户端能够正确显示 lobby 里的所有人。
 */
void net_send_existing_players(Game *g, int new_pid)
{
    for (int s = 0; s < g->seat_count; s++) {
        int id = g->seat_order[s];
        if (id == new_pid || !g->players[id].connected) continue;
        uint8_t eb[1 + MAX_NAME];
        eb[0] = (uint8_t)id;
        int elen = (int)strlen(g->players[id].name);
        memcpy(eb + 1, g->players[id].name, (size_t)elen);
        send_msg(g->players[new_pid].fd, MSG_PLAYER_JOIN, eb,
                 (uint16_t)(1 + elen));
    }
}

void net_send_welcome(Game *g, int pid)
{
    uint8_t wb[2] = {(uint8_t)pid, (uint8_t)g->num_connected};
    send_msg(g->players[pid].fd, MSG_WELCOME, wb, 2);
}

void net_broadcast_player_left(Game *g, int pid)
{
    uint8_t lb[1] = {(uint8_t)pid};
    net_broadcast(g, MSG_PLAYER_LEFT, lb, 1);
}

/* 广播聊天消息：
 * payload 的第一个字节是发送者 id，
 * 后面跟聊天文本内容。
 */
void net_broadcast_chat(Game *g, int sender, uint8_t *text, uint16_t len)
{
    uint8_t buf[MAX_PAYLOAD];
    buf[0] = (uint8_t)sender;
    if (len > MAX_PAYLOAD - 1) len = MAX_PAYLOAD - 1;
    memcpy(buf + 1, text, len);
    net_broadcast(g, MSG_CHAT_RECV, buf, (uint16_t)(1 + len));
}
