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

static void send_raw(int socket_fd, const struct sockaddr_in& client_addr,
                     const char* data, size_t len) {
    ssize_t sent = sendto(socket_fd, data, len, 0,
                          reinterpret_cast<const struct sockaddr*>(&client_addr),
                          sizeof(client_addr));
    if (sent < 0) {
        syserr("failed to send response to client");
    }
}

static GameState* find_game_and_verify_players(
    uint32_t game_id, uint32_t player_id,
    const char* buf, size_t len,
    uint8_t error_idx_game, uint8_t error_idx_player,
    std::unordered_map<uint32_t, GameState>& active_games,
    int socket_fd, const struct sockaddr_in& client_addr) {
    auto it = active_games.find(game_id);
    if (it == active_games.end()) {
        handle_wrong_message(buf, len, error_idx_game, socket_fd, client_addr);
        return nullptr;
    }

    GameState& game = it->second;
    if (game.player_a_id != player_id && game.player_b_id != player_id) {
        handle_wrong_message(buf, len, error_idx_player, socket_fd, client_addr);
        return nullptr;
    }

    return &game;
}

static void send_game_state(const GameState& game_state, uint32_t game_id,
                            int socket_fd, const struct sockaddr_in& client_addr) {
    const size_t pawn_bytes = game_state.pawn_row.size();
    const size_t msg_size = 4u + 4u + 4u + 1u + 1u + pawn_bytes;

    std::vector<char> response(msg_size);
    size_t off = 0;

    write_u32(response, off, game_id); // 4 B
    write_u32(response, off, game_state.player_a_id); // 4 B
    write_u32(response, off, game_state.player_b_id); // 4 B
    response[off++] = static_cast<char>(game_state.status); // 1 B
    response[off++] = static_cast<char>(game_state.max_pawn); // 1 B
    memcpy(response.data() + off, // N B
           game_state.pawn_row.data(), pawn_bytes);

    send_raw(socket_fd, client_addr, response.data(), msg_size);
}

void handle_join_game(const char* buf, size_t len,
                      std::unordered_map<uint32_t, GameState>& active_games,
                      const GameState& template_game,
                      int socket_fd, const struct sockaddr_in& client_addr) {
    if (len != JOIN_SIZE) {
        uint8_t err_idx = static_cast<uint8_t>(len < JOIN_SIZE ? len : JOIN_SIZE);
        handle_wrong_message(buf, len, err_idx, socket_fd, client_addr);
        return;
    }

    const uint32_t assigned_player_id = read_u32(buf, OFF_PLAYER_ID);
    if (assigned_player_id == 0) {
        handle_wrong_message(buf, len, OFF_PLAYER_ID, socket_fd, client_addr);
        return;
    }

    uint32_t current_game_id = 0;
    bool found = false;

    for (auto& game : active_games) {
        if (game.second.status == WAITING_FOR_OPPONENT) {
            game.second.player_b_id = assigned_player_id;
            game.second.status = TURN_B;
            game.second.last_activity = time(nullptr);

            current_game_id = game.first;
            found = true;

            std::cout << "player no " << assigned_player_id
                << " joined game no " << current_game_id << std::endl;
            break;
        }
    }

    if (!found) {
        if (active_games.size() >= static_cast<size_t>(UINT32_MAX) - 1u) {
            return;
        }

        while (active_games.find(next_game_id) != active_games.end()) {
            next_game_id = (next_game_id == UINT32_MAX) ? 1u : next_game_id + 1u;
        }

        current_game_id = next_game_id;
        next_game_id = (next_game_id == UINT32_MAX) ? 1u : next_game_id + 1u;

        try {
            GameState new_game = template_game;
            new_game.player_a_id = assigned_player_id;
            new_game.player_b_id = WAITING_FOR_OPPONENT;
            new_game.status = WAITING_FOR_OPPONENT;
            new_game.last_activity = time(nullptr);

            active_games[current_game_id] = new_game;
            std::cout << "game no " << current_game_id
                << " created. player no " << assigned_player_id << " joined it." << std::endl;
        }
        catch (const std::bad_alloc&) {
            return;
        }
    }

    GameState& current_game = active_games[current_game_id];
    send_game_state(current_game, current_game_id, socket_fd, client_addr);
}

void handle_make_move_one(const char* buf, size_t len,
                          std::unordered_map<uint32_t, GameState>& active_games,
                          const GameState& /*template_game*/,
                          int socket_fd, const struct sockaddr_in& client_addr) {
    if (len != MOVE_SIZE) {
        uint8_t err_idx = static_cast<uint8_t>(len < MOVE_SIZE ? len : MOVE_SIZE);
        handle_wrong_message(buf, len, err_idx, socket_fd, client_addr);
        return;
    }

    const uint32_t player_id = read_u32(buf, OFF_PLAYER_ID); // bajty 1-4
    if (player_id == 0) {
        handle_wrong_message(buf, len, OFF_PLAYER_ID, socket_fd, client_addr);
        return;
    }
    const uint32_t game_id = read_u32(buf, OFF_GAME_ID); // bajty 5-8
    const uint8_t pawn_idx = static_cast<uint8_t>(buf[OFF_PAWN_IDX]);

    GameState* game = find_game_and_verify_players(
        game_id, player_id, buf, len,
        static_cast<uint8_t>(OFF_GAME_ID), // error: nieprawidłowe game_id
        static_cast<uint8_t>(OFF_PLAYER_ID), // error: gracz nie w tej grze
        active_games, socket_fd, client_addr);
    if (!game) return;

    if (game->player_a_id == player_id && game->status != TURN_A) {
        send_game_state(*game, game_id, socket_fd, client_addr);
        return;
    }
    if (game->player_b_id == player_id && game->status != TURN_B) {
        send_game_state(*game, game_id, socket_fd, client_addr);
        return;
    }

    if (!is_legal_move(*game, pawn_idx)) {
        send_game_state(*game, game_id, socket_fd, client_addr);
        return;
    }


    knock_pawn_down(*game, pawn_idx);

    game->status = verify_game_state_after_move(*game);
    game->last_activity = time(nullptr);

    send_game_state(*game, game_id, socket_fd, client_addr);
}

