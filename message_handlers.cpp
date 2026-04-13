#include "message_handlers.h"

#include <arpa/inet.h>
#include <sys/socket.h>

#include <algorithm>
#include <ctime>
#include <cstring>
#include <iostream>
#include <new>

#include "common.h"
#include "err.h"

static uint32_t next_game_id = 1;

// constexpr int JOIN_LEN = 2;
// constexpr int MOVE_LEN = 4;
// constexpr int GIVE_UP_LEN = 3;
// constexpr int KEEP_ALIVE_LEN = 3;

static GameState* find_game_and_verify_players(uint32_t game_id, uint32_t player_id, const std::string& buffer,
                                               std::unordered_map<uint32_t, GameState>& active_games, int socket_fd,
                                               const struct sockaddr_in& client_addr) {
    auto it = active_games.find(game_id);
    if (it == active_games.end()) {
        // Gra nie istnieje - błąd w polu game_id (zaczyna się na indeksie 5)
        handle_wrong_message(buffer, 5, socket_fd, client_addr);
        return nullptr;
    }

    GameState& game = it->second;

    if (game.player_a_id != player_id && game.player_b_id != player_id) {
        // Gracz nie należy do gry - błąd w polu player_id (zaczyna się na indeksie 1)
        handle_wrong_message(buffer, 1, socket_fd, client_addr);
        return nullptr;
    }

    return &game;
}

static void send_response_to_client(int socket_fd, const struct sockaddr_in& client_addr, const std::string& message) {
    ssize_t sent_length = sendto(socket_fd, message.data(), message.length(), 0,
                                 reinterpret_cast<const struct sockaddr*>(&client_addr), sizeof(client_addr));

    if (sent_length < 0) {
        syserr("failed to send response to client");
    }
}

static void send_game_state(const GameState& game_state, uint32_t game_id, int socket_fd,
                            const struct sockaddr_in& client_addr) {
    std::string response;

    // Zmiana na format sieciowy (Big-Endian)
    uint32_t net_game_id = htonl(game_id);
    uint32_t net_pa = htonl(game_state.player_a_id);
    uint32_t net_pb = htonl(game_state.player_b_id);

    // Kopiowanie 4-bajtowych ID
    response.append(reinterpret_cast<const char*>(&net_game_id), 4);
    response.append(reinterpret_cast<const char*>(&net_pa), 4);
    response.append(reinterpret_cast<const char*>(&net_pb), 4);

    // Kopiowanie 1-bajtowych statusów
    response.push_back(static_cast<char>(game_state.status));
    response.push_back(static_cast<char>(game_state.max_pawn));

    // Kopiowanie bitów kręgli prosto z wektora
    for (uint8_t byte : game_state.pawn_row) {
        response.push_back(static_cast<char>(byte));
    }

    send_response_to_client(socket_fd, client_addr, response);
}

void handle_join_game(const std::string& buffer, std::unordered_map<uint32_t,
                          GameState>& active_games, const GameState& template_game, int socket_fd,
                      const struct sockaddr_in& client_addr) {
    if (buffer.length() != 5) {
        handle_wrong_message(buffer, static_cast<uint8_t>(buffer.length()), socket_fd, client_addr);
        return;
    }

    // Rozpakowywanie binariów
    uint32_t assigned_player_id;
    memcpy(&assigned_player_id, buffer.data() + 1, 4);
    assigned_player_id = ntohl(assigned_player_id);

    // Gracz nie może mieć ID = 0
    if (assigned_player_id == 0) {
        handle_wrong_message(buffer, 1, socket_fd, client_addr);
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

void handle_make_move_one(const std::string& buffer,
                          std::unordered_map<uint32_t, GameState>& active_games,
                          const GameState& /*template_game*/, int socket_fd, const struct sockaddr_in& client_addr) {

    // sprawdzamy, czy wiadomość, która przyszła jest na pewno ok:
    if (buffer.length() != 10) {
        handle_wrong_message(buffer, static_cast<uint8_t>(buffer.length()), socket_fd, client_addr);
        return;
    }

    uint32_t player_id, game_id;
    memcpy(&player_id, buffer.data() + 1, 4);
    memcpy(&game_id, buffer.data() + 5, 4);

    player_id = ntohl(player_id);
    game_id = ntohl(game_id);
    uint8_t pawn_idx = static_cast<uint8_t>(buffer[9]);

    if (player_id == 0) {
        handle_wrong_message(buffer, 1, socket_fd, client_addr);
        return;
    }

    // sprawdzamy, czy gra o podanym ID istnieje i czy gracz o podanym ID bierze udział w danej grze
    GameState* game = find_game_and_verify_players(game_id, player_id, buffer, active_games, socket_fd,
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

void handle_make_move_two(const std::string& buffer,
                          std::unordered_map<uint32_t, GameState>& active_games,
                          const GameState& /*template_game*/, int socket_fd, const struct sockaddr_in& client_addr) {
    // sprawdzamy, czy wiadomość, która przyszła jest na pewno ok:
    if (buffer.length() != 10) {
        handle_wrong_message(buffer, static_cast<uint8_t>(buffer.length()), socket_fd, client_addr);
        return;
    }

    // Rozpakowywanie binariów
    uint32_t player_id, game_id;
    memcpy(&player_id, buffer.data() + 1, 4);
    memcpy(&game_id, buffer.data() + 5, 4);

    player_id = ntohl(player_id);
    game_id = ntohl(game_id);
    uint8_t first_pawn_idx = static_cast<uint8_t>(buffer[9]);

    if (player_id == 0) {
        handle_wrong_message(buffer, 1, socket_fd, client_addr);
        return;
    }

    uint32_t second_pawn_idx = first_pawn_idx + 1;

    // sprawdzamy, czy gra o podanym ID istnieje i czy gracz o podanym ID bierze udział w danej grze
    GameState* game = find_game_and_verify_players(game_id, player_id, buffer, active_games, socket_fd,
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

void handle_give_up(const std::string& buffer,
                    std::unordered_map<uint32_t, GameState>& active_games,
                    const GameState& /*template_game*/, int socket_fd, const struct sockaddr_in& client_addr) {
    if (buffer.length() != 9) {
        handle_wrong_message(buffer, static_cast<uint8_t>(buffer.length()), socket_fd, client_addr);
        return;
    }

    uint32_t player_id, game_id;
    memcpy(&player_id, buffer.data() + 1, 4);
    memcpy(&game_id, buffer.data() + 5, 4);

    player_id = ntohl(player_id);
    game_id = ntohl(game_id);

    if (player_id == 0) {
        handle_wrong_message(buffer, 1, socket_fd, client_addr);
        return;
    }

    GameState* game = find_game_and_verify_players(game_id, player_id, buffer, active_games, socket_fd,
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

void handle_keep_alive(const std::string& buffer,
                       std::unordered_map<uint32_t, GameState>& active_games,
                       const GameState& /*template_game*/, int socket_fd, const struct sockaddr_in& client_addr) {
    if (buffer.length() != 9) {
        handle_wrong_message(buffer, static_cast<uint8_t>(buffer.length()), socket_fd, client_addr);
        return;
    }

    uint32_t player_id, game_id;
    memcpy(&player_id, buffer.data() + 1, 4);
    memcpy(&game_id, buffer.data() + 5, 4);

    player_id = ntohl(player_id);
    game_id = ntohl(game_id);

    if (player_id == 0) {
        handle_wrong_message(buffer, 1, socket_fd, client_addr);
        return;
    }

    GameState* game = find_game_and_verify_players(game_id, player_id, buffer, active_games, socket_fd,
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
