#include <cinttypes>
#include <cstring>
#include <algorithm>
#include <arpa/inet.h>
#include "common.h"
#include "err.h"
#include "game_logic.h"
#include <sys/socket.h>
#include <netdb.h>
#include <vector>
#include <unordered_map>


#define DELIMITER '/'
#define MESSAGE_BYTES 12
#define ERROR_STATUS 255
#define JOIN_LEN 2
#define MOVE_LEN 4
#define NO_ERROR (-1)

static uint32_t next_game_id = 1000;

static int create_sever_socket(const AppConfig& config) {
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (socket_fd < 0) {
        syserr("unable to create socket for server socket.");
    }

    struct sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(config.port);

    if (inet_pton(AF_INET, config.address.c_str(), &server_address.sin_addr) <= 0) {
        fatal("invalid IP address provided.");
    }

    if (::bind(socket_fd, reinterpret_cast<struct sockaddr *>(&server_address),
        static_cast<socklen_t>(sizeof(server_address))) < 0) {
        syserr("unable to bind to port %d", config.port);
    }

    return socket_fd;
}

static void send_response_to_client(int socket_fd, const struct sockaddr_in& client_addr, const string& message) {
    ssize_t sent_length = sendto(socket_fd, message.c_str(), message.length(), 0,
        reinterpret_cast<const struct sockaddr*>(&client_addr), sizeof(client_addr));

    if (sent_length < 0) {
        syserr("failed to send response to client");
    }
}

static void send_game_state(const GameState& game_state, uint32_t game_id, int socket_fd, const struct sockaddr_in& client_addr) {
    string response = to_string(game_id) + "/" +
                      to_string(game_state.player_a_id) + "/" +
                      to_string(game_state.player_b_id) + "/" +
                      to_string(game_state.status) + "/" +
                      to_string(game_state.max_pawn) + "/" +
                      serialize_pawn_row(game_state);

    send_response_to_client(socket_fd, client_addr, response);
}

static void handle_wrong_message(const string& buffer, uint8_t error_index, int socket_fd,
    const struct sockaddr_in& client_addr) {

    string response (14, '\0'); // 12 message + 1 status + 1 error_index

    size_t copy_len = min(buffer.length(), static_cast<size_t>(MESSAGE_BYTES));
    for (size_t i = 0; i < copy_len; i++) {
        response[i] = buffer[i];
    }

    response[12] = static_cast<char>(ERROR_STATUS);
    response[13] = static_cast<char>(error_index);

    send_response_to_client(socket_fd, client_addr, response);
}

static GameState* find_game_and_verify_players(uint32_t game_id, uint32_t player_id, const string& buffer, const vector<string>& parts, unordered_map<uint32_t, GameState>& active_games, int socket_fd, const struct sockaddr_in& client_addr) {
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

static int validate_message_format(const string& buffer, const vector<string>& parts, size_t expected_parts_count) {
    if (parts.size() != expected_parts_count) {
        uint8_t err_idx = 0;

        if (parts.size() < expected_parts_count) {
            return static_cast<uint8_t>(buffer.length());
        } else {
            size_t length_sum = 0;
            for (size_t i = 0; i < expected_parts_count; i++) {
                length_sum += parts[i].length();
            }

            length_sum += (expected_parts_count - 1);
            return static_cast<uint8_t>(length_sum);
        }
    }

    size_t current_idx = 0;

    for (size_t p = 1; p < expected_parts_count; p++) {
        for (size_t i = 0; i < parts[p].length(); i++) {
            if (!isdigit(parts[p][i])) {
                return static_cast<int>(current_idx + i);
            }
        }
        current_idx += parts[p].length() + 1;
    }

    return NO_ERROR; // brak błędu
}

static void handle_join_game(const string& buffer, const vector<string>& parts, unordered_map<uint32_t, GameState>& active_games,
    const GameState& template_game, int socket_fd, const struct sockaddr_in& client_addr) {

    int err_idx = validate_message_format(buffer, parts, JOIN_LEN);

    if (err_idx != NO_ERROR) {
        handle_wrong_message(buffer, static_cast<uint8_t>(err_idx), socket_fd, client_addr);
        return;
    }

    uint32_t assigned_player_id;
    try {
        assigned_player_id = static_cast<uint32_t>(stoul(parts[1]));
    } catch (...){ // parse error
        handle_wrong_message(buffer, 2, socket_fd, client_addr);
        return;
    }

    uint32_t current_game_id = 0;
    bool found = false;

    for (auto& game : active_games) {
        if (game.second.status == WAITING_FOR_OPPONENT) {
            game.second.player_b_id = assigned_player_id;
            game.second.status = TURN_B;

            current_game_id = game.first;
            found = true;

            cout << "player no " << assigned_player_id << " joined game no " << current_game_id << endl;
            break;
        }
    }

    if (!found) {
        current_game_id = next_game_id++;

        GameState new_game = template_game;
        new_game.player_a_id = assigned_player_id;
        new_game.player_b_id = WAITING_FOR_OPPONENT;
        new_game.status = WAITING_FOR_OPPONENT;

        active_games[current_game_id] = new_game;
        cout << "game no " << current_game_id << " created. player no " << assigned_player_id << " joined it. " << endl;
    }

    GameState& current_game = active_games[current_game_id];
    send_game_state(current_game, current_game_id, socket_fd, client_addr);
}

static void handle_make_move_one(const string& buffer, const vector<string>& parts, unordered_map<uint32_t, GameState>& active_games,
    const GameState& template_game, int socket_fd, const struct sockaddr_in& client_addr) {
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
    } catch (...) {
        handle_wrong_message(buffer, static_cast<uint8_t>(parts[0].length() + 1), socket_fd, client_addr);
        return;
    }

    // sprawdzamy, czy gra o podanym ID istnieje i czy gracz o podanym ID bierze udział w danej grze
    GameState* game = find_game_and_verify_players(game_id, player_id, buffer, parts, active_games, socket_fd, client_addr);
    if (!game) {
        return;
    }

    // sprawdzamy, czy jest teraz pora na ruch tego gracza
    if (game->player_a_id == player_id && game->status != TURN_A) return;
    if (game->player_b_id == player_id && game->status != TURN_B) return;

    if (!is_legal_move(*game, pawn_idx)) {
        return;
    }

    // jeżeli wszystko jest ok odsyłamy graczowi wiadomość
    game->status = (game->status == TURN_A) ? TURN_B : TURN_A;

    send_game_state(*game, game_id, socket_fd, client_addr);
}

