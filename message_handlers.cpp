#include "message_handlers.h"

#include <arpa/inet.h>
#include <sys/socket.h>

#include <algorithm>
#include <ctime>
#include <iostream>
#include <new>

#include "common.h"
#include "err.h"

static uint32_t next_game_id = 1;

constexpr int JOIN_LEN = 2;
constexpr int MOVE_LEN = 4;
constexpr int GIVE_UP_LEN = 3;
constexpr int KEEP_ALIVE_LEN = 3;

static GameState* find_game_and_verify_players(uint32_t game_id, uint32_t player_id, const std::string& buffer,
                                               const std::vector<std::string>& parts,
                                               std::unordered_map<uint32_t, GameState>& active_games, int socket_fd,
                                               const struct sockaddr_in& client_addr) {
    auto it = active_games.find(game_id);
    if (it == active_games.end()) {
        auto error_index = static_cast<uint8_t>(parts[0].length() + parts[1].length() + 2);
        handle_wrong_message(buffer, error_index, socket_fd, client_addr);
        return nullptr;
    }

    GameState& game = it->second;

    if (game.player_a_id != player_id && game.player_b_id != player_id) {
        auto error_index = static_cast<uint8_t>(parts[0].length() + parts[1].length() + parts[2].length() + 3);
        handle_wrong_message(buffer, error_index, socket_fd, client_addr);
        return nullptr;
    }

    return &game;
}

static void send_response_to_client(int socket_fd, const struct sockaddr_in& client_addr, const std::string& message) {
    ssize_t sent_length = sendto(socket_fd, message.c_str(), message.length(), 0,
                                 reinterpret_cast<const struct sockaddr*>(&client_addr), sizeof(client_addr));

    if (sent_length < 0) {
        syserr("failed to send response to client");
    }
}

static void send_game_state(const GameState& game_state, uint32_t game_id, int socket_fd,
                            const struct sockaddr_in& client_addr) {
    std::string response = std::to_string(game_id) + "/" +
        std::to_string(game_state.player_a_id) + "/" +
        std::to_string(game_state.player_b_id) + "/" +
        std::to_string(game_state.status) + "/" +
        std::to_string(game_state.max_pawn) + "/" +
        serialize_pawn_row(game_state);

    send_response_to_client(socket_fd, client_addr, response);
}

void handle_join_game(const std::string& buffer, const std::vector<std::string>& parts, std::unordered_map<uint32_t,
                          GameState>& active_games, const GameState& template_game, int socket_fd,
                      const struct sockaddr_in& client_addr) {
    int err_idx = validate_message_format(buffer, parts, JOIN_LEN);

    if (err_idx != NO_ERROR) {
        handle_wrong_message(buffer, static_cast<uint8_t>(err_idx), socket_fd, client_addr);
        return;
    }

    uint32_t assigned_player_id;
    try {
        assigned_player_id = static_cast<uint32_t>(stoul(parts[1]));
    }
    catch (...) { // parse error
        handle_wrong_message(buffer, 2, socket_fd, client_addr);
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

            std::cout << "player no " << assigned_player_id << " joined game no " << current_game_id << std::endl;
            break;
        }
    }

    if (!found) {
        if (active_games.size() >= (static_cast<size_t>(UINT32_MAX) - 1)) {
            return;
        }

        while (active_games.find(next_game_id) != active_games.end()) {
            next_game_id = (next_game_id == UINT32_MAX) ? 1 : next_game_id + 1;
        }

        current_game_id = next_game_id;
        next_game_id = (next_game_id == UINT32_MAX) ? 1 : next_game_id + 1;

        try {
            GameState new_game = template_game;
            new_game.player_a_id = assigned_player_id;
            new_game.player_b_id = WAITING_FOR_OPPONENT;
            new_game.status = WAITING_FOR_OPPONENT;
            new_game.last_activity = time(nullptr);

            active_games[current_game_id] = new_game;
            std::cout << "game no " << current_game_id << " created. player no " << assigned_player_id << " joined it. "
                << std::endl;
        }
        catch (const std::bad_alloc& e) {
            return;
        }
    }

    GameState& current_game = active_games[current_game_id];
    send_game_state(current_game, current_game_id, socket_fd, client_addr);
}

void handle_make_move_one(const std::string& buffer, const std::vector<std::string>& parts,
                          std::unordered_map<uint32_t, GameState>& active_games,
                          const GameState& /*template_game*/, int socket_fd, const struct sockaddr_in& client_addr) {
    // sprawdzamy, czy wiadomość, która przyszła jest na pewno ok:
    int err_idx = validate_message_format(buffer, parts, MOVE_LEN);
    if (err_idx != NO_ERROR) {
        handle_wrong_message(buffer, static_cast<uint8_t>(err_idx), socket_fd, client_addr);
        return;
    }

    uint32_t game_id, player_id;
    uint32_t pawn_idx;

    try {
        player_id = static_cast<uint32_t>(stoul(parts[1]));
        game_id = static_cast<uint32_t>(stoul(parts[2]));
        pawn_idx = static_cast<uint32_t>(stoul(parts[3]));
    }
    catch (...) {
        handle_wrong_message(buffer, static_cast<uint8_t>(parts[0].length() + 1), socket_fd, client_addr);
        return;
    }

    // sprawdzamy, czy gra o podanym ID istnieje i czy gracz o podanym ID bierze udział w danej grze
    GameState* game = find_game_and_verify_players(game_id, player_id, buffer, parts, active_games, socket_fd,
                                                   client_addr);
    if (!game) {
        return;
    }

    // sprawdzamy, czy jest teraz pora na ruch tego gracza
    if (game->player_a_id == player_id && game->status != TURN_A) return;
    if (game->player_b_id == player_id && game->status != TURN_B) return;

    if (!is_legal_move(*game, pawn_idx)) {
        return;
    }

    knock_pawn_down(*game, pawn_idx);

    // jeżeli wszystko jest ok odsyłamy graczowi wiadomość
    game->status = verify_game_state_after_move(*game);
    game->last_activity = time(nullptr);

    send_game_state(*game, game_id, socket_fd, client_addr);
}

