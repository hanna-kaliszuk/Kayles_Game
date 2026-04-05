#include "common.h"
#include "err.h"

int main(int argc, char* argv[]) {
    AppConfig config;

    parse_arguments(argc, argv, config, "m:a:p:t:", false);
}