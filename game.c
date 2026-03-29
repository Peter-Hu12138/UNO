/*
 * game.c  --  UNO Game Logic
 *
 * Pure state manipulation.  No printf to clients, no sockets.
 * Server-side logging (to stderr) is allowed for debugging.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "game.h"

/* ═══════════════════════════════════════════════════════════
 *  Initialization
 * ═══════════════════════════════════════════════════════════ */

 /* 初始化整个游戏状态结构体：
 * - 清零所有字段
 * - 记录预计玩家人数
 * - 默认方向为顺时针（1）
 * - 把所有玩家的 fd 设为 -1，表示该座位当前没人连接
 */

void game_init(Game *g, int num_expected)
{
    memset(g, 0, sizeof(*g));
    g->num_expected = num_expected;
    g->direction    = 1;
    for (int i = 0; i < MAX_PLAYERS; i++)
        g->players[i].fd = -1;
}

/* ═══════════════════════════════════════════════════════════
 *  Seat Management
 * ═══════════════════════════════════════════════════════════ */

/* 把新玩家加入座位顺序表。
 * seat_order 按加入顺序保存玩家 id，
 * seat_pos 则记录“某个玩家当前在第几个座位”。
 * 这样后面找下一个玩家时，就可以按座位顺序转圈。
 */
void game_seat_add(Game *g, int pid)
{
    g->seat_order[g->seat_count] = pid;
    g->seat_pos[pid]             = g->seat_count;
    g->seat_count++;
}

/* ═══════════════════════════════════════════════════════════
 *  Deck
 * ═══════════════════════════════════════════════════════════ */

/* Fisher-Yates 洗牌算法：
 * 从后往前遍历，每次随机选一个位置交换，
 * 从而把牌堆均匀随机打乱。
 */
static void shuffle_cards(Card *pile, int n)
{
    for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        Card t = pile[i]; pile[i] = pile[j]; pile[j] = t;
    }
}

/* 构造一整副 UNO 牌并洗牌：
 * - 四种颜色各有 1 张 0
 * - 1 到 Draw2 每种牌各有 2 张
 * - Wild / Wild+4 各有 4 张
 * 最后把抽牌指针 draw_top 设到最前面，并洗牌。
 */
void game_build_deck(Game *g)
{
    g->draw_count = 0;
    for (int c = 0; c < NUM_COLORS; c++) {
        g->draw_pile[g->draw_count++] = (Card){(uint8_t)c, CARD_0};
        for (int v = CARD_1; v <= CARD_DRAW2; v++) {
            g->draw_pile[g->draw_count++] = (Card){(uint8_t)c, (uint8_t)v};
            g->draw_pile[g->draw_count++] = (Card){(uint8_t)c, (uint8_t)v};
        }
    }
    for (int i = 0; i < 4; i++) {
        g->draw_pile[g->draw_count++] = (Card){COLOR_WILD, CARD_WILD};
        g->draw_pile[g->draw_count++] = (Card){COLOR_WILD, CARD_WILD4};
    }
    g->draw_top = 0;
    shuffle_cards(g->draw_pile, g->draw_count);
}

/* 从抽牌堆顶抽一张牌。
 * 如果抽牌堆已经空了，就把弃牌堆（除了最上面那张）
 * 重新洗回抽牌堆继续用。
 */
Card game_draw_card(Game *g)
{
    if (g->draw_top >= g->draw_count) {
        /* Reshuffle discard into draw pile, keep the top discard */
        if (g->discard_count <= 1)
            return (Card){COLOR_WILD, CARD_WILD};   /* emergency card */
        Card save = g->discard[g->discard_count - 1];
        g->draw_count = g->discard_count - 1;
        memcpy(g->draw_pile, g->discard,
               (size_t)g->draw_count * sizeof(Card));
        g->discard_count = 1;
        g->discard[0] = save;
        g->draw_top = 0;
        shuffle_cards(g->draw_pile, g->draw_count);
        fprintf(stderr, "[Game] Reshuffled discard (%d cards).\n",
                g->draw_count);
    }
    return g->draw_pile[g->draw_top++];
}

/* ═══════════════════════════════════════════════════════════
 *  Hand Manipulation
 * ═══════════════════════════════════════════════════════════ */

/* 从某个玩家手牌中移除第 idx 张牌：
 * - 保存被移除的牌
 * - 后面的牌整体前移
 * - 手牌数量减 1
 * 返回被打出的那张牌
 */
Card game_remove_card(Game *g, int pid, int idx)
{
    Player *p = &g->players[pid];
    Card c = p->hand[idx];
    for (int i = idx; i < p->hand_size - 1; i++)
        p->hand[i] = p->hand[i + 1];
    p->hand_size--;
    return c;
}


/* 给指定玩家发 count 张牌。
 * 如果 drawn 不为 NULL，就把发到的牌顺便记录到 drawn 数组中，
 * 方便上层网络代码把“你抽到了什么”发给客户端。
 *
 * 另外，抽牌后要清除 UNO 相关标记，因为玩家不再处于只剩一张牌的状态。
 */
int game_deal_cards(Game *g, int pid, int count, Card *drawn)
{
    Player *p = &g->players[pid];
    int dealt = 0;
    for (int i = 0; i < count; i++) {
        Card c = game_draw_card(g);
        p->hand[p->hand_size++] = c;
        if (drawn) drawn[dealt] = c;
        dealt++;
    }
    /* drawing cards resets UNO flags */
    p->said_uno       = 0;
    p->uno_vulnerable = 0;
    return dealt;
}