void handle_make_move_two(const char* buf, size_t len,
                          std::unordered_map<uint32_t, GameState>& active_games,
                          const GameState& /*template_game*/,
                          int socket_fd, const struct sockaddr_in& client_addr) {
    if (len != MOVE_SIZE) {
        uint8_t err_idx = static_cast<uint8_t>(len < MOVE_SIZE ? len : MOVE_SIZE);
        handle_wrong_message(buf, len, err_idx, socket_fd, client_addr);
        return;
    }

    const uint32_t player_id = read_u32(buf, OFF_PLAYER_ID);
    if (player_id == 0) {
        handle_wrong_message(buf, len, OFF_PLAYER_ID, socket_fd, client_addr);
        return;
    }
    const uint32_t game_id = read_u32(buf, OFF_GAME_ID);
    const uint8_t first_pawn_idx = static_cast<uint8_t>(buf[OFF_PAWN_IDX]);
    const uint8_t second_pawn_idx = first_pawn_idx + 1;

    GameState* game = find_game_and_verify_players(
        game_id, player_id, buf, len,
        static_cast<uint8_t>(OFF_GAME_ID),
        static_cast<uint8_t>(OFF_PLAYER_ID),
        active_games, socket_fd, client_addr);
    if (!game) return;

    if (game->player_a_id == player_id && game->status != TURN_A) {
        send_game_state(*game, game_id, socket_fd, client_addr);
        return;
    }
    if (game->player_b_id == player_id && game->status != TURN_B) {
        send_game_state(*game, game_id, socket_fd, client_addr);
        return;
    }

    if (!is_legal_move(*game, first_pawn_idx) || !is_legal_move(*game, second_pawn_idx)) {
        send_game_state(*game, game_id, socket_fd, client_addr);
        return;
    }

    knock_pawn_down(*game, first_pawn_idx);
    knock_pawn_down(*game, second_pawn_idx);
    game->status = verify_game_state_after_move(*game);
    game->last_activity = time(nullptr);

    send_game_state(*game, game_id, socket_fd, client_addr);
}

void handle_give_up(const char* buf, size_t len,
                    std::unordered_map<uint32_t, GameState>& active_games,
                    const GameState& /*template_game*/,
                    int socket_fd, const struct sockaddr_in& client_addr) {
    if (len != GIVE_UP_SIZE) {
        uint8_t err_idx = static_cast<uint8_t>(len < GIVE_UP_SIZE ? len : GIVE_UP_SIZE);
        handle_wrong_message(buf, len, err_idx, socket_fd, client_addr);
        return;
    }

    const uint32_t player_id = read_u32(buf, OFF_PLAYER_ID);
    if (player_id == 0) {
        handle_wrong_message(buf, len, OFF_PLAYER_ID, socket_fd, client_addr);
        return;
    }
    const uint32_t game_id = read_u32(buf, OFF_GAME_ID);

    GameState* game = find_game_and_verify_players(
        game_id, player_id, buf, len,
        static_cast<uint8_t>(OFF_GAME_ID),
        static_cast<uint8_t>(OFF_PLAYER_ID),
        active_games, socket_fd, client_addr);
    if (!game) return;

    if (game->player_a_id == player_id && game->status != TURN_A) return;
    if (game->player_b_id == player_id && game->status != TURN_B) return;

    game->status = (game->player_a_id == player_id) ? WIN_B : WIN_A;
    game->last_activity = time(nullptr);

    send_game_state(*game, game_id, socket_fd, client_addr);
}

void handle_keep_alive(const char* buf, size_t len,
                       std::unordered_map<uint32_t, GameState>& active_games,
                       const GameState& /*template_game*/,
                       int socket_fd, const struct sockaddr_in& client_addr) {
    if (len != KEEP_ALIVE_SIZE) {
        uint8_t err_idx = static_cast<uint8_t>(len < KEEP_ALIVE_SIZE ? len : KEEP_ALIVE_SIZE);
        handle_wrong_message(buf, len, err_idx, socket_fd, client_addr);
        return;
    }

    const uint32_t player_id = read_u32(buf, OFF_PLAYER_ID);
    if (player_id == 0) {
        handle_wrong_message(buf, len, OFF_PLAYER_ID, socket_fd, client_addr);
        return;
    }
    const uint32_t game_id = read_u32(buf, OFF_GAME_ID);

    GameState* game = find_game_and_verify_players(
        game_id, player_id, buf, len,
        static_cast<uint8_t>(OFF_GAME_ID),
        static_cast<uint8_t>(OFF_PLAYER_ID),
        active_games, socket_fd, client_addr);
    if (!game) return;

    game->last_activity = time(nullptr);
    send_game_state(*game, game_id, socket_fd, client_addr);
}

void handle_wrong_message(const char* buf, size_t len,
                          uint8_t error_index,
                          int socket_fd, const struct sockaddr_in& client_addr) {
    char response[WRONG_MSG_LEN] = {};

    const size_t copy_len = std::min(len, static_cast<size_t>(MESSAGE_LEN));
    memcpy(response, buf, copy_len);

    response[MESSAGE_LEN] = static_cast<char>(ERROR_STATUS); 
    response[MESSAGE_LEN + 1] = static_cast<char>(error_index);

    send_raw(socket_fd, client_addr, response, static_cast<size_t>(WRONG_MSG_LEN));
}
