#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "common.h"
#include "err.h"
#include "message_handlers.h"

using namespace std;

constexpr char  DELIMITER = '/';

using MessageHandler = function<void(
    const string& buffer,
    const vector<string>& parts,
    unordered_map<uint32_t, GameState>& active_games,
    const GameState& template_game,
    int socket_fd,
    const struct sockaddr_in& client_addr
)>;

static int create_sever_socket(const AppConfig& config) {
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (socket_fd < 0) {
        syserr("unable to create socket for server socket.");
    }

    struct timeval tv{};
    tv.tv_sec = config.timeout;
    tv.tv_usec = 0;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        syserr("setsockopt failed");
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

static void remove_timed_out_games(unordered_map<uint32_t, GameState>& active_games, int timeout_seconds) {
    time_t current_time = time(nullptr);

    for (auto it = active_games.begin(); it != active_games.end(); ) {
        if (current_time - it->second.last_activity > timeout_seconds) {
            cout << "game no " << it->first << " timed out" << endl;
            it = active_games.erase(it);
        } else {
            ++it;
        }
    }
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
    char buffer[BUFFER_SIZE];

    unordered_map<uint32_t, GameState> active_games;

    while (true) {
        struct sockaddr_in client_address;
        socklen_t client_address_length = sizeof(client_address);

        ssize_t received_length = recvfrom(socket_fd, buffer, BUFFER_SIZE - 1, 0,
            reinterpret_cast<struct sockaddr*>(&client_address), &client_address_length);

        if (received_length < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                remove_timed_out_games(active_games, config.timeout);
                continue;
            } else {
                syserr("recvfrom failed");
            }
        }

        buffer[received_length] = '\0';

        decode_and_verify_message(buffer, active_games, template_game, socket_fd, client_address);

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_address.sin_addr, client_ip, sizeof(client_ip));
        uint16_t client_port = ntohs(client_address.sin_port);

        cout << "received message from " << client_ip <<":" << client_port <<":" << buffer << endl;

        remove_timed_out_games(active_games, config.timeout);
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