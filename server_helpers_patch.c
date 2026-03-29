/*
 * Drop-in replacement helpers for server.c
 *
 * Goal:
 * - make turn / next-player logic clearer
 * - keep public player hand counts available through MSG_STATE
 * - make UNO vulnerability and card-effect handling more explicit
 *
 * Notes:
 * - This patch assumes the same globals already present in your server.c:
 *     players[], current, direction, top_card, top_color,
 *     game_started, game_over, num_connected
 * - It also assumes these existing helpers still exist:
 *     notify_all(), send_error(), send_hand(), send_state_to(),
 *     broadcast(), broadcast_except(), draw_card(), give_cards(),
 *     remove_card(), broadcast_state()
 */

/* ──────────────────────────────────────────────────────────
 *  Active-player / turn helpers
 * ────────────────────────────────────────────────────────── */

static int is_active_player(int pid)
{
    return pid >= 0 && pid < MAX_PLAYERS && players[pid].connected;
}

static int count_active_players(void)
{
    int active = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].connected) active++;
    }
    return active;
}

static int next_active_player(int from)
{
    int p = from;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        p = (p + direction + MAX_PLAYERS) % MAX_PLAYERS;
        if (is_active_player(p)) return p;
    }
    return from;
}

static int nth_next_active_player(int from, int steps)
{
    int p = from;
    for (int i = 0; i < steps; i++) {
        p = next_active_player(p);
    }
    return p;
}

static void clear_noncurrent_uno_windows(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (i != current) players[i].uno_vulnerable = 0;
    }
}

static void reset_turn_flags_for_player(int pid)
{
    if (!is_active_player(pid)) return;
    players[pid].has_drawn = 0;
}

static void announce_turn(void)
{
    char buf[80];
    snprintf(buf, sizeof(buf), "It's %s's turn.", players[current].name);
    notify_all(buf);

    uint8_t tbuf[1] = {(uint8_t)current};
    broadcast(MSG_TURN, tbuf, 1);
    broadcast_state();
}

static void advance_turn_steps(int steps)
{
    clear_noncurrent_uno_windows();
    current = nth_next_active_player(current, steps);
    reset_turn_flags_for_player(current);
    announce_turn();
}

static void advance_turn(void)
{
    advance_turn_steps(1);
}

/* ──────────────────────────────────────────────────────────
 *  Card / UNO helpers
 * ────────────────────────────────────────────────────────── */

static int can_play(Card c)
{
    if (c.value == CARD_WILD || c.value == CARD_WILD4) return 1;
    if (c.color == top_color) return 1;
    if (c.value == top_card.value) return 1;
    return 0;
}

static int has_playable(int pid)
{
    for (int i = 0; i < players[pid].hand_size; i++) {
        if (can_play(players[pid].hand[i])) return 1;
    }
    return 0;
}

static void clear_uno_flags_if_needed(int pid)
{
    if (!is_active_player(pid)) return;
    if (players[pid].hand_size != 1) {
        players[pid].said_uno = 0;
        players[pid].uno_vulnerable = 0;
    }
}

static void mark_uno_vulnerability_if_needed(int pid)
{
    if (!is_active_player(pid)) return;

    if (players[pid].hand_size == 1) {
        if (!players[pid].said_uno) {
            players[pid].uno_vulnerable = 1;
        }
    } else {
        players[pid].uno_vulnerable = 0;
        if (players[pid].hand_size > 1) players[pid].said_uno = 0;
    }
}

static void give_penalty_cards(int pid, int count)
{
    give_cards(pid, count);
    clear_uno_flags_if_needed(pid);
}

/*
 * Apply the effect of a successfully played card.
 * Returns how many turn-steps should be advanced after the play.
 *
 * 1 = normal next player
 * 2 = skip one player
 */
static int apply_card_effect(int pid, Card card)
{
    switch (card.value) {
    case CARD_SKIP: {
        int skipped = next_active_player(pid);
        char buf[80];
        snprintf(buf, sizeof(buf), "%s is skipped!", players[skipped].name);
        notify_all(buf);
        return 2;
    }
    case CARD_REVERSE:
        direction = -direction;
        notify_all(direction == 1
                   ? "Direction: clockwise."
                   : "Direction: counter-clockwise.");
        if (count_active_players() == 2) {
            return 2;
        }
        return 1;

    case CARD_DRAW2: {
        int victim = next_active_player(pid);
        give_penalty_cards(victim, 2);
        char buf[96];
        snprintf(buf, sizeof(buf), "%s draws 2 and is skipped!",
                 players[victim].name);
        notify_all(buf);
        return 2;
    }
    case CARD_WILD:
        return 1;

    case CARD_WILD4: {
        int victim = next_active_player(pid);
        give_penalty_cards(victim, 4);
        char buf[96];
        snprintf(buf, sizeof(buf), "%s draws 4 and is skipped!",
                 players[victim].name);
        notify_all(buf);
        return 2;
    }
    default:
        return 1;
    }
}

