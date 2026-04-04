#include "common.h"

#include <cstdlib>

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
        cerr << message << endl;
        exit(EXIT_FAILURE);
    }
}
