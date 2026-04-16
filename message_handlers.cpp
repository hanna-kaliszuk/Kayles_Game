/**
 * @file message_handlers.cpp
 * @brief Implementation of message-handling utilities
**/

#include "message_handlers.h"

#include <arpa/inet.h>
#include <sys/socket.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <iostream>
#include <new>
#include <vector>

#include "common.h"
#include "err.h"

static uint32_t next_game_id = 1;

constexpr uint8_t ERROR_IDX_GAME = 5;
constexpr uint8_t ERROR_IDX_PLAYER = 1;
// game_id (4B) + player_a_id (4B) + player_b_id (4B) + game_status (1B) + max_pawn_idx (1B)
constexpr size_t BASIC_MSG_SIZE = 14u;
constexpr size_t INVALID_PLAYER_ID = 0u;

/**
 * @brief Helper function to send a raw binary response to the client.
 *
 * In case sentto fails, a syserr function is called. 
 * 
 * @param socket_fd server socket file descriptor
 * @param client_addr the address structure of the receiving client
 * @param data pointer to the raw binary data to be sent
 * @param len number of bytes to send
**/
static void send_raw(int socket_fd, const struct sockaddr_in& client_addr, const char* data, size_t len) {
    ssize_t sent = sendto(socket_fd, data, len, 0, reinterpret_cast<const struct sockaddr*>(&client_addr),
                          sizeof(client_addr));
    if (sent < 0) {
        syserr("failed to send response to client");
    }
}

/**
 * @brief Helper function to find a game in the map of active games and verify if the player with the given id is
 * a registered player there.
 *
 * Validates if the game exists and if the player is registered in it. If not, it sends error response to the client.
 *
 * @param game_id the unique identifier of the game to find
 * @param player_id the unique identifier of the player attempting to make a move
 * @param buf the raw network buffer received from the client
 * @param len the size of received buffer
 * @param active_games the map of active games
 * @param socket_fd the server socket file descriptor
 * @param client_addr the address structure of the client
 * @return a pointer to the valid GameState if all checks pass, or nullptr if validation fails.
**/
static GameState* find_game_and_verify_players(uint32_t game_id, uint32_t player_id, const char* buf, size_t len,
                                               std::unordered_map<uint32_t, GameState>& active_games,
                                               int socket_fd, const struct sockaddr_in& client_addr) {
    auto it = active_games.find(game_id);

    // ensure the game exists. if not, abort and point the client to the byte offset where the invalid game_id was read
    if (it == active_games.end()) {
        handle_wrong_message(buf, len, ERROR_IDX_GAME, socket_fd, client_addr);
        return nullptr;
    }

    GameState& game = it->second;

    // ensure the player is registered in the game. if not, abort and point the client to the byte offset where the
    // invalid player_id was read
    if (game.player_a_id != player_id && game.player_b_id != player_id) {
        handle_wrong_message(buf, len, ERROR_IDX_PLAYER, socket_fd, client_addr);
        return nullptr;
    }

    return &game;
}

/**
 * @brief Helper function to send the current game state to the client.
 *
 * The function calculates the size of the message, writes in the parameters in the specified order(game_id -> player_a_id -> player_b_id -> game_state -> max_pawn)
 * and sends the message to the client. 
 * 
 * @param game_state the game state structure of the game
 * @param game_id the unique identifier of the game which state is to be sent
 * @param socket_fd the server socket file descriptor
 * @param client_addr the address structure of the client
**/
static void send_game_state(const GameState& game_state, uint32_t game_id, int socket_fd,
                            const struct sockaddr_in& client_addr) {
    // calculate the exact message size
    const size_t pawn_bytes = game_state.pawn_row.size();
    const size_t msg_size = BASIC_MSG_SIZE + pawn_bytes;

    std::vector<char> response(msg_size);

    // to track the write position
    size_t off = 0;

    write_u32(response, off, game_id);
    write_u32(response, off, game_state.player_a_id);
    write_u32(response, off, game_state.player_b_id);

    response[off++] = static_cast<char>(game_state.status);
    response[off++] = static_cast<char>(game_state.max_pawn);

    // append the pawn data to the message buffer
    memcpy(response.data() + off, game_state.pawn_row.data(), pawn_bytes);

    send_raw(socket_fd, client_addr, response.data(), msg_size);
}


