# UNO Multiplayer Card Game — Project Documentation

**Course:** CSC209 — Software Tools and Systems Programming
**Category:** Category 2 — Client-Server Application Using Sockets

---

## 1. Project Overview

This project implements a multiplayer UNO card game using a TCP client-server architecture. One server process manages all authoritative game state, while 2-4 client processes connect over the network to play.

**What it does:**
Players connect to the server, join a lobby, and play a full game of UNO following the standard rules: matching cards by color or value, using action cards (Skip, Reverse, Draw Two), Wild and Wild Draw Four cards, declaring UNO when down to one card, and calling out opponents who forget.

**Architecture:**
The server (`server.c`) uses a single-threaded `select()` event loop to handle all connected clients concurrently without ever blocking on a single client. It delegates pure game logic to `game.c` (deck management, turn order, rule validation) and message serialization to `net.c` (broadcasting, state encoding). Clients are built from three modules: `input.c` parses terminal commands into structured `Command` values, `ui.c` renders all terminal output with ANSI-colored cards, and `client.c` ties them together with a `select()` loop over stdin and the server socket.

**Key features:**
- Complete UNO rule set: number cards, Skip, Reverse, Draw Two, Wild, Wild Draw Four, UNO declaration, and UNO callout penalty.
- Hidden information: each player sees only their own hand. The server sends private hand data via `MSG_HAND` and public card counts via `MSG_STATE`.
- In-game chat system.
- Graceful handling of player disconnection mid-game.
- Seat-order turn tracking that preserves join order regardless of internal slot assignment.

**User interaction:**
Players type commands in the terminal: `play <index> [color]`, `draw`, `pass`, `uno`, `callout <name>`, `chat <msg>`, `status`, `start`, and `help`. The UI displays colored card representations, a player list with turn indicators, and playability hints on the hand.

---

## 2. Build Instructions

**Build:**

```
make            # produces: ./server and ./client
```

The Makefile uses `gcc -Wall -Wextra -std=c11 -g` and produces zero warnings. The default port is controlled by the `PORT` variable: `make PORT=5555`.

**Run (three terminals):**

```
# Terminal 1: start server (port 4242, wait for 3 players)
./server 4242 3

# Terminal 2: first player
./client 127.0.0.1 4242 Alice

# Terminal 3: second player
./client 127.0.0.1 4242 Bob

# Terminal 4: third player — game auto-starts when all join
./client 127.0.0.1 4242 Carol
```

Players can also type `start` to begin with fewer than the expected number (minimum 2). No external libraries or input files are required. The project runs on teach.cs without modification.

---

## 3. Architecture Diagram

```
  +============+         +============+         +============+
  |  Client 1  |         |  Client 2  |         |  Client 3  |
  |  (Alice)   |         |  (Bob)     |         |  (Carol)   |
  +-----+------+         +-----+------+         +-----+------+
        |                       |                       |
        |      TCP sockets      |                       |
        +-----------+-----------+-----------+-----------+
                    |                       |
                    v                       v
          +-------------------------------------------+
          |            Server  (server.c)              |
          |                                           |
          |   select() event loop                     |
          |   +-----------+   +------------------+    |
          |   | listen_fd |   | players[i].fd    |    |
          |   | (pre-game)|   | (all connected)  |    |
          |   +-----------+   +------------------+    |
          |                                           |
          |   +-------------+     +---------------+   |
          |   |  game.c     |     |   net.c       |   |
          |   |  Pure rules |     |   Serialize & |   |
          |   |  No network |     |   broadcast   |   |
          |   +-------------+     +---------------+   |
          +-------------------------------------------+

  Data flow on each TCP socket:

  Client --> Server:                Server --> Client:
    MSG_JOIN    (0x01)                MSG_WELCOME     (0x10)
    MSG_PLAY    (0x02)                MSG_PLAYER_JOIN (0x11)
    MSG_DRAW    (0x03)                MSG_GAME_BEGIN  (0x12)
    MSG_PASS    (0x04)                MSG_HAND        (0x13)  [private]
    MSG_UNO     (0x05)                MSG_STATE       (0x14)
    MSG_CALLOUT (0x06)                MSG_TURN        (0x15)
    MSG_CHAT    (0x07)                MSG_PLAYED      (0x16)
    MSG_START   (0x08)                MSG_DREW        (0x17)
                                      MSG_DRAW_RESULT (0x18)  [private]
                                      MSG_NOTIFY      (0x19)
                                      MSG_ERROR       (0x1A)
                                      MSG_GAME_OVER   (0x1B)
                                      MSG_CHAT_RECV   (0x1C)
                                      MSG_PLAYER_LEFT (0x1D)

  Client internal architecture:

    stdin --> input.c ----------> client.c ----------> socket
              parse_command()     execute_command()     send_msg()
              returns Command     validates state

    socket --> client.c --------> ui.c
               dispatch()         ui_render()
               updates GameView   ui_event_played()
                                  ui_event_draw_result()
```

