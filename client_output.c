#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "game_entities.h"

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

// ========= printing
void print_help(void) {
  printf("\n");
  printf("  %s┌─ Commands ─────────────────────────────────────────┐%s\n", FG_GRAY, RESET);
  printf("  %s│%s  %splay <n>%s          play card at index n    %s│%s\n", FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
  printf("  %s│%s  %splay <n> <color>%s  play Wild, choose color %s│%s\n", FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
  printf("  %s│%s  %sdraw%s              draw a card             %s│%s\n", FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
  printf("  %s│%s  %spass%s              pass after drawing      %s│%s\n", FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
  printf("  %s│%s  %suno%s               declare UNO             %s│%s\n", FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
  printf("  %s│%s  %scallout <name>%s    catch someone's UNO     %s│%s\n", FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
  printf("  %s│%s  %schat <msg>%s        send a chat message     %s│%s\n", FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
  printf("  %s│%s  %sstart%s             request game start      %s│%s\n", FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
  printf("  %s│%s  %shelp%s              show this help          %s│%s\n", FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
  printf("  %s│%s                                                %s│%s\n", FG_GRAY, RESET, FG_GRAY, RESET);
  printf("  %s│%s  %sColors:%s r(ed) b(lue) g(reen) y(ellow)     %s│%s\n", FG_GRAY, RESET, DIM, RESET, FG_GRAY, RESET);
  printf("  %s└────────────────────────────────────────────────────┘%s\n", FG_GRAY, RESET);
  print_prompt();
}

void print_event(const char* tag, const char* tag_color, const char* msg) {
  printf("\r%s%s%s %s\n", tag_color, tag, RESET, msg);
  fflush(stdout);
}

void print_prompt(void) {
  printf("%s>%s ", FG_CYAN, RESET);
  fflush(stdout);
}

char* card_bg(uint8_t color) {
  switch (color) {
  case COLOR_RED:    return BG_RED;
  case COLOR_BLUE:   return BG_BLUE;
  case COLOR_GREEN:  return BG_GREEN;
  case COLOR_YELLOW: return BG_YELLOW;
  case COLOR_WILD:   return BG_WILD;
  default:           return "";
  }
}
char* color_fg(uint8_t color) {
  switch (color) {
  case COLOR_RED:    return FG_RED;
  case COLOR_BLUE:   return FG_BLUE;
  case COLOR_GREEN:  return FG_GREEN;
  case COLOR_YELLOW: return FG_YELLOW;
  case COLOR_WILD:   return FG_MAGENTA;
  default:           return "";
  }
}
char* color_name(uint8_t c) {
  switch (c) {
  case COLOR_RED:    return "Red";
  case COLOR_BLUE:   return "Blue";
  case COLOR_GREEN:  return "Green";
  case COLOR_YELLOW: return "Yellow";
  case COLOR_WILD:   return "Wild";
  default:           return "???";
  }
}

/* Print a colored card inline: e.g. [Red 5] with background color */
void print_card(Card c) {
  const char* bg = card_bg(c.color);
  if (c.value == CARD_WILD || c.value == CARD_WILD4) {
    printf("%s %s %s", BG_WILD, value_name(c.value), RESET);
  }
  else {
    printf("%s %s %s %s", bg, color_name(c.color), short_value(c.value), RESET);
  }
}

/* Print the effective color indicator dot */
void print_color_dot(uint8_t color) {
  printf("%s %s %s", card_bg(color), color_name(color), RESET);
}

/*
 * p is the player on this client, st is the global game state
 */
void print_status(const Player* p, const GameState* st) {
  if (!p->connected) return;

  if (!st->game_started) {
    printf("\n  %sWaiting for players to join...%s\n", DIM, RESET);
    printf("  Players in lobby:\n");
    for (int i = 0; i < st->player_count; i++) {
      const Player* lobby_player = st->players[i];
      if (!lobby_player) continue;

      if (lobby_player->id == p->id)
        printf("    %s* %s (you)%s\n", FG_GREEN, lobby_player->name, RESET);
      else
        printf("    %s* %s%s\n", FG_WHITE, lobby_player->name, RESET);
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
    print_color_dot(st->top_card.wild_actual_color);
  }
  printf("\n");

  /* ── Direction ────────────────────────────────── */
  printf("  %sDirection:%s %s\n", DIM, RESET,
    (st->direction == 1) ? "--> Clockwise" : "<-- Counter-clockwise");

  /* ── Players (in seat / join order) ──────────── */
  printf("\n  %s┌─ Players ──────────────────────────────────┐%s\n", FG_GRAY, RESET);
  for (int d = 0; d < st->player_count; d++) {
    const Player* player = st->players[d];
    if (!player) continue;

    int is_current = (player->id == st->current_player_id);
    int is_me = (player->id == p->id);

    /* Turn indicator */
    if (is_current)
      printf("%s>>%s ", FG_YELLOW, RESET);
    else
      printf("   ");

    /* Player name */
    if (is_me)
      printf("%s%s%s (you)", FG_GREEN, player->name, RESET);
    else if (!player->connected)
      printf("%s%s (left)%s", FG_GRAY, player->name, RESET);
    else
      printf("%s", player->name);

    /* Card count */
    printf("  %s[%d card%s]%s", DIM, player->hand_count,
      player->hand_count == 1 ? "" : "s", RESET);

    /* UNO indicator */
    if (player->called_uno)
      printf("  %s%sUNO!%s", BOLD, FG_RED, RESET);

    printf("\n");
  }
  printf("  %s└────────────────────────────────────────────┘%s\n", FG_GRAY, RESET);

  /* ── Your Hand ───────────────────────────────── */
  printf("\n  %s┌─ Your Hand ────────────────────────────────┐%s\n", FG_GRAY, RESET);
  if (p->hand_count == 0) {
    printf("  %s│%s   %s(empty)%s\n", FG_GRAY, RESET, DIM, RESET);
  }
  else {
    for (int i = 0; i < p->hand_count; i++) {
      Card c = p->hand[i];
      int playable = 0;

      /* Check if card is playable */
      if (c.value == CARD_WILD || c.value == CARD_WILD4)
        playable = 1;
      else if (c.color == effective_color)
        playable = 1;
      else if (c.value == st->top_card.value)
        playable = 1;

      printf("  %s│%s ", FG_GRAY, RESET);

      /* Index */
      if (playable && st->current_player_id == p->id)
        printf("  %s%s%2d%s ", BOLD, FG_GREEN, i, RESET);
      else
        printf("  %s%2d%s ", DIM, i, RESET);

      /* Card with color */
      print_card(c);

      /* Playable hint */
      if (playable && st->current_player_id == p->id)
        printf(" %s<-%s", FG_GREEN, RESET);

      printf("\n");
    }
  }
  printf("  %s└────────────────────────────────────────────┘%s\n", FG_GRAY, RESET);

  /* ── Turn prompt ─────────────────────────────── */
  if (st->current_player_id == p->id) {
    printf("\n  %s%s*** YOUR TURN ***%s  play <n> [color] | draw | pass | uno\n",
      BOLD, FG_YELLOW, RESET);
  }
  else {
    printf("\n  %sWaiting for %s...%s  (chat, uno, callout, status)\n",
      DIM,
      current_player ? current_player->name : "...",
      RESET);
  }

  print_prompt();
}

