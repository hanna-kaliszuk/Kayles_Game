/**
 * @file kayles_client.cpp
 * @brief Implementations of the UDP client for the network Kayles game.
 *
 * Game rules are specified in @file kayles_server.cpp
 *
 * This implementation allows to send a single hand-crafted message to the server, wait for one response, print a
 * human-readable decode of that response and exit.
 *
 * 1. MESSAGE FORMAT (flag -m)
 * Messages are specified as '/'-delimited strings on the command line. The first token is the numeric message type
 * (0 - 4), followed by the required fields for that message type.
 *
 * - MSG_JOIN       :   "0/<player_id>"         :   [type(1B)][player_id(4B)]
 * - MSG_MOVE_1     :   "1/<pid>/<gid>/<pawn>"  :   [type(1B)][player_id(4B)][game_id(4B)][pawn_idx(1B)]
 * - MSG_MOVE_2     :   "2/<pid>/<gid>/<pawn>"  :   [type(1B)][player_id(4B)][game_id(4B)][pawn_idx(1B)]
 * - MSG_KEEP_ALIVE :   "3/<pid>/<gid>"         :   [type(1B)][player_id(4B)][game_id(4B)]
 * - MSG_GIVE_UP    :   "4/<pid>/<gid>"         :   [type(1B)][player_id(4B)][game_id(4B)]
 *
 * Note: pawn_dx is serialized as 1 byte (uint8_t), not as uint32_t.
 *
 * 2. RESPONSE DECODING:
 * Two response types are recognized:
 * - MSG_WRONG_MSG  : 14 bytes, status = 255     : prints the echoed 12 bytes of the message, status and error_index
 * - MSG_GAME_STATE : up to 13 bytes             : prints game_id, player_a_id, player_b_id, status, max_pawn, pawn_row
**/

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <cinttypes>
#include <cstdio>
#include <vector>

#include "client_messages.h"
#include "common.h"
#include "err.h"
#include "game_logic.h"

using namespace std;

constexpr size_t OFF_STATE_GAME_ID  = 0u;
constexpr size_t OFF_STATE_PLAYER_A = 4u;
constexpr size_t OFF_STATE_PLAYER_B = 8u;
constexpr size_t OFF_STATE_STATUS   = 12u;
constexpr size_t OFF_STATE_MAX_PAWN = 13u;
constexpr size_t OFF_STATE_PAWNS    = 14u;

/**
 * @brief Creates a UDP socket and logically connects it to the target server address.
 *
 * Uses getaddrinfo() to resolve both IP addresses and domain names.
 * Uses connect() on the UDP socket so that standard read()/writ() calls can be used instead of sendto() / recvfrom().
 *
 * @param config application configuration
 * @return the configured and connected socket file descriptor
**/
static int create_client_socket(const AppConfig& config) {
    // standard UDP socket
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        syserr("unable to create socket for client socket.");
    }

    // set the timeout
    struct timeval tv{};
    tv.tv_sec = static_cast<time_t>(config.timeout);
    tv.tv_usec = static_cast<suseconds_t>((config.timeout - tv.tv_sec) * 1000000.0);
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        syserr("setsockopt failed");
    }

    struct sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(config.port);

    struct addrinfo hints{};
    struct addrinfo* res = nullptr;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    // resolve dotted IP / domain into a binary address structure
    int err = getaddrinfo(config.address.c_str(), nullptr,&hints,&res);
    if (err != 0 || !res) {
        fatal("invalid IP address provided.");
    }

    // extract the binary IPv4 address
    server_address.sin_addr = ((struct sockaddr_in*)res->ai_addr)->sin_addr;

    if (connect(socket_fd, reinterpret_cast<struct sockaddr*>(&server_address), sizeof(server_address)) < 0) {
        freeaddrinfo(res);
        syserr("connect failed");
    }

    freeaddrinfo(res);
    return socket_fd;
}

/**
 * @brief Waits for the server's response, decodes it and displays it to the standard output.
 *
 * Handles 3 possible outcomes:
 * 1. timeout - exit with code 0
 * 2. WRONG_MESSAGE - prints echoed message and error offset
 * 3. GAME_SATE - prints the parse game state and pawn board
 * @param socket_fd the connected socket file descriptor to read from
**/
static void receive_and_display_message(int socket_fd) {
    char recv_buffer[BUFFER_SIZE];

    // read the server's response
    ssize_t received_len = read(socket_fd, recv_buffer, sizeof(recv_buffer) - 1);

    if (received_len < 0) {
        // handle timeout
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNREFUSED) {
            printf("[TIMEOUT] no response from server\n");
            return;
        }
        syserr("read failed");
    }

    // check if it is WRG_MSG
    if (static_cast<size_t>(received_len) == static_cast<size_t>(WRONG_MSG_LEN) &&
        static_cast<uint8_t>(recv_buffer[MESSAGE_LEN]) == static_cast<uint8_t>(ERROR_STATUS)) {

        uint8_t err_idx = static_cast<uint8_t>(recv_buffer[MESSAGE_LEN + 1]);
        printf("WRONG_MESSAGE received \n");
        printf("echoed message bytes: ");

        // print the echoed part
        for (size_t i = 0; i < MESSAGE_LEN; i++) {
            printf("%c", static_cast<char>(recv_buffer[i]));
        }

        printf("\n");
        printf("error status: %d, error index: %d\n", ERROR_STATUS, err_idx);
    } else {
        // normal message
        printf("GAME_STATE received \n");
        uint32_t game_id = read_u32(recv_buffer, OFF_STATE_GAME_ID);
        uint32_t player_a = read_u32(recv_buffer, OFF_STATE_PLAYER_A);
        uint32_t player_b = read_u32(recv_buffer, OFF_STATE_PLAYER_B);
        uint8_t status = static_cast<uint8_t>(recv_buffer[OFF_STATE_STATUS]);
        uint8_t max_pawn = static_cast<uint8_t>(recv_buffer[OFF_STATE_MAX_PAWN]);

        string pawns_str = "";

        size_t pawns_bytes = static_cast<size_t>(received_len) - 14;

        if (pawns_bytes > 0) {
            GameState state;
            state.max_pawn = max_pawn;
            state.pawn_row.assign(recv_buffer + OFF_STATE_PAWNS, recv_buffer + OFF_STATE_PAWNS + pawns_bytes);
            pawns_str = serialize_pawn_row(state);
        }

        printf("%u/%u/%u/%d/%d/%s\n", game_id, player_a, player_b, status, max_pawn, pawns_str.c_str());
    }
}

int main(int argc, char* argv[]) {
    AppConfig config;

    parse_arguments(argc, argv, config, "m:a:p:t:", false);

    const std::vector<char> msg_bytes = serialize_message(config.message);

    int socket_fd = create_client_socket(config);

    // using write instead of sendto() because in create_client_socket() connect() is used
    if (write(socket_fd, msg_bytes.data(), msg_bytes.size()) < 0) {
        syserr("write failed");
    }

    receive_and_display_message(socket_fd);
    close(socket_fd);

    return 0;
}
