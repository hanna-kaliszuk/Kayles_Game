#ifndef KAYLES_COMMON_H
#define KAYLES_COMMON_H

#include <string>
#include <vector>

constexpr int INVALID_VALUE = (-1);
constexpr int BUFFER_SIZE = 1000;
constexpr int WRONG_MSG_LEN = 14;
constexpr int ERROR_STATUS = 255;
constexpr int MESSAGE_LEN = 12;
constexpr int NO_ERROR = (-1);
constexpr int MAX_MESSAGE_PARTS = 4;
constexpr int MIN_MESSAGE_PARTS = 0;

struct AppConfig {
    std::string address;
    std::string pawn_row;
    std::string message;
    int port = INVALID_VALUE;
    int timeout = INVALID_VALUE;
};

int validate_and_convert_number(const char* text_value, int min_value, int max_value);

void ensure_not_set(bool flag, const std::string& message);

void parse_arguments(int argc, char* argv[], AppConfig& config, const char* allowed_flags, bool is_server);

std::vector<std::string> split_message(const std::string& message, char delimiter);

int validate_message_format(const std::string& buffer, const std::vector<std::string>& parts,
                            size_t expected_parts_count);

#endif //KAYLES_COMMON_H