void handle_join_game(const char* buf, size_t len, std::unordered_map<uint32_t, GameState>& active_games,
                      const GameState& template_game, int socket_fd, const struct sockaddr_in& client_addr) {
    // validate message length
    if (len != JOIN_SIZE) {
        const auto err_idx = static_cast<uint8_t>(len < JOIN_SIZE ? len : JOIN_SIZE);
        handle_wrong_message(buf, len, err_idx, socket_fd, client_addr);
        return;
    }

    // extract and validate player_id
    const uint32_t assigned_player_id = read_u32(buf, OFF_PLAYER_ID);
    if (assigned_player_id == INVALID_PLAYER_ID) {
        handle_wrong_message(buf, len, OFF_PLAYER_ID, socket_fd, client_addr);
        return;
    }

    uint32_t current_game_id = 0;
    bool found = false;

    // try to find an active game with only one player registered
    for (auto& game : active_games) {
        if (game.second.status == WAITING_FOR_OPPONENT) {
            // assign the client as the second player and start the game
            game.second.player_b_id = assigned_player_id;
            game.second.status = TURN_B;
            game.second.last_activity = time(nullptr);

            current_game_id = game.first;
            found = true;

            std::cout << "player no " << assigned_player_id << " joined game no " << current_game_id << std::endl;
            break;
        }
    }

    // if no game found, create a new one
    if (!found) {
        // ensure a new game can be registered
        if (active_games.size() >= static_cast<size_t>(UINT32_MAX) - 1u) {
            return;
        }

        // endure a unique game_id is available
        while (active_games.find(next_game_id) != active_games.end()) {
            next_game_id = (next_game_id == UINT32_MAX) ? 1u : next_game_id + 1u;
        }

        current_game_id = next_game_id;
        next_game_id = (next_game_id == UINT32_MAX) ? 1u : next_game_id + 1u;

        try {
            // initialize new game from template
            GameState new_game = template_game;
            new_game.player_a_id = assigned_player_id;
            new_game.player_b_id = WAITING_FOR_OPPONENT;
            new_game.status = WAITING_FOR_OPPONENT;
            new_game.last_activity = time(nullptr);

            active_games[current_game_id] = new_game;
            std::cout << "game no " << current_game_id << " created. player no " << assigned_player_id
            << " joined it." << std::endl;
        }
        catch (const std::bad_alloc&) {
            // fail silently
            return;
        }
    }

    GameState& current_game = active_games[current_game_id];

    send_game_state(current_game, current_game_id, socket_fd, client_addr);
}

void handle_make_move_one(const char* buf, size_t len, std::unordered_map<uint32_t, GameState>& active_games,
                          const GameState& /*template_game*/, int socket_fd, const struct sockaddr_in& client_addr) {
    // validate message length
    if (len != MOVE_SIZE) {
        const auto err_idx = static_cast<uint8_t>(len < MOVE_SIZE ? len : MOVE_SIZE);
        handle_wrong_message(buf, len, err_idx, socket_fd, client_addr);
        return;
    }

    // extract and verify player_id
    const uint32_t player_id = read_u32(buf, OFF_PLAYER_ID);
    if (player_id == INVALID_PLAYER_ID) {
        handle_wrong_message(buf, len, OFF_PLAYER_ID, socket_fd, client_addr);
        return;
    }

    // extract game_id and pawn_idx
    const uint32_t game_id = read_u32(buf, OFF_GAME_ID);
    const auto pawn_idx = static_cast<uint8_t>(buf[OFF_PAWN_IDX]);

    // verify that the game exists and the requesting client is a registered player
    GameState* game = find_game_and_verify_players(game_id, player_id, buf, len,active_games, socket_fd, client_addr);
    if (!game) {
        handle_wrong_message(buf, len, ERROR_IDX_GAME, socket_fd, client_addr);
        return;
    }

    // verify that it is the clients turn
    if (game->player_a_id == player_id && game->status != TURN_A) {
        send_game_state(*game, game_id, socket_fd, client_addr);
        return;
    }
    if (game->player_b_id == player_id && game->status != TURN_B) {
        send_game_state(*game, game_id, socket_fd, client_addr);
        return;
    }

    // check if the move is legal
    if (!is_legal_move(*game, pawn_idx)) {
        send_game_state(*game, game_id, socket_fd, client_addr);
        return;
    }

    // make the move
    knock_pawn_down(*game, pawn_idx);

    // change game state
    game->status = verify_game_state_after_move(*game);
    game->last_activity = time(nullptr);

    send_game_state(*game, game_id, socket_fd, client_addr);
}

void handle_make_move_two(const char* buf, size_t len, std::unordered_map<uint32_t, GameState>& active_games,
                          const GameState& /*template_game*/, int socket_fd, const struct sockaddr_in& client_addr) {
    // validate message length
    if (len != MOVE_SIZE) {
        const auto err_idx = static_cast<uint8_t>(len < MOVE_SIZE ? len : MOVE_SIZE);
        handle_wrong_message(buf, len, err_idx, socket_fd, client_addr);
        return;
    }

    // extract and verify player_id
    const uint32_t player_id = read_u32(buf, OFF_PLAYER_ID);
    if (player_id == INVALID_PLAYER_ID) {
        handle_wrong_message(buf, len, OFF_PLAYER_ID, socket_fd, client_addr);
        return;
    }

    // extract game_id and pawn_idxs
    const uint32_t game_id = read_u32(buf, OFF_GAME_ID);
    const uint8_t first_pawn_idx = static_cast<uint8_t>(buf[OFF_PAWN_IDX]);
    const uint8_t second_pawn_idx = first_pawn_idx + 1;

    // verify that the game exists and the requesting client is a registered player
    GameState* game = find_game_and_verify_players(game_id, player_id, buf, len,active_games, socket_fd, client_addr);
    if (!game) {
        handle_wrong_message(buf, len, ERROR_IDX_GAME, socket_fd, client_addr);
        return;
    }

    // verify that it is the clients turn
    if (game->player_a_id == player_id && game->status != TURN_A) {
        send_game_state(*game, game_id, socket_fd, client_addr);
        return;
    }
    if (game->player_b_id == player_id && game->status != TURN_B) {
        send_game_state(*game, game_id, socket_fd, client_addr);
        return;
    }

    // check if the move is legal
    if (!is_legal_move(*game, first_pawn_idx) || !is_legal_move(*game, second_pawn_idx)) {
        send_game_state(*game, game_id, socket_fd, client_addr);
        return;
    }

    // make the move
    knock_pawn_down(*game, first_pawn_idx);
    knock_pawn_down(*game, second_pawn_idx);

    // change game state
    game->status = verify_game_state_after_move(*game);
    game->last_activity = time(nullptr);

    send_game_state(*game, game_id, socket_fd, client_addr);
}

