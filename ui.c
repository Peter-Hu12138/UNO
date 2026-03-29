/*
 * ui.c  --  Terminal UI Rendering for UNO Client
 *
 * All terminal output lives here.  Functions only call
 * printf/fflush — no network, no state mutation.
 */
#include <stdio.h>
#include <string.h>
#include "ui.h"

/* ═══════════════════════════════════════════════════════════
 *  Card Rendering
 * ═══════════════════════════════════════════════════════════ */

const char *ui_card_bg(uint8_t color)
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

void ui_print_card(Card c)
{
    if (c.value == CARD_WILD || c.value == CARD_WILD4) {
        printf("%s %s %s", BG_WILD, value_name(c.value), RESET);
    } else {
        printf("%s %s %s %s",
               ui_card_bg(c.color), color_name(c.color),
               short_value(c.value), RESET);
    }
}

void ui_print_color_dot(uint8_t color)
{
    printf("%s %s %s", ui_card_bg(color), color_name(color), RESET);
}

void ui_card_to_str(Card c, char *buf, int n)
{
    if (c.value == CARD_WILD || c.value == CARD_WILD4)
        snprintf(buf, (size_t)n, "%s", value_name(c.value));
    else
        snprintf(buf, (size_t)n, "%s %s",
                 color_name(c.color), value_name(c.value));
}

/* ═══════════════════════════════════════════════════════════
 *  Event / Status Output
 * ═══════════════════════════════════════════════════════════ */

void ui_event(const char *tag, const char *tag_color, const char *msg)
{
    printf("\r%s%s%s %s\n", tag_color, tag, RESET, msg);
    fflush(stdout);
}

void ui_prompt(void)
{
    printf("%s>%s ", FG_CYAN, RESET);
    fflush(stdout);
}

/* ═══════════════════════════════════════════════════════════
 *  Banner & Help
 * ═══════════════════════════════════════════════════════════ */

void ui_banner(void)
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

