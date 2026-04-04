#include "common.h"

#include <cstdlib>

int validate_and_convert_port(const char* text_value, int min_port_value) {
    char* endptr;
    long max_val = 65535;

    long value = strtol(text_value, &endptr, 10);

    if (text_value == endptr) { // pusty ciąg znaków
        return -1;
    }

    if (*endptr != '\0') { // czy ostatni znak to '\0' a nie np 'a'
        return -1;
    }

    if (value < min_port_value || value > max_val) {
        return -1;
    }

    return static_cast<int>(value);
}

int validate_and_convert_timeout(const char* text_value) {
    char* endptr;
    long max_val = 99;

    long value = strtol(text_value, &endptr, 10);

    if (text_value == endptr) { // pusty ciąg znaków
        return -1;
    }

    if (*endptr != '\0') { // czy ostatni znak to '\0' a nie np 'a'
        return -1;
    }

    if (value < 1 || value > max_val) {
        return -1;
    }

    return static_cast<int>(value);
}
