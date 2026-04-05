#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cinttypes>
#include <cstdio>
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

int main(int argc, char* argv[]) {
    AppConfig config;

    parse_arguments(argc, argv, config, "m:a:p:t:", false);
    int socket_fd = create_client_socket(config);
    write(socket_fd, config.message.c_str(), config.message.length());

    char buffer[1000];
    ssize_t received_bytes = read(socket_fd, buffer, sizeof(buffer) - 1);

    if (received_bytes < 0) {
        // Sprawdzamy, czy powodem błędu był upływ czasu (timeout)
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            fatal("Brak odpowiedzi! Serwer milczał przez %d sekund (timeout).", config.timeout);
        } else {
            // Jakiś inny błąd sieciowy
            syserr("read failed");
        }
    }

    buffer[received_bytes] = '\0';
    printf("%s\n", buffer);
    close(socket_fd);
    return 0;
}