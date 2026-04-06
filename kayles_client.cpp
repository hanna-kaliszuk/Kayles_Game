#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cinttypes>
#include <cstdio>
#include <netdb.h>

#include "common.h"
#include "err.h"

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

static void receive_and_display_message(int socket_fd, int timeout) {
    char buffer[BUFFER_SIZE];
    ssize_t received_bytes = read(socket_fd, buffer, sizeof(buffer) - 1);

    if (received_bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        } else {
            syserr("read failed");
        }
    }

    if (received_bytes == 14 && static_cast<unsigned char>(buffer[12]) == 255) {
        printf("----------------------------------------\n");
        printf("[SERVER -> CLIENT] received MSG_WRONG_MSG\n");

        for(int i = 0; i < 12 && buffer[i] != '\0'; i++) {
            printf("%c", buffer[i]);
        }

        printf("'\nstatus: %d\n", static_cast<unsigned char>(buffer[12]));
        printf("error index: %d\n", static_cast<unsigned char>(buffer[13]));
        printf("----------------------------------------\n\n");
    } else {
        buffer[received_bytes] = '\0';
        printf("----------------------------------------\n");
        printf("[SERVER -> CLIENT] received MSG_GAME_STATE:\n");
        printf("%s\n", buffer);
        printf("----------------------------------------\n\n");
    }
}

int main(int argc, char* argv[]) {
    AppConfig config;

    parse_arguments(argc, argv, config, "m:a:p:t:", false);
    int socket_fd = create_client_socket(config);
    write(socket_fd, config.message.c_str(), config.message.length());

    receive_and_display_message(socket_fd, config.timeout);
    close(socket_fd);

    return 0;
}