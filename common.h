#ifndef KAYLES_COMMON_H
#define KAYLES_COMMON_H

#include <string>
#include <iostream>
#include <unistd.h>

using namespace std;

int validate_and_convert_number(const char* text_value, int min_value, int max_value);

void ensure_not_set(bool flag, const string& message);

#endif //KAYLES_COMMON_H