---

## 4. Communication Protocol

### 4.1 Wire Format

Every message on the wire uses a Type-Length-Value (TLV) encoding, defined in `protocol.h` (line 89, `MSG_HDR_SIZE = 3`):

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 1 byte | type | Message type identifier (see tables below) |
| 1 | 2 bytes | length | Payload length in big-endian uint16 |
| 3 | length bytes | payload | Message-specific data |

**Sending** is implemented in `send_msg()` (`protocol.h`, line 111). It writes the 3-byte header, then the payload, using a loop to handle partial `write()` returns. If `write()` returns -1 or 0, the function returns -1 to signal failure.

**Receiving** is implemented in `recv_msg()` (`protocol.h`, line 139). Each player (server-side) and the client maintain a per-connection byte buffer. On each `read()`, new bytes are appended to the buffer. `recv_msg()` checks whether a complete message (header + payload) is available; if so, it copies the payload out, shifts the buffer forward with `memmove()`, and returns 1. If the message is incomplete, it returns 0 and the caller waits for the next `read()`.

### 4.2 Client-to-Server Messages

---

**MSG_JOIN (0x01)**

| Field | Description |
|-------|-------------|
| Direction | Client -> Server |
| Encoding | `[name : N bytes]` (UTF-8 string, max 20 bytes) |
| Semantics | Client requests to join the game with the given player name. Server assigns a slot id, registers the player in the seat ring via `game_seat_add()` (`game.c`, line 42), sends `MSG_WELCOME` back, broadcasts `MSG_PLAYER_JOIN` to others, and sends existing player info to the new client. If enough players have joined, the game starts automatically. |
| Error handling | If the game has already started, server replies with `MSG_ERROR` "Game in progress." See `process()` in `server.c`, line 358. |

---

**MSG_PLAY (0x02)**

| Field | Description |
|-------|-------------|
| Direction | Client -> Server |
| Encoding | `[card_index : 1 byte][chosen_color : 1 byte]` |
| Semantics | Player requests to play the card at `card_index` in their hand. If the card is Wild or Wild+4, `chosen_color` (0=Red, 1=Blue, 2=Green, 3=Yellow) specifies the new effective color. For non-wild cards, `chosen_color` is ignored (set to 255 by client). The server validates the play in `handle_play()` (`server.c`, line 100): checks it is the player's turn, the index is valid, the card is playable per `game_can_play()` (`game.c`, line 165), and wild cards have a valid color. On success, the server removes the card, updates the top card and effective color, broadcasts `MSG_PLAYED`, checks for a win condition, applies action card effects via `game_card_effect()` (`game.c`, line 241), and advances the turn. |
| Error handling | "Not your turn." if `pid != g.current`. "Invalid card index." if out of range. "That card can't be played here." if rule check fails. "Wild card requires a valid color (0-3)." if color >= 4 on a wild card. |

---

**MSG_DRAW (0x03)**

| Field | Description |
|-------|-------------|
| Direction | Client -> Server |
| Encoding | (empty payload) |
| Semantics | Player requests to draw one card. Handled by `handle_draw()` (`server.c`, line 194). Server validates it is the player's turn and they have not already drawn this turn. On success, `game_deal_cards()` (`game.c`, line 140) draws from the pile (reshuffling discard if needed), the drawn card is sent privately via `MSG_DRAW_RESULT`, and a public `MSG_DREW` is broadcast to others. |
| Error handling | "Not your turn." or "Already drew. Play a card or pass." |

---

**MSG_PASS (0x04)**

| Field | Description |
|-------|-------------|
| Direction | Client -> Server |
| Encoding | (empty payload) |
| Semantics | Player passes their turn after drawing. Handled by `handle_pass()` (`server.c`, line 220). Requires `has_drawn == 1`. On success, turn advances via `do_advance_turn()` (`server.c`, line 62). |
| Error handling | "Not your turn." or "You must draw before passing." |

---

**MSG_UNO (0x05)**

| Field | Description |
|-------|-------------|
| Direction | Client -> Server |
| Encoding | (empty payload) |
| Semantics | Player declares UNO. Valid when hand size <= 2. Sets `said_uno = 1` and clears `uno_vulnerable`, preventing callout penalties. Broadcasts notification to all. |
| Error handling | "UNO: you need 1-2 cards." if hand size > 2. |

---

**MSG_CALLOUT (0x06)**

| Field | Description |
|-------|-------------|
| Direction | Client -> Server |
| Encoding | `[target_id : 1 byte]` |
| Semantics | Player accuses another player of forgetting to say UNO. If the target has `uno_vulnerable == 1`, they draw 2 penalty cards. Otherwise, the caller gets an error. |
| Error handling | "Invalid player id." if target is out of range or disconnected. "Can't call out yourself." "That player isn't vulnerable." |

