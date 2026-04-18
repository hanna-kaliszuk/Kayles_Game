/**
* @file common.cpp
 * @brief Implementation of shared argument-parsing, validation and serialization utilities used by both the server and
 * the client.
**/

#include "common.h"

#include <arpa/inet.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <unistd.h>

#include "err.h"


// highest valid UDP port number
constexpr int MAX_PORT_NUMBER = 65535;

// shortest acceptable session timeout (seconds)
constexpr int MIN_TIMEOUT_VALUE = 1;

// longest acceptable session timeout (seconds)
constexpr int MAX_TIMEOUT_VALUE = 99;

constexpr int MAX_PAWNS = 256;
constexpr char PAWN_STANDING = '1';
constexpr char PAWN_KNOCKED_DOWN = '0';
constexpr int MIN_SERVER_PORT = 0;
constexpr int MIN_CLIENT_PORT = 1;


int validate_and_convert_number(const char* text_value, int min_value, int max_value) {
    char* endptr;

    long value = strtol(text_value, &endptr, 10);

    // empty string
    if (text_value == endptr) {
        return INVALID_VALUE;
    }

    // trailing non-numeric character (eg. 'a')
    if (*endptr != '\0') {
        return INVALID_VALUE;
    }

    // outside the range
    if (value < min_value || value > max_value) {
        return INVALID_VALUE;
    }

    return static_cast<int>(value);
}

void ensure_not_set(bool flag, const std::string& message) {
    if (flag) {
        fatal(message.c_str());
    }
}

static bool is_valid_pawn_row(const std::string& pawns) {
    if (pawns.empty()) return false;

    if (pawns.length() > MAX_PAWNS) return false;

    if (pawns.front() != PAWN_STANDING || pawns.back() != PAWN_STANDING) return false;

    return all_of(pawns.begin(), pawns.end(), [](char c) {
        return c == PAWN_STANDING || c == PAWN_KNOCKED_DOWN;
    });
}

void parse_arguments(int argc, char* argv[], AppConfig& config, const char* allowed_flags, bool is_server) {
    bool has_pawns = false;
    bool has_address = false;
    bool has_port = false;
    bool has_timeout = false;
    bool has_message = false;

    int opt;
    while ((opt = getopt(argc, argv, allowed_flags)) != -1) {
        switch (opt) {
        case 'a':
            ensure_not_set(has_address, "multiple -a options provided.");
            config.address = optarg;
            has_address = true;
            break;

        case 'm':
            ensure_not_set(has_message, "multiple -m options provided.");
            config.message = optarg;
            has_message = true;
            break;

        case 't':
            ensure_not_set(has_timeout, "multiple -t options provided.");
            config.timeout = validate_and_convert_number(optarg, MIN_TIMEOUT_VALUE, MAX_TIMEOUT_VALUE);

            if (config.timeout == INVALID_VALUE) {
                fatal("invalid timeout value. expected a value from 1 to 99.");
            }

            has_timeout = true;
            break;

        case 'r':
            ensure_not_set(has_pawns, "multiple -r options provided.");
            if (!is_valid_pawn_row(optarg)) {
                fatal("invalid pawn row. expected a non-empty string of '0' and '1' with the first and last one "
                      "being '1'");
            }
            config.pawn_row = optarg;
            has_pawns = true;
            break;

        case 'p':
            {
                ensure_not_set(has_port, "multiple -p options provided.");
                int min_port = is_server ? MIN_SERVER_PORT : MIN_CLIENT_PORT;
                config.port = validate_and_convert_number(optarg, min_port, MAX_PORT_NUMBER);

                if (config.port == INVALID_VALUE) {
                    if (is_server) fatal("invalid port number. expected 0 to 65535");
                    fatal("invalid port number. expected 1 to 65535");
                }

                has_port = true;
                break;
            }

        case '?':
        default:
            fatal("unknown option or missing argument. expected: -a, -p, -m, -t");
        }
    }

    std::string flags_str(allowed_flags);

    // verify that every flag present in allowed_flags has been set
    if (flags_str.find('a') != std::string::npos && !has_address) fatal("missing required argument -a");
    if (flags_str.find('p') != std::string::npos && !has_port) fatal("missing required argument -p");
    if (flags_str.find('t') != std::string::npos && !has_timeout) fatal("missing required argument -t");
    if (flags_str.find('r') != std::string::npos && !has_pawns) fatal("missing required argument -r");
    if (flags_str.find('m') != std::string::npos && !has_message) fatal("missing required argument -m");
}

std::vector<std::string> split_message(const std::string& message, char delimiter) {
    std::vector<std::string> result;
    std::string s;

    std::stringstream tokenStream(message);

    while (getline(tokenStream, s, delimiter)) {
        result.push_back(s);
    }

    return result;
}

int validate_message_format(const std::string& buffer, const std::vector<std::string>& parts, size_t expected_parts_count) {
    // validate number of message parts
    if (parts.size() != expected_parts_count) {
        if (parts.size() < expected_parts_count) {
            return static_cast<uint8_t>(buffer.length());
        }
        else {
            // first missing part
            size_t length_sum = 0;
            for (size_t i = 0; i < expected_parts_count; i++) {
                length_sum += parts[i].length();
            }

            length_sum += (expected_parts_count - 1);
            return static_cast<uint8_t>(length_sum);
        }
    }

    size_t current_idx = 0;

    // first non digit element
    for (size_t p = 1; p < expected_parts_count; p++) {
        for (size_t i = 0; i < parts[p].length(); i++) {
            if (!isdigit(parts[p][i])) {
                return static_cast<int>(current_idx + i);
            }
        }
        // +1 for the separator
        current_idx += parts[p].length() + 1;
    }

    return NO_ERROR; // brak błędu
}

uint32_t read_u32(const char* buf, size_t offset) {
    uint32_t val;
    memcpy(&val, buf + offset, sizeof(val));
    return ntohl(val);
}

void write_u32(std::vector<char>& out, size_t& off, uint32_t val) {
    uint32_t net = htonl(val);
    memcpy(out.data() + off, &net, sizeof(net));
    off += sizeof(net);
}