/**
* @file common.cpp
 * @brief Implementation of shared argument-parsing, validation and serialization utilities used by both the server and
 * the client.
**/

#include "common.h"

#include <arpa/inet.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ranges>
#include <sstream>
#include <unistd.h>

#include "err.h"
#include "game_logic.h" // to get pawn constants

constexpr int MAX_PORT_NUMBER = 65535;
constexpr double MIN_TIMEOUT_VALUE = 1.0;
constexpr double MAX_TIMEOUT_VALUE = 99.0;
constexpr int MAX_PAWNS = 256;
constexpr int MIN_SERVER_PORT = 0;
constexpr int MIN_CLIENT_PORT = 1;


int validate_and_convert_number(const char* text_value, int min_value, int max_value) {
    char* endptr;

    long value = strtol(text_value, &endptr, 10);

    // empty string pr not a number
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

double validate_and_convert_double(const char* text_value, double min_value, double max_value) {
    char* endptr;
    double value = strtod(text_value, &endptr);

    if (text_value == endptr || *endptr != '\0' || value < min_value || value > max_value) {
        return static_cast<double>(INVALID_VALUE);
    }

    return value;
}

uint64_t get_current_time_ms() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
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

    return std::all_of(pawns.begin(), pawns.end(), [](char c) {
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
            config.timeout = validate_and_convert_double(optarg, MIN_TIMEOUT_VALUE, MAX_TIMEOUT_VALUE);

            if (config.timeout == static_cast<double>(INVALID_VALUE)) {
                fatal("invalid timeout value. expected a value from 1.0 to 99.0");
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

                // server can bind to port 0 (auto-assign)
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

    // extract chunks
    while (getline(tokenStream, s, delimiter)) {
        result.push_back(s);
    }

    return result;
}

int validate_message_format(const std::string& buffer, const std::vector<std::string>& parts,
                            size_t expected_parts_count) {
    // validate number of message parts
    if (parts.size() != expected_parts_count) {
        if (parts.size() < expected_parts_count) {
            return static_cast<uint8_t>(buffer.length());
        }
        else {
            // too many arguments: offset of the first excess part
            size_t length_sum = 0;
            for (size_t i = 0; i < expected_parts_count; i++) {
                length_sum += parts[i].length();
            }

            // inlude delimiters in the count
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

    return NO_ERROR;
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