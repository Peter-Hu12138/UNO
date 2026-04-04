/*
 * input.c  --  Command Parsing for UNO Client
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "client_input.h"

 /* ═══════════════════════════════════════════════════════════
 *  Internal Helpers
 * ═══════════════════════════════════════════════════════════ */

 /**
 * @brief removes trailing whitespace characters such as spaces, tabs, newlines,
 * and carriage returns—from a given C-style string.
 * 
 * It modifies the provided string in-place by replacing the
 * trailing whitespace characters with null terminators.
 * 
 * Source - Claude 
 * Retrieved 2026-03-29
 */
static void trim(char* s) {
  if (!s) return;
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'
    || s[n - 1] == ' ' || s[n - 1] == '\t')) {
    s[n - 1] = '\0';
    n--;
  }
}

/**
 * @brief campare strings, return 0 if strings are different
 *
 * @param a s1
 * @param b s2
 * @return int
 * 
 * Source - Claude 
 * Retrieved 2026-03-29
 */
static int eq_nocase(const char* a, const char* b) {
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
      return 0;
    a++; b++;
  }
  return *a == '\0' && *b == '\0';
}

/**
 * @brief removes leading whitespace characters spaces
 * 
 * @param s string
 * @return char* string starting from the first non space char
 * 
 * Source - Claude 
 * Retrieved 2026-03-29
 */
static char* skip_spaces(char* s) {
  while (s && *s && isspace((unsigned char)*s)) s++;
  return s;
}

/**
 * @brief Check if input color string is valid, return 0 if valid
 * 
 * @param s 
 * @return int 
 */
static int parse_color_str(const char* s) {
  if (!s) return -1;
  if (eq_nocase(s, "red") || eq_nocase(s, "r")) { return 0; }
  if (eq_nocase(s, "blue") || eq_nocase(s, "b")) { return 0; }
  if (eq_nocase(s, "green") || eq_nocase(s, "g")) { return 0; }
  if (eq_nocase(s, "yellow") || eq_nocase(s, "y")) { return 0; }
  return -1;
}

/* ═══════════════════════════════════════════════════════════
 *  Public: Command Parsing
 * ═══════════════════════════════════════════════════════════ */

/**
 * @brief creates an error command with the given message
 * 
 * @param msg 
 * @return Command
 * 
 * Source - Claude 
 * Retrieved 2026-03-29z 
 */
static Command make_error(const char* msg) {
  Command cmd = { 0 };
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = CMD_INVALID;
  strncpy(cmd.error, msg, sizeof(cmd.error) - 1);
  return cmd;
}

/**
 * @brief creates a simple command with the given type
 * 
 * @param t 
 * @return Command
 * 
 * Source - Claude 
 * Retrieved 2026-03-29z 
 */
static Command make_simple(CmdType t) {
  Command cmd = { 0 };
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = t;
  return cmd;
}

/**
 * @brief parses a 'play' command, which has a index and a optional color
 * 
 * @param rest 
 * @return Command
 * 
 * Source - Claude 
 * Retrieved 2026-03-29
 */
static Command parse_play(char* rest) {
  Command cmd = { 0 };
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = CMD_PLAY;

  /* Extract index token */
  // split by whitespace, space or tab
  char* saveptr = NULL;
  char* idx_str = strtok_r(rest, " \t", &saveptr);
  if (!idx_str) {
    return make_error("Usage: play <index> [color]");
  }

  char* endptr;
  long idx = strtol(idx_str, &endptr, 10);
  if (*endptr != '\0' || idx < 0 || idx > 255) {
    return make_error("Invalid card index.");
  }
  strncpy(cmd.card_index_str, idx_str, sizeof(cmd.card_index_str) - 1);

  /* Optional color token */
  char* color_str = strtok_r(NULL, " \t", &saveptr);
  if (color_str) {
    if (parse_color_str(color_str) < 0) {
      return make_error("Color must be: red(r), blue(b), green(g), or yellow(y).");
    }
    strncpy(cmd.chosen_color_str, color_str, sizeof(cmd.chosen_color_str) - 1);
  }

  return cmd;
}

/**
 * @brief parse a command with 1 argument
 * 
 * @param type 
 * @param rest 
 * @param usage 
 * @return Command 
 * 
 * Source - Claude 
 * Retrieved 2026-03-29
 */
static Command parse_with_arg(CmdType type, char* rest, const char* usage) {
  char* arg = skip_spaces(rest);
  if (!arg || *arg == '\0')
    return make_error(usage);

  Command cmd = { 0 };
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = type;
  strncpy(cmd.arg, arg, sizeof(cmd.arg) - 1);
  return cmd;
}

 /**
  * @brief Parse a raw input line into a Command struct.
  *
  * The input string may be modified.
  *
  * Returns a filled Command; check cmd.type for result.
  *
  * @param line
  * @return Command
  */
Command parse_command(char* line) {
  if (!line) return make_simple(CMD_NONE);

  // remove leading and ending white spaces
  trim(line);
  line = skip_spaces(line);
  if (*line == '\0') return make_simple(CMD_NONE);

  /* Split into command word + rest */
  char* saveptr = NULL;
  char* cmd_word = strtok_r(line, " \t", &saveptr);
  if (!cmd_word) return make_simple(CMD_NONE);

  /* rest = remaining content after first token (may be NULL) */
  char* rest = skip_spaces(saveptr);
  if (rest && *rest == '\0') {
    rest = NULL;
  }

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

  if (eq_nocase(cmd_word, "status") || eq_nocase(cmd_word, "st"))
    return make_simple(CMD_STATUS);

  if (eq_nocase(cmd_word, "start"))
    return make_simple(CMD_START);

  if (eq_nocase(cmd_word, "help") || eq_nocase(cmd_word, "h"))
    return make_simple(CMD_HELP);

  return make_error("Unknown command. Type 'help' for commands.");
}
