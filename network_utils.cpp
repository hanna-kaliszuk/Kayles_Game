#include "network_utils.h"

void send_response_to_client(int socket_fd, const struct sockaddr_in& client_addr, const string& message) {
    ssize_t sent_length = sendto(socket_fd, message.c_str(), message.length(), 0,
        reinterpret_cast<const struct sockaddr*>(&client_addr), sizeof(client_addr));

    if (sent_length < 0) {
        syserr("failed to send response to client");
    }
}

void send_game_state(const GameState& game_state, uint32_t game_id, int socket_fd, const struct sockaddr_in& client_addr) {
    string response = to_string(game_id) + "/" +
                      to_string(game_state.player_a_id) + "/" +
                      to_string(game_state.player_b_id) + "/" +
                      to_string(game_state.status) + "/" +
                      to_string(game_state.max_pawn) + "/" +
                      serialize_pawn_row(game_state);

    send_response_to_client(socket_fd, client_addr, response);
}

void handle_wrong_message(const string& buffer, uint8_t error_index, int socket_fd,
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

int validate_message_format(const string& buffer, const vector<string>& parts, size_t expected_parts_count) {
    if (parts.size() != expected_parts_count) {
        uint8_t err_idx = 0;

        if (parts.size() < expected_parts_count) {
            return static_cast<uint8_t>(buffer.length());
        } else {
            size_t length_sum = 0;
            for (size_t i = 0; i < expected_parts_count; i++) {
                length_sum += parts[i].length();
            }

            length_sum += (expected_parts_count - 1);
            return static_cast<uint8_t>(length_sum);
        }
    }

    size_t current_idx = 0;

    for (size_t p = 1; p < expected_parts_count; p++) {
        for (size_t i = 0; i < parts[p].length(); i++) {
            if (!isdigit(parts[p][i])) {
                return static_cast<int>(current_idx + i);
            }
        }
        current_idx += parts[p].length() + 1;
    }

    return NO_ERROR; // brak błędu
}