---

**MSG_CHAT_SEND (0x07)**

| Field | Description |
|-------|-------------|
| Direction | Client -> Server |
| Encoding | `[text : N bytes]` (UTF-8 string) |
| Semantics | Server prepends the sender id and broadcasts `MSG_CHAT_RECV` to all players. See `net_broadcast_chat()` in `net.c`. |
| Error handling | Message is truncated if it exceeds `MAX_PAYLOAD - 1`. |

---

**MSG_START (0x08)**

| Field | Description |
|-------|-------------|
| Direction | Client -> Server |
| Encoding | (empty payload) |
| Semantics | Request to start the game before all expected players have joined. Requires at least `MIN_PLAYERS` (2). Calls `start_game()` which invokes `game_start()` (`game.c`, line 289) to build the deck, deal hands, and flip the first card. |
| Error handling | "Already started." or "Need 2+ players." |

---

### 4.3 Server-to-Client Messages

---

**MSG_WELCOME (0x10)**

| Field | Description |
|-------|-------------|
| Direction | Server -> Client |
| Encoding | `[your_id : 1 byte][num_players : 1 byte]` |
| Semantics | Sent immediately after a client's MSG_JOIN is accepted. Tells the client its assigned player id and how many players are currently connected. See `net_send_welcome()` in `net.c`. |

---

**MSG_STATE (0x14) — Full Game State Snapshot**

| Field | Description |
|-------|-------------|
| Direction | Server -> Client (each client individually) |
| Encoding | Variable-length binary structure (see format below) |
| Semantics | Contains all public game information. Sent after every state change (play, draw, turn advance, UNO declaration, etc.). Does NOT contain any player's actual hand cards — that is private information sent separately via MSG_HAND. See `net_send_state()` (`net.c`, line 92). |

**MSG_STATE payload format:**

```
Offset  Field                    Size
------  -----                    ----
0       num_active_players       1 byte

Per player (repeated num_active times, in seat/join order):
  +0    player_id                1 byte
  +1    name_length              1 byte
  +2    name                     name_length bytes
  +N    hand_count               1 byte
  +N+1  said_uno                 1 byte (0 or 1)

After all players:
  +0    current_player_id        1 byte
  +1    direction                1 byte (0 = clockwise, 1 = counter-clockwise)
  +2    top_card_color           1 byte
  +3    top_card_value           1 byte
  +4    effective_color          1 byte
```

The players are serialized in **seat order** (the order they joined), not by slot id. This ensures the client displays players in a consistent, intuitive order. The client stores this order in `display_order[]` and uses it for rendering. See `on_state()` in `client.c` (line 179).

---

**MSG_HAND (0x13) — Private Hand Data**

| Field | Description |
|-------|-------------|
| Direction | Server -> one specific Client |
| Encoding | `[count : 1 byte][color : 1, value : 1] * count` |
| Semantics | Sent only to the owning player. Contains the full list of cards in their hand. Other players never receive this message for someone else's hand — they only see hand counts via MSG_STATE. See `net_send_hand()` (`net.c`, line 70). |

---

**MSG_PLAYED (0x16)**

| Field | Description |
|-------|-------------|
| Direction | Server -> all Clients |
| Encoding | `[pid : 1][card_color : 1][card_value : 1][effective_color : 1][new_hand_count : 1]` |
| Semantics | Broadcast when a card is played. All clients can update the top card, effective color, and the player's card count. See `net_broadcast_played()` (`net.c`, line 147). |

---

**MSG_DRAW_RESULT (0x18) — Private Draw Details**

| Field | Description |
|-------|-------------|
| Direction | Server -> one specific Client |
| Encoding | `[count : 1][color : 1, value : 1] * count [playable : 1]` |
| Semantics | Sent only to the player who drew. Shows exactly which card(s) were drawn, and whether any card in their updated hand is now playable. Other players receive MSG_DREW instead, which only contains the count. This two-tier notification is implemented in `net_broadcast_drew()` (`net.c`, line 163). |

---

**MSG_DREW (0x17)**

| Field | Description |
|-------|-------------|
| Direction | Server -> all Clients except drawer |
| Encoding | `[pid : 1][count : 1]` |
| Semantics | Public notification that a player drew cards. Does not reveal which cards. |

---

**MSG_GAME_OVER (0x1B)**

| Field | Description |
|-------|-------------|
| Direction | Server -> all Clients |
| Encoding | `[winner_id : 1]` |
| Semantics | Sent when a player empties their hand or all other players disconnect. The server sets `g.over = 1` and stops the select loop. |

---

