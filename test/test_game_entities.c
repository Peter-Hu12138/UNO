#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_entities.h"

static const char* color_name(uint8_t color) {
	switch (color) {
		case COLOR_RED: return "RED";
		case COLOR_BLUE: return "BLUE";
		case COLOR_GREEN: return "GREEN";
		case COLOR_YELLOW: return "YELLOW";
		case COLOR_WILD: return "WILD";
		default: return "?";
	}
}

static const char* value_name(uint8_t value) {
	switch (value) {
		case CARD_0: return "0";
		case CARD_1: return "1";
		case CARD_2: return "2";
		case CARD_3: return "3";
		case CARD_4: return "4";
		case CARD_5: return "5";
		case CARD_6: return "6";
		case CARD_7: return "7";
		case CARD_8: return "8";
		case CARD_9: return "9";
		case CARD_SKIP: return "SKIP";
		case CARD_REVERSE: return "REVERSE";
		case CARD_DRAW2: return "DRAW2";
		case CARD_WILD: return "WILD";
		case CARD_WILD4: return "WILD4";
		default: return "?";
	}
}

static void print_card(Card c) {
	printf("%s-%s", color_name(c.color), value_name(c.value));
	if (c.value == CARD_WILD || c.value == CARD_WILD4) {
		printf("(set=%s)", color_name(c.wild_actual_color));
	}
}

static Player* make_player(int id, const char* name) {
	Player* p = (Player*)calloc(1, sizeof(Player));
	if (p == NULL) {
		return NULL;
	}
	p->id = id;
	p->connected = 1;
	strncpy(p->name, name, MAX_NAME);
	p->name[MAX_NAME] = '\0';
	return p;
}

static void link_three_players(Player* p1, Player* p2, Player* p3) {
	p1->next = p2;
	p2->next = p3;
	p3->next = p1;

	p1->prev = p3;
	p2->prev = p1;
	p3->prev = p2;
}

static Player* ring_find_player(Player* head, int count, int pid) {
	if (head == NULL || count <= 0) {
		return NULL;
	}

	Player* p = head;
	for (int i = 0; i < count; i++) {
		if (p->id == pid) {
			return p;
		}
		p = p->next;
	}
	return NULL;
}

static void set_hand(Player* p, const Card* cards, int count) {
	p->hand_count = 0;
	for (int i = 0; i < count; i++) {
		p->hand[p->hand_count++] = cards[i];
	}
}

static int failures = 0;

static void check(const char* label, int ok) {
	printf("[CHECK] %-58s : %s\n", label, ok ? "PASS" : "FAIL");
	if (!ok) {
		failures++;
	}
}

static void print_state(const char* label, const GameState* g) {
	printf("\n========== %s ==========\n", label);
	printf("started=%d over=%d current_pid=%d direction=%d players=%d draw_top=%d discard_top=%d effect_applied=%d\n",
				 g->game_started,
				 g->game_over,
				 g->current_player_id,
				 g->direction,
				 g->player_count,
				 g->draw_top_idx,
				 g->discard_top_idx,
				 g->effect_applied);

	if (g->discard_top_idx >= 0) {
		printf("top discard: ");
		print_card(g->discard_pile[g->discard_top_idx]);
		printf("\n");
	}

	if (g->players == NULL || g->player_count <= 0) {
		printf("no players\n");
		return;
	}

	const Player* p = g->players;
	for (int i = 0; i < g->player_count; i++) {
		printf("P%d(%s) conn=%d hand=%d called_uno=%d drawn_this_turn=%d | cards: ",
					 p->id,
					 p->name,
					 p->connected,
					 p->hand_count,
					 p->called_uno,
					 p->drawn_this_turn);

		int show = (p->hand_count < 6) ? p->hand_count : 6;
		for (int h = 0; h < show; h++) {
			print_card(p->hand[h]);
			if (h < show - 1) {
				printf(", ");
			}
		}
		if (p->hand_count > show) {
			printf(", ...");
		}
		printf("\n");
		p = p->next;
	}
}

static void free_ring(Player* head, int count) {
	if (head == NULL || count <= 0) {
		return;
	}

	Player* p = head;
	for (int i = 0; i < count; i++) {
		Player* next = p->next;
		free(p);
		p = next;
	}
}

