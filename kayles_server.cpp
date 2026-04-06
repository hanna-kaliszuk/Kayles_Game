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

#define BUFFER_SIZE 1000
#define DELIMITER '/'
#define MESSAGE_BYTES 12
#define ERROR_STATUS 255

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

static void handle_join_game(const string& buffer, const vector<string>& parts, unordered_map<uint32_t, GameState>& active_games,
    const GameState& template_game, int socket_fd, const struct sockaddr_in& client_addr) {

    if (parts.size() != 2) {
        uint8_t err_idx = 0;

        if (parts.size() < 2) {
            err_idx = static_cast<uint8_t>(buffer.length());
        } else {
            err_idx = static_cast<uint8_t>(parts[0].length() + 1 + parts[1].length());
        }

        handle_wrong_message(buffer, err_idx, socket_fd, client_addr);
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
    string response = to_string(current_game_id) + "/" +
                      to_string(current_game.player_a_id) + "/" +
                      to_string(current_game.player_b_id) + "/" +
                      to_string(current_game.status) + "/" +
                      to_string(current_game.max_pawn) + "/" +
                      serialize_pawn_row(current_game);

    send_response_to_client(socket_fd, client_addr, response);
}

static void handle_make_move_one(const string& buffer, const vector<string>& parts, unordered_map<uint32_t, GameState>& active_games,
    const GameState& template_game, int socket_fd, const struct sockaddr_in& client_addr) {
    // sprawdzamy, czy zawiera poprawne wartości w polach
    // czy jest poprawna wartość pola pawn, jak nie to ignorujemy
    // sprawdzamy, czy jest tura gracza


}

static void handle_make_move_two(const string& buffer, const vector<string>& parts, unordered_map<uint32_t, GameState>& active_games,
    const GameState& template_game, int socket_fd, const struct sockaddr_in& client_addr) {
    // sprawdzamy, czy zawiera poprawne wartości w polach
    // czy jest poprawna wartość pola pawn, jak nie to ignorujemy
    // sprawdzamy, czy jest tura gracza


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