void handle_give_up(const char* buf, size_t len, std::unordered_map<uint32_t, GameState>& active_games,
                    const GameState& /*template_game*/, int socket_fd, const struct sockaddr_in& client_addr) {
    // validate message length
    if (len != GIVE_UP_SIZE) {
        const auto err_idx = static_cast<uint8_t>(len < GIVE_UP_SIZE ? len : GIVE_UP_SIZE);
        handle_wrong_message(buf, len, err_idx, socket_fd, client_addr);
        return;
    }

    // extract and verify player_id
    const uint32_t player_id = read_u32(buf, OFF_PLAYER_ID);
    if (player_id == INVALID_PLAYER_ID) {
        handle_wrong_message(buf, len, OFF_PLAYER_ID, socket_fd, client_addr);
        return;
    }

    // extract game_id
    const uint32_t game_id = read_u32(buf, OFF_GAME_ID);

    // verify that the game exists and the requesting client is a registered player
    GameState* game = find_game_and_verify_players(game_id, player_id, buf, len,active_games, socket_fd, client_addr);
    if (!game) {
        handle_wrong_message(buf, len, ERROR_IDX_GAME, socket_fd, client_addr);
        return;
    }

    // verify that it is the clients turn
    if (game->player_a_id == player_id && game->status != TURN_A) {
        send_game_state(*game, game_id, socket_fd, client_addr);
        return;
    }
    if (game->player_b_id == player_id && game->status != TURN_B) {
        send_game_state(*game, game_id, socket_fd, client_addr);
        return;
    }

    // change game state accordingly
    game->status = (game->player_a_id == player_id) ? WIN_B : WIN_A;
    game->last_activity = time(nullptr);

    send_game_state(*game, game_id, socket_fd, client_addr);
}

void handle_keep_alive(const char* buf, size_t len, std::unordered_map<uint32_t, GameState>& active_games,
                       const GameState& /*template_game*/, int socket_fd, const struct sockaddr_in& client_addr) {
    // validate message length
    if (len != KEEP_ALIVE_SIZE) {
        const auto err_idx = static_cast<uint8_t>(len < KEEP_ALIVE_SIZE ? len : KEEP_ALIVE_SIZE);
        handle_wrong_message(buf, len, err_idx, socket_fd, client_addr);
        return;
    }

    // extract and verify player_id
    const uint32_t player_id = read_u32(buf, OFF_PLAYER_ID);
    if (player_id == INVALID_PLAYER_ID) {
        handle_wrong_message(buf, len, OFF_PLAYER_ID, socket_fd, client_addr);
        return;
    }

    // extract game_id
    const uint32_t game_id = read_u32(buf, OFF_GAME_ID);

    // verify that the game exists and the requesting client is a registered player
    GameState* game = find_game_and_verify_players(game_id, player_id, buf, len, active_games, socket_fd, client_addr);
    if (!game) {
        handle_wrong_message(buf, len, ERROR_IDX_GAME, socket_fd, client_addr);
        return;
    }

    // change game state accordingly
    game->last_activity = time(nullptr);

    send_game_state(*game, game_id, socket_fd, client_addr);
}

void handle_wrong_message(const char* buf, size_t len, uint8_t error_index, int socket_fd, const struct sockaddr_in& client_addr) {
    // initialize an empty buffer
    char response[WRONG_MSG_LEN] = {};

    // echo back the original response. use min function to prevent buffer overload in case a client sent a massive garbage packet
    const size_t copy_len = std::min(len, static_cast<size_t>(MESSAGE_LEN));
    memcpy(response, buf, copy_len);

    // append the protocol's error status code (255) and the specific byte index where the first detected error ocurred.
    response[MESSAGE_LEN] = static_cast<char>(ERROR_STATUS);
    response[MESSAGE_LEN + 1] = static_cast<char>(error_index);

    send_raw(socket_fd, client_addr, response, static_cast<size_t>(WRONG_MSG_LEN));
}
