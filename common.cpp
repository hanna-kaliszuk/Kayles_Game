#include "common.h"
#include "err.h"

#include <cstdlib>
#include <sstream>

#define MAX_PORT_NUMBER 65535
#define MIN_TIMEOUT_VALUE 1
#define MAX_TIMEOUT_VALUE 99

int validate_and_convert_number(const char* text_value, int min_value, int max_value) {
    char* endptr;

    long value = strtol(text_value, &endptr, 10);

    if (text_value == endptr) { // pusty ciąg znaków
        return -1;
    }

    if (*endptr != '\0') { // czy ostatni znak to '\0' a nie np 'a'
        return -1;
    }

    if (value < min_value || value > max_value) {
        return -1;
    }

    return static_cast<int>(value);
}

void ensure_not_set(bool flag, const string& message) {
    if (flag) {
        fatal(message.c_str());
    }
}

static bool is_valid_pawn_row(const string& pawns) {
    if (pawns.empty()) return false;

    if (pawns.front() != '1' || pawns.back() != '1') return false;

    return all_of(pawns.begin(), pawns.end(), [](char c) {
        return c == '0' || c == '1';
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
            ensure_not_set(has_timeout, "multiple -t options provided." );
            config.timeout = validate_and_convert_number(optarg, MIN_TIMEOUT_VALUE, MAX_TIMEOUT_VALUE);

            if (config.timeout == INVALID_VALUE) {
                fatal("invalid timeout value. expected a value from 1 to 99.");
            }

            has_timeout = true;
            break;

        case 'r' :
            ensure_not_set(has_pawns, "multiple -r options provided.");
            if (!is_valid_pawn_row(optarg)) {
                fatal("invalid pawn row. expected a non-empty string of '0' and '1' with the first and last one being '1'");
            }
            config.pawn_row = optarg;
            has_pawns = true;
            break;

        case 'p':
            {
                ensure_not_set(has_port, "multiple -p options provided.");
                int min_port = is_server ? 0 : 1;
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

    string flags_str(allowed_flags);

    if (flags_str.find('a') != string::npos && !has_address) fatal("missing required argument -a");
    if (flags_str.find('p') != string::npos && !has_port)    fatal("missing required argument -p");
    if (flags_str.find('t') != string::npos && !has_timeout) fatal("missing required argument -t");
    if (flags_str.find('r') != string::npos && !has_pawns)   fatal("missing required argument -r");
    if (flags_str.find('m') != string::npos && !has_message) fatal("missing required argument -m");

}

vector<string> split_message(const string& message, char delimiter) {
    vector<string> result;
    string s;

    stringstream tokenStream(message);

    while (getline(tokenStream, s, delimiter)) {
        result.push_back(s);
    }

    return result;
}