void ui_help(void)
{
    printf("\n");
    printf("  %s+-- Commands ----------------------------------------+%s\n", FG_GRAY, RESET);
    printf("  %s|%s  %splay <n>%s          play card at index n         %s|%s\n",
           FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
    printf("  %s|%s  %splay <n> <color>%s  play Wild, choose color      %s|%s\n",
           FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
    printf("  %s|%s  %sdraw%s              draw a card                  %s|%s\n",
           FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
    printf("  %s|%s  %spass%s              pass after drawing           %s|%s\n",
           FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
    printf("  %s|%s  %suno%s               declare UNO                  %s|%s\n",
           FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
    printf("  %s|%s  %scallout <name>%s    catch someone's UNO          %s|%s\n",
           FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
    printf("  %s|%s  %schat <msg>%s        send a chat message          %s|%s\n",
           FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
    printf("  %s|%s  %sstatus%s            refresh game display         %s|%s\n",
           FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
    printf("  %s|%s  %sstart%s             request game start           %s|%s\n",
           FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
    printf("  %s|%s  %shelp%s              show this help               %s|%s\n",
           FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
    printf("  %s|%s                                                  %s|%s\n",
           FG_GRAY, RESET, FG_GRAY, RESET);
    printf("  %s|%s  %sColors:%s r(ed)  b(lue)  g(reen)  y(ellow)      %s|%s\n",
           FG_GRAY, RESET, DIM, RESET, FG_GRAY, RESET);
    printf("  %s+----------------------------------------------------+%s\n", FG_GRAY, RESET);
    ui_prompt();
}

/* ═══════════════════════════════════════════════════════════
 *  Inline Event Helpers
 * ═══════════════════════════════════════════════════════════ */

void ui_event_played(const char *who, Card c, uint8_t eff_color)
{
    printf("\r%s[Play]%s %s played ", FG_MAGENTA, RESET, who);
    ui_print_card(c);
    if (c.value == CARD_WILD || c.value == CARD_WILD4) {
        printf(" -> ");
        ui_print_color_dot(eff_color);
    }
    printf("\n");
    fflush(stdout);
}

void ui_event_draw_result(const Card *cards, int count, int playable)
{
    printf("\r%s[Draw]%s You drew: ", FG_BLUE, RESET);
    for (int i = 0; i < count; i++) {
        if (i > 0) printf(" ");
        ui_print_card(cards[i]);
    }
    if (playable)
        printf("  %s(playable! use: play <index> or pass)%s", FG_GREEN, RESET);
    else
        printf("  %s(not playable, type: pass)%s", FG_GRAY, RESET);
    printf("\n");
    fflush(stdout);
}

void ui_event_win(void)
{
    printf("\n");
    printf("  %s%s", BOLD, FG_YELLOW);
    printf("  +===============================+\n");
    printf("  |        YOU WIN!               |\n");
    printf("  +===============================+\n");
    printf("  %s\n", RESET);
}

/* ═══════════════════════════════════════════════════════════
 *  Full Game-State Render
 * ═══════════════════════════════════════════════════════════ */

void ui_render(const GameView *v)
{
    if (!v->connected) return;

    /* ── Header ──────────────────────────────────── */
    printf("\n%s+================================================+%s\n", FG_CYAN, RESET);
    printf("%s|%s  %s U  N  O%s                                      %s|%s\n",
           FG_CYAN, RESET, BOLD, RESET, FG_CYAN, RESET);
    printf("%s+================================================+%s\n", FG_CYAN, RESET);

    /* ── Lobby (pre-game) ────────────────────────── */
    if (!v->game_started) {
        printf("\n  %sWaiting for players to join...%s\n", DIM, RESET);
        printf("  Players in lobby:\n");
        for (int d = 0; d < v->display_count; d++) {
            int i = v->display_order[d];
            if (!v->players[i].present) continue;
            if (i == v->my_id)
                printf("    %s* %s (you)%s\n", FG_GREEN,
                       v->players[i].name, RESET);
            else
                printf("    %s* %s%s\n", FG_WHITE,
                       v->players[i].name, RESET);
        }
        /* Also show players not yet in display_order (pre-STATE) */
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!v->players[i].present) continue;
            int found = 0;
            for (int d = 0; d < v->display_count; d++)
                if (v->display_order[d] == i) { found = 1; break; }
            if (found) continue;
            if (i == v->my_id)
                printf("    %s* %s (you)%s\n", FG_GREEN,
                       v->players[i].name, RESET);
            else
                printf("    %s* %s%s\n", FG_WHITE,
                       v->players[i].name, RESET);
        }
        printf("\n  Type %sstart%s to begin (if enough players), or wait.\n",
               BOLD, RESET);
        ui_prompt();
        return;
    }

    /* ── Game Over ───────────────────────────────── */
    if (v->game_over) {
        printf("\n  %s*** GAME OVER ***%s\n\n", BOLD, RESET);
        ui_prompt();
        return;
    }

    /* ── Top Card & Effective Color ──────────────── */
    printf("\n  %sTop Card:%s  ", DIM, RESET);
    ui_print_card(v->top_card);
    if (v->top_card.value == CARD_WILD || v->top_card.value == CARD_WILD4) {
        printf("  %sColor:%s ", DIM, RESET);
        ui_print_color_dot(v->effective_color);
    }
    printf("\n");

    /* ── Direction ────────────────────────────────── */
    printf("  %sDirection:%s %s\n", DIM, RESET,
           (v->direction == 1) ? "--> Clockwise"
                               : "<-- Counter-clockwise");

    /* ── Player List (seat order) ────────────────── */
    printf("\n  %s+-- Players -----------------------------------------+%s\n",
           FG_GRAY, RESET);
    for (int d = 0; d < v->display_count; d++) {
        int i = v->display_order[d];
        if (!v->players[i].present) continue;
        const PlayerInfo *p = &v->players[i];
        int is_cur = (i == v->current_player);
        int is_me  = (i == v->my_id);

        printf("  %s|%s ", FG_GRAY, RESET);

        /* Turn arrow */
        if (is_cur) printf("%s>>%s ", FG_YELLOW, RESET);
        else        printf("   ");

        /* Name */
        if (is_me)
            printf("%s%s%s (you)", FG_GREEN, p->name, RESET);
        else if (!p->connected)
            printf("%s%s (left)%s", FG_GRAY, p->name, RESET);
        else
            printf("%s", p->name);

        /* Card count */
        printf("  %s[%d card%s]%s", DIM, p->hand_count,
               p->hand_count == 1 ? "" : "s", RESET);

        /* UNO flag */
        if (p->said_uno)
            printf("  %s%sUNO!%s", BOLD, FG_RED, RESET);

        printf("\n");
    }
    printf("  %s+----------------------------------------------------+%s\n",
           FG_GRAY, RESET);

    /* ── Hand ────────────────────────────────────── */
    printf("\n  %s+-- Your Hand ---------------------------------------+%s\n",
           FG_GRAY, RESET);
    if (v->hand_size == 0) {
        printf("  %s|%s   %s(empty)%s\n", FG_GRAY, RESET, DIM, RESET);
    } else {
        int my_turn = (v->current_player == v->my_id);
        for (int i = 0; i < v->hand_size; i++) {
            Card c = v->hand[i];

            /* Check playability */
            int ok = (c.value == CARD_WILD || c.value == CARD_WILD4
                      || c.color == v->effective_color
                      || c.value == v->top_card.value);

            printf("  %s|%s ", FG_GRAY, RESET);

            /* Index number */
            if (ok && my_turn)
                printf("  %s%s%2d%s ", BOLD, FG_GREEN, i, RESET);
            else
                printf("  %s%2d%s ", DIM, i, RESET);

            /* Colored card */
            ui_print_card(c);

            /* Playable hint */
            if (ok && my_turn)
                printf(" %s<-%s", FG_GREEN, RESET);

            printf("\n");
        }
    }
    printf("  %s+----------------------------------------------------+%s\n",
           FG_GRAY, RESET);

    /* ── Turn Prompt ─────────────────────────────── */
    if (v->current_player == v->my_id) {
        printf("\n  %s%s*** YOUR TURN ***%s  play <n> [color] | draw | pass | uno\n",
               BOLD, FG_YELLOW, RESET);
    } else {
        const char *who = "...";
        if (v->current_player >= 0 && v->current_player < MAX_PLAYERS
            && v->players[v->current_player].present)
            who = v->players[v->current_player].name;
        printf("\n  %sWaiting for %s...%s  (chat, uno, callout, status)\n",
               DIM, who, RESET);
    }

    ui_prompt();
}