int main(void) {
	srand(7);

	GameState g;
	game_init(&g);
	print_state("1) game_init", &g);

	Player* p1 = make_player(1, "Alice");
	Player* p2 = make_player(2, "Bob");
	Player* p3 = make_player(3, "Carol");
	if (p1 == NULL || p2 == NULL || p3 == NULL) {
		printf("failed to allocate players\n");
		free(p1);
		free(p2);
		free(p3);
		return 1;
	}

	link_three_players(p1, p2, p3);
	print_state("2) created + connected players", &(GameState){ .players = p1, .player_count = 3 });

	game_start(&g, p1, 3);
	print_state("3) game_start", &g);

	check("game_started == 1", g.game_started == 1);
	check("player_count == 3", g.player_count == 3);
	check("discard initialized", g.discard_top_idx >= 0);
	check("draw cursor valid", g.draw_top_idx >= 0 && g.draw_top_idx < DECK_SIZE);
	check("all players have initial hand", p1->hand_count >= INITIAL_HAND && p2->hand_count >= INITIAL_HAND && p3->hand_count >= INITIAL_HAND);

	printf("\n===== Full turn flow: each player plays a valid card, then next turn =====\n");
	g.discard_top_idx = 0;
	g.discard_pile[0] = (Card){ COLOR_RED, CARD_5, COLOR_RED };
	g.current_player_id = 1;
	g.direction = 1;
	g.effect_applied = 1;

	set_hand(p1, (Card[]){ { COLOR_RED, CARD_7, COLOR_RED }, { COLOR_BLUE, CARD_2, COLOR_BLUE } }, 2);
	set_hand(p2, (Card[]){ { COLOR_GREEN, CARD_7, COLOR_GREEN }, { COLOR_BLUE, CARD_1, COLOR_BLUE } }, 2);
	set_hand(p3, (Card[]){ { COLOR_YELLOW, CARD_7, COLOR_YELLOW }, { COLOR_GREEN, CARD_4, COLOR_GREEN } }, 2);
	print_state("4) before sequential plays", &g);

	check("P1 has playable", game_has_playable(&g, 1));
	check("P1 plays idx 0", game_play_card(&g, 1, 0, COLOR_RED) == 1);
	check("next turn is P2", g.current_player_id == 2);
	print_state("5) after P1 play", &g);

	check("P2 has playable", game_has_playable(&g, 2));
	check("P2 plays idx 0", game_play_card(&g, 2, 0, COLOR_GREEN) == 1);
	check("next turn is P3", g.current_player_id == 3);
	print_state("6) after P2 play", &g);

	check("P3 has playable", game_has_playable(&g, 3));
	check("P3 plays idx 0", game_play_card(&g, 3, 0, COLOR_YELLOW) == 1);
	check("next turn wraps to P1", g.current_player_id == 1);
	print_state("7) after P3 play", &g);

	printf("\n===== No playable cards for multiple players + effect applies once =====\n");
	g.discard_top_idx = 0;
	g.discard_pile[0] = (Card){ COLOR_BLUE, CARD_DRAW2, COLOR_BLUE };
	g.current_player_id = 1;
	g.direction = 1;
	g.effect_applied = 0;

	set_hand(p1, (Card[]){ { COLOR_RED, CARD_1, COLOR_RED } }, 1);
	set_hand(p2, (Card[]){ { COLOR_YELLOW, CARD_9, COLOR_YELLOW } }, 1);
	set_hand(p3, (Card[]){ { COLOR_GREEN, CARD_2, COLOR_GREEN } }, 1);
	print_state("8) before DRAW2 effect turn", &g);

	int p2_before = p2->hand_count;
	game_advance_turn(&g);
	check("DRAW2 applied exactly once to P2 (+2 cards)", p2->hand_count == p2_before + 2);
	check("turn advanced to P3 (P2 skipped)", g.current_player_id == 3);
	check("P3 has no playable", game_has_playable(&g, 3) == 0);
	print_state("9) after DRAW2 effect applied", &g);

	int p1_before = p1->hand_count;
	game_advance_turn(&g);
	check("P3 pass moves turn to P1", g.current_player_id == 1);
	check("no extra penalty on pass", p1->hand_count == p1_before);
	check("P1 has no playable", game_has_playable(&g, 1) == 0);
	print_state("10) after P3 pass", &g);

	int p2_after_effect = p2->hand_count;
	game_advance_turn(&g);
	check("P1 pass moves turn to P2", g.current_player_id == 2);
	check("effect not re-applied to P2", p2->hand_count == p2_after_effect);
	print_state("11) after P1 pass", &g);

  

	printf("\n===== Disconnecting test =====\n");
	g.current_player_id = 2;
	g.discard_top_idx = 0;
	g.discard_pile[0] = (Card){ COLOR_RED, CARD_9, COLOR_RED };
	g.effect_applied = 1;
	p2->connected = 0;
	set_hand(p2, (Card[]){ { COLOR_BLUE, CARD_3, COLOR_BLUE }, { COLOR_YELLOW, CARD_4, COLOR_YELLOW } }, 2);
	int players_before = g.player_count;
	print_state("12) before removing disconnected", &g);

	game_remove_disconnected_players(&g);
	check("one disconnected player removed", g.player_count == players_before - 1);
	check("removed player no longer in ring", ring_find_player(g.players, g.player_count, 2) == NULL);
	check("turn moved off disconnected player", g.current_player_id != 2);
	check("game still active with 2 players", g.game_over == 0 && g.player_count == 2);
	print_state("13) after removing disconnected", &g);

	printf("\nTOTAL FAILURES: %d\n", failures);
	free_ring(g.players, g.player_count);
	return (failures == 0) ? 0 : 1;
}
