/*
 * input.h  --  Command Parsing for UNO Client
 *
 * Pure parsing layer: takes a raw input string, produces a
 * structured Command.  No I/O, no network, no side effects.
 */
#ifndef INPUT_H
#define INPUT_H

#define MAX_PAYLOAD 512

 /* ═══════════════════════════════════════════════════════════
  *  Command Types
  * ═══════════════════════════════════════════════════════════ */

typedef enum {
  CMD_NONE,           /* empty / whitespace-only input      */
  
  CMD_START,          /* start                              */
  CMD_PLAY,           /* play <index> [color]               */
  CMD_DRAW,           /* draw                               */
  CMD_PASS,           /* pass                               */
  CMD_UNO,            /* uno                                */
  CMD_CALLOUT,        /* callout <name>                     */
  CMD_CHAT,           /* chat <message>                     */
  CMD_STATUS,         /* status */

  CMD_HELP,           /* help / h                           */
  CMD_INVALID         /* unrecognized command               */
} CmdType;

/* ═══════════════════════════════════════════════════════════
 *  Parsed Command
 * ═══════════════════════════════════════════════════════════ */

typedef struct {
  CmdType type;

  /* CMD_PLAY fields */
  char card_index_str[10];     /* 0-based hand index AS STRING         */
  char chosen_color_str[10];   /*  red blue green yellow AS STRING   */

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
Command parse_command(char* line);


#endif /* INPUT_H */
