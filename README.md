# Kayles Game
![CI](https://github.com/hanna-kaliszuk/Kayles_Game/actions/workflows/test.yml/badge.svg?branch=main)


**A C++23 networked game built directly on UDP sockets.**

Kayles Game is a networked, two-player implementation of the classic combinatorial game Kayles, built around a **single-threaded UDP server** and a command-line client.

The project focuses on low-level network programming, custom binary protocols, server-side state management, timeout handling, and protocol validation using the **POSIX sockets API**.

---

## Why this project?

The project implements the complete communication path between a command-line client and a stateful game server:

```text
Client
  │
  │ UDP datagram
  ▼
Server
  │
  ├── Message validation
  │
  ├── Player / game lookup
  │
  ├── Game state transition
  │
  ├── Timeout handling
  │
  └── Response serialization
  │
  ▼
Client
```

The server handles multiple independent games in a single process and maintains their state between individual UDP messages.

---

## Key Features

### UDP networking

- Direct communication through **POSIX sockets**
- **IPv4** UDP communication
- Client/server architecture
- Network byte order for transmitted integer values
- One-shot command-line client
- Single-threaded server event loop based on `recvfrom()`

### Stateful game server

The server maintains multiple concurrent game sessions and associates them with unique game IDs.

Each game stores:

- participating player IDs
- current game status
- maximum pawn index
- current pawn layout
- game result

The server supports:

- creating a waiting game
- joining an existing waiting game
- multiple independent games
- a player participating in multiple games
- the same player occupying both roles in one game

### Game logic

Players can:

- knock down one pawn
- knock down two adjacent pawns
- send keep-alive messages
- give up a game

The server validates moves and preserves the game state when a move is illegal.

A game can finish through:

- removing the last remaining pawn
- player forfeit
- inactivity timeout

### Custom binary protocol

Client and server communicate using a compact binary protocol.

All multi-byte integer values are transmitted in **network byte order**.

The protocol defines separate message types for:

```text
JOIN
MOVE_1
MOVE_2
KEEP_ALIVE
GIVE_UP
```

Server responses contain either the current game state or a structured `WRONG_MSG` response for malformed or invalid protocol messages.

The server also reports the position of an invalid field through the protocol's `error_index` value.

---

## Testing

The repository contains an automated test suite implemented with Python's **`unittest`** framework.

The tests launch the actual `kayles_server` and communicate with it over UDP, exercising the implementation as an integration test rather than testing isolated functions.

The suite covers, among other things:

- game creation
- joining existing games
- a player participating on both sides
- legal moves
- illegal moves
- wrong turns
- already-removed pawns
- out-of-bounds moves
- malformed messages
- truncated messages
- trailing bytes
- invalid participants
- unknown game IDs
- waiting-game timeouts
- finished-game retention and expiration
- independent games

The test suite also validates the binary representation of game-state and error messages.

Run the tests with:

```bash
python3 -m unittest test_kayles.py -v
```

---

## Continuous Integration

The repository uses **GitHub Actions** to automatically:

1. build the C++ client and server,
2. run the complete Python integration test suite.

This ensures that changes are checked against both the compilation step and the existing network-level tests.

---

## Architecture

The implementation is divided into several focused components:

| Component | Responsibility |
|---|---|
| `kayles_server.cpp` | Server entry point and main UDP event loop |
| `kayles_client.cpp` | Command-line client |
| `game_logic.cpp/.h` | Board representation and game-state transitions |
| `message_handlers.cpp/.h` | Server-side handling of protocol messages |
| `client_messages.cpp/.h` | Client-side message serialization |
| `common.cpp/.h` | Shared argument parsing and networking utilities |
| `err.cpp/.h` | Error handling helpers |

The server keeps active game sessions in an `unordered_map` indexed by game ID.

The server is intentionally **single-threaded** and processes incoming UDP datagrams as events.

---

## Server CLI

```text
./kayles_server -r <pawn_row> -a <address> -p <port> -t <timeout>
```

### Options

| Option | Description |
|---|---|
| `-r <pawn_row>` | Initial pawn layout consisting of `0` and `1` characters |
| `-a <address>` | IPv4 address to bind to |
| `-p <port>` | UDP port to listen on; `0` lets the OS select a port |
| `-t <timeout>` | Session inactivity timeout in seconds |

Example:

```bash
./kayles_server -r 11111111 -a 127.0.0.1 -p 8080 -t 10
```

---

## Client CLI

```text
./kayles_client -m <message> -a <address> -p <port> -t <timeout>
```

The client sends one protocol message, waits for the server response up to the configured timeout, prints the response, and exits.

Messages use slash-separated decimal values.

### Join a game

```bash
./kayles_client -m "0/42" -a 127.0.0.1 -p 8080 -t 5
```

### Remove one pawn

```bash
./kayles_client -m "1/42/1/3" -a 127.0.0.1 -p 8080 -t 5
```

### Remove two adjacent pawns

```bash
./kayles_client -m "2/42/1/3" -a 127.0.0.1 -p 8080 -t 5
```

### Keep the game alive

```bash
./kayles_client -m "3/42/1" -a 127.0.0.1 -p 8080 -t 5
```

### Give up

```bash
./kayles_client -m "4/42/1" -a 127.0.0.1 -p 8080 -t 5
```

---

## Build

### Requirements

- C++23-compatible compiler
- GNU Make
- Python 3 for the test suite

The project is compiled with strict warning flags:

```text
-Wall
-Wextra
-Werror
-Wsign-conversion
```

Build both executables:

```bash
make
```

This produces:

```text
kayles_server
kayles_client
```

Run the test suite:

```bash
python3 -m unittest test_kayles.py -v
```

Clean build artifacts:

```bash
make clean
```

---

## Project Structure

```text
.
├── kayles_server.cpp
├── kayles_client.cpp
├── game_logic.cpp
├── game_logic.h
├── message_handlers.cpp
├── message_handlers.h
├── client_messages.cpp
├── client_messages.h
├── common.cpp
├── common.h
├── err.cpp
├── err.h
├── test_kayles.py
├── Makefile
├── CMakeLists.txt
└── .github/
    └── workflows/
        └── ci.yml
```

---

## Technical Focus

- **C++23**
- **Network programming**
- **POSIX sockets**
- **UDP**
- **IPv4**
- **Binary protocol design**
- **Network byte order**
- **Client/server architecture**
- **Stateful server design**
- **Timeout and session management**
- **Protocol validation**
- **Error handling**
- **Automated integration testing**
- **GitHub Actions / Continuous Integration**

---

## Academic Context

Originally developed as part of the **Computer Networks (Sieci Komputerowe)** course at the **University of Warsaw**, summer semester 2025/26.
