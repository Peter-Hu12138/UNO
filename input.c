/*
 * input.c  --  Command Parsing for UNO Client
 *
 * Pure parsing: no printf, no network, no global state.
 * Every function is deterministic and side-effect free.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "input.h"

/* ═══════════════════════════════════════════════════════════
 *  Internal Helpers
 * ═══════════════════════════════════════════════════════════ */

static void trim(char *s)
{
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'
                     || s[n - 1] == ' '  || s[n - 1] == '\t')) {
        s[n - 1] = '\0';
        n--;
    }
}

static int eq_nocase(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static char *skip_spaces(char *s)
{
    while (s && *s && isspace((unsigned char)*s)) s++;
    return s;
}

/* ═══════════════════════════════════════════════════════════
 *  Public: Color Parsing
 * ═══════════════════════════════════════════════════════════ */

int parse_color_str(const char *s, uint8_t *out)
{
    if (!s || !out) return -1;
    if (eq_nocase(s, "red")    || eq_nocase(s, "r")) { *out = COLOR_RED;    return 0; }
    if (eq_nocase(s, "blue")   || eq_nocase(s, "b")) { *out = COLOR_BLUE;   return 0; }
    if (eq_nocase(s, "green")  || eq_nocase(s, "g")) { *out = COLOR_GREEN;  return 0; }
    if (eq_nocase(s, "yellow") || eq_nocase(s, "y")) { *out = COLOR_YELLOW; return 0; }
    return -1;
}

/* ═══════════════════════════════════════════════════════════
 *  Public: Command Parsing
 * ═══════════════════════════════════════════════════════════ */

static Command make_error(const char *msg)
{
    Command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = CMD_INVALID;
    strncpy(cmd.error, msg, sizeof(cmd.error) - 1);
    return cmd;
}

static Command make_simple(CmdType t)
{
    Command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = t;
    return cmd;
}

static Command parse_play(char *rest)
{
    Command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = CMD_PLAY;

    /* Extract index token */
    char *idx_str = strtok(rest, " \t");
    if (!idx_str)
        return make_error("Usage: play <index> [color]");

    char *endptr;
    long idx = strtol(idx_str, &endptr, 10);
    if (*endptr != '\0' || idx < 0 || idx > 255)
        return make_error("Invalid card index.");
    cmd.card_index = (int)idx;

    /* Optional color token */
    char *color_str = strtok(NULL, " \t");
    if (color_str) {
        uint8_t c;
        if (parse_color_str(color_str, &c) < 0)
            return make_error("Color must be: red(r), blue(b), green(g), or yellow(y).");
        cmd.has_color = 1;
        cmd.chosen_color = c;
    }

    return cmd;
}

static Command parse_with_arg(CmdType type, char *rest, const char *usage)
{
    char *arg = skip_spaces(rest);
    if (!arg || *arg == '\0')
        return make_error(usage);

    Command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = type;
    strncpy(cmd.arg, arg, sizeof(cmd.arg) - 1);
    return cmd;
}

Command parse_command(char *line)
{
    if (!line) return make_simple(CMD_NONE);

    trim(line);
    line = skip_spaces(line);
    if (*line == '\0') return make_simple(CMD_NONE);

    /* Split into command word + rest */
    char *cmd_word = strtok(line, " \t");
    if (!cmd_word) return make_simple(CMD_NONE);

    /* rest = everything after the first token (may be NULL) */
    char *rest = strtok(NULL, "");

    if (eq_nocase(cmd_word, "play"))
        return parse_play(rest);

    if (eq_nocase(cmd_word, "draw"))
        return make_simple(CMD_DRAW);

    if (eq_nocase(cmd_word, "pass"))
        return make_simple(CMD_PASS);

    if (eq_nocase(cmd_word, "uno"))
        return make_simple(CMD_UNO);

    if (eq_nocase(cmd_word, "callout"))
        return parse_with_arg(CMD_CALLOUT, rest, "Usage: callout <name>");

    if (eq_nocase(cmd_word, "chat"))
        return parse_with_arg(CMD_CHAT, rest, "Usage: chat <message>");

    if (eq_nocase(cmd_word, "status"))
        return make_simple(CMD_STATUS);

    if (eq_nocase(cmd_word, "start"))
        return make_simple(CMD_START);

    if (eq_nocase(cmd_word, "help") || eq_nocase(cmd_word, "h"))
        return make_simple(CMD_HELP);

    return make_error("Unknown command. Type 'help' for commands.");
}