static void handle_make_move_two(const string& buffer, const vector<string>& parts, unordered_map<uint32_t, GameState>& active_games,
    const GameState& template_game, int socket_fd, const struct sockaddr_in& client_addr) {
    // sprawdzamy, czy wiadomość, która przyszła jest na pewno ok:
    int err_idx = validate_message_format(buffer, parts, MOVE_LEN);
    if (err_idx != -1) {
        handle_wrong_message(buffer, static_cast<uint8_t>(err_idx), socket_fd, client_addr);
        return;
    }

    uint32_t game_id, player_id;
    uint32_t first_pawn_idx;

    try {
        player_id = static_cast<uint32_t>(stoul(parts[1]));
        game_id = static_cast<uint32_t>(stoul(parts[2]));
        first_pawn_idx = static_cast<uint32_t>(stoul(parts[3]));
    } catch (...) {
        handle_wrong_message(buffer, static_cast<uint8_t>(parts[0].length() + 1), socket_fd, client_addr);
        return;
    }

    uint32_t second_pawn_idx = first_pawn_idx + 1;

    // sprawdzamy, czy gra o podanym ID istnieje i czy gracz o podanym ID bierze udział w danej grze
    GameState* game = find_game_and_verify_players(game_id, player_id, buffer, parts, active_games, socket_fd, client_addr);
    if (!game) {
        return;
    }

    // sprawdzamy, czy jest teraz pora na ruch tego gracza
    if (game->player_a_id == player_id && game->status != TURN_A) return;
    if (game->player_b_id == player_id && game->status != TURN_B) return;


    // sprawdzamy, czy jest teraz pora na ruch tego gracza
    if (game->player_a_id == player_id && game->status != TURN_A) return;
    if (game->player_b_id == player_id && game->status != TURN_B) return;

    if (!is_pawn_standing(*game, first_pawn_idx) || !is_pawn_standing(*game, second_pawn_idx)) return;

    // jeżeli wszystko jest ok, zmieniamy stan gry, odsyłamy graczowi wiadomość
    knock_pawn_down(*game, first_pawn_idx);
    knock_pawn_down(*game, second_pawn_idx);
    game->status = (game->status == TURN_A) ? TURN_B : TURN_A;

    send_game_state(*game, game_id, socket_fd, client_addr);
}

static void handle_keep_alive(const string& buffer, const vector<string>& parts, unordered_map<uint32_t, GameState>& active_games,
    const GameState& template_game, int socket_fd, const struct sockaddr_in& client_addr) {


}

static void handle_give_up(const string& buffer, const vector<string>& parts, unordered_map<uint32_t, GameState>& active_games,
    const GameState& template_game, int socket_fd, const struct sockaddr_in& client_addr) {


}

static void decode_and_verify_message( const string& buffer, unordered_map<uint32_t, GameState>& active_games,
    const GameState& template_game, int socket_fd, const struct sockaddr_in& client_addr) {
    vector<string> parts = split_message(buffer, DELIMITER);

    if (parts.empty()) {
        handle_wrong_message(buffer, 0, socket_fd, client_addr);
        return;
    }

    const int message_type = validate_and_convert_number(parts[0].c_str(), 0, 4);

    static const unordered_map<int, MessageHandler> handlers = {
        {0, handle_join_game},
        {1, handle_make_move_one},
        {2, handle_make_move_two},
        {3, handle_keep_alive},
        {4, handle_give_up}
    };

    auto it = handlers.find(message_type);
    if (it != handlers.end()) {
        it->second(buffer, parts, active_games, template_game, socket_fd, client_addr);
    } else {
        handle_wrong_message(buffer, 0, socket_fd, client_addr);
    }
}

static void run_server(const AppConfig& config, const GameState& template_game) {
    cout << "running server on port " << config.port << endl;
    int socket_fd = create_sever_socket(config);
    static char buffer[BUFFER_SIZE];

    unordered_map<uint32_t, GameState> active_games;

    while (true) {
        struct sockaddr_in client_address;
        socklen_t client_address_length = sizeof(client_address);

        ssize_t received_length = recvfrom(socket_fd, buffer, BUFFER_SIZE - 1, 0,
            reinterpret_cast<struct sockaddr*>(&client_address), &client_address_length);

        if (received_length < 0) {
            syserr("recvfrom failed");
        }

        buffer[received_length] = '\0';

        decode_and_verify_message(buffer, active_games, template_game, socket_fd, client_address);

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_address.sin_addr, client_ip, sizeof(client_ip));
        uint16_t client_port = ntohs(client_address.sin_port);

        printf("received message from %s:%u: %s\n",
              client_ip, client_port, buffer);
    }
}

int main(int argc, char* argv[]) {
    AppConfig config;
    GameState template_game;

    parse_arguments(argc, argv, config, "r:a:p:t:", true);
    initialize_pawn_row(config.pawn_row, template_game);

    run_server(config, template_game);

    return 0;
}