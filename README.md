# Kayles Game
A networked, two-player implementation of the classic combinatorial game **Kayles**, built in C++23 with a UDP client/server architecture. 

> Project for **Sieci Komputerowe** (Computer Networks), summer semester 2025/26, University of Warsaw.

## What is Kayles?
Kayles is played on a row of standing pawns. Players take turns either knocking down a single pawn or two adjacent pawns. The player who makes the last move **wins**. A game ends when:
 
- No legal move is available for the active player (normal end),
- A player sends `GIVE_UP` (forfeit), or
- The session receives no messages for `timeout` seconds (abandoned).
---

## Architecture
The server is **single-threaded** and driven by a `recvfrom()` event loop — no threads, no async framework. All game sessions are stored in an `unordered_map<uint32_t, GameState>` keyed by game ID. Stale sessions are evicted on every received packet and whenever the `SO_RCVTIMEO` fires.
 
The client is a one-shot tool: send one hand-crafted message, print the decoded response, and exit.

## Protocol
All integers are transmitted in **network byte order** (big-endian).

## Building
 
### Prerequisites
- CMake ≥ 3.31
- A C++23-capable compiler (GCC 13+ or Clang 16+)

### CMake
```bash
mkdir build && cd build
cmake ..
make
```
 
This produces two executables: `kayles_server` (from `kayles_server.cpp`) and `kayles_client` (from `kayles_client.cpp`). Both are built from the same CMake target.

### Makefile (alternative)
```bash
make
```
 
---

## Running
 
### Server
All flags are **required** and can appear in any order.
 
```
./kayles_server -r <pawn_row> -a <address> -p <port> -t <timeout>
```

| Flag | Description |
|------|-------------|
| `-r` | Initial pawn layout — a string of `0`s and `1`s. Must start and end with `1`. Length: 1–256. |
| `-a` | IPv4 address to bind to (dotted-decimal or hostname). |
| `-p` | UDP port to listen on (`0` = OS-assigned). |
| `-t` | Session inactivity timeout in seconds (1–99). |

**Example:**
```bash
./kayles_server -r 11111111 -a 127.0.0.1 -p 8080 -t 10
```

### Client
 
All flags are **required**.
 
```
./kayles_client -m <message> -a <address> -p <port> -t <timeout>
```
 
Messages are `/`-delimited strings matching the protocol format:
 
```bash
# Join a game as player 42
./kayles_client -m "0/42" -a 127.0.0.1 -p 8080 -t 5
 
# Knock down pawn 3 in game 1 as player 42
./kayles_client -m "1/42/1/3" -a 127.0.0.1 -p 8080 -t 5
 
# Knock down pawns 3 and 4 in game 1 as player 42
./kayles_client -m "2/42/1/3" -a 127.0.0.1 -p 8080 -t 5
 
# Keep-alive ping
./kayles_client -m "3/42/1" -a 127.0.0.1 -p 8080 -t 5
 
# Give up
./kayles_client -m "4/42/1" -a 127.0.0.1 -p 8080 -t 5
```
 
---

## Project Structure
 
```
.
├── kayles_server.cpp       # Server entry point and main event loop
├── kayles_client.cpp       # Client entry point
├── game_logic.h/.cpp       # Board representation and game-state transitions
├── message_handlers.h/.cpp # Per-message-type server logic
├── client_messages.h/.cpp  # Client-side message serialization
├── common.h/.cpp           # Shared utilities: arg parsing, network I/O helpers
├── err.h/.cpp              # Fatal error helpers (syserr / fatal)
├── CMakeLists.txt
└── Makefile
```
 
---

## Notes
 
- `player_id` must be non-zero; `0` is reserved as a sentinel for `WAITING_FOR_OPPONENT`.
- A player is identified by their `player_id`, not by their source IP/port, so NAT traversal and port changes are handled transparently.
- On resource exhaustion (`std::bad_alloc` or game-ID space full), `MSG_JOIN` is silently dropped — no response is sent.
- Illegal moves (wrong turn, already-knocked pawn, out-of-range index) return the current game state unchanged; they are not protocol errors.
