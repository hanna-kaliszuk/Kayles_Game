#ifndef KAYLES_COMMON_H
#define KAYLES_COMMON_H

#include <string>
#include <iostream>
#include <unistd.h>
#include <vector>

#define INVALID_VALUE (-1)


using namespace std;

struct AppConfig {
    string address;
    string pawn_row;
    string message;
    int port = INVALID_VALUE;
    int timeout = INVALID_VALUE;
};

int validate_and_convert_number(const char* text_value, int min_value, int max_value);

void ensure_not_set(bool flag, const string& message);

void parse_arguments(int argc, char* argv[], AppConfig& config, const char* allowed_flags, bool is_server);

vector<string> split_message(const string& message, char delimiter);

#endif //KAYLES_COMMON_H