/* ═══════════════════════════════════════════════════════════
 *  Rules / Validation
 * ═══════════════════════════════════════════════════════════ */

/* 判断某张牌当前是否可以打出：
 * - Wild / Wild+4 永远可打
 * - 颜色与当前有效颜色相同可打
 * - 数值 / 功能与顶部牌相同可打
 */
int game_can_play(const Game *g, Card c)
{
    if (c.value == CARD_WILD || c.value == CARD_WILD4) return 1;
    if (c.color == g->top_color)      return 1;
    if (c.value == g->top_card.value) return 1;
    return 0;
}

/* 检查某个玩家手里是否至少有一张可出的牌。
 * 常用于“抽了一张牌之后，是否马上就能出”。
 */
int game_has_playable(const Game *g, int pid)
{
    const Player *p = &g->players[pid];
    for (int i = 0; i < p->hand_size; i++)
        if (game_can_play(g, p->hand[i])) return 1;
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 *  Turn Management
 * ═══════════════════════════════════════════════════════════ */

 /* 从当前玩家开始，按当前方向寻找下一个仍然在线的玩家。
 * 由于可能有人掉线，所以不能简单地 +1 或 -1，
 * 必须沿着 seat_order 找“仍 connected 的玩家”。
 */
int game_next_player(Game *g, int from_pid)
{
    int si = g->seat_pos[from_pid];
    for (int step = 0; step < g->seat_count; step++) {
        si = (si + g->direction + g->seat_count) % g->seat_count;
        int pid = g->seat_order[si];
        if (g->players[pid].connected) {
            g->seat_idx = si;
            return pid;
        }
    }
    return from_pid;
}

/* 推进回合：
 * - 关闭其他玩家的 UNO 可举报窗口
 * - 切到下一个在线玩家
 * - 重置新当前玩家的 has_drawn 标记
 */
void game_advance_turn(Game *g)
{
    /* Close UNO vulnerability window for non-current players */
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (i != g->current) g->players[i].uno_vulnerable = 0;

    g->current = game_next_player(g, g->current);
    g->players[g->current].has_drawn = 0;
}

/* ═══════════════════════════════════════════════════════════
 *  Card Effects
 * ═══════════════════════════════════════════════════════════ */

/* 反转游戏方向：顺时针 <-> 逆时针 */
void game_reverse(Game *g)
{
    g->direction = -(g->direction);
}


/* 根据刚打出的牌，计算它会造成的效果。
 * 这里只“计算效果”，不直接发网络消息。
 *
 * 返回的 CardEffect 包含：
 * - 是否跳过下一位
 * - 是否反转方向
 * - 谁是受影响玩家（victim）
 * - 需要罚抽多少张
 */
CardEffect game_card_effect(Game *g, Card card)
{
    CardEffect e = {0, -1, 0, 0};
    int victim;

    switch (card.value) {
    case CARD_SKIP:
        victim = game_next_player(g, g->current);
        e.skip   = 1;
        e.victim = victim;
        break;
    case CARD_REVERSE:
        e.reversed = 1;
        /* In 2-player, reverse acts as skip */
        if (g->num_connected == 2) e.skip = 1;
        break;
    case CARD_DRAW2:
        victim = game_next_player(g, g->current);
        e.skip         = 1;
        e.victim       = victim;
        e.draw_penalty = 2;
        break;
    case CARD_WILD4:
        victim = game_next_player(g, g->current);
        e.skip         = 1;
        e.victim       = victim;
        e.draw_penalty = 4;
        break;
    default:
        break;
    }
    return e;
}

/* ═══════════════════════════════════════════════════════════
 *  Game Start
 * ═══════════════════════════════════════════════════════════ */

/* 正式开始一局游戏：
 * - 建立牌堆并洗牌
 * - 给每个在线玩家发初始手牌
 * - 翻开第一张顶部牌
 * - 确定当前有效颜色
 * - 设置第一位行动玩家
 * - 如果首张牌本身有特殊效果，也要立即生效
 *
 * 返回首张牌的效果，供上层 server/net 层决定如何通知客户端。
 */
CardEffect game_start(Game *g)
{
    g->started = 1;

    /* Build & shuffle deck */
    game_build_deck(g);

    /* Deal hands */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!g->players[i].connected) continue;
        for (int j = 0; j < INITIAL_HAND; j++)
            g->players[i].hand[j] = game_draw_card(g);
        g->players[i].hand_size = INITIAL_HAND;
    }

    /* Flip first card (skip Wild+4) */
    do {
        g->top_card = game_draw_card(g);
    } while (g->top_card.value == CARD_WILD4);

    g->top_color = g->top_card.color;
    if (g->top_card.value == CARD_WILD)
        g->top_color = (uint8_t)(rand() % NUM_COLORS);
    g->discard[0]    = g->top_card;
    g->discard_count = 1;

    /* First player = seat 0 */
    g->seat_idx = 0;
    g->current  = g->seat_order[0];

    /* Compute first-card effect */
    CardEffect eff = game_card_effect(g, g->top_card);

    /* Apply first-card effects to game state */
    if (eff.reversed)
        game_reverse(g);

    if (eff.draw_penalty > 0)
        game_deal_cards(g, g->current, eff.draw_penalty, NULL);

    if (eff.skip || eff.draw_penalty > 0)
        g->current = game_next_player(g, g->current);

    g->players[g->current].has_drawn = 0;

    return eff;
}
