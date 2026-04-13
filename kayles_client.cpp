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
#include <vector>

#include "common.h"
#include "err.h"

static void append_u32(std::vector<char>& buf, uint32_t val) {
    uint32_t net = htonl(val);
    const char* bytes = reinterpret_cast<const char*>(&net);
    buf.insert(buf.end(), bytes, bytes + sizeof(net));
}

static std::vector<char> serialize_message(const std::string& human_readable) {
    const std::vector<std::string> parts = split_message(human_readable, '/');
    if (parts.empty()) {
        fatal("empty message");
    }

    const int msg_type = validate_and_convert_number(parts[0].c_str(), 0, 4);
    if (msg_type == INVALID_VALUE) {
        fatal("invalid message type: expected 0-4, got: %s", parts[0].c_str());
    }

    std::vector<char> buf;
    buf.push_back(static_cast<char>(msg_type));

    auto take_u32 = [&](size_t idx, const char* field) -> uint32_t {
        if (idx >= parts.size()) {
            fatal("missing field '%s' in message", field);
        }
        const char* s = parts[idx].c_str();
        if (*s == '\0') {
            fatal("field '%s' is empty", field);
        }
        char* endptr;
        const long val = strtol(s, &endptr, 10);
        if (*endptr != '\0') {
            fatal("field '%s' is not a valid number", field);
        }
        if (val < 0) {
            fatal("field '%s' must be non-negative", field);
        }
        return static_cast<uint32_t>(val);
    };

    size_t expected_parts;
    switch (msg_type) {
    case 0: expected_parts = 2;
        break;
    case 1:
    case 2: expected_parts = 4;
        break;
    case 3:
    case 4: expected_parts = 3;
        break;
    default: expected_parts = 0;
        break;
    }
    if (parts.size() != expected_parts) {
        fatal("wrong number of fields for message type %d (got %zu, expected %zu)",
              msg_type, parts.size(), expected_parts);
    }

    switch (msg_type) {
    case 0:
        append_u32(buf, take_u32(1, "player_id"));
        break;
    case 1:
    case 2:
        append_u32(buf, take_u32(1, "player_id"));
        append_u32(buf, take_u32(2, "game_id"));
        buf.push_back(static_cast<char>(take_u32(3, "pawn_idx"))); // 1 bajt, nie uint32_t
        break;
    case 3:
    case 4:
        append_u32(buf, take_u32(1, "player_id"));
        append_u32(buf, take_u32(2, "game_id"));
        break;
    default:
        fatal("unknown message type");
    }

    return buf;
}

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

    if (connect(socket_fd,
                reinterpret_cast<struct sockaddr*>(&server_address),
                sizeof(server_address)) < 0) {
        syserr("connect failed");
    }

    return socket_fd;
}

static uint32_t parse_u32(const char* buf, size_t offset) {
    uint32_t val;
    memcpy(&val, buf + offset, sizeof(val));
    return ntohl(val);
}

static void receive_and_display_message(int socket_fd) {
    char buffer[BUFFER_SIZE];
    const ssize_t received_bytes = read(socket_fd, buffer, sizeof(buffer) - 1);

    if (received_bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNREFUSED) {
            printf("[TIMEOUT] no response from server\n");
            return;
        }
        syserr("read failed");
    }

    printf("----------------------------------------\n");

    if (received_bytes == WRONG_MSG_LEN
        && static_cast<unsigned char>(buffer[MESSAGE_LEN]) == ERROR_STATUS) {
        printf("[SERVER -> CLIENT] MSG_WRONG_MSG\n");
        printf("fragment (12 B): \"");
        for (int i = 0; i < MESSAGE_LEN; i++) {
            printf("%c", buffer[i]);
        }
        printf("\"\n");
        printf("status:      0x%02X\n", static_cast<unsigned char>(buffer[MESSAGE_LEN]));
        printf("error_index: %u\n", static_cast<unsigned char>(buffer[MESSAGE_LEN + 1]));
    }
    else if (received_bytes >= static_cast<ssize_t>(4 + 4 + 4 + 1 + 1)) {
        const uint32_t game_id = parse_u32(buffer, 0);
        const uint32_t player_a = parse_u32(buffer, 4);
        const uint32_t player_b = parse_u32(buffer, 8);
        const uint8_t status = static_cast<uint8_t>(buffer[12]);
        const uint8_t max_pawn = static_cast<uint8_t>(buffer[13]);

        printf("[SERVER -> CLIENT] MSG_GAME_STATE\n");
        printf("game_id:   %" PRIu32 "\n", game_id);
        printf("player_a:  %" PRIu32 "\n", player_a);
        printf("player_b:  %" PRIu32 "\n", player_b);
        printf("status:    %u\n", static_cast<unsigned>(status));
        printf("max_pawn:  %u\n", static_cast<unsigned>(max_pawn));
        printf("pawn_row:  ");
        for (ssize_t i = 14; i < received_bytes; i++) {
            const uint8_t byte = static_cast<uint8_t>(buffer[i]);
            for (int bit = 7; bit >= 0; bit--) {
                printf("%d", (byte >> bit) & 1);
            }
        }
        printf("\n");
    }
    else {
        printf("[SERVER -> CLIENT] unknown response (%zd bytes)\n", received_bytes);
    }

    printf("----------------------------------------\n\n");
}

int main(int argc, char* argv[]) {
    AppConfig config;

    parse_arguments(argc, argv, config, "m:a:p:t:", false);

    const std::vector<char> msg_bytes = serialize_message(config.message);

    int socket_fd = create_client_socket(config);

    if (write(socket_fd, msg_bytes.data(), msg_bytes.size()) < 0) {
        syserr("write failed");
    }

    receive_and_display_message(socket_fd);
    close(socket_fd);

    return 0;
}
