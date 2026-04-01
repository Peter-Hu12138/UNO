#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "game_entities.h"
#include "client_output.h"


// ========= printing
static void print_prompt() {
  printf("%s>%s ", FG_CYAN, RESET);
  fflush(stdout);
}

void print_help(void) {
  printf("\n");
  printf("  %s┌─ Commands ─────────────────────────────────┐%s\n", FG_GRAY, RESET);
  printf("  %s│%s  %splay <n>%s          play card at index n    %s│%s\n", FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
  printf("  %s│%s  %splay <n> <color>%s  play Wild, choose color %s│%s\n", FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
  printf("  %s│%s  %sdraw%s              draw a card             %s│%s\n", FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
  printf("  %s│%s  %spass%s              pass after drawing      %s│%s\n", FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
  printf("  %s│%s  %suno%s               declare UNO             %s│%s\n", FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
  printf("  %s│%s  %scallout <name>%s    catch someone's UNO     %s│%s\n", FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
  printf("  %s│%s  %schat <msg>%s        send a chat message     %s│%s\n", FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
  printf("  %s│%s  %sstatus%s            see game status         %s│%s\n", FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
  printf("  %s│%s  %sstart%s             request game start      %s│%s\n", FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
  printf("  %s│%s  %shelp%s              show this help          %s│%s\n", FG_GRAY, RESET, BOLD, RESET, FG_GRAY, RESET);
  printf("  %s│%s                                            %s│%s\n", FG_GRAY, RESET, FG_GRAY, RESET);
  printf("  %s│%s  %sColors:%s r(ed) b(lue) g(reen) y(ellow)     %s│%s\n", FG_GRAY, RESET, DIM, RESET, FG_GRAY, RESET);
  printf("  %s└────────────────────────────────────────────┘%s\n", FG_GRAY, RESET);
  print_prompt();
}

void print_event(const char* tag, const char* tag_color, const char* msg) {
  printf("\r%s%s%s %s\n", tag_color, tag, RESET, msg);
  fflush(stdout);
  print_prompt();
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
static inline const char* value_name(uint8_t v) {
  static const char* t[] = {
      "0","1","2","3","4","5","6","7","8","9",
      "Skip","Reverse","+2","Wild","Wild+4"
  };
  return (v <= CARD_WILD4) ? t[v] : "???";
}
static inline const char* short_value(uint8_t v) {
  static const char* t[] = {
      " 0"," 1"," 2"," 3"," 4"," 5"," 6"," 7"," 8"," 9",
      "Sk","Re","+2"," W","+4"
  };
  return (v <= CARD_WILD4) ? t[v] : "??";
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
  Card top = st->discard_pile[st->discard_top_idx];
  print_card(top);
  if (top.value == CARD_WILD || top.value == CARD_WILD4) {
    printf("  %sColor:%s ", DIM, RESET);
    print_color_dot(top.wild_actual_color);
  }
  printf("\n");

  /* ── Direction ────────────────────────────────── */
  printf("  %sDirection:%s %s\n", DIM, RESET, (st->direction == 1) ? "--> Clockwise" : "<-- Counter-clockwise");

  /* ── Players (in seat / join order) ──────────── */
  printf("\n  %s─ Players ──────────────────────────────────%s\n", FG_GRAY, RESET);
  Player* player = st->players;
  for (int d = 0; d < st->player_count; d++) {
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
    printf("  %s[%d card%s]%s", DIM, player->hand_count, player->hand_count == 1 ? "" : "s", RESET);

    /* UNO indicator */
    if (player->called_uno) {
      printf("  %s%sUNO!%s", BOLD, FG_RED, RESET);
    }

    printf("\n");

    player = player->next;
  }
  printf("  %s────────────────────────────────────────────%s\n", FG_GRAY, RESET);

  /* ── Your Hand ───────────────────────────────── */
  printf("\n  %s─ Your Hand ────────────────────────────────%s\n", FG_GRAY, RESET);
  if (p->hand_count == 0) {
    printf("  %s│%s   %s(empty)%s\n", FG_GRAY, RESET, DIM, RESET);
  }
  else {
    for (int i = 0; i < p->hand_count; i++) {
      Card c = p->hand[i];
      /* Index */
      printf("  %s%2d%s ", DIM, i, RESET);

      /* Card with color */
      print_card(c);

      printf("\n");
    }
  }
  printf("  %s────────────────────────────────────────────%s\n", FG_GRAY, RESET);

  print_prompt();
}

