/*
 * input.h  --  Command Parsing for UNO Client
 *
 * Pure parsing layer: takes a raw input string, produces a
 * structured Command.  No I/O, no network, no side effects.
 */
#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>
#include "protocol.h"

/* ═══════════════════════════════════════════════════════════
 *  Command Types
 * ═══════════════════════════════════════════════════════════ */

typedef enum {
    CMD_NONE,           /* empty / whitespace-only input      */
    CMD_PLAY,           /* play <index> [color]               */
    CMD_DRAW,           /* draw                               */
    CMD_PASS,           /* pass                               */
    CMD_UNO,            /* uno                                */
    CMD_CALLOUT,        /* callout <name>                     */
    CMD_CHAT,           /* chat <message>                     */
    CMD_STATUS,         /* status                             */
    CMD_START,          /* start                              */
    CMD_HELP,           /* help / h                           */
    CMD_INVALID         /* unrecognized command               */
} CmdType;

/* ═══════════════════════════════════════════════════════════
 *  Parsed Command
 * ═══════════════════════════════════════════════════════════ */

typedef struct {
    CmdType type;

    /* CMD_PLAY fields */
    int      card_index;     /* 0-based hand index          */
    int      has_color;      /* 1 if user specified a color */
    uint8_t  chosen_color;   /* COLOR_RED .. COLOR_YELLOW   */

    /* CMD_CALLOUT / CMD_CHAT: text argument */
    char     arg[MAX_PAYLOAD];

    /* If parsing failed, human-readable reason */
    char     error[128];
} Command;

/* ═══════════════════════════════════════════════════════════
 *  API
 * ═══════════════════════════════════════════════════════════ */

/*
 * Parse a raw input line into a Command struct.
 * The input string may be modified (strtok).
 * Returns a filled Command; check cmd.type for result.
 */
Command parse_command(char *line);

/*
 * Parse a color string ("red", "r", "blue", "b", etc.)
 * Returns 0 on success and writes to *out, -1 on failure.
 */
int parse_color_str(const char *s, uint8_t *out);

#endif /* INPUT_H */