/* ──────────────────────────────────────────────────────────
 *  Suggested replacements for the command handlers
 * ────────────────────────────────────────────────────────── */

static void handle_play(int pid, uint8_t *payload, uint16_t len)
{
    if (!game_started || game_over) {
        send_error(pid, "Game is not active.");
        return;
    }
    if (pid != current) {
        send_error(pid, "Not your turn.");
        return;
    }
    if (len < 2) {
        send_error(pid, "Bad play message.");
        return;
    }

    int idx = payload[0];
    uint8_t chosen_color = payload[1];

    if (idx < 0 || idx >= players[pid].hand_size) {
        send_error(pid, "Invalid card index.");
        return;
    }

    Card card = players[pid].hand[idx];
    if (!can_play(card)) {
        send_error(pid, "That card cannot be played now.");
        return;
    }

    if ((card.value == CARD_WILD || card.value == CARD_WILD4) &&
        chosen_color >= NUM_COLORS) {
        send_error(pid, "Wild card requires a valid color.");
        return;
    }

    card = remove_card(pid, idx);
    top_card = card;
    top_color = (card.value == CARD_WILD || card.value == CARD_WILD4)
                    ? chosen_color
                    : card.color;

    players[pid].has_drawn = 0;

    uint8_t played_buf[5] = {
        (uint8_t)pid,
        card.color,
        card.value,
        top_color,
        (uint8_t)players[pid].hand_size
    };
    broadcast(MSG_PLAYED, played_buf, 5);

    mark_uno_vulnerability_if_needed(pid);

    if (players[pid].hand_size == 0) {
        char winmsg[80];
        snprintf(winmsg, sizeof(winmsg), "%s wins the game!", players[pid].name);
        notify_all(winmsg);
        uint8_t wb[1] = {(uint8_t)pid};
        broadcast(MSG_GAME_OVER, wb, 1);
        game_over = 1;
        return;
    }

    int steps = apply_card_effect(pid, card);
    advance_turn_steps(steps);
}

static void handle_draw(int pid)
{
    if (!game_started || game_over) {
        send_error(pid, "Game is not active.");
        return;
    }
    if (pid != current) {
        send_error(pid, "Not your turn.");
        return;
    }
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

    send_hand(pid);
    send_state_to(pid);
}

static void handle_pass(int pid)
{
    if (!game_started || game_over) {
        send_error(pid, "Game is not active.");
        return;
    }
    if (pid != current) {
        send_error(pid, "Not your turn.");
        return;
    }
    if (!players[pid].has_drawn) {
        send_error(pid, "You can only pass after drawing.");
        return;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%s passed.", players[pid].name);
    notify_all(buf);
    advance_turn();
}

static void handle_uno(int pid)
{
    if (!game_started || game_over) {
        send_error(pid, "Game is not active.");
        return;
    }
    if (players[pid].hand_size != 1) {
        send_error(pid, "UNO can only be declared when you have exactly 1 card.");
        return;
    }

    players[pid].said_uno = 1;
    players[pid].uno_vulnerable = 0;

    char buf[64];
    snprintf(buf, sizeof(buf), "%s declared UNO!", players[pid].name);
    notify_all(buf);
    broadcast_state();
}

static void handle_callout(int pid, uint8_t *payload, uint16_t len)
{
    if (!game_started || game_over) {
        send_error(pid, "Game is not active.");
        return;
    }
    if (len < 1) {
        send_error(pid, "Bad callout.");
        return;
    }

    int target = payload[0];
    if (!is_active_player(target)) {
        send_error(pid, "Invalid player id.");
        return;
    }
    if (target == pid) {
        send_error(pid, "Can't call out yourself.");
        return;
    }

    if (!players[target].uno_vulnerable) {
        send_error(pid, "That player isn't vulnerable.");
        return;
    }

    players[target].uno_vulnerable = 0;
    give_penalty_cards(target, 2);

    char buf[128];
    snprintf(buf, sizeof(buf),
             "%s caught %s forgetting UNO! %s draws 2 penalty cards!",
             players[pid].name, players[target].name, players[target].name);
    notify_all(buf);
    broadcast_state();
}
