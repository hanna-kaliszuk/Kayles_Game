#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "common.h"
#include "err.h"

using namespace std;

static int create_client_socket(const AppConfig& config) {
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        syserr("unable to create a socket");
    }

    struct timeval tv{};
    tv.tv_sec = config.timeout;
    tv.tv_usec = 0;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        syserr("setsockopt failed");
    }

    struct sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(static_cast<uint16_t>(config.port));

    if (inet_pton(AF_INET, config.address.c_str(), &server_address.sin_addr) <= 0) {
        fatal("invalid IP address format");
    }

    if (connect(socket_fd, reinterpret_cast<struct sockaddr*>(&server_address), sizeof(server_address)) < 0) {
        syserr("connect failed");
    }

    return socket_fd;
}

static void receive_and_display_message(int socket_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t received_bytes = read(socket_fd, buffer, sizeof(buffer));

    if (received_bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        else {
            syserr("read failed");
        }
    }

    if (received_bytes == WRONG_MSG_LEN && static_cast<unsigned char>(buffer[MESSAGE_LEN]) == ERROR_STATUS) {
        printf("----------------------------------------\n");
        printf("[SERVER -> CLIENT] received MSG_WRONG_MSG\n");

        printf("'\nstatus: %d\n", static_cast<unsigned char>(buffer[MESSAGE_LEN]));
        printf("error index: %d\n", static_cast<unsigned char>(buffer[MESSAGE_LEN + 1]));
        printf("----------------------------------------\n\n");
    }
    else if (received_bytes >= WRONG_MSG_LEN) {
        uint32_t game_id, player_a, player_b;

        memcpy(&game_id, buffer, 4);
        memcpy(&player_a, buffer + 4, 4);
        memcpy(&player_b, buffer + 8, 4);

        game_id = ntohl(game_id);
        player_a = ntohl(player_a);
        player_b = ntohl(player_b);

        uint8_t status = static_cast<uint8_t>(buffer[12]);
        uint8_t max_pawn = static_cast<uint8_t>(buffer[13]);


        printf("----------------------------------------\n");
        printf("[SERVER -> CLIENT] received MSG_GAME_STATE:\n");
        printf("game id: %u\n", game_id);
        printf("player A: %u\n", player_a);
        printf("player B: %u\n", player_b);
        printf("status: %u\n", status);
        printf("max pawn: %u\n", max_pawn);
        printf("----------------------------------------\n\n");
    } else {
        printf("[SERVER -> CLIENT] received unknown binary message (%zd bytes)\n", received_bytes);
    }
}

static vector<uint8_t> build_binary_payload(const string& message) {
    vector<string> parts = split_message(message, '/');

    if (parts.empty()) {
        fatal("empty message");
    }

    vector<uint8_t> payload;

    try {
        uint8_t message_type = static_cast<uint8_t>(stoul(parts[0]));
        payload.push_back(message_type);

        if (parts.size() >= 2) {
            uint32_t player_id = htonl(static_cast<uint32_t>(stoul(parts[1])));
            auto* ptr = reinterpret_cast<uint8_t*>(&player_id);
            payload.insert(payload.end(), ptr, ptr + sizeof(player_id));
        }
        if (parts.size() >= 3) {
            uint32_t game_id = htonl(static_cast<uint32_t>(stoul(parts[2])));
            auto* ptr = reinterpret_cast<uint8_t*>(&game_id);
            payload.insert(payload.end(), ptr, ptr + sizeof(game_id));
        }
        if (parts.size() == 4) {
            uint8_t pawn = static_cast<uint8_t>(stoul(parts[3]));
            payload.push_back(pawn);
        }
    } catch (const std::exception& e) {
        // Łapiemy wyjątki stoul i ładnie wyłączamy program
        fatal("invalid numeric format in message");
    }

    return payload;

}

int main(int argc, char* argv[]) {
    AppConfig config;

    parse_arguments(argc, argv, config, "m:a:p:t:", false);
    int socket_fd = create_client_socket(config);

    vector<uint8_t> binary_payload = build_binary_payload(config.message);

    if (write(socket_fd, binary_payload.data(), binary_payload.size()) < 0) {
        syserr("write failed");
    }

    receive_and_display_message(socket_fd);
    close(socket_fd);

    return 0;
}