**MSG_NOTIFY (0x19), MSG_ERROR (0x1A), MSG_TURN (0x15), MSG_GAME_BEGIN (0x12), MSG_PLAYER_JOIN (0x11), MSG_PLAYER_LEFT (0x1D), MSG_CHAT_RECV (0x1C)**

These are straightforward text or single-byte messages. Their encodings are documented in `protocol.h` (lines 51-75) alongside each `#define`.

---

## 5. Concurrency Model

This project uses **Category 2** concurrency: a single `select()` call inside the server's main event loop handles all I/O without threads or `fork()`.

### The select() Loop

The main loop in `server.c` (line 430, `main()`) rebuilds the `fd_set` on every iteration:

1. **Before game starts:** `listen_fd` is added to the read set (line 477) to accept new connections.
2. **Always:** Every connected player's socket `g.players[i].fd` is added (line 482).
3. `select()` is called (line 489) and blocks until at least one fd is ready.
4. If `listen_fd` is ready (line 497): `accept()` a new client, assign a slot.
5. For each player fd that is ready (line 526): `read()` into a per-player buffer, then extract complete messages with `recv_msg()`.

### Why the server never blocks on one client

- `read()` is only called when `FD_ISSET()` confirms data is available (line 529-531). A slow or silent client does not prevent the server from servicing others.
- Each player has an independent receive buffer (`players[i].buf`, `players[i].buf_len`). Partial messages remain in the buffer until the next `read()` completes them. The `recv_msg()` function (`protocol.h`, line 139) checks whether a full TLV message is available before extracting it.
- All game logic functions (`handle_play`, `handle_draw`, etc.) are non-blocking: they update state, call `net_*` broadcast functions, and return immediately.

### Client-side concurrency

The client (`client.c`, line 506) uses a similar `select()` loop over two fds: `STDIN_FILENO` (keyboard input) and the server socket. This allows the client to display incoming server messages (other players' moves, chat) even while the user is idle.

---

## 6. Error Handling and Robustness

### Bad Behaviour 1: Client disconnects mid-game

**Scenario:** A player closes their terminal or loses network connectivity while the game is in progress.

**Detection:** `read()` returns 0 (clean close) or -1 (error) in the main select loop (`server.c`, line 531).

**Handling:** `disconnect_player()` (`server.c`, line 317) performs the following cleanup:
- Closes the socket fd and sets `players[pid].fd = -1`.
- Sets `players[pid].connected = 0` and decrements `num_connected`.
- Broadcasts `MSG_PLAYER_LEFT` so other clients remove the player from their display.
- If the disconnected player was the current turn holder, calls `do_advance_turn()` to skip to the next player.
- If fewer than `MIN_PLAYERS` (2) remain, the last connected player is declared the winner via `MSG_GAME_OVER`.

No zombie file descriptors or leaked resources remain after disconnection.

### Bad Behaviour 2: Client sends invalid or out-of-turn commands

**Scenario:** A player sends `MSG_PLAY` when it is not their turn, provides an out-of-range card index, tries to play a card that does not match, tries to pass without drawing first, or sends a Wild card without choosing a color.

**Handling:** Every action handler validates input before mutating state:
- `handle_play()` (`server.c`, line 100) checks: `pid == g.current`, `idx` is in range, `game_can_play()` returns true, and wild cards have `chosen_color < NUM_COLORS`.
- `handle_pass()` (`server.c`, line 220) checks `players[pid].has_drawn`.
- `handle_draw()` (`server.c`, line 194) checks `!players[pid].has_drawn`.
- On failure, `net_send_error()` sends a `MSG_ERROR` with a human-readable explanation. The game state is never modified by an invalid action.
- Additionally, the client performs pre-validation in `input.c` (`parse_command()`, line 121) and `client.c` (`execute_command()`, line 401): wild cards without a color choice are caught client-side before any message is sent.

### Bad Behaviour 3: Broken pipe (SIGPIPE) when writing to a closed socket

**Scenario:** The server attempts to broadcast a message to a player who has already disconnected (e.g., the OS has closed the socket but the server has not yet detected it via `read()`).

**Handling:**
- `signal(SIGPIPE, SIG_IGN)` at server startup (`server.c`, line 440) prevents the default SIGPIPE termination.
- `send_msg()` (`protocol.h`, line 111) checks every `write()` return value. If `write()` returns -1 or 0, `send_msg()` returns -1.
- On the client side, every `send_msg()` call checks the return value. A failure sets `view.connected = 0`, which causes the main loop to exit cleanly.
- The server will detect the dead connection on the next `select()` cycle when `read()` returns 0, triggering `disconnect_player()`.

---

## 7. Team Contributions

*(Fill in one paragraph per team member describing their specific contributions.)*

**Member 1 (Name):** ...

**Member 2 (Name):** ...

**Member 3 (Name):** ...
