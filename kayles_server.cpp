#include <cinttypes>
#include <arpa/inet.h>
#include "common.h"
#include "err.h"
#include <sys/socket.h>
#include <netdb.h>
#include <vector>

#define MAX_PORT_NUMBER 65535
#define MAX_TIMEOUT_VALUE 99
#define MIN_PORT_VALUE 0
#define MIN_TIMEOUT_VALUE 1
#define INVALID_VALUE (-1)

struct ServerConfig {
    string pawn_row;
    string address;
    int port;
    int timeout;
};

struct GameState {
    uint32_t player_a_id;
    uint32_t player_b_id;
    uint8_t status;
    uint8_t max_pawn;
    vector<uint8_t> pawn_row;
};

static bool is_valid_pawn_row(const string& pawns) {
    if (pawns.empty()) return false;

    if (pawns.front() != '1' || pawns.back() != '1') return false;

    return all_of(pawns.begin(), pawns.end(), [](char c) {
        return c == '0' || c == '1';
    });
}

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

static void parse_server_arguments(int argc, char* argv[], ServerConfig& config) {
    cout << "------ PARSING SERVER ARGUMENTS ------" << endl;
    bool has_pawns = false;
    bool has_address = false;
    bool has_port = false;
    bool has_timeout = false;

    int opt;

    while ((opt = getopt(argc, argv, "r:a:p:t:")) != -1) {
        switch (opt) {
        case 'r':
            ensure_not_set(has_pawns, "multiple -r options provided.");
            if (!is_valid_pawn_row(optarg)) {
                fatal("invalid pawn row. expected a non-empty string of '0' and '1' with the first and last "
                        "one being '1'");
            }
            has_pawns = true;
            config.pawn_row = optarg;
            break;

        case 'a':
            ensure_not_set(has_address, "multiple -a options provided.");
            config.address = optarg;
            has_address = true;
            break;

        case 'p':
            ensure_not_set(has_port, "multiple -p options provided.");
            config.port = validate_and_convert_number(optarg, MIN_PORT_VALUE, MAX_PORT_NUMBER);

            if (config.port == INVALID_VALUE) {
                fatal("invalid port number. expected value from 0 to 2^16 - 1.");
                exit(EXIT_FAILURE);
            }

            has_port = true;
            break;

        case 't':
            ensure_not_set(has_timeout, "multiple -t options provided." );
            config.timeout = validate_and_convert_number(optarg, MIN_TIMEOUT_VALUE, MAX_TIMEOUT_VALUE);

            if (config.timeout == INVALID_VALUE) {
                fatal("invalid timeout value. expected a value from 1 to 99.");
            }

            has_timeout = true;
            break;

        case '?':
        default:
            fatal("unknown option or missing argument. expected: -r, -a, -p, -t");
        }
    }

    if (!has_pawns || !has_address || !has_port || !has_timeout) {
        fatal("missing required arguments. expected: -r, -a, -p, -t");
    }

    cout << "--- SUCCESS: arguments parsed correctly ---" << endl;
    cout << "pawns:         " << config.pawn_row << endl;
    cout << "address:       " << config.address << endl;
    cout << "port:          " << config.port << endl;
    cout << "timeout:       " << config.timeout << endl;
}

static int create_sever_socket (const ServerConfig& config) {
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
    ServerConfig config;
    GameState new_game;

    parse_server_arguments(argc, argv, config);
    initialize_pawn_row(config.pawn_row, new_game);

    cout << "-- game state --" << endl;
    cout << "max pawn: " << static_cast<int>(new_game.max_pawn) << endl;
    cout << "pawns: ";
    for (size_t i = 0; i < config.pawn_row.length(); i++) {
        cout << config.pawn_row[i];
    }

    int socket_fd = create_sever_socket(config);
    printf("\n listening on port %" PRIu16 "\n", config.port);
    if (socket_fd != 1) {
        cout << "";
    }

    return 0;
}