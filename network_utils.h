#ifndef KAYLES_NETWORK_UTILS_H
#define KAYLES_NETWORK_UTILS_H

#include <algorithm>

void send_response_to_client(int socket_fd, const struct sockaddr_in& client_addr, const string& message);

void send_game_state(const GameState& game_state, uint32_t game_id, int socket_fd, const struct sockaddr_in& client_addr);

void handle_wrong_message(const string& buffer, uint8_t error_index, int socket_fd,
    const struct sockaddr_in& client_addr);

int validate_message_format(const string& buffer, const vector<string>& parts, size_t expected_parts_count);

#endif //KAYLES_NETWORK_UTILS_H