void handle_make_move_two(const std::string& buffer, const std::vector<std::string>& parts,
                          std::unordered_map<uint32_t, GameState>& active_games,
                          const GameState& /*template_game*/, int socket_fd, const struct sockaddr_in& client_addr) {
    // sprawdzamy, czy wiadomość, która przyszła jest na pewno ok:
    int err_idx = validate_message_format(buffer, parts, MOVE_LEN);
    if (err_idx != NO_ERROR) {
        handle_wrong_message(buffer, static_cast<uint8_t>(err_idx), socket_fd, client_addr);
        return;
    }

    uint32_t game_id, player_id;
    uint32_t first_pawn_idx;

    try {
        player_id = static_cast<uint32_t>(stoul(parts[1]));
        game_id = static_cast<uint32_t>(stoul(parts[2]));
        first_pawn_idx = static_cast<uint32_t>(stoul(parts[3]));
    }
    catch (...) {
        handle_wrong_message(buffer, static_cast<uint8_t>(parts[0].length() + 1), socket_fd, client_addr);
        return;
    }

    uint32_t second_pawn_idx = first_pawn_idx + 1;

    // sprawdzamy, czy gra o podanym ID istnieje i czy gracz o podanym ID bierze udział w danej grze
    GameState* game = find_game_and_verify_players(game_id, player_id, buffer, parts, active_games, socket_fd,
                                                   client_addr);
    if (!game) {
        return;
    }

    // sprawdzamy, czy jest teraz pora na ruch tego gracza
    if (game->player_a_id == player_id && game->status != TURN_A) return;
    if (game->player_b_id == player_id && game->status != TURN_B) return;

    if (!is_legal_move(*game, first_pawn_idx) || !is_legal_move(*game, second_pawn_idx)) return;

    // jeżeli wszystko jest ok, zmieniamy stan gry, odsyłamy graczowi wiadomość
    knock_pawn_down(*game, first_pawn_idx);
    knock_pawn_down(*game, second_pawn_idx);
    game->status = verify_game_state_after_move(*game);
    game->last_activity = time(nullptr);

    send_game_state(*game, game_id, socket_fd, client_addr);
}

void handle_give_up(const std::string& buffer, const std::vector<std::string>& parts,
                    std::unordered_map<uint32_t, GameState>& active_games,
                    const GameState& /*template_game*/, int socket_fd, const struct sockaddr_in& client_addr) {
    int err_idx = validate_message_format(buffer, parts, GIVE_UP_LEN);
    if (err_idx != NO_ERROR) {
        handle_wrong_message(buffer, static_cast<uint8_t>(err_idx), socket_fd, client_addr);
        return;
    }

    uint32_t game_id, player_id;
    try {
        player_id = static_cast<uint32_t>(stoul(parts[1]));
        game_id = static_cast<uint32_t>(stoul(parts[2]));
    }
    catch (...) {
        handle_wrong_message(buffer, static_cast<uint8_t>(parts[0].length() + 1), socket_fd, client_addr);
        return;
    }

    GameState* game = find_game_and_verify_players(game_id, player_id, buffer, parts, active_games, socket_fd,
                                                   client_addr);
    if (!game) {
        return;
    }

    if (game->player_a_id == player_id && game->status != TURN_A) return;
    if (game->player_b_id == player_id && game->status != TURN_B) return;

    if (game->player_a_id == player_id) {
        game->status = WIN_B;
    }
    else {
        game->status = WIN_A;
    }

    game->last_activity = time(nullptr);

    send_game_state(*game, game_id, socket_fd, client_addr);
}

void handle_keep_alive(const std::string& buffer, const std::vector<std::string>& parts,
                       std::unordered_map<uint32_t, GameState>& active_games,
                       const GameState& /*template_game*/, int socket_fd, const struct sockaddr_in& client_addr) {
    int err_idx = validate_message_format(buffer, parts, KEEP_ALIVE_LEN);
    if (err_idx != NO_ERROR) {
        handle_wrong_message(buffer, static_cast<uint8_t>(err_idx), socket_fd, client_addr);
        return;
    }

    uint32_t game_id, player_id;

    try {
        player_id = static_cast<uint32_t>(stoul(parts[1]));
        game_id = static_cast<uint32_t>(stoul(parts[2]));
    }
    catch (...) {
        handle_wrong_message(buffer, static_cast<uint8_t>(parts[0].length() + 1), socket_fd, client_addr);
        return;
    }

    GameState* game = find_game_and_verify_players(game_id, player_id, buffer, parts, active_games, socket_fd,
                                                   client_addr);
    if (!game) {
        return;
    }

    game->last_activity = time(nullptr);
    send_game_state(*game, game_id, socket_fd, client_addr);
}

void handle_wrong_message(const std::string& buffer, uint8_t error_index, int socket_fd,
                          const struct sockaddr_in& client_addr) {
    std::string response(WRONG_MSG_LEN, '\0');

    size_t copy_len = std::min(buffer.length(), static_cast<size_t>(MESSAGE_LEN));
    for (size_t i = 0; i < copy_len; i++) {
        response[i] = buffer[i];
    }

    response[MESSAGE_LEN] = static_cast<char>(ERROR_STATUS);
    response[MESSAGE_LEN + 1] = static_cast<char>(error_index);

    send_response_to_client(socket_fd, client_addr, response);
}
