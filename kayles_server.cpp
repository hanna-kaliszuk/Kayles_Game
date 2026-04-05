#include <cinttypes>
#include <arpa/inet.h>
#include "common.h"
#include "err.h"
#include <sys/socket.h>
#include <netdb.h>
#include <vector>

struct GameState {
    uint32_t player_a_id;
    uint32_t player_b_id;
    uint8_t status;
    uint8_t max_pawn;
    vector<uint8_t> pawn_row;
};

static void initialize_pawn_row(const string& str_pawns, GameState& game) {
    game.max_pawn = static_cast<uint8_t>(str_pawns.length() - 1);

    const size_t num_bytes = (game.max_pawn / 8) + 1;

    game.pawn_row.assign(num_bytes, 0);
    for (size_t i = 0; i < str_pawns.length(); i++) {
        if (str_pawns[i] == '1') {
            size_t byte_index = i /8;
            size_t bit_index = 7 - (i % 8);

            game.pawn_row[byte_index] |= (1 << bit_index);
        }
    }
}

static int create_sever_socket (const AppConfig& config) {
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

int main(int argc, char* argv[]) {
    AppConfig config;
    GameState new_game;

    parse_arguments(argc, argv, config, "r:a:p:t:", true);
    initialize_pawn_row(config.pawn_row, new_game);
    int socket_fd = create_sever_socket(config);

    printf("\n listening on port %" PRIu16 "\n", config.port);
    if (socket_fd != 1) {
        cout << "";
    }

    return